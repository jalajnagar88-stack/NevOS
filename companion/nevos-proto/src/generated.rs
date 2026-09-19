// GENERATED FROM schema/nevos.toml BY tools/schema/gen.py — DO NOT EDIT
#![allow(clippy::all)]

use crate::cbor::{CborReader, CborWriter, ProtoError};

pub const PROTOCOL_VERSION: u16 = 1;
pub const MAX_FRAME_BYTES: usize = 8192;
pub const FRAME_HEADER: usize = 4;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MsgId {
    Hello = 1,
    HelloAck = 2,
    Pair = 3,
    PairResult = 4,
    Ping = 5,
    Pong = 6,
    AudioChunk = 16,
    CaptureMarker = 19,
    TranscriptPartial = 17,
    TranscriptFinal = 18,
    AgentRequest = 32,
    AgentToken = 33,
    AgentDone = 34,
    MoodHint = 48,
    Notification = 49,
    OtaAvailable = 50,
}

impl MsgId {
    pub fn from_u16(v: u16) -> Option<Self> {
        match v {
            1 => Some(MsgId::Hello),
            2 => Some(MsgId::HelloAck),
            3 => Some(MsgId::Pair),
            4 => Some(MsgId::PairResult),
            5 => Some(MsgId::Ping),
            6 => Some(MsgId::Pong),
            16 => Some(MsgId::AudioChunk),
            19 => Some(MsgId::CaptureMarker),
            17 => Some(MsgId::TranscriptPartial),
            18 => Some(MsgId::TranscriptFinal),
            32 => Some(MsgId::AgentRequest),
            33 => Some(MsgId::AgentToken),
            34 => Some(MsgId::AgentDone),
            48 => Some(MsgId::MoodHint),
            49 => Some(MsgId::Notification),
            50 => Some(MsgId::OtaAvailable),
            _ => None,
        }
    }

    pub fn name(self) -> &'static str {
        match self {
            MsgId::Hello => "hello",
            MsgId::HelloAck => "hello_ack",
            MsgId::Pair => "pair",
            MsgId::PairResult => "pair_result",
            MsgId::Ping => "ping",
            MsgId::Pong => "pong",
            MsgId::AudioChunk => "audio_chunk",
            MsgId::CaptureMarker => "capture_marker",
            MsgId::TranscriptPartial => "transcript_partial",
            MsgId::TranscriptFinal => "transcript_final",
            MsgId::AgentRequest => "agent_request",
            MsgId::AgentToken => "agent_token",
            MsgId::AgentDone => "agent_done",
            MsgId::MoodHint => "mood_hint",
            MsgId::Notification => "notification",
            MsgId::OtaAvailable => "ota_available",
        }
    }
}

/// First frame after the socket opens. Identifies the device and states what it can do.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct Hello {
    /// protocol_version from this schema. A mismatch is refused by the daemon.
    pub protocol: u16,
    /// Stable per device. Derived from the MAC, not a secret.
    pub device_id: String,
    pub firmware: String,
    /// The pairing token from a previous session, or empty when unpaired.
    pub token: String,
}

impl Hello {
    pub const ID: u16 = 1;

    pub fn encode(&self) -> Vec<u8> {
        let mut w = CborWriter::new();
        w.array(5);
        w.u64(1);
        w.u64(self.protocol as u64);
        w.text(&self.device_id);
        w.text(&self.firmware);
        w.text(&self.token);
        w.finish()
    }

    pub fn decode(buf: &[u8]) -> Result<Self, ProtoError> {
        let mut r = CborReader::new(buf);
        let count = r.array()?;
        if count < 1 { return Err(ProtoError::Malformed); }
        let id = r.u64()?;
        if id != 1 { return Err(ProtoError::WrongMessage); }
        let present = count - 1;
        let mut out = Self::default();
        if present > 0 {
            let v = r.u64()?;
            if v > u16::MAX as u64 { return Err(ProtoError::OutOfRange); }
            out.protocol = v as u16;
        }
        if present > 1 {
            out.device_id = r.text(32)?;
        }
        if present > 2 {
            out.firmware = r.text(24)?;
        }
        if present > 3 {
            out.token = r.text(64)?;
        }
        // Fields appended by a newer peer are stepped over, not an error.
        for _ in 4..present { r.skip()?; }
        Ok(out)
    }
}

