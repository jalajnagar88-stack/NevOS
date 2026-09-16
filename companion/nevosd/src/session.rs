//! One device connection, as a state machine with no socket in it.
//!
//! This is the same split the firmware uses for its games: the part that
//! decides is separated from the part that does, so the decisions can be tested
//! exhaustively and the doing stays thin enough to read. Everything about the
//! protocol that could be wrong — a device that skips hello, a device that
//! presents another device's token, audio that arrives out of order, a second
//! question asked before the first is answered — is decided here, against a
//! clock that is an argument, and is a unit test rather than something found by
//! plugging in the robot.
//!
//! The daemon owns the async parts: sockets, the model, the transcriber. They
//! appear here only as `Action`s handed back to the caller.
use nevos_proto::{
    frame_wrap, peek_id, AgentDone, AgentRequest, AgentToken, AudioChunk, Hello, HelloAck, MoodHint,
    MsgId, Notification, OtaAvailable, Pair, PairResult, Ping, Pong, TranscriptFinal,
    TranscriptPartial, PROTOCOL_VERSION,
};

/// 16 kHz mono, as the device captures it.
pub const SAMPLE_RATE: u32 = 16_000;

/// The longest single utterance the daemon will hold in memory, in seconds.
///
/// Push-to-talk utterances are a few seconds. This bound exists because a
/// device with a stuck button — or a hostile one — otherwise streams audio
/// until the daemon is killed by the OS. Meeting mode, which records for an
/// hour, streams to disk instead of buffering and is a separate path (M7).
pub const MAX_UTTERANCE_SECS: u32 = 120;
const MAX_UTTERANCE_SAMPLES: usize = (SAMPLE_RATE * MAX_UTTERANCE_SECS) as usize;

/// A message the daemon should send to this device.
///
/// This is the daemon's whole half of the protocol, including the three the
/// daemon does not send yet: partial transcripts wait on a streaming
/// transcriber, and notifications and OTA offers are M7. They are listed here
/// rather than added later so that `to_frame` below is exhaustive over the
/// schema, and a message added to the schema fails to compile until it is
/// handled.
#[allow(dead_code)]
#[derive(Debug, Clone, PartialEq)]
pub enum Out {
    HelloAck(HelloAck),
    PairResult(PairResult),
    Pong(Pong),
    TranscriptPartial(TranscriptPartial),
    TranscriptFinal(TranscriptFinal),
    AgentToken(AgentToken),
    AgentDone(AgentDone),
    MoodHint(MoodHint),
    Notification(Notification),
    OtaAvailable(OtaAvailable),
}

impl Out {
    /// Encodes and length-prefixes, ready for the socket.
    pub fn to_frame(&self) -> Result<Vec<u8>, nevos_proto::ProtoError> {
        let payload = match self {
            Out::HelloAck(m) => m.encode(),
            Out::PairResult(m) => m.encode(),
            Out::Pong(m) => m.encode(),
            Out::TranscriptPartial(m) => m.encode(),
            Out::TranscriptFinal(m) => m.encode(),
            Out::AgentToken(m) => m.encode(),
            Out::AgentDone(m) => m.encode(),
            Out::MoodHint(m) => m.encode(),
            Out::Notification(m) => m.encode(),
            Out::OtaAvailable(m) => m.encode(),
        };
        frame_wrap(&payload)
    }
}

/// Work for the daemon to do, or a message to send.
#[derive(Debug, Clone, PartialEq)]
pub enum Action {
    Send(Out),
    /// Close the connection. The string is for the log, not for the device:
    /// a peer that is breaking the protocol is not owed an explanation.
    Close(String),
    /// The device is showing this code and wants to pair.
    RequestPairing { device_id: String, code: String },
    /// A complete utterance, ready to transcribe.
    Utterance { session: u32, pcm: Vec<i16> },
    /// Ask the model. Any turn already running should be abandoned.
    Ask { turn: u32, text: String, app: String },
    /// A gap in the audio sequence. Worth recording: it means the transcript
    /// has a hole in it, and a transcript with an unmarked hole is worse than
    /// one that says so.
    AudioGap { session: u32, expected: u32, got: u32 },
    /// The device is alive. Refreshes last_seen.
    Seen,
}

