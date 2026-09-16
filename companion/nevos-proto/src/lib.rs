//! The NEVOS bridge wire protocol.
//!
//! `generated.rs` and `golden.rs` are produced from `schema/nevos.toml` by
//! `tools/schema/gen.py` — the same run that produces the firmware's C codec.
//! Neither side is hand-written, so the two cannot drift: a field added to the
//! schema that one side forgets to handle is a build failure rather than a
//! corrupt value discovered in production.
//!
//! `cargo test` checks this crate against the same golden vectors the firmware
//! tests use, so an encoding disagreement fails a build rather than a session.

pub mod cbor;
pub mod generated;

#[cfg(test)]
mod golden;

pub use cbor::ProtoError;
pub use generated::*;

#[cfg(test)]
mod tests {
    use super::golden::*;
    use super::*;

    /// Encoding must be byte-identical to the Python reference, which is also
    /// what the firmware is checked against. Canonical CBOR has exactly one
    /// encoding per value, so any difference means an implementation drifted.
    macro_rules! golden_round_trip {
        ($sample:ident, $bytes:ident, $ty:ty) => {
            let want = $sample();
            assert_eq!(
                want.encode(),
                $bytes,
                "encoded bytes differ from the reference"
            );
            let decoded = <$ty>::decode($bytes).expect("failed to decode the reference bytes");
            assert_eq!(decoded, want, "decoded fields differ from the reference");
        };
    }

    #[test]
    fn golden_vectors_match_byte_for_byte() {
        golden_round_trip!(sample_hello, GOLDEN_HELLO, Hello);
        golden_round_trip!(sample_hello_ack, GOLDEN_HELLO_ACK, HelloAck);
        golden_round_trip!(sample_pair, GOLDEN_PAIR, Pair);
        golden_round_trip!(sample_pair_result, GOLDEN_PAIR_RESULT, PairResult);
        golden_round_trip!(sample_ping, GOLDEN_PING, Ping);
        golden_round_trip!(sample_pong, GOLDEN_PONG, Pong);
        golden_round_trip!(sample_audio_chunk, GOLDEN_AUDIO_CHUNK, AudioChunk);
        golden_round_trip!(
            sample_transcript_partial,
            GOLDEN_TRANSCRIPT_PARTIAL,
            TranscriptPartial
        );
        golden_round_trip!(
            sample_transcript_final,
            GOLDEN_TRANSCRIPT_FINAL,
            TranscriptFinal
        );
        golden_round_trip!(sample_agent_request, GOLDEN_AGENT_REQUEST, AgentRequest);
        golden_round_trip!(sample_agent_token, GOLDEN_AGENT_TOKEN, AgentToken);
        golden_round_trip!(sample_agent_done, GOLDEN_AGENT_DONE, AgentDone);
        golden_round_trip!(sample_mood_hint, GOLDEN_MOOD_HINT, MoodHint);
        golden_round_trip!(sample_notification, GOLDEN_NOTIFICATION, Notification);
        golden_round_trip!(sample_ota_available, GOLDEN_OTA_AVAILABLE, OtaAvailable);
    }

    #[test]
    fn peek_reads_the_id_without_decoding() {
        assert_eq!(peek_id(GOLDEN_MOOD_HINT).unwrap(), MoodHint::ID);
        assert_eq!(MsgId::from_u16(MoodHint::ID).unwrap().name(), "mood_hint");
        assert!(MsgId::from_u16(9999).is_none());
    }

    #[test]
    fn decoding_as_the_wrong_message_is_refused() {
        assert_eq!(Ping::decode(GOLDEN_PONG), Err(ProtoError::WrongMessage));
    }

    /// A newer peer appends a field. An older decoder must ignore it, or every
    /// schema addition becomes a flag day for every deployed device.
    #[test]
    fn trailing_fields_from_a_newer_peer_are_ignored() {
        let mut w = cbor::CborWriter::new();
        w.array(3);
        w.u64(Ping::ID as u64);
        w.u64(4242);
        w.text("a field this build has never heard of");
        let decoded = Ping::decode(&w.finish()).expect("refused a message from a newer peer");
        assert_eq!(decoded.nonce, 4242);
    }

    /// An older peer omits a field. It must decode with that field at its zero
    /// value rather than failing.
    #[test]
    fn missing_trailing_fields_default_to_zero() {
        let mut w = cbor::CborWriter::new();
        w.array(3);
        w.u64(Hello::ID as u64);
        w.u64(1);
        w.text("abc123");
        let decoded = Hello::decode(&w.finish()).expect("refused a message from an older peer");
        assert_eq!(decoded.protocol, 1);
        assert_eq!(decoded.device_id, "abc123");
        assert_eq!(decoded.firmware, "");
    }

    #[test]
    fn a_string_past_its_schema_bound_is_refused() {
        let mut w = cbor::CborWriter::new();
        w.array(2);
        w.u64(Pair::ID as u64);
        w.text(&"x".repeat(128)); // pair.code is capped at 12
        assert_eq!(Pair::decode(&w.finish()), Err(ProtoError::TooLong));
    }

    #[test]
    fn frames_wrap_and_split() {
        let framed = frame_wrap(GOLDEN_PING).unwrap();
        let (range, len) = frame_split(&framed)
            .unwrap()
            .expect("a whole frame read as partial");
        assert_eq!(len, framed.len());
        assert_eq!(&framed[range], GOLDEN_PING);
    }

    /// A stream delivers frames in arbitrary pieces; a partial one is "wait".
    #[test]
    fn a_partial_frame_asks_for_more() {
        let framed = frame_wrap(GOLDEN_HELLO).unwrap();
        for have in 0..framed.len() {
            assert!(
                frame_split(&framed[..have]).unwrap().is_none(),
                "a partial frame was treated as complete"
            );
        }
    }

    /// A length prefix is the first thing a peer controls.
    #[test]
    fn an_absurd_length_prefix_is_refused() {
        assert_eq!(
            frame_split(&[0xFF, 0xFF, 0xFF, 0xFF, 0x00]),
            Err(ProtoError::TooLong)
        );
    }

    /// Arbitrary bytes must never panic. Rust's bounds checks would turn an
    /// indexing mistake into an abort, which in the daemon means a dropped
    /// session for every device, not just the one that sent the bad frame.
    #[test]
    fn fuzz_no_decoder_panics() {
        let mut seed: u32 = 0x5EED_1234;
        let mut noise = [0u8; 128];

        for iter in 0..20_000 {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            let n = 1 + (seed as usize % noise.len());
            let mut s = seed;
            for b in noise.iter_mut().take(n) {
                s = s.wrapping_mul(1664525).wrapping_add(1013904223);
                *b = (s >> 24) as u8;
            }
            // Sometimes give it a valid header, so the field decoders are
            // reached rather than every blob being rejected at the array.
            if iter % 4 == 0 && n > 3 {
                noise[0] = 0x84;
                noise[1] = 0x01;
            }
            let data = &noise[..n];

            let _ = Hello::decode(data);
            let _ = HelloAck::decode(data);
            let _ = PairResult::decode(data);
            let _ = AudioChunk::decode(data);
            let _ = TranscriptFinal::decode(data);
            let _ = AgentToken::decode(data);
            let _ = MoodHint::decode(data);
            let _ = OtaAvailable::decode(data);
            let _ = peek_id(data);
            let _ = frame_split(data);
        }
    }
}
