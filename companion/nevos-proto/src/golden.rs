// GENERATED FROM schema/nevos.toml BY tools/schema/gen.py — DO NOT EDIT
//
// The same canonical vectors the C tests use. If the two codecs ever disagree,
// one of these suites fails rather than a field arriving corrupt in production.
#![allow(clippy::all)]

use crate::generated::*;

pub const GOLDEN_HELLO: &[u8] = &[
    0x85, 0x01, 0x07, 0x6B, 0x64, 0x65, 0x76, 0x69, 0x63, 0x65, 0x5F, 0x69, 0x64, 0x2D, 0x31, 0x6A, 0x66, 0x69, 0x72, 0x6D, 0x77, 0x61, 0x72, 0x65, 0x2D, 0x32, 0x67, 0x74, 0x6F, 0x6B, 0x65, 0x6E, 0x2D, 0x33
];

pub fn sample_hello() -> Hello {
    Hello {
        protocol: 7,
        device_id: "device_id-1".to_string(),
        firmware: "firmware-2".to_string(),
        token: "token-3".to_string(),
    }
}

pub const GOLDEN_HELLO_ACK: &[u8] = &[
    0x85, 0x02, 0xF5, 0xF4, 0x6D, 0x64, 0x61, 0x65, 0x6D, 0x6F, 0x6E, 0x5F, 0x6E, 0x61, 0x6D, 0x65, 0x2D, 0x32, 0x18, 0x1C
];

pub fn sample_hello_ack() -> HelloAck {
    HelloAck {
        accepted: true,
        needs_pairing: false,
        daemon_name: "daemon_name-2".to_string(),
        unix_time: 28,
    }
}

pub const GOLDEN_PAIR: &[u8] = &[
    0x82, 0x03, 0x66, 0x63, 0x6F, 0x64, 0x65, 0x2D, 0x30
];

pub fn sample_pair() -> Pair {
    Pair {
        code: "code-0".to_string(),
    }
}

pub const GOLDEN_PAIR_RESULT: &[u8] = &[
    0x84, 0x04, 0xF5, 0x67, 0x74, 0x6F, 0x6B, 0x65, 0x6E, 0x2D, 0x31, 0x68, 0x72, 0x65, 0x61, 0x73, 0x6F, 0x6E, 0x2D, 0x32
];

pub fn sample_pair_result() -> PairResult {
    PairResult {
        granted: true,
        token: "token-1".to_string(),
        reason: "reason-2".to_string(),
    }
}

pub const GOLDEN_PING: &[u8] = &[
    0x82, 0x05, 0x07
];

pub fn sample_ping() -> Ping {
    Ping {
        nonce: 7,
    }
}

pub const GOLDEN_PONG: &[u8] = &[
    0x82, 0x06, 0x07
];

pub fn sample_pong() -> Pong {
    Pong {
        nonce: 7,
    }
}

pub const GOLDEN_AUDIO_CHUNK: &[u8] = &[
    0x86, 0x10, 0x07, 0x0E, 0xF5, 0x48, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x18, 0x23
];

pub fn sample_audio_chunk() -> AudioChunk {
    AudioChunk {
        seq: 7,
        session: 14,
        r#final: true,
        pcm: vec![0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A],
        kind: 35,
    }
}

pub const GOLDEN_CAPTURE_MARKER: &[u8] = &[
    0x83, 0x13, 0x07, 0xFA, 0x3F, 0xC0, 0x00, 0x00
];

pub fn sample_capture_marker() -> CaptureMarker {
    CaptureMarker {
        session: 7,
        at_seconds: 1.5f32,
    }
}

pub const GOLDEN_TRANSCRIPT_PARTIAL: &[u8] = &[
    0x83, 0x11, 0x07, 0x66, 0x74, 0x65, 0x78, 0x74, 0x2D, 0x31
];

pub fn sample_transcript_partial() -> TranscriptPartial {
    TranscriptPartial {
        session: 7,
        text: "text-1".to_string(),
    }
}

pub const GOLDEN_TRANSCRIPT_FINAL: &[u8] = &[
    0x84, 0x12, 0x07, 0x66, 0x74, 0x65, 0x78, 0x74, 0x2D, 0x31, 0xFA, 0x40, 0x20, 0x00, 0x00
];