#[derive(Debug, Clone, PartialEq, Eq)]
enum State {
    /// Nothing has been said yet.
    New,
    /// Hello arrived with no usable token; waiting for a code.
    AwaitingPairing,
    /// Authenticated.
    Ready,
    /// Closed. Further frames are ignored rather than acted on.
    Closed,
}

/// Looks up whether a token belongs to a device.
pub trait Authenticator: Send + Sync {
    /// Returns the stored device name when `token` is the token issued to
    /// `device_id`. Checking the pair rather than the token alone is the point:
    /// a token leaked from one device must not authenticate another.
    fn lookup(&self, device_id: &str, token: &str) -> Option<String>;
}

pub struct Session {
    state: State,
    device_id: String,
    firmware: String,
    daemon_name: String,
    /// Audio session currently being captured, and what has arrived of it.
    audio_session: Option<u32>,
    audio: Vec<i16>,
    next_seq: u32,
}

impl Session {
    pub fn new(daemon_name: impl Into<String>) -> Self {
        Self {
            state: State::New,
            device_id: String::new(),
            firmware: String::new(),
            daemon_name: daemon_name.into(),
            audio_session: None,
            audio: Vec::new(),
            next_seq: 0,
        }
    }

    pub fn device_id(&self) -> &str {
        &self.device_id
    }

    pub fn firmware(&self) -> &str {
        &self.firmware
    }

    pub fn is_ready(&self) -> bool {
        self.state == State::Ready
    }

    pub fn is_awaiting_pairing(&self) -> bool {
        self.state == State::AwaitingPairing
    }

    /// True while the microphone is streaming. The tray's live indicator reads
    /// this: it must light up because audio is arriving, not because something
    /// remembered to set a flag when it started.
    pub fn is_capturing(&self) -> bool {
        self.audio_session.is_some()
    }

    /// Handles one decoded frame payload.
    pub fn on_frame(&mut self, payload: &[u8], now: u64, auth: &dyn Authenticator) -> Vec<Action> {
        if self.state == State::Closed {
            return Vec::new();
        }
        let Ok(raw_id) = peek_id(payload) else {
            return self.close("frame is not a protocol message");
        };
        let Some(id) = MsgId::from_u16(raw_id) else {
            // A newer device sending a message this daemon has never heard of
            // is explicitly allowed by the schema's compatibility rules.
            tracing::debug!(id = raw_id, "ignoring unknown message id");
            return vec![Action::Seen];
        };

        if self.state == State::New && id != MsgId::Hello {
            return self.close("first frame was not hello");
        }

        match id {
            MsgId::Hello => self.on_hello(payload, now, auth),
            MsgId::Pair => self.on_pair(payload),
            MsgId::Ping => match Ping::decode(payload) {
                Ok(m) => vec![Action::Seen, Action::Send(Out::Pong(Pong { nonce: m.nonce }))],
                Err(_) => self.close("malformed ping"),
            },
            MsgId::Pong => vec![Action::Seen],
            MsgId::AudioChunk => self.on_audio(payload),
            MsgId::AgentRequest => self.on_agent_request(payload),
            // Everything else is ours to send, not the device's. A device
            // sending one is either broken or pretending to be the daemon.
            MsgId::HelloAck
            | MsgId::PairResult
            | MsgId::TranscriptPartial
            | MsgId::TranscriptFinal
            | MsgId::AgentToken
            | MsgId::AgentDone
            | MsgId::MoodHint
            | MsgId::Notification
            | MsgId::OtaAvailable => {
                self.close(format!("device sent a daemon message: {}", id.name()))
            }
        }
    }

