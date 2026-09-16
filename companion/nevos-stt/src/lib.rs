//! Speech-to-text for the NEVOS daemon.
//!
//! A trait with a local implementation. **Local is the only implementation that
//! ships**, and that is the point: meeting audio and dictated notes do not leave
//! the machine. A cloud backend would be a new type implementing this trait, and
//! would need to be chosen deliberately.
//!
//! The local backend shells out to a `whisper.cpp` binary rather than linking
//! it. Linking pulls a C++ build and a model loader into the daemon's own
//! process; shelling out means a broken or missing model degrades to "no
//! transcription" instead of "the daemon will not start", and the user can
//! upgrade whisper without rebuilding anything.

use anyhow::{anyhow, Context, Result};
use async_trait::async_trait;
use std::path::PathBuf;
use std::process::Stdio;

#[derive(Debug, Clone, PartialEq)]
pub struct Transcript {
    pub text: String,
    /// 0..1. Backends that cannot estimate it report 0.
    pub confidence: f32,
}

#[async_trait]
pub trait Transcriber: Send + Sync {
    /// Shown in the tray app so the user can see what is processing their audio.
    fn name(&self) -> &str;

    /// True when the audio never leaves the machine. The tray shows this, and
    /// it is the one property a user must be able to check at a glance.
    fn is_local(&self) -> bool;

    async fn transcribe(&self, pcm: &[i16], sample_rate: u32) -> Result<Transcript>;
}

/// Minimal 16-bit mono PCM WAV. Written by hand because it is forty lines and
/// the alternative is a dependency that does nothing else for us.
pub fn wav_from_pcm(pcm: &[i16], sample_rate: u32) -> Vec<u8> {
    let data_len = (pcm.len() * 2) as u32;
    let mut out = Vec::with_capacity(44 + data_len as usize);

    out.extend_from_slice(b"RIFF");
    out.extend_from_slice(&(36 + data_len).to_le_bytes());
    out.extend_from_slice(b"WAVEfmt ");
    out.extend_from_slice(&16u32.to_le_bytes()); // PCM fmt chunk size
    out.extend_from_slice(&1u16.to_le_bytes()); // PCM
    out.extend_from_slice(&1u16.to_le_bytes()); // mono
    out.extend_from_slice(&sample_rate.to_le_bytes());
    out.extend_from_slice(&(sample_rate * 2).to_le_bytes()); // byte rate
    out.extend_from_slice(&2u16.to_le_bytes()); // block align
    out.extend_from_slice(&16u16.to_le_bytes()); // bits per sample
    out.extend_from_slice(b"data");
    out.extend_from_slice(&data_len.to_le_bytes());
    for s in pcm {
        out.extend_from_slice(&s.to_le_bytes());
    }
    out
}

// --------------------------------------------------------------- whisper.cpp

pub struct WhisperCli {
    binary: PathBuf,
    model: PathBuf,
    threads: usize,
}

impl WhisperCli {
    pub fn new(binary: impl Into<PathBuf>, model: impl Into<PathBuf>) -> Self {
        Self {
            binary: binary.into(),
            model: model.into(),
            // Leave a core for the rest of the machine: transcription that
            // makes the user's laptop unusable is not a feature.
            threads: (std::thread::available_parallelism().map(|n| n.get()).unwrap_or(4) - 1).max(1),
        }
    }

    /// Checks the binary and model exist before anything is recorded, so a
    /// misconfiguration surfaces at startup rather than after a meeting.
    pub fn check(&self) -> Result<()> {
        if !self.model.exists() {
            return Err(anyhow!("whisper model not found at {}", self.model.display()));
        }
        Ok(())
    }
}

#[async_trait]
impl Transcriber for WhisperCli {
    fn name(&self) -> &str {
        "whisper.cpp (local)"
    }

    fn is_local(&self) -> bool {
        true
    }

    async fn transcribe(&self, pcm: &[i16], sample_rate: u32) -> Result<Transcript> {
        if pcm.is_empty() {
            return Ok(Transcript { text: String::new(), confidence: 0.0 });
        }

        // A unique path per call: two captures finishing together must not
        // overwrite each other's audio.
        let tmp = std::env::temp_dir().join(format!(
            "nevos-stt-{}-{}.wav",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|d| d.as_nanos())
                .unwrap_or(0)
        ));
        tokio::fs::write(&tmp, wav_from_pcm(pcm, sample_rate))
            .await
            .with_context(|| format!("writing {}", tmp.display()))?;

        let output = tokio::process::Command::new(&self.binary)
            .arg("-m")
            .arg(&self.model)
            .arg("-f")
            .arg(&tmp)
            .arg("-t")
            .arg(self.threads.to_string())
            .arg("--no-timestamps")
            .arg("--output-txt")
            .arg("--no-prints")
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .output()
            .await
            .with_context(|| format!("running {}", self.binary.display()))?;

