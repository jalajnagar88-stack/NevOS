//! Local-first storage for the NEVOS companion daemon.
//!
//! Plain files in a directory the user owns, in a format they can read. No
//! database, no service, no account. You can back this up with a file manager
//! and delete it with `rm -r`, and the daemon will be fine.
//!
//! That is a privacy decision as much as a simplicity one. This holds meeting
//! audio and dictated notes; anything the user cannot inspect or delete without
//! the daemon's cooperation is something they have to take on trust.

use anyhow::{Context, Result};
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

/// A paired device. The token is what the device presents on every reconnect.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Device {
    pub device_id: String,
    pub token: String,
    pub name: String,
    pub paired_at: u64,
    pub last_seen: u64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum RecordKind {
    /// A short dictated note.
    Note,
    /// A long-form capture from meeting mode.
    Transcript,
}

impl RecordKind {
    fn dir(self) -> &'static str {
        match self {
            RecordKind::Note => "notes",
            RecordKind::Transcript => "transcripts",
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Record {
    pub id: String,
    pub kind: RecordKind,
    pub created_at: u64,
    pub text: String,
    /// Chapter markers, in seconds from the start. Meeting mode writes these
    /// when the user presses the button during a capture.
    #[serde(default)]
    pub markers: Vec<f32>,
    /// Filename of the retained audio, if any. Purged separately from the text.
    #[serde(default)]
    pub audio: Option<String>,
}

pub struct Store {
    root: PathBuf,
}

/// Where the daemon keeps its data, honouring XDG on Linux.
pub fn default_root() -> PathBuf {
    if let Ok(dir) = std::env::var("NEVOS_DATA_DIR") {
        return PathBuf::from(dir);
    }
    if let Ok(dir) = std::env::var("XDG_DATA_HOME") {
        return PathBuf::from(dir).join("nevos");
    }
    if let Ok(home) = std::env::var("HOME") {
        return PathBuf::from(home).join(".local/share/nevos");
    }
    PathBuf::from("nevos-data")
}

pub fn now_unix() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0)
}

impl Store {
    pub fn open(root: impl Into<PathBuf>) -> Result<Self> {
        let root = root.into();
        for sub in ["notes", "transcripts", "audio"] {
            fs::create_dir_all(root.join(sub))
                .with_context(|| format!("creating {}", root.join(sub).display()))?;
        }
        Ok(Self { root })
    }

    pub fn root(&self) -> &Path {
        &self.root
    }

    // ----------------------------------------------------------- devices

    fn devices_path(&self) -> PathBuf {
        self.root.join("devices.json")
    }

    pub fn devices(&self) -> Result<Vec<Device>> {
        let path = self.devices_path();
        if !path.exists() {
            return Ok(Vec::new());
        }
        let raw = fs::read_to_string(&path)?;
        // A corrupt registry must not stop the daemon starting: the worst case
        // is that devices re-pair, which is a six-digit code, not a lost evening.
        Ok(serde_json::from_str(&raw).unwrap_or_default())
    }

    pub fn upsert_device(&self, device: Device) -> Result<()> {
        let mut all = self.devices()?;
        match all.iter_mut().find(|d| d.device_id == device.device_id) {
            Some(existing) => *existing = device,
            None => all.push(device),
        }
        write_atomic(&self.devices_path(), serde_json::to_vec_pretty(&all)?.as_slice())
    }

    pub fn device_by_token(&self, token: &str) -> Result<Option<Device>> {
        if token.is_empty() {
            return Ok(None); // an empty token is "unpaired", never a match
        }
        Ok(self.devices()?.into_iter().find(|d| d.token == token))
    }

    pub fn forget_device(&self, device_id: &str) -> Result<bool> {
        let mut all = self.devices()?;
        let before = all.len();
        all.retain(|d| d.device_id != device_id);
        if all.len() == before {
            return Ok(false);
        }
        write_atomic(&self.devices_path(), serde_json::to_vec_pretty(&all)?.as_slice())?;
        Ok(true)
    }

    // ----------------------------------------------------------- records