/// Accepts the session, or tells the device it must pair first.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct HelloAck {
    pub accepted: bool,
    pub needs_pairing: bool,
    /// Shown on the device so the user can tell which machine answered.
    pub daemon_name: String,
    /// The device has no RTC; this is where its wall clock comes from.
    pub unix_time: u64,
}

impl HelloAck {
    pub const ID: u16 = 2;

    pub fn encode(&self) -> Vec<u8> {
        let mut w = CborWriter::new();
        w.array(5);
        w.u64(2);
        w.bool(self.accepted);
        w.bool(self.needs_pairing);
        w.text(&self.daemon_name);
        w.u64(self.unix_time as u64);
        w.finish()
    }

    pub fn decode(buf: &[u8]) -> Result<Self, ProtoError> {
        let mut r = CborReader::new(buf);
        let count = r.array()?;
        if count < 1 { return Err(ProtoError::Malformed); }
        let id = r.u64()?;
        if id != 2 { return Err(ProtoError::WrongMessage); }
        let present = count - 1;
        let mut out = Self::default();
        if present > 0 {
            out.accepted = r.bool()?;
        }
        if present > 1 {
            out.needs_pairing = r.bool()?;
        }
        if present > 2 {
            out.daemon_name = r.text(48)?;
        }
        if present > 3 {
            let v = r.u64()?;
            out.unix_time = v as u64;
        }
        // Fields appended by a newer peer are stepped over, not an error.
        for _ in 4..present { r.skip()?; }
        Ok(out)
    }
}

/// Sent while the user is entering the on-screen code in the desktop app.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct Pair {
    /// The one-time code shown on the device screen.
    pub code: String,
}

impl Pair {
    pub const ID: u16 = 3;

    pub fn encode(&self) -> Vec<u8> {
        let mut w = CborWriter::new();
        w.array(2);
        w.u64(3);
        w.text(&self.code);
        w.finish()
    }

    pub fn decode(buf: &[u8]) -> Result<Self, ProtoError> {
        let mut r = CborReader::new(buf);
        let count = r.array()?;
        if count < 1 { return Err(ProtoError::Malformed); }
        let id = r.u64()?;
        if id != 3 { return Err(ProtoError::WrongMessage); }
        let present = count - 1;
        let mut out = Self::default();
        if present > 0 {
            out.code = r.text(12)?;
        }
        // Fields appended by a newer peer are stepped over, not an error.
        for _ in 1..present { r.skip()?; }
        Ok(out)
    }
}

/// Grants a long-lived device token, or refuses.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct PairResult {
    pub granted: bool,
    /// Stored in NVS. Erased by a factory reset.
    pub token: String,
    pub reason: String,
}

impl PairResult {
    pub const ID: u16 = 4;

    pub fn encode(&self) -> Vec<u8> {
        let mut w = CborWriter::new();
        w.array(4);
        w.u64(4);
        w.bool(self.granted);
        w.text(&self.token);
        w.text(&self.reason);
        w.finish()
    }

    pub fn decode(buf: &[u8]) -> Result<Self, ProtoError> {
        let mut r = CborReader::new(buf);
        let count = r.array()?;
        if count < 1 { return Err(ProtoError::Malformed); }
        let id = r.u64()?;
        if id != 4 { return Err(ProtoError::WrongMessage); }
        let present = count - 1;
        let mut out = Self::default();
        if present > 0 {
            out.granted = r.bool()?;
        }
        if present > 1 {
            out.token = r.text(64)?;
        }
        if present > 2 {
            out.reason = r.text(64)?;
        }
        // Fields appended by a newer peer are stepped over, not an error.
        for _ in 3..present { r.skip()?; }
        Ok(out)
    }
}

/// Liveness. A socket that has stopped delivering does not always report itself closed.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct Ping {
    pub nonce: u32,
}

impl Ping {
    pub const ID: u16 = 5;

    pub fn encode(&self) -> Vec<u8> {
        let mut w = CborWriter::new();
        w.array(2);
        w.u64(5);
        w.u64(self.nonce as u64);
        w.finish()
    }

    pub fn decode(buf: &[u8]) -> Result<Self, ProtoError> {
        let mut r = CborReader::new(buf);
        let count = r.array()?;
        if count < 1 { return Err(ProtoError::Malformed); }
        let id = r.u64()?;
        if id != 5 { return Err(ProtoError::WrongMessage); }
        let present = count - 1;
        let mut out = Self::default();
        if present > 0 {
            let v = r.u64()?;
            if v > u32::MAX as u64 { return Err(ProtoError::OutOfRange); }
            out.nonce = v as u32;
        }
        // Fields appended by a newer peer are stepped over, not an error.
        for _ in 1..present { r.skip()?; }
        Ok(out)
    }
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct Pong {
    /// Echoed from the ping, so a stale reply cannot be mistaken for a fresh one.
    pub nonce: u32,
}

impl Pong {
    pub const ID: u16 = 6;

