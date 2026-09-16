//! Turning a reply into a face.
//!
//! The device shows a mood while the agent talks, and the mood has to come from
//! somewhere. Asking the model for one does not work well at this size: a 3B
//! model asked to emit `[mood: happy]` forgets about a third of the time, and
//! when it does remember it sometimes says it out loud to the user. So the tag
//! is inferred here from the finished text instead.
//!
//! This is a pure function over a string. It is allowed to be wrong — a wrong
//! mood is a slightly odd face, not a bug — but it must be instant and it must
//! never panic, so there is no model and no allocation beyond lowercasing.
use serde::{Deserialize, Serialize};

/// Mirrors `nev_mood_t` in components/nev_persona/include/nev_persona/face_params.h.
///
/// The discriminants are the wire values for `mood_hint.mood`, so they are
/// fixed by that header, not by anything here.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[repr(u8)]
pub enum Mood {
    Idle = 0,
    Curious = 1,
    Happy = 2,
    Focused = 3,
    Sleepy = 4,
    Celebrating = 5,
    Concerned = 6,
    Thinking = 7,
}

impl Mood {
    pub fn as_u8(self) -> u8 {
        self as u8
    }

    pub fn name(self) -> &'static str {
        match self {
            Mood::Idle => "idle",
            Mood::Curious => "curious",
            Mood::Happy => "happy",
            Mood::Focused => "focused",
            Mood::Sleepy => "sleepy",
            Mood::Celebrating => "celebrating",
            Mood::Concerned => "concerned",
            Mood::Thinking => "thinking",
        }
    }
}

/// A mood and how hard to play it.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct MoodHint {
    pub mood: Mood,
    /// 0..=255. The persona scales the preset toward neutral by this.
    pub intensity: u8,
    /// 0 makes it the resting mood; non-zero returns to the previous one after.
    pub duration_ms: u16,
}

impl MoodHint {
    pub fn new(mood: Mood, intensity: u8, duration_ms: u16) -> Self {
        Self { mood, intensity, duration_ms }
    }
}

/// Words that give a reply away. Ordered by strength: the first table that
/// matches wins, so an apology beats a greeting in the same sentence.
const CONCERNED: &[&str] = &[
    "sorry", "unfortunately", "i can't", "i cannot", "failed", "error", "problem",
    "unable", "went wrong", "afraid",
];
const CELEBRATING: &[&str] = &[
    "congratulations", "well done", "nailed it", "brilliant", "amazing", "you did it",
    "high score", "new record",
];
const HAPPY: &[&str] = &[
    "glad", "happy to", "nice", "great", "lovely", "good one", "enjoy", "welcome",
    "of course", "sure thing",
];
const THINKING: &[&str] = &[
    "let me", "one moment", "i think", "probably", "it depends", "hmm", "considering",
];
const CURIOUS: &[&str] = &["what ", "which ", "who ", "where ", "when ", "how about", "tell me more"];
const FOCUSED: &[&str] = &["step 1", "first,", "here's how", "here is how", "instructions", "recipe"];

fn contains_any(haystack: &str, needles: &[&str]) -> bool {
    needles.iter().any(|n| haystack.contains(n))
}

/// Infers the mood a finished reply should be delivered in.
///
/// Returns `None` when nothing in the text argues for a mood, which is not the
/// same as idle: the daemon sends no hint at all and the face keeps whatever it
/// was doing. A hint per sentence would make the face twitch.
pub fn infer(text: &str) -> Option<MoodHint> {
    let t = text.trim();
    if t.is_empty() {
        return None;
    }
    let lower = t.to_lowercase();

    if contains_any(&lower, CONCERNED) {
        return Some(MoodHint::new(Mood::Concerned, 200, 0));
    }
    if contains_any(&lower, CELEBRATING) || lower.contains('!') && contains_any(&lower, HAPPY) {
        return Some(MoodHint::new(Mood::Celebrating, 230, 2500));
    }
    if contains_any(&lower, HAPPY) {
        return Some(MoodHint::new(Mood::Happy, 180, 0));
    }
    if contains_any(&lower, THINKING) {
        return Some(MoodHint::new(Mood::Thinking, 170, 0));
    }
    if contains_any(&lower, FOCUSED) {
        return Some(MoodHint::new(Mood::Focused, 190, 0));
    }
    // A question back to the user is the single most reliable cue in the set,
    // but only when the reply is mostly the question rather than ending on one.
    if t.ends_with('?') || contains_any(&lower, CURIOUS) {
        return Some(MoodHint::new(Mood::Curious, 190, 0));
    }
    None
}

/// The mood to wear while the model is still generating.
pub fn while_thinking() -> MoodHint {
    MoodHint::new(Mood::Thinking, 160, 0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mood_values_match_the_firmware_enum() {
        // If these ever disagree the device shows the wrong face and nothing
        // else complains, so pin them here against face_params.h.
        assert_eq!(Mood::Idle.as_u8(), 0);
        assert_eq!(Mood::Curious.as_u8(), 1);
        assert_eq!(Mood::Happy.as_u8(), 2);
        assert_eq!(Mood::Focused.as_u8(), 3);
        assert_eq!(Mood::Sleepy.as_u8(), 4);
        assert_eq!(Mood::Celebrating.as_u8(), 5);
        assert_eq!(Mood::Concerned.as_u8(), 6);
        assert_eq!(Mood::Thinking.as_u8(), 7);
    }

    #[test]
    fn an_apology_reads_as_concerned() {
        assert_eq!(infer("Sorry, I couldn't find that file.").unwrap().mood, Mood::Concerned);
    }

    #[test]
    fn an_apology_beats_a_pleasantry_in_the_same_reply() {
        // "Sorry, but great question!" must not come out celebrating.
        let hint = infer("Sorry, that's a great question but I can't help.").unwrap();
        assert_eq!(hint.mood, Mood::Concerned);
    }

    #[test]
    fn praise_celebrates_and_then_returns() {
        let hint = infer("Congratulations, a new record!").unwrap();
        assert_eq!(hint.mood, Mood::Celebrating);
        assert!(hint.duration_ms > 0, "celebrating must not become the resting mood");
    }

    #[test]
    fn a_resting_mood_has_no_duration() {
        assert_eq!(infer("Glad to help.").unwrap().duration_ms, 0);
    }

    #[test]
    fn a_question_back_is_curious() {
        assert_eq!(infer("Which one did you mean?").unwrap().mood, Mood::Curious);
    }

    #[test]
    fn hedging_reads_as_thinking() {
        assert_eq!(infer("I think it depends on the room.").unwrap().mood, Mood::Thinking);
    }

    #[test]
    fn plain_text_gets_no_hint_at_all() {
        assert!(infer("The kettle is in the kitchen.").is_none());
        assert!(infer("   ").is_none());
        assert!(infer("").is_none());
    }

    #[test]
    fn inference_never_panics_on_awkward_input() {
        for s in ["?", "!", "\u{1F600}", "??!?", "\0", "é?", &"x".repeat(10_000)] {
            let _ = infer(s);
        }
    }
}