    pub fn save_record(&self, record: &Record) -> Result<PathBuf> {
        let path = self.root.join(record.kind.dir()).join(format!("{}.json", record.id));
        write_atomic(&path, serde_json::to_vec_pretty(record)?.as_slice())?;
        Ok(path)
    }

    pub fn records(&self, kind: RecordKind) -> Result<Vec<Record>> {
        let dir = self.root.join(kind.dir());
        let mut out = Vec::new();
        if !dir.exists() {
            return Ok(out);
        }
        for entry in fs::read_dir(&dir)? {
            let path = entry?.path();
            if path.extension().and_then(|e| e.to_str()) != Some("json") {
                continue;
            }
            // Skip anything unreadable rather than failing the whole listing:
            // one bad file should not hide the rest of someone's notes.
            if let Ok(raw) = fs::read_to_string(&path) {
                if let Ok(rec) = serde_json::from_str::<Record>(&raw) {
                    out.push(rec);
                }
            }
        }
        out.sort_by(|a, b| b.created_at.cmp(&a.created_at)); // newest first
        Ok(out)
    }

    pub fn save_audio(&self, id: &str, wav: &[u8]) -> Result<PathBuf> {
        let path = self.root.join("audio").join(format!("{id}.wav"));
        write_atomic(&path, wav)?;
        Ok(path)
    }

    // ------------------------------------------------------------- purge

    /// Deletes captured audio and keeps the text. The common case: the
    /// transcript is what is wanted, the recording is what is sensitive.
    pub fn purge_audio(&self) -> Result<usize> {
        let removed = clear_dir(&self.root.join("audio"))?;
        for kind in [RecordKind::Note, RecordKind::Transcript] {
            for mut rec in self.records(kind)? {
                if rec.audio.take().is_some() {
                    self.save_record(&rec)?;
                }
            }
        }
        Ok(removed)
    }

    /// Everything the user ever said: audio, notes and transcripts. The
    /// one-click purge. Paired devices are deliberately kept — erasing them
    /// would silently unpair the hardware, which is not what "delete my data"
    /// means to anyone.
    pub fn purge_all(&self) -> Result<usize> {
        let mut n = 0;
        for sub in ["audio", "notes", "transcripts"] {
            n += clear_dir(&self.root.join(sub))?;
        }
        Ok(n)
    }
}

/// Write to a temporary file and rename. An interrupted write then leaves the
/// previous contents intact rather than a half-written file.
fn write_atomic(path: &Path, data: &[u8]) -> Result<()> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }
    let tmp = path.with_extension("tmp");
    fs::write(&tmp, data).with_context(|| format!("writing {}", tmp.display()))?;
    fs::rename(&tmp, path).with_context(|| format!("renaming into {}", path.display()))?;
    Ok(())
}