    pub fn encode(&self) -> Vec<u8> {
        let mut w = CborWriter::new();
        w.array(2);
        w.u64(6);
        w.u64(self.nonce as u64);
        w.finish()
    }

    pub fn decode(buf: &[u8]) -> Result<Self, ProtoError> {
        let mut r = CborReader::new(buf);
        let count = r.array()?;
        if count < 1 { return Err(ProtoError::Malformed); }
        let id = r.u64()?;
        if id != 6 { return Err(ProtoError::WrongMessage); }
        let present = count - 1;
        let mut out = Self::default();
        if present > 0 {
            let v = r.u64()?;
            if v > u32::MAX as u64 { return Err(ProtoError::OutOfRange); }
            out.nonce = v as u32;
        }
        // Fields appended by a newer peer are stepped over, not an error.
        for _ in 1..present { r.skip()?; }
        Ok(out)
    }
}

/// One frame of captured microphone audio. Roughly 50 a second while recording.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct AudioChunk {
    /// Monotonic within a capture. Lets the daemon spot a dropped frame.
    pub seq: u32,
    /// Groups chunks into one utterance or recording.
    pub session: u32,
    /// Last chunk of this session; the daemon can finalise the transcript.
    pub r#final: bool,
    /// 16 kHz mono signed 16-bit little-endian.
    pub pcm: Vec<u8>,
    /// 0 for a dictated note, 1 for a long-form capture (meeting mode), 2 for a spoken
    /// question.
    ///
    /// The difference is what the daemon does with it, and none of the three are the
    /// same job. A note is a few seconds, transcribed in one go and filed when it ends.
    /// A capture runs for an hour, is transcribed in segments as it arrives, and must
    /// never be held in memory whole. A question is transcribed like a note and then
    /// deliberately not filed: it is the text of something the user said to the agent,
    /// and keeping a file for every "what time is it" turns the notes folder into
    /// rubbish nobody asked to keep.
    ///
    /// Appended after the fact, so an older device that does not send it gets 0 — which
    /// is the behaviour it had before the field existed. An unknown value is treated as
    /// a note, which keeps the audio rather than dropping it.
    pub kind: u8,
}

impl AudioChunk {
    pub const ID: u16 = 16;

    pub fn encode(&self) -> Vec<u8> {
        let mut w = CborWriter::new();
        w.array(6);
        w.u64(16);
        w.u64(self.seq as u64);
        w.u64(self.session as u64);
        w.bool(self.r#final);
        w.bytes(&self.pcm);
        w.u64(self.kind as u64);
        w.finish()
    }

    pub fn decode(buf: &[u8]) -> Result<Self, ProtoError> {
        let mut r = CborReader::new(buf);
        let count = r.array()?;
        if count < 1 { return Err(ProtoError::Malformed); }
        let id = r.u64()?;
        if id != 16 { return Err(ProtoError::WrongMessage); }
        let present = count - 1;
        let mut out = Self::default();
        if present > 0 {
            let v = r.u64()?;
            if v > u32::MAX as u64 { return Err(ProtoError::OutOfRange); }
            out.seq = v as u32;
        }
        if present > 1 {
            let v = r.u64()?;
            if v > u32::MAX as u64 { return Err(ProtoError::OutOfRange); }
            out.session = v as u32;
        }
        if present > 2 {
            out.r#final = r.bool()?;
        }
        if present > 3 {
            out.pcm = r.bytes(4096)?;
        }
        if present > 4 {
            let v = r.u64()?;
            if v > u8::MAX as u64 { return Err(ProtoError::OutOfRange); }
            out.kind = v as u8;
        }
        // Fields appended by a newer peer are stepped over, not an error.
        for _ in 5..present { r.skip()?; }
        Ok(out)
    }
}

/// The user pressed the button during a long capture. The daemon records the
/// position in the transcript so it can be found again.
/// Sent rather than inferred, because the point of a marker is that a person
/// decided something mattered — and the device is the only thing that knows when
/// they pressed it, to the second, while the daemon is still transcribing what was
/// said a minute ago.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct CaptureMarker {
    pub session: u32,
    /// Seconds from the start of the capture, as the device counted them.
    pub at_seconds: f32,
}

