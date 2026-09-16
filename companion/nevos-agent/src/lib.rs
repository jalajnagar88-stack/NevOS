//! The NEVOS agent: a language model that runs on the user's own machine.
//!
//! There is no cloud backend in this crate and there is not meant to be one.
//! The device is a small always-listening object in someone's home, and the
//! cheapest way to be trustworthy about that — as well as the cheapest way to
//! run it, since a companion that is spoken to all day would otherwise bill per
//! word — is for every token to be generated on hardware the owner controls.
//!
//! The backend is a trait rather than a concrete type because "local" covers
//! several things: Ollama today, a raw llama.cpp server or a future runtime
//! tomorrow. `is_local()` exists so the tray can state, from the object itself
//! rather than from a config file it hopes matches, whether anything leaves the
//! machine. A backend that answered `false` would have to be added deliberately.
pub mod http;
pub mod mood;
pub mod ollama;

use anyhow::Result;
use async_trait::async_trait;
use serde::{Deserialize, Serialize};
use tokio::sync::mpsc;

pub use mood::{Mood, MoodHint};
pub use ollama::OllamaAgent;

/// Who said a thing.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum Role {
    User,
    Assistant,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Turn {
    pub role: Role,
    pub text: String,
}

impl Turn {
    pub fn user(text: impl Into<String>) -> Self {
        Self { role: Role::User, text: text.into() }
    }
    pub fn assistant(text: impl Into<String>) -> Self {
        Self { role: Role::Assistant, text: text.into() }
    }
}

/// One exchange, as it arrives from the device.
#[derive(Debug, Clone)]
pub struct AgentRequest {
    /// The device's turn counter. Echoed on every token so a late reply from an
    /// abandoned turn can be dropped rather than typed over the new one.
    pub turn: u32,
    pub text: String,
    /// Which app asked. Lets the prompt differ between the notes app and a
    /// game's between-rounds banter without the device having to explain itself.
    pub app: String,
}

/// What a backend emits while it works.
#[derive(Debug, Clone)]
pub enum AgentEvent {
    /// A fragment of the reply, to be rendered as it arrives.
    Token(String),
    /// A face to wear. Sent at most twice per turn: once on the way in, once
    /// when the finished text has been read.
    Mood(MoodHint),
}

#[async_trait]
pub trait Agent: Send + Sync {
    /// Shown in the tray and the logs.
    fn name(&self) -> &str;

    /// True when generation happens on this machine and no request leaves it.
    /// The tray reads this; it is a statement about the transport, not a hope.
    fn is_local(&self) -> bool;

    /// Streams a reply into `events`.
    ///
    /// The backend must return when the sender is closed: that is how a user
    /// walking away, or asking something else, stops the model mid-sentence.
    async fn respond(
        &self,
        req: &AgentRequest,
        history: &[Turn],
        events: mpsc::Sender<AgentEvent>,
    ) -> Result<()>;
}

/// The character, and the shape the answer has to fit.
///
/// The length rule is not a stylistic preference: replies are read at a glance
/// on a 480x480 screen from across a desk, and a local 3B model left to its own
/// devices writes five paragraphs with a numbered list in it. Stating the
/// constraint in the prompt is far cheaper than truncating afterwards, which
/// cuts mid-sentence and looks broken.
pub const SYSTEM_PROMPT: &str = "\
You are NEVOS, the voice of a small desk companion robot with a round screen \
that shows a pair of expressive eyes. You are warm, dry, and brief.

Rules:
- Two or three sentences at most. The screen is small and the person is reading \
you from across a desk.
- Plain prose. No markdown, no bullet points, no headings, no emoji.
- Say the answer first. If you are unsure, say so in a few words rather than \
hedging at length.
- Never describe your own expression or narrate actions. The face handles that.
- You run entirely on this person's own computer. If asked, say so plainly.";

/// A per-device rolling context.
///
/// Bounded twice over: by number of turns and by total characters. A local
/// model's context window is the scarce resource on the companion machine, and
/// an unbounded history makes the first slow reply arrive after an hour of
/// chatting, which reads as the device breaking rather than as a full buffer.
#[derive(Debug, Clone)]
pub struct Conversation {
    turns: Vec<Turn>,
    max_turns: usize,
    max_chars: usize,
}

impl Conversation {
    pub fn new(max_turns: usize, max_chars: usize) -> Self {
        Self { turns: Vec::new(), max_turns, max_chars }
    }

    pub fn push(&mut self, turn: Turn) {
        self.turns.push(turn);
        self.trim();
    }

    pub fn turns(&self) -> &[Turn] {
        &self.turns
    }

    pub fn clear(&mut self) {
        self.turns.clear();
    }

    fn trim(&mut self) {
        while self.turns.len() > self.max_turns {
            self.turns.remove(0);
        }
        // Drop from the front until the budget is met, but never drop the last
        // turn: a context of nothing at all cannot answer "and the other one?"
        // any better than a full one, and an empty history is a worse bug
        // because it looks like amnesia rather than like forgetting.
        while self.turns.len() > 1 && self.total_chars() > self.max_chars {
            self.turns.remove(0);
        }
    }

    fn total_chars(&self) -> usize {
        self.turns.iter().map(|t| t.text.chars().count()).sum()
    }
}

impl Default for Conversation {
    fn default() -> Self {
        // Eight turns is about four exchanges, which is as far back as anyone
        // refers by pronoun in practice.
        Self::new(8, 4000)
    }
}