pub fn sample_transcript_final() -> TranscriptFinal {
    TranscriptFinal {
        session: 7,
        text: "text-1".to_string(),
        confidence: 2.5f32,
    }
}

pub const GOLDEN_AGENT_REQUEST: &[u8] = &[
    0x84, 0x18, 0x20, 0x07, 0x66, 0x74, 0x65, 0x78, 0x74, 0x2D, 0x31, 0x65, 0x61, 0x70, 0x70, 0x2D, 0x32
];

pub fn sample_agent_request() -> AgentRequest {
    AgentRequest {
        turn: 7,
        text: "text-1".to_string(),
        app: "app-2".to_string(),
    }
}

pub const GOLDEN_AGENT_TOKEN: &[u8] = &[
    0x83, 0x18, 0x21, 0x07, 0x66, 0x74, 0x65, 0x78, 0x74, 0x2D, 0x31
];

pub fn sample_agent_token() -> AgentToken {
    AgentToken {
        turn: 7,
        text: "text-1".to_string(),
    }
}

pub const GOLDEN_AGENT_DONE: &[u8] = &[
    0x83, 0x18, 0x22, 0x07, 0x67, 0x65, 0x72, 0x72, 0x6F, 0x72, 0x2D, 0x31
];

pub fn sample_agent_done() -> AgentDone {
    AgentDone {
        turn: 7,
        error: "error-1".to_string(),
    }
}

pub const GOLDEN_MOOD_HINT: &[u8] = &[
    0x84, 0x18, 0x30, 0x07, 0x0E, 0x15
];

pub fn sample_mood_hint() -> MoodHint {
    MoodHint {
        mood: 7,
        intensity: 14,
        duration_ms: 21,
    }
}

pub const GOLDEN_NOTIFICATION: &[u8] = &[
    0x84, 0x18, 0x31, 0x67, 0x74, 0x69, 0x74, 0x6C, 0x65, 0x2D, 0x30, 0x66, 0x62, 0x6F, 0x64, 0x79, 0x2D, 0x31, 0xF5
];

pub fn sample_notification() -> Notification {
    Notification {
        title: "title-0".to_string(),
        body: "body-1".to_string(),
        urgent: true,
    }
}

pub const GOLDEN_OTA_AVAILABLE: &[u8] = &[
    0x85, 0x18, 0x32, 0x69, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6F, 0x6E, 0x2D, 0x30, 0x65, 0x75, 0x72, 0x6C, 0x2D, 0x31, 0x15, 0x48, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A
];

pub fn sample_ota_available() -> OtaAvailable {
    OtaAvailable {
        version: "version-0".to_string(),
        url: "url-1".to_string(),
        size_bytes: 21,
        sha256: vec![0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A],
    }
}

#[cfg(test)]
mod golden_tests {
    use super::*;