impl CaptureMarker {
    pub const ID: u16 = 19;

    pub fn encode(&self) -> Vec<u8> {
        let mut w = CborWriter::new();
        w.array(3);
        w.u64(19);
        w.u64(self.session as u64);
        w.f32(self.at_seconds);
        w.finish()
    }

    pub fn decode(buf: &[u8]) -> Result<Self, ProtoError> {
        let mut r = CborReader::new(buf);
        let count = r.array()?;
        if count < 1 { return Err(ProtoError::Malformed); }
        let id = r.u64()?;
        if id != 19 { return Err(ProtoError::WrongMessage); }
        let present = count - 1;
        let mut out = Self::default();
        if present > 0 {
            let v = r.u64()?;
            if v > u32::MAX as u64 { return Err(ProtoError::OutOfRange); }
            out.session = v as u32;
        }
        if present > 1 {
            out.at_seconds = r.f32()?;
        }
        // Fields appended by a newer peer are stepped over, not an error.
        for _ in 2..present { r.skip()?; }
        Ok(out)
    }
}

/// Best guess so far. Replaces any previous partial for this session.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct TranscriptPartial {
    pub session: u32,
    pub text: String,
}

impl TranscriptPartial {
    pub const ID: u16 = 17;

    pub fn encode(&self) -> Vec<u8> {
        let mut w = CborWriter::new();
        w.array(3);
        w.u64(17);
        w.u64(self.session as u64);
        w.text(&self.text);
        w.finish()
    }

    pub fn decode(buf: &[u8]) -> Result<Self, ProtoError> {
        let mut r = CborReader::new(buf);
        let count = r.array()?;
        if count < 1 { return Err(ProtoError::Malformed); }
        let id = r.u64()?;
        if id != 17 { return Err(ProtoError::WrongMessage); }
        let present = count - 1;
        let mut out = Self::default();
        if present > 0 {
            let v = r.u64()?;
            if v > u32::MAX as u64 { return Err(ProtoError::OutOfRange); }
            out.session = v as u32;
        }
        if present > 1 {
            out.text = r.text(512)?;
        }
        // Fields appended by a newer peer are stepped over, not an error.
        for _ in 2..present { r.skip()?; }
        Ok(out)
    }
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct TranscriptFinal {
    pub session: u32,
    pub text: String,
    pub confidence: f32,
}

impl TranscriptFinal {
    pub const ID: u16 = 18;

    pub fn encode(&self) -> Vec<u8> {
        let mut w = CborWriter::new();
        w.array(4);
        w.u64(18);
        w.u64(self.session as u64);
        w.text(&self.text);
        w.f32(self.confidence);
        w.finish()
    }

    pub fn decode(buf: &[u8]) -> Result<Self, ProtoError> {
        let mut r = CborReader::new(buf);
        let count = r.array()?;
        if count < 1 { return Err(ProtoError::Malformed); }
        let id = r.u64()?;
        if id != 18 { return Err(ProtoError::WrongMessage); }
        let present = count - 1;
        let mut out = Self::default();
        if present > 0 {
            let v = r.u64()?;
            if v > u32::MAX as u64 { return Err(ProtoError::OutOfRange); }
            out.session = v as u32;
        }
        if present > 1 {
            out.text = r.text(512)?;
        }
        if present > 2 {
            out.confidence = r.f32()?;
        }
        // Fields appended by a newer peer are stepped over, not an error.
        for _ in 3..present { r.skip()?; }
        Ok(out)
    }
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct AgentRequest {
    /// Increments per exchange. The daemon keeps the conversation context.
    pub turn: u32,
    pub text: String,
    /// Which app asked, so the daemon can vary its tone or tools.
    pub app: String,
}

impl AgentRequest {
    pub const ID: u16 = 32;

    pub fn encode(&self) -> Vec<u8> {
        let mut w = CborWriter::new();
        w.array(4);
        w.u64(32);
        w.u64(self.turn as u64);
        w.text(&self.text);
        w.text(&self.app);
        w.finish()
    }