    fn on_hello(&mut self, payload: &[u8], now: u64, auth: &dyn Authenticator) -> Vec<Action> {
        if self.state != State::New {
            return self.close("hello sent twice");
        }
        let Ok(hello) = Hello::decode(payload) else {
            return self.close("malformed hello");
        };

        if hello.protocol != PROTOCOL_VERSION {
            // Answered rather than dropped: the device shows "update needed"
            // instead of retrying a connection that will never work.
            let ack = HelloAck {
                accepted: false,
                needs_pairing: false,
                daemon_name: self.daemon_name.clone(),
                unix_time: now,
            };
            let mut actions = vec![Action::Send(Out::HelloAck(ack))];
            actions.extend(self.close(format!(
                "protocol {}, daemon speaks {PROTOCOL_VERSION}",
                hello.protocol
            )));
            return actions;
        }

        if hello.device_id.is_empty() {
            return self.close("hello with no device id");
        }
        self.device_id = hello.device_id.clone();
        self.firmware = hello.firmware.clone();

        // An empty token never authenticates. Worth stating here as well as in
        // the store, because "" is what an unpaired device sends and a lookup
        // that matched it would pair every new device to the first record.
        let known = if hello.token.is_empty() {
            None
        } else {
            auth.lookup(&hello.device_id, &hello.token)
        };

        match known {
            Some(_name) => {
                self.state = State::Ready;
                vec![
                    Action::Seen,
                    Action::Send(Out::HelloAck(HelloAck {
                        accepted: true,
                        needs_pairing: false,
                        daemon_name: self.daemon_name.clone(),
                        unix_time: now,
                    })),
                ]
            }
            None => {
                self.state = State::AwaitingPairing;
                vec![Action::Send(Out::HelloAck(HelloAck {
                    accepted: false,
                    needs_pairing: true,
                    daemon_name: self.daemon_name.clone(),
                    unix_time: now,
                }))]
            }
        }
    }

    fn on_pair(&mut self, payload: &[u8]) -> Vec<Action> {
        if self.state != State::AwaitingPairing {
            // A paired device asking to pair again would be a way to get a
            // second token without the user seeing a code.
            return self.close("pair outside of pairing");
        }
        let Ok(pair) = Pair::decode(payload) else {
            return self.close("malformed pair");
        };
        if !crate::pairing::is_valid_code(&pair.code) {
            return vec![Action::Send(Out::PairResult(PairResult {
                granted: false,
                token: String::new(),
                reason: "code must be six digits".into(),
            }))];
        }
        // No result yet: the grant waits on a human typing the same code into
        // the desktop app. `pair_granted` or `pair_refused` finishes it.
        vec![Action::RequestPairing {
            device_id: self.device_id.clone(),
            code: pair.code,
        }]
    }

    /// The user entered the code and the daemon issued a token.
    pub fn pair_granted(&mut self, token: String) -> Vec<Action> {
        if self.state != State::AwaitingPairing {
            return Vec::new();
        }
        self.state = State::Ready;
        vec![
            Action::Seen,
            Action::Send(Out::PairResult(PairResult {
                granted: true,
                token,
                reason: String::new(),
            })),
        ]
    }

    /// The attempt failed or timed out.
    pub fn pair_refused(&mut self, reason: impl Into<String>) -> Vec<Action> {
        if self.state != State::AwaitingPairing {
            return Vec::new();
        }
        vec![Action::Send(Out::PairResult(PairResult {
            granted: false,
            token: String::new(),
            reason: reason.into(),
        }))]
    }