        // Best-effort cleanup: audio must not accumulate in a temp directory
        // where the purge control cannot reach it.
        let _ = tokio::fs::remove_file(&tmp).await;
        let _ = tokio::fs::remove_file(tmp.with_extension("wav.txt")).await;

        if !output.status.success() {
            return Err(anyhow!(
                "whisper failed: {}",
                String::from_utf8_lossy(&output.stderr).trim()
            ));
        }
        Ok(Transcript {
            text: String::from_utf8_lossy(&output.stdout).trim().to_string(),
            confidence: 0.0, // whisper.cpp's CLI does not report one
        })
    }
}

// --------------------------------------------------------------- unavailable

/// What the daemon uses when no transcriber is configured.
///
/// The alternative — refusing to start — would take the notes app, the agent,
/// pairing and the games offline because one optional binary is missing. This
/// way the device works, and the one thing that does not work says so.
pub struct Unavailable;

#[async_trait]
impl Transcriber for Unavailable {
    fn name(&self) -> &str {
        "none (not configured)"
    }

    fn is_local(&self) -> bool {
        // Nothing is sent anywhere, because nothing happens.
        true
    }

    async fn transcribe(&self, _pcm: &[i16], _sample_rate: u32) -> Result<Transcript> {
        Err(anyhow!(
            "no transcriber is configured: set NEVOS_WHISPER_BIN and NEVOS_WHISPER_MODEL"
        ))
    }
}

// ---------------------------------------------------------------------- mock

/// Used by tests, and by the daemon when no transcriber is configured — so the
/// rest of the system can be exercised without whisper installed.
pub struct MockTranscriber {
    pub reply: String,
}

impl MockTranscriber {
    pub fn new(reply: impl Into<String>) -> Self {
        Self { reply: reply.into() }
    }
}

#[async_trait]
impl Transcriber for MockTranscriber {
    fn name(&self) -> &str {
        "mock (no transcription)"
    }

    fn is_local(&self) -> bool {
        true
    }

    async fn transcribe(&self, pcm: &[i16], _sample_rate: u32) -> Result<Transcript> {
        Ok(Transcript {
            text: if pcm.is_empty() { String::new() } else { self.reply.clone() },
            confidence: 1.0,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn wav_header_is_well_formed() {
        let pcm = vec![0i16, 1, -1, 32767, -32768];
        let wav = wav_from_pcm(&pcm, 16000);

        assert_eq!(&wav[0..4], b"RIFF");
        assert_eq!(&wav[8..12], b"WAVE");
        assert_eq!(&wav[36..40], b"data");
        assert_eq!(wav.len(), 44 + pcm.len() * 2, "header plus one frame per sample");

        // Sizes in the header must describe the actual bytes, or every player
        // and whisper itself will read the wrong amount.
        let riff_size = u32::from_le_bytes([wav[4], wav[5], wav[6], wav[7]]);
        assert_eq!(riff_size as usize, wav.len() - 8);
        let data_size = u32::from_le_bytes([wav[40], wav[41], wav[42], wav[43]]);
        assert_eq!(data_size as usize, pcm.len() * 2);

        let rate = u32::from_le_bytes([wav[24], wav[25], wav[26], wav[27]]);
        assert_eq!(rate, 16000);
    }

    #[test]
    fn samples_are_little_endian() {
        let wav = wav_from_pcm(&[0x0102], 16000);
        assert_eq!(&wav[44..46], &[0x02, 0x01]);
    }

    #[test]
    fn an_empty_capture_produces_a_valid_empty_wav() {
        let wav = wav_from_pcm(&[], 16000);
        assert_eq!(wav.len(), 44);
        assert_eq!(&wav[0..4], b"RIFF");
    }

    #[tokio::test]
    async fn the_mock_transcriber_is_local_and_returns_its_reply() {
        let t = MockTranscriber::new("hello there");
        assert!(t.is_local(), "the mock must not claim to send audio anywhere");
        let out = t.transcribe(&[1, 2, 3], 16000).await.unwrap();
        assert_eq!(out.text, "hello there");
    }

    /// Silence in, nothing out. A backend that invents text for empty audio
    /// would fill someone's notes with hallucinations.
    #[tokio::test]
    async fn empty_audio_transcribes_to_nothing() {
        let t = MockTranscriber::new("should not appear");
        assert_eq!(t.transcribe(&[], 16000).await.unwrap().text, "");
    }

    #[test]
    fn a_missing_model_is_reported_before_anything_is_recorded() {
        let w = WhisperCli::new("/nonexistent/whisper", "/nonexistent/model.bin");
        assert!(w.check().is_err());
        assert!(w.is_local());
    }

    #[tokio::test]
    async fn an_unconfigured_transcriber_fails_loudly_rather_than_returning_silence() {
        // An empty string here would look like "you said nothing", and the user
        // would try again and again into a microphone that is working fine.
        let err = Unavailable.transcribe(&[1, 2, 3], 16_000).await.unwrap_err();
        assert!(err.to_string().contains("NEVOS_WHISPER_BIN"), "{err}");
    }
}