    pub fn decode(buf: &[u8]) -> Result<Self, ProtoError> {
        let mut r = CborReader::new(buf);
        let count = r.array()?;
        if count < 1 { return Err(ProtoError::Malformed); }
        let id = r.u64()?;
        if id != 32 { return Err(ProtoError::WrongMessage); }
        let present = count - 1;
        let mut out = Self::default();
        if present > 0 {
            let v = r.u64()?;
            if v > u32::MAX as u64 { return Err(ProtoError::OutOfRange); }
            out.turn = v as u32;
        }
        if present > 1 {
            out.text = r.text(512)?;
        }
        if present > 2 {
            out.app = r.text(16)?;
        }
        // Fields appended by a newer peer are stepped over, not an error.
        for _ in 3..present { r.skip()?; }
        Ok(out)
    }
}

/// One streamed fragment of the reply. Rendered as it arrives.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct AgentToken {
    pub turn: u32,
    pub text: String,
}

impl AgentToken {
    pub const ID: u16 = 33;

    pub fn encode(&self) -> Vec<u8> {
        let mut w = CborWriter::new();
        w.array(3);
        w.u64(33);
        w.u64(self.turn as u64);
        w.text(&self.text);
        w.finish()
    }

    pub fn decode(buf: &[u8]) -> Result<Self, ProtoError> {
        let mut r = CborReader::new(buf);
        let count = r.array()?;
        if count < 1 { return Err(ProtoError::Malformed); }
        let id = r.u64()?;
        if id != 33 { return Err(ProtoError::WrongMessage); }
        let present = count - 1;
        let mut out = Self::default();
        if present > 0 {
            let v = r.u64()?;
            if v > u32::MAX as u64 { return Err(ProtoError::OutOfRange); }
            out.turn = v as u32;
        }
        if present > 1 {
            out.text = r.text(512)?;
        }
        // Fields appended by a newer peer are stepped over, not an error.
        for _ in 2..present { r.skip()?; }
        Ok(out)
    }
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct AgentDone {
    pub turn: u32,
    /// Empty on success. The device shows it rather than failing silently.
    pub error: String,
}

impl AgentDone {
    pub const ID: u16 = 34;

    pub fn encode(&self) -> Vec<u8> {
        let mut w = CborWriter::new();
        w.array(3);
        w.u64(34);
        w.u64(self.turn as u64);
        w.text(&self.error);
        w.finish()
    }

    pub fn decode(buf: &[u8]) -> Result<Self, ProtoError> {
        let mut r = CborReader::new(buf);
        let count = r.array()?;
        if count < 1 { return Err(ProtoError::Malformed); }
        let id = r.u64()?;
        if id != 34 { return Err(ProtoError::WrongMessage); }
        let present = count - 1;
        let mut out = Self::default();
        if present > 0 {
            let v = r.u64()?;
            if v > u32::MAX as u64 { return Err(ProtoError::OutOfRange); }
            out.turn = v as u32;
        }
        if present > 1 {
            out.error = r.text(128)?;
        }
        // Fields appended by a newer peer are stepped over, not an error.
        for _ in 2..present { r.skip()?; }
        Ok(out)
    }
}

/// Lets the agent's tone drive the face, so the device's mood and the agent's mood
/// are the same thing to the user. Published as BRIDGE.MOOD_HINT; the persona
/// subscribes. The bridge never calls the persona directly — they are peers.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct MoodHint {
    /// nev_mood_t. An unknown value is ignored rather than defaulting to idle.
    pub mood: u8,
    pub intensity: u8,
    /// 0 makes it the resting mood; non-zero returns to the previous one.
    pub duration_ms: u16,
}

impl MoodHint {
    pub const ID: u16 = 48;

    pub fn encode(&self) -> Vec<u8> {
        let mut w = CborWriter::new();
        w.array(4);
        w.u64(48);
        w.u64(self.mood as u64);
        w.u64(self.intensity as u64);
        w.u64(self.duration_ms as u64);
        w.finish()
    }

    pub fn decode(buf: &[u8]) -> Result<Self, ProtoError> {
        let mut r = CborReader::new(buf);
        let count = r.array()?;
        if count < 1 { return Err(ProtoError::Malformed); }
        let id = r.u64()?;
        if id != 48 { return Err(ProtoError::WrongMessage); }
        let present = count - 1;
        let mut out = Self::default();
        if present > 0 {
            let v = r.u64()?;
            if v > u8::MAX as u64 { return Err(ProtoError::OutOfRange); }
            out.mood = v as u8;
        }
        if present > 1 {
            let v = r.u64()?;
            if v > u8::MAX as u64 { return Err(ProtoError::OutOfRange); }
            out.intensity = v as u8;
        }
        if present > 2 {
            let v = r.u64()?;
            if v > u16::MAX as u64 { return Err(ProtoError::OutOfRange); }
            out.duration_ms = v as u16;
        }
        // Fields appended by a newer peer are stepped over, not an error.
        for _ in 3..present { r.skip()?; }
        Ok(out)
    }
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct Notification {
    pub title: String,
    pub body: String,
    pub urgent: bool,
}

impl Notification {
    pub const ID: u16 = 49;