    fn on_audio(&mut self, payload: &[u8]) -> Vec<Action> {
        if self.state != State::Ready {
            return self.close("audio before pairing");
        }
        let Ok(chunk) = AudioChunk::decode(payload) else {
            return self.close("malformed audio chunk");
        };
        if chunk.pcm.len() % 2 != 0 {
            return self.close("audio chunk is not whole 16-bit samples");
        }

        let mut actions = vec![Action::Seen];

        // A new session id starts a new utterance, discarding any unfinished
        // one: the device moved on, and stitching the two together would put
        // half of an abandoned sentence in front of the new one.
        if self.audio_session != Some(chunk.session) {
            self.audio_session = Some(chunk.session);
            self.audio.clear();
            self.next_seq = 0;
        } else if chunk.seq != self.next_seq {
            actions.push(Action::AudioGap {
                session: chunk.session,
                expected: self.next_seq,
                got: chunk.seq,
            });
        }
        self.next_seq = chunk.seq.wrapping_add(1);

        let samples = chunk.pcm.chunks_exact(2).map(|p| i16::from_le_bytes([p[0], p[1]]));
        if self.audio.len() + chunk.pcm.len() / 2 > MAX_UTTERANCE_SAMPLES {
            // Cut it off here and transcribe what we have rather than growing
            // without bound. The user gets a truncated note; the daemon lives.
            tracing::warn!(session = chunk.session, "utterance exceeded {MAX_UTTERANCE_SECS}s");
            let pcm = std::mem::take(&mut self.audio);
            self.audio_session = None;
            actions.push(Action::Utterance { session: chunk.session, pcm });
            return actions;
        }
        self.audio.extend(samples);

        if chunk.r#final {
            let pcm = std::mem::take(&mut self.audio);
            self.audio_session = None;
            // An empty capture — the button pressed and released — is not an
            // utterance. Transcribing silence produces confident nonsense.
            if !pcm.is_empty() {
                actions.push(Action::Utterance { session: chunk.session, pcm });
            }
        }
        actions
    }

    fn on_agent_request(&mut self, payload: &[u8]) -> Vec<Action> {
        if self.state != State::Ready {
            return self.close("agent request before pairing");
        }
        let Ok(req) = AgentRequest::decode(payload) else {
            return self.close("malformed agent request");
        };
        // The newest question wins. Which turn is live is the connection's
        // business rather than the protocol's — it is whichever task is still
        // running — so this only reports the request.
        vec![
            Action::Seen,
            Action::Ask { turn: req.turn, text: req.text, app: req.app },
        ]
    }

    fn close(&mut self, why: impl Into<String>) -> Vec<Action> {
        self.state = State::Closed;
        self.audio.clear();
        self.audio_session = None;
        vec![Action::Close(why.into())]
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;

    const NOW: u64 = 1_700_000_000;

    #[derive(Default)]
    struct FakeAuth {
        by_device: HashMap<String, String>,
    }

    impl FakeAuth {
        fn with(device_id: &str, token: &str) -> Self {
            let mut by_device = HashMap::new();
            by_device.insert(device_id.to_string(), token.to_string());
            Self { by_device }
        }
    }

    impl Authenticator for FakeAuth {
        fn lookup(&self, device_id: &str, token: &str) -> Option<String> {
            match self.by_device.get(device_id) {
                Some(t) if t == token && !token.is_empty() => Some("desk robot".into()),
                _ => None,
            }
        }
    }

    fn hello(token: &str) -> Vec<u8> {
        Hello {
            protocol: PROTOCOL_VERSION,
            device_id: "nev-abc".into(),
            firmware: "0.1.0".into(),
            token: token.into(),
        }
        .encode()
    }

    fn ready_session() -> (Session, FakeAuth) {
        let auth = FakeAuth::with("nev-abc", "good-token");
        let mut s = Session::new("studio-mac");
        let actions = s.on_frame(&hello("good-token"), NOW, &auth);
        assert!(s.is_ready(), "{actions:?}");
        (s, auth)
    }

    fn audio(session: u32, seq: u32, samples: &[i16], last: bool) -> Vec<u8> {
        let mut pcm = Vec::new();
        for s in samples {
            pcm.extend_from_slice(&s.to_le_bytes());
        }
        AudioChunk { seq, session, r#final: last, pcm }.encode()
    }

    #[test]
    fn a_known_token_is_accepted_and_carries_the_clock() {
        let (mut s, auth) = ready_session();
        let acks: Vec<_> = s
            .on_frame(&Ping { nonce: 9 }.encode(), NOW, &auth)
            .into_iter()
            .filter(|a| matches!(a, Action::Send(Out::Pong(_))))
            .collect();
        assert_eq!(acks, vec![Action::Send(Out::Pong(Pong { nonce: 9 }))]);

        let mut fresh = Session::new("studio-mac");
        let actions = fresh.on_frame(&hello("good-token"), NOW, &auth);
        match &actions[1] {
            Action::Send(Out::HelloAck(ack)) => {
                assert!(ack.accepted);
                assert!(!ack.needs_pairing);
                // The device has no RTC; if this is wrong every note is
                // timestamped wrong and nothing on the device can tell.
                assert_eq!(ack.unix_time, NOW);
                assert_eq!(ack.daemon_name, "studio-mac");
            }
            other => panic!("expected hello_ack, got {other:?}"),
        }
    }

    #[test]
    fn an_unknown_token_is_told_to_pair_rather_than_dropped() {
        let auth = FakeAuth::with("nev-abc", "good-token");
        let mut s = Session::new("studio-mac");
        let actions = s.on_frame(&hello("stolen-token"), NOW, &auth);
        assert!(s.is_awaiting_pairing());
        assert_eq!(
            actions,
            vec![Action::Send(Out::HelloAck(HelloAck {
                accepted: false,
                needs_pairing: true,
                daemon_name: "studio-mac".into(),
                unix_time: NOW,
            }))]
        );
    }

    #[test]
    fn one_devices_token_does_not_authenticate_another() {
        // The check is on the pair, not the token, so a token read out of one
        // robot's flash is useless in a second robot.
        let auth = FakeAuth::with("nev-abc", "good-token");
        let mut s = Session::new("d");
        let other = Hello {
            protocol: PROTOCOL_VERSION,
            device_id: "nev-xyz".into(),
            firmware: "0.1.0".into(),
            token: "good-token".into(),
        }
        .encode();
        s.on_frame(&other, NOW, &auth);
        assert!(s.is_awaiting_pairing(), "a borrowed token must not be accepted");
    }

    #[test]
    fn an_empty_token_never_authenticates() {
        let mut auth = FakeAuth::default();
        auth.by_device.insert("nev-abc".into(), String::new());
        let mut s = Session::new("d");
        s.on_frame(&hello(""), NOW, &auth);
        assert!(s.is_awaiting_pairing());
    }

    #[test]
    fn a_protocol_mismatch_is_answered_then_closed() {
        let auth = FakeAuth::default();
        let mut s = Session::new("d");
        let wrong = Hello {
            protocol: PROTOCOL_VERSION + 1,
            device_id: "nev-abc".into(),
            firmware: "9.9.9".into(),
            token: String::new(),
        }
        .encode();
        let actions = s.on_frame(&wrong, NOW, &auth);
        assert!(matches!(actions[0], Action::Send(Out::HelloAck(_))));
        assert!(matches!(actions[1], Action::Close(_)));
        assert!(!s.is_ready());
    }

    #[test]
    fn nothing_is_accepted_before_hello() {
        let auth = FakeAuth::default();
        for frame in [
            Ping { nonce: 1 }.encode(),
            audio(1, 0, &[1, 2, 3], true),
            AgentRequest { turn: 1, text: "hi".into(), app: "agent".into() }.encode(),
            Pair { code: "123456".into() }.encode(),
        ] {
            let mut s = Session::new("d");
            assert!(
                matches!(s.on_frame(&frame, NOW, &auth).as_slice(), [Action::Close(_)]),
                "an unauthenticated frame was acted on"
            );
        }
    }

    #[test]
    fn a_device_impersonating_the_daemon_is_closed() {
        let (mut s, auth) = ready_session();
        let frame = PairResult { granted: true, token: "mine-now".into(), reason: String::new() }
            .encode();
        assert!(matches!(s.on_frame(&frame, NOW, &auth).as_slice(), [Action::Close(_)]));
    }

    #[test]
    fn a_frame_after_close_does_nothing() {
        let (mut s, auth) = ready_session();
        s.on_frame(&hello("good-token"), NOW, &auth); // hello twice closes
        assert!(s.on_frame(&Ping { nonce: 1 }.encode(), NOW, &auth).is_empty());
    }

    #[test]
    fn an_unknown_message_id_is_ignored_not_fatal() {
        // A newer device must be able to talk to an older daemon.
        let (mut s, auth) = ready_session();
        let mut w = nevos_proto::cbor::CborWriter::new();
        w.array(2);
        w.u64(999);
        w.u64(0);
        let actions = s.on_frame(&w.finish(), NOW, &auth);
        assert_eq!(actions, vec![Action::Seen]);
        assert!(s.is_ready());
    }

    #[test]
    fn garbage_closes_the_connection() {
        let (mut s, auth) = ready_session();
        assert!(matches!(
            s.on_frame(&[0xff, 0xff, 0xff], NOW, &auth).as_slice(),
            [Action::Close(_)]
        ));
    }

    #[test]
    fn audio_accumulates_and_the_final_chunk_delivers_it() {
        let (mut s, auth) = ready_session();
        s.on_frame(&audio(4, 0, &[1, 2], false), NOW, &auth);
        assert!(s.is_capturing(), "the tray indicator must be lit while audio arrives");
        s.on_frame(&audio(4, 1, &[3, 4], false), NOW, &auth);
        let actions = s.on_frame(&audio(4, 2, &[5, 6], true), NOW, &auth);

        assert_eq!(
            actions.last(),
            Some(&Action::Utterance { session: 4, pcm: vec![1, 2, 3, 4, 5, 6] })
        );
        assert!(!s.is_capturing(), "capture must end with the final chunk");
    }

    #[test]
    fn a_gap_in_the_sequence_is_reported() {
        let (mut s, auth) = ready_session();
        s.on_frame(&audio(4, 0, &[1], false), NOW, &auth);
        let actions = s.on_frame(&audio(4, 2, &[2], false), NOW, &auth);
        assert!(actions.contains(&Action::AudioGap { session: 4, expected: 1, got: 2 }));
        // The audio still counts: a hole is better than nothing.
        let actions = s.on_frame(&audio(4, 3, &[3], true), NOW, &auth);
        assert_eq!(
            actions.last(),
            Some(&Action::Utterance { session: 4, pcm: vec![1, 2, 3] })
        );
    }

    #[test]
    fn a_new_session_discards_an_unfinished_one() {
        let (mut s, auth) = ready_session();
        s.on_frame(&audio(1, 0, &[9, 9, 9], false), NOW, &auth);
        let actions = s.on_frame(&audio(2, 0, &[1, 2], true), NOW, &auth);
        assert_eq!(actions.last(), Some(&Action::Utterance { session: 2, pcm: vec![1, 2] }));
    }

    #[test]
    fn an_empty_capture_produces_no_utterance() {
        let (mut s, auth) = ready_session();
        let actions = s.on_frame(&audio(1, 0, &[], true), NOW, &auth);
        assert!(!actions.iter().any(|a| matches!(a, Action::Utterance { .. })));
    }

    #[test]
    fn an_odd_pcm_length_closes_the_connection() {
        let (mut s, auth) = ready_session();
        let frame = AudioChunk { seq: 0, session: 1, r#final: true, pcm: vec![1, 2, 3] }.encode();
        assert!(matches!(s.on_frame(&frame, NOW, &auth).as_slice(), [Action::Close(_)]));
    }

    #[test]
    fn a_device_that_never_stops_talking_is_cut_off() {
        // A stuck button must not be able to exhaust the daemon's memory.
        let (mut s, auth) = ready_session();
        // 2048 samples is 4096 bytes, the largest pcm field the schema allows,
        // so this is the fastest a device can legally fill the buffer.
        let block = vec![7i16; 2048];
        let mut delivered = None;
        for seq in 0..(MAX_UTTERANCE_SAMPLES / 2048 + 4) as u32 {
            for action in s.on_frame(&audio(1, seq, &block, false), NOW, &auth) {
                if let Action::Utterance { pcm, .. } = action {
                    delivered = Some(pcm.len());
                }
            }
            if delivered.is_some() {
                break;
            }
        }
        let len = delivered.expect("the daemon buffered without bound");
        assert!(len <= MAX_UTTERANCE_SAMPLES, "{len} samples retained");
        assert!(!s.is_capturing());
    }

    #[test]
    fn a_question_becomes_an_ask_carrying_its_turn() {
        let (mut s, auth) = ready_session();
        let actions = s.on_frame(
            &AgentRequest { turn: 2, text: "second".into(), app: "notes".into() }.encode(),
            NOW,
            &auth,
        );
        assert_eq!(
            actions.last(),
            Some(&Action::Ask { turn: 2, text: "second".into(), app: "notes".into() })
        );
    }

    #[test]
    fn pairing_waits_for_the_human_then_grants() {
        let auth = FakeAuth::default();
        let mut s = Session::new("studio-mac");
        s.on_frame(&hello(""), NOW, &auth);

        let actions = s.on_frame(&Pair { code: "123456".into() }.encode(), NOW, &auth);
        assert_eq!(
            actions,
            vec![Action::RequestPairing { device_id: "nev-abc".into(), code: "123456".into() }],
            "no token may be issued before someone types the code"
        );
        assert!(!s.is_ready());

        let actions = s.pair_granted("tok".into());
        assert!(matches!(
            actions.last(),
            Some(Action::Send(Out::PairResult(PairResult { granted: true, .. })))
        ));
        assert!(s.is_ready());
    }

    #[test]
    fn a_paired_device_cannot_ask_to_pair_again() {
        let (mut s, auth) = ready_session();
        let actions = s.on_frame(&Pair { code: "123456".into() }.encode(), NOW, &auth);
        assert!(matches!(actions.as_slice(), [Action::Close(_)]));
    }

    #[test]
    fn a_malformed_code_is_refused_without_closing() {
        // A device with a display bug should be told, not disconnected.
        let auth = FakeAuth::default();
        let mut s = Session::new("d");
        s.on_frame(&hello(""), NOW, &auth);
        let actions = s.on_frame(&Pair { code: "12".into() }.encode(), NOW, &auth);
        assert!(matches!(
            actions.as_slice(),
            [Action::Send(Out::PairResult(PairResult { granted: false, .. }))]
        ));
        assert!(s.is_awaiting_pairing());
    }

    #[test]
    fn every_outbound_message_frames() {
        // Encoding is generated and tested elsewhere; what is checked here is
        // that Out covers the daemon's half of the protocol and nothing in it
        // produces an over-long frame.
        let outs = [
            Out::HelloAck(HelloAck::default()),
            Out::PairResult(PairResult::default()),
            Out::Pong(Pong::default()),
            Out::TranscriptPartial(TranscriptPartial::default()),
            Out::TranscriptFinal(TranscriptFinal::default()),
            Out::AgentToken(AgentToken::default()),
            Out::AgentDone(AgentDone::default()),
            Out::MoodHint(MoodHint::default()),
            Out::Notification(Notification::default()),
            Out::OtaAvailable(OtaAvailable::default()),
        ];
        for out in outs {
            let frame = out.to_frame().expect("encodes");
            assert!(frame.len() >= 4);
        }
    }
}