    #[test]
    fn every_message_matches_the_reference_bytes() {
        let want = sample_hello();
        assert_eq!(want.encode(), GOLDEN_HELLO, "hello: encoded bytes differ from the reference");
        let got = Hello::decode(GOLDEN_HELLO).expect("hello: failed to decode the reference bytes");
        assert_eq!(got, want, "hello: decoded fields differ from the reference");

        let want = sample_hello_ack();
        assert_eq!(want.encode(), GOLDEN_HELLO_ACK, "hello_ack: encoded bytes differ from the reference");
        let got = HelloAck::decode(GOLDEN_HELLO_ACK).expect("hello_ack: failed to decode the reference bytes");
        assert_eq!(got, want, "hello_ack: decoded fields differ from the reference");

        let want = sample_pair();
        assert_eq!(want.encode(), GOLDEN_PAIR, "pair: encoded bytes differ from the reference");
        let got = Pair::decode(GOLDEN_PAIR).expect("pair: failed to decode the reference bytes");
        assert_eq!(got, want, "pair: decoded fields differ from the reference");

        let want = sample_pair_result();
        assert_eq!(want.encode(), GOLDEN_PAIR_RESULT, "pair_result: encoded bytes differ from the reference");
        let got = PairResult::decode(GOLDEN_PAIR_RESULT).expect("pair_result: failed to decode the reference bytes");
        assert_eq!(got, want, "pair_result: decoded fields differ from the reference");

        let want = sample_ping();
        assert_eq!(want.encode(), GOLDEN_PING, "ping: encoded bytes differ from the reference");
        let got = Ping::decode(GOLDEN_PING).expect("ping: failed to decode the reference bytes");
        assert_eq!(got, want, "ping: decoded fields differ from the reference");

        let want = sample_pong();
        assert_eq!(want.encode(), GOLDEN_PONG, "pong: encoded bytes differ from the reference");
        let got = Pong::decode(GOLDEN_PONG).expect("pong: failed to decode the reference bytes");
        assert_eq!(got, want, "pong: decoded fields differ from the reference");

        let want = sample_audio_chunk();
        assert_eq!(want.encode(), GOLDEN_AUDIO_CHUNK, "audio_chunk: encoded bytes differ from the reference");
        let got = AudioChunk::decode(GOLDEN_AUDIO_CHUNK).expect("audio_chunk: failed to decode the reference bytes");
        assert_eq!(got, want, "audio_chunk: decoded fields differ from the reference");

        let want = sample_capture_marker();
        assert_eq!(want.encode(), GOLDEN_CAPTURE_MARKER, "capture_marker: encoded bytes differ from the reference");
        let got = CaptureMarker::decode(GOLDEN_CAPTURE_MARKER).expect("capture_marker: failed to decode the reference bytes");
        assert_eq!(got, want, "capture_marker: decoded fields differ from the reference");

        let want = sample_transcript_partial();
        assert_eq!(want.encode(), GOLDEN_TRANSCRIPT_PARTIAL, "transcript_partial: encoded bytes differ from the reference");
        let got = TranscriptPartial::decode(GOLDEN_TRANSCRIPT_PARTIAL).expect("transcript_partial: failed to decode the reference bytes");
        assert_eq!(got, want, "transcript_partial: decoded fields differ from the reference");

        let want = sample_transcript_final();
        assert_eq!(want.encode(), GOLDEN_TRANSCRIPT_FINAL, "transcript_final: encoded bytes differ from the reference");
        let got = TranscriptFinal::decode(GOLDEN_TRANSCRIPT_FINAL).expect("transcript_final: failed to decode the reference bytes");
        assert_eq!(got, want, "transcript_final: decoded fields differ from the reference");

        let want = sample_agent_request();
        assert_eq!(want.encode(), GOLDEN_AGENT_REQUEST, "agent_request: encoded bytes differ from the reference");
        let got = AgentRequest::decode(GOLDEN_AGENT_REQUEST).expect("agent_request: failed to decode the reference bytes");
        assert_eq!(got, want, "agent_request: decoded fields differ from the reference");

        let want = sample_agent_token();
        assert_eq!(want.encode(), GOLDEN_AGENT_TOKEN, "agent_token: encoded bytes differ from the reference");
        let got = AgentToken::decode(GOLDEN_AGENT_TOKEN).expect("agent_token: failed to decode the reference bytes");
        assert_eq!(got, want, "agent_token: decoded fields differ from the reference");

        let want = sample_agent_done();
        assert_eq!(want.encode(), GOLDEN_AGENT_DONE, "agent_done: encoded bytes differ from the reference");
        let got = AgentDone::decode(GOLDEN_AGENT_DONE).expect("agent_done: failed to decode the reference bytes");
        assert_eq!(got, want, "agent_done: decoded fields differ from the reference");

        let want = sample_mood_hint();
        assert_eq!(want.encode(), GOLDEN_MOOD_HINT, "mood_hint: encoded bytes differ from the reference");
        let got = MoodHint::decode(GOLDEN_MOOD_HINT).expect("mood_hint: failed to decode the reference bytes");
        assert_eq!(got, want, "mood_hint: decoded fields differ from the reference");

        let want = sample_notification();
        assert_eq!(want.encode(), GOLDEN_NOTIFICATION, "notification: encoded bytes differ from the reference");
        let got = Notification::decode(GOLDEN_NOTIFICATION).expect("notification: failed to decode the reference bytes");
        assert_eq!(got, want, "notification: decoded fields differ from the reference");

        let want = sample_ota_available();
        assert_eq!(want.encode(), GOLDEN_OTA_AVAILABLE, "ota_available: encoded bytes differ from the reference");
        let got = OtaAvailable::decode(GOLDEN_OTA_AVAILABLE).expect("ota_available: failed to decode the reference bytes");
        assert_eq!(got, want, "ota_available: decoded fields differ from the reference");

    }
}