    pub fn encode(&self) -> Vec<u8> {
        let mut w = CborWriter::new();
        w.array(4);
        w.u64(49);
        w.text(&self.title);
        w.text(&self.body);
        w.bool(self.urgent);
        w.finish()
    }

    pub fn decode(buf: &[u8]) -> Result<Self, ProtoError> {
        let mut r = CborReader::new(buf);
        let count = r.array()?;
        if count < 1 { return Err(ProtoError::Malformed); }
        let id = r.u64()?;
        if id != 49 { return Err(ProtoError::WrongMessage); }
        let present = count - 1;
        let mut out = Self::default();
        if present > 0 {
            out.title = r.text(48)?;
        }
        if present > 1 {
            out.body = r.text(160)?;
        }
        if present > 2 {
            out.urgent = r.bool()?;
        }
        // Fields appended by a newer peer are stepped over, not an error.
        for _ in 3..present { r.skip()?; }
        Ok(out)
    }
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct OtaAvailable {
    pub version: String,
    pub url: String,
    pub size_bytes: u32,
    /// The image is verified against this and its signature before it is applied.
    pub sha256: Vec<u8>,
}

impl OtaAvailable {
    pub const ID: u16 = 50;

    pub fn encode(&self) -> Vec<u8> {
        let mut w = CborWriter::new();
        w.array(5);
        w.u64(50);
        w.text(&self.version);
        w.text(&self.url);
        w.u64(self.size_bytes as u64);
        w.bytes(&self.sha256);
        w.finish()
    }

    pub fn decode(buf: &[u8]) -> Result<Self, ProtoError> {
        let mut r = CborReader::new(buf);
        let count = r.array()?;
        if count < 1 { return Err(ProtoError::Malformed); }
        let id = r.u64()?;
        if id != 50 { return Err(ProtoError::WrongMessage); }
        let present = count - 1;
        let mut out = Self::default();
        if present > 0 {
            out.version = r.text(24)?;
        }
        if present > 1 {
            out.url = r.text(256)?;
        }
        if present > 2 {
            let v = r.u64()?;
            if v > u32::MAX as u64 { return Err(ProtoError::OutOfRange); }
            out.size_bytes = v as u32;
        }
        if present > 3 {
            out.sha256 = r.bytes(32)?;
        }
        // Fields appended by a newer peer are stepped over, not an error.
        for _ in 4..present { r.skip()?; }
        Ok(out)
    }
}

/// Reads the message id without decoding the body, so a receiver can dispatch.
pub fn peek_id(buf: &[u8]) -> Result<u16, ProtoError> {
    let mut r = CborReader::new(buf);
    let count = r.array()?;
    if count < 1 { return Err(ProtoError::Malformed); }
    let id = r.u64()?;
    if id > u16::MAX as u64 { return Err(ProtoError::OutOfRange); }
    Ok(id as u16)
}

/// Wraps a payload in its big-endian length prefix.
pub fn frame_wrap(payload: &[u8]) -> Result<Vec<u8>, ProtoError> {
    if payload.len() > MAX_FRAME_BYTES { return Err(ProtoError::TooLong); }
    let mut out = Vec::with_capacity(payload.len() + FRAME_HEADER);
    out.extend_from_slice(&(payload.len() as u32).to_be_bytes());
    out.extend_from_slice(payload);
    Ok(out)
}

/// Returns the payload range of the first complete frame, and its total length.
/// `Ok(None)` means more bytes are needed.
pub fn frame_split(buf: &[u8]) -> Result<Option<(std::ops::Range<usize>, usize)>, ProtoError> {
    if buf.len() < FRAME_HEADER { return Ok(None); }
    let declared = u32::from_be_bytes([buf[0], buf[1], buf[2], buf[3]]) as usize;
    // Checked before it is trusted: a length prefix is the first thing a peer controls.
    if declared > MAX_FRAME_BYTES { return Err(ProtoError::TooLong); }
    if buf.len() - FRAME_HEADER < declared { return Ok(None); }
    Ok(Some((FRAME_HEADER..FRAME_HEADER + declared, FRAME_HEADER + declared)))
}
