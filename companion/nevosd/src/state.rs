//! What the daemon knows, shared across connections.
//!
//! One process, several devices in principle, one owner. The locks here are
//! `std::sync::Mutex` rather than tokio's: every critical section is a map
//! lookup or a few file writes, and a synchronous lock that is never held
//! across an await is both faster and much harder to deadlock.
use anyhow::Result;
use nevos_agent::{Agent, Conversation};
use nevos_store::{Device, Store};
use nevos_stt::Transcriber;
use std::collections::HashMap;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};
use tokio::sync::broadcast;

use crate::pairing::{EntryOutcome, Pairing};
use crate::session::Authenticator;

/// Sent to a waiting connection when the user completes pairing at the desktop.
#[derive(Debug, Clone)]
pub struct Grant {
    pub device_id: String,
    pub token: String,
}

pub struct Daemon {
    pub name: String,
    pub store: Store,
    pub agent: Arc<dyn Agent>,
    pub stt: Arc<dyn Transcriber>,
    /// Whether captured audio is written to disk. Off by default: the text is
    /// the useful part, and audio of a household is the part worth not keeping.
    pub keep_audio: bool,
    pairing: Mutex<Pairing>,
    conversations: Mutex<HashMap<String, Conversation>>,
    grants: broadcast::Sender<Grant>,
    /// How many devices are streaming audio right now. The tray's live
    /// indicator reads this, and it is incremented by audio arriving rather
    /// than by anything announcing an intention to record.
    capturing: AtomicUsize,
    connected: Mutex<Vec<String>>,
}

impl Daemon {
    pub fn new(
        name: impl Into<String>,
        store: Store,
        agent: Arc<dyn Agent>,
        stt: Arc<dyn Transcriber>,
        keep_audio: bool,
    ) -> Self {
        let (grants, _) = broadcast::channel(16);
        Self {
            name: name.into(),
            store,
            agent,
            stt,
            keep_audio,
            pairing: Mutex::new(Pairing::new()),
            conversations: Mutex::new(HashMap::new()),
            grants,
            capturing: AtomicUsize::new(0),
            connected: Mutex::new(Vec::new()),
        }
    }

    pub fn subscribe_grants(&self) -> broadcast::Receiver<Grant> {
        self.grants.subscribe()
    }

    /// A device is showing a pairing code.
    pub fn device_requested_pairing(&self, device_id: &str, code: &str, now: u64) -> bool {
        self.pairing.lock().unwrap().device_requested(device_id, code, now)
    }

    /// Withdraws a device's open pairing request.
    pub fn cancel_pairing(&self, device_id: &str) {
        self.pairing.lock().unwrap().cancel(device_id);
    }

    pub fn pending_pairings(&self, now: u64) -> Vec<String> {
        self.pairing.lock().unwrap().pending(now)
    }

    /// The user typed a code into the desktop app. On success a token is
    /// issued, stored, and announced to whichever connection is waiting.
    pub fn complete_pairing(&self, code: &str, now: u64) -> Result<EntryOutcome> {
        let outcome = self.pairing.lock().unwrap().user_entered(code, now);
        if let EntryOutcome::Granted { device_id } = &outcome {
            let token = crate::pairing::random_token()?;
            self.store.upsert_device(Device {
                device_id: device_id.clone(),
                token: token.clone(),
                name: String::new(),
                paired_at: now,
                last_seen: now,
            })?;
            // A closed channel means no connection is waiting — the device
            // dropped off between asking and the user typing. The token is
            // stored either way, so reconnecting works.
            let _ = self.grants.send(Grant { device_id: device_id.clone(), token });
        }
        Ok(outcome)
    }

    pub fn capture_started(&self) {
        self.capturing.fetch_add(1, Ordering::SeqCst);
    }

    pub fn capture_stopped(&self) {
        // Saturating: an underflow here would wrap to a huge number and leave
        // the tray indicator permanently lit, which is the one failure mode
        // this counter must not have.
        let _ = self
            .capturing
            .fetch_update(Ordering::SeqCst, Ordering::SeqCst, |n| Some(n.saturating_sub(1)));
    }

    pub fn is_capturing(&self) -> bool {
        self.capturing.load(Ordering::SeqCst) > 0
    }

    pub fn device_connected(&self, device_id: &str) {
        let mut c = self.connected.lock().unwrap();
        if !c.iter().any(|d| d == device_id) {
            c.push(device_id.to_string());
        }
    }

    pub fn device_disconnected(&self, device_id: &str) {
        self.connected.lock().unwrap().retain(|d| d != device_id);
    }

    pub fn connected_devices(&self) -> Vec<String> {
        self.connected.lock().unwrap().clone()
    }