fn clear_dir(dir: &Path) -> Result<usize> {
    if !dir.exists() {
        return Ok(0);
    }
    let mut n = 0;
    for entry in fs::read_dir(dir)? {
        let path = entry?.path();
        if path.is_file() {
            fs::remove_file(&path)?;
            n += 1;
        }
    }
    Ok(n)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp_store() -> (Store, PathBuf) {
        let dir = std::env::temp_dir().join(format!("nevos-test-{}-{:?}", now_unix(), std::thread::current().id()));
        let _ = fs::remove_dir_all(&dir);
        (Store::open(&dir).unwrap(), dir)
    }

    fn note(id: &str) -> Record {
        Record {
            id: id.to_string(),
            kind: RecordKind::Note,
            created_at: now_unix(),
            text: "remember the milk".into(),
            markers: vec![],
            audio: Some(format!("{id}.wav")),
        }
    }

    #[test]
    fn records_round_trip_and_list_newest_first() {
        let (store, dir) = temp_store();
        let mut older = note("a");
        older.created_at = 100;
        let mut newer = note("b");
        newer.created_at = 200;
        store.save_record(&older).unwrap();
        store.save_record(&newer).unwrap();

        let listed = store.records(RecordKind::Note).unwrap();
        assert_eq!(listed.len(), 2);
        assert_eq!(listed[0].id, "b", "listing should be newest first");
        assert_eq!(listed[0].text, "remember the milk");
        assert!(store.records(RecordKind::Transcript).unwrap().is_empty());
        let _ = fs::remove_dir_all(dir);
    }

    /// One unreadable file must not hide the rest of someone's notes.
    #[test]
    fn a_corrupt_record_is_skipped_not_fatal() {
        let (store, dir) = temp_store();
        store.save_record(&note("good")).unwrap();
        fs::write(dir.join("notes/broken.json"), b"{ not json at all").unwrap();

        let listed = store.records(RecordKind::Note).unwrap();
        assert_eq!(listed.len(), 1);
        assert_eq!(listed[0].id, "good");
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn devices_upsert_and_lookup_by_token() {
        let (store, dir) = temp_store();
        let d = Device {
            device_id: "nev-1".into(),
            token: "tok-abc".into(),
            name: "Desk".into(),
            paired_at: 1,
            last_seen: 1,
        };
        store.upsert_device(d.clone()).unwrap();
        assert_eq!(store.device_by_token("tok-abc").unwrap().unwrap(), d);
        assert!(store.device_by_token("wrong").unwrap().is_none());

        // Upsert replaces rather than duplicating.
        let mut renamed = d.clone();
        renamed.name = "Shelf".into();
        store.upsert_device(renamed).unwrap();
        assert_eq!(store.devices().unwrap().len(), 1);
        assert_eq!(store.devices().unwrap()[0].name, "Shelf");

        assert!(store.forget_device("nev-1").unwrap());
        assert!(!store.forget_device("nev-1").unwrap());
        let _ = fs::remove_dir_all(dir);
    }

    /// An empty token means "unpaired". Matching it against a stored record
    /// would let any unpaired device impersonate a paired one.
    #[test]
    fn an_empty_token_never_matches() {
        let (store, dir) = temp_store();
        store
            .upsert_device(Device {
                device_id: "nev-1".into(),
                token: String::new(),
                name: "Odd".into(),
                paired_at: 0,
                last_seen: 0,
            })
            .unwrap();
        assert!(store.device_by_token("").unwrap().is_none());
        let _ = fs::remove_dir_all(dir);
    }

    /// The transcript is usually what is wanted; the recording is what is
    /// sensitive. Purging audio must also clear the reference to it.
    #[test]
    fn purging_audio_keeps_the_text_and_drops_the_reference() {
        let (store, dir) = temp_store();
        store.save_record(&note("a")).unwrap();
        store.save_audio("a", b"RIFF....").unwrap();

        assert_eq!(store.purge_audio().unwrap(), 1);
        let listed = store.records(RecordKind::Note).unwrap();
        assert_eq!(listed.len(), 1, "purging audio deleted the note");
        assert_eq!(listed[0].audio, None, "the note still points at deleted audio");
        assert!(!dir.join("audio/a.wav").exists());
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn purge_all_removes_everything_said_but_keeps_pairing() {
        let (store, dir) = temp_store();
        store.save_record(&note("a")).unwrap();
        let mut t = note("b");
        t.kind = RecordKind::Transcript;
        store.save_record(&t).unwrap();
        store.save_audio("a", b"RIFF").unwrap();
        store
            .upsert_device(Device {
                device_id: "nev-1".into(),
                token: "tok".into(),
                name: "Desk".into(),
                paired_at: 1,
                last_seen: 1,
            })
            .unwrap();

        assert_eq!(store.purge_all().unwrap(), 3);
        assert!(store.records(RecordKind::Note).unwrap().is_empty());
        assert!(store.records(RecordKind::Transcript).unwrap().is_empty());
        // "Delete my data" does not mean "unpair my hardware".
        assert_eq!(store.devices().unwrap().len(), 1);
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn opening_an_existing_store_is_idempotent() {
        let (store, dir) = temp_store();
        store.save_record(&note("a")).unwrap();
        let again = Store::open(&dir).unwrap();
        assert_eq!(again.records(RecordKind::Note).unwrap().len(), 1);
        let _ = fs::remove_dir_all(dir);
    }
}