/// Splits text into pieces that fit a wire field, without cutting a character
/// in half.
///
/// `agent_token.text` is capped at 512 bytes by the schema and the device's
/// decoder rejects an over-long string outright, so this is the difference
/// between a streamed reply and a dropped frame. Byte-indexing a `String` at
/// 512 would panic on any reply containing a non-ASCII character, which in
/// practice means the first one with an apostrophe the model curled.
pub fn split_for_frames(text: &str, max_bytes: usize) -> Vec<&str> {
    assert!(max_bytes >= 4, "a frame must hold at least one character");
    let mut out = Vec::new();
    let mut rest = text;
    while rest.len() > max_bytes {
        // Walk back to a boundary. At most three steps: UTF-8 is at most four
        // bytes per character.
        let mut cut = max_bytes;
        while cut > 0 && !rest.is_char_boundary(cut) {
            cut -= 1;
        }
        let (head, tail) = rest.split_at(cut);
        out.push(head);
        rest = tail;
    }
    if !rest.is_empty() || out.is_empty() {
        out.push(rest);
    }
    out
}

/// A backend that answers from a script. Used by the daemon's tests and by
/// `nevosd --mock`, so the whole pipeline — pairing, audio, transcript, reply,
/// mood — can be exercised on a machine with no model installed.
pub struct MockAgent {
    reply: String,
}

impl MockAgent {
    pub fn new(reply: impl Into<String>) -> Self {
        Self { reply: reply.into() }
    }
}

impl Default for MockAgent {
    fn default() -> Self {
        Self::new("I am the mock agent. No model is running, so this is all I can say.")
    }
}

#[async_trait]
impl Agent for MockAgent {
    fn name(&self) -> &str {
        "mock"
    }

    fn is_local(&self) -> bool {
        true
    }

    async fn respond(
        &self,
        _req: &AgentRequest,
        _history: &[Turn],
        events: mpsc::Sender<AgentEvent>,
    ) -> Result<()> {
        // Word at a time, so callers that only work against a single-token
        // reply fail here rather than against a real model.
        for word in self.reply.split_inclusive(' ') {
            if events.send(AgentEvent::Token(word.to_string())).await.is_err() {
                return Ok(());
            }
        }
        if let Some(hint) = mood::infer(&self.reply) {
            let _ = events.send(AgentEvent::Mood(hint)).await;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn frames_never_split_a_character() {
        let text = "café ☕ résumé ☕ naïve";
        for max in 4..40 {
            let parts = split_for_frames(text, max);
            assert_eq!(parts.concat(), text, "max={max}");
            assert!(parts.iter().all(|p| p.len() <= max), "max={max}");
        }
    }

    #[test]
    fn a_short_reply_is_one_frame() {
        assert_eq!(split_for_frames("hello", 512), vec!["hello"]);
    }

    #[test]
    fn an_empty_reply_is_still_one_frame() {
        // The daemon sends what it is given; an empty vec would silently drop
        // the turn instead of ending it.
        assert_eq!(split_for_frames("", 512), vec![""]);
    }

    #[test]
    fn a_multibyte_character_at_the_boundary_does_not_panic() {
        // Four bytes of emoji straddling a 5-byte frame is the exact case that
        // panics if you slice a String by byte index.
        let parts = split_for_frames("a\u{1F600}b", 5);
        assert_eq!(parts.concat(), "a\u{1F600}b");
    }

    #[test]
    fn history_is_bounded_by_turn_count() {
        let mut c = Conversation::new(4, 10_000);
        for i in 0..10 {
            c.push(Turn::user(format!("message {i}")));
        }
        assert_eq!(c.turns().len(), 4);
        assert_eq!(c.turns()[3].text, "message 9", "the newest turn must survive");
    }

    #[test]
    fn history_is_bounded_by_size_too() {
        let mut c = Conversation::new(100, 50);
        for _ in 0..10 {
            c.push(Turn::user("x".repeat(20)));
        }
        assert!(c.turns().len() <= 3);
    }

    #[test]
    fn one_oversized_turn_is_kept_rather_than_erased() {
        let mut c = Conversation::new(8, 10);
        c.push(Turn::user("x".repeat(500)));
        assert_eq!(c.turns().len(), 1, "an empty context is worse than a full one");
    }

    #[tokio::test]
    async fn the_mock_streams_and_then_offers_a_mood() {
        let (tx, mut rx) = mpsc::channel(32);
        let agent = MockAgent::new("Sorry, I can't reach the kettle.");
        let req = AgentRequest { turn: 1, text: "tea?".into(), app: "agent".into() };
        agent.respond(&req, &[], tx).await.unwrap();

        let mut text = String::new();
        let mut hint = None;
        while let Some(ev) = rx.recv().await {
            match ev {
                AgentEvent::Token(t) => text.push_str(&t),
                AgentEvent::Mood(m) => hint = Some(m),
            }
        }
        assert_eq!(text, "Sorry, I can't reach the kettle.");
        assert_eq!(hint.unwrap().mood, Mood::Concerned);
    }

    #[tokio::test]
    async fn a_dropped_receiver_stops_generation() {
        let (tx, rx) = mpsc::channel(1);
        drop(rx);
        let agent = MockAgent::new("one two three four five");
        let req = AgentRequest { turn: 1, text: String::new(), app: String::new() };
        // Must return promptly rather than blocking on a closed channel.
        agent.respond(&req, &[], tx).await.unwrap();
    }
}