    /// Appends to a device's rolling conversation and returns the history that
    /// should go to the model, which is everything before the new turn.
    pub fn conversation_for(&self, device_id: &str) -> Conversation {
        self.conversations
            .lock()
            .unwrap()
            .entry(device_id.to_string())
            .or_default()
            .clone()
    }

    pub fn push_turn(&self, device_id: &str, turn: nevos_agent::Turn) {
        self.conversations
            .lock()
            .unwrap()
            .entry(device_id.to_string())
            .or_default()
            .push(turn);
    }

    pub fn forget_conversation(&self, device_id: &str) {
        self.conversations.lock().unwrap().remove(device_id);
    }

    /// Erases stored content. The conversation in memory goes too — a purge
    /// that left the last ten minutes of talk in RAM would not be a purge.
    pub fn purge_all(&self) -> Result<usize> {
        self.conversations.lock().unwrap().clear();
        self.store.purge_all()
    }
}

impl Authenticator for Daemon {
    fn lookup(&self, device_id: &str, token: &str) -> Option<String> {
        if token.is_empty() {
            return None;
        }
        match self.store.device_by_token(token) {
            // The token must belong to the device presenting it.
            Ok(Some(device)) if device.device_id == device_id => Some(device.name),
            Ok(_) => None,
            Err(e) => {
                tracing::error!(error = %e, "device lookup failed");
                None
            }
        }
    }
}

/// Fixtures shared by the daemon's tests. A test daemon is backed by a real
/// store in a temporary directory rather than a fake: the store is forty lines
/// of file writes, and a fake of it would only prove the fake works.
#[cfg(test)]
pub mod tests_support {
    use super::*;
    use nevos_agent::MockAgent;
    use nevos_stt::MockTranscriber;

    pub fn tempdir(tag: &str) -> std::path::PathBuf {
        let p = std::env::temp_dir().join(format!("nevosd-test-{tag}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&p);
        std::fs::create_dir_all(&p).unwrap();
        p
    }

    pub fn daemon(tag: &str) -> Arc<Daemon> {
        Arc::new(Daemon::new(
            "test",
            Store::open(tempdir(tag)).unwrap(),
            Arc::new(MockAgent::default()),
            Arc::new(MockTranscriber::new("hello")),
            false,
        ))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tests_support::daemon;

    #[test]
    fn pairing_end_to_end_issues_a_usable_token() {
        let d = daemon("pair");
        let now = 1_700_000_000;

        assert!(d.device_requested_pairing("nev-abc", "123456", now));
        let outcome = d.complete_pairing("123456", now).unwrap();
        assert!(matches!(outcome, EntryOutcome::Granted { .. }));

        let device = d.store.devices().unwrap().remove(0);
        assert_eq!(d.lookup("nev-abc", &device.token), Some(String::new()));
        assert_eq!(d.lookup("nev-other", &device.token), None, "token is bound to its device");
        assert_eq!(d.lookup("nev-abc", ""), None);
        assert_eq!(d.lookup("nev-abc", "guess"), None);
    }

    #[test]
    fn a_wrong_code_issues_nothing() {
        let d = daemon("wrong");
        let now = 1_700_000_000;
        d.device_requested_pairing("nev-abc", "123456", now);
        assert_eq!(d.complete_pairing("000000", now).unwrap(), EntryOutcome::Rejected);
        assert!(d.store.devices().unwrap().is_empty(), "a refused attempt must store nothing");
    }

    #[test]
    fn the_live_indicator_cannot_get_stuck_on() {
        let d = daemon("live");
        assert!(!d.is_capturing());
        d.capture_started();
        assert!(d.is_capturing());
        d.capture_stopped();
        // Twice: a dropped connection can report a stop for a capture that
        // already ended, and an underflow would light the indicator forever.
        d.capture_stopped();
        assert!(!d.is_capturing());
        d.capture_started();
        assert!(d.is_capturing());
    }

    #[test]
    fn a_purge_clears_memory_as_well_as_disk() {
        let d = daemon("purge");
        d.push_turn("nev-abc", nevos_agent::Turn::user("something private"));
        d.store
            .save_record(&nevos_store::Record {
                id: "r1".into(),
                kind: nevos_store::RecordKind::Note,
                created_at: 1,
                text: "a note".into(),
                markers: vec![],
                audio: None,
            })
            .unwrap();

        assert!(d.purge_all().unwrap() >= 1);
        assert!(d.conversation_for("nev-abc").turns().is_empty());
        assert!(d.store.records(nevos_store::RecordKind::Note).unwrap().is_empty());
    }
}
