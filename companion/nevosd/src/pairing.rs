//! Pairing: proving that whoever is connecting is in the room.
//!
//! The device is on the same network as everything else in the house, and it
//! finds the daemon by mDNS, which anything can answer or impersonate. So a
//! connection alone proves nothing. The device shows a six-digit code on its
//! screen and the user types it into the desktop app; a token is granted only
//! when the code the device sent over the wire matches the code a human read
//! off the screen and typed on the machine that owns the data.
//!
//! That out-of-band step is the whole security model, so this file is written
//! to be read: no transport, no clock of its own, no randomness of its own.
//! Time and token generation are arguments, which is what makes expiry,
//! throttling and the two possible orderings testable rather than hopeful.
use std::collections::HashMap;

/// How long a device's pairing attempt stays open. Long enough to walk to the
/// computer, short enough that a code left on screen overnight is not a key.
pub const REQUEST_TTL_SECS: u64 = 180;

/// Codes are only ever entered after the device is already showing one — the
/// user reads it off the screen — so there is no "entered early" case to hold.
/// An entry with nothing pending is simply a wrong code.
///
/// Wrong codes tolerated before the daemon stops listening for a while. Six
/// digits is a million possibilities; without a limit, a program on the LAN can
/// walk the whole space in an afternoon.
pub const MAX_FAILED_ENTRIES: u32 = 5;
pub const LOCKOUT_SECS: u64 = 300;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PendingRequest {
    pub device_id: String,
    pub code: String,
    pub expires_at: u64,
}

/// What the tray should tell the user after they typed a code.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum EntryOutcome {
    /// The device is paired. Its id is returned so a token can be granted.
    Granted { device_id: String },
    /// No device is presenting that code. Deliberately not distinguished from
    /// an expired one: the difference is only useful to someone guessing.
    Rejected,
    /// Too many wrong codes. `retry_after` is seconds.
    LockedOut { retry_after: u64 },
}

#[derive(Debug, Default)]
pub struct Pairing {
    /// One open request per device; a device that reconnects replaces its own.
    requests: HashMap<String, PendingRequest>,
    failed: u32,
    locked_until: u64,
}

impl Pairing {
    pub fn new() -> Self {
        Self::default()
    }

    /// Records that a device is showing `code` on its screen.
    ///
    /// Returns false for a code that is not six digits. The device generates it,
    /// and a device that is confused about the format is a device we should not
    /// pair with — accepting an empty code here would pair anything that sent
    /// an empty code entry.
    pub fn device_requested(&mut self, device_id: &str, code: &str, now: u64) -> bool {
        if !is_valid_code(code) {
            return false;
        }
        self.expire(now);
        self.requests.insert(
            device_id.to_string(),
            PendingRequest {
                device_id: device_id.to_string(),
                code: code.to_string(),
                expires_at: now + REQUEST_TTL_SECS,
            },
        );
        true
    }

    /// The user typed a code into the desktop app.
    pub fn user_entered(&mut self, code: &str, now: u64) -> EntryOutcome {
        if now < self.locked_until {
            return EntryOutcome::LockedOut { retry_after: self.locked_until - now };
        }
        self.expire(now);

        let hit = self
            .requests
            .values()
            .find(|r| constant_time_eq(r.code.as_bytes(), code.as_bytes()))
            .map(|r| r.device_id.clone());

        match hit {
            Some(device_id) => {
                self.requests.remove(&device_id);
                self.failed = 0;
                EntryOutcome::Granted { device_id }
            }
            None => {
                self.failed += 1;
                if self.failed >= MAX_FAILED_ENTRIES {
                    self.locked_until = now + LOCKOUT_SECS;
                    self.failed = 0;
                    return EntryOutcome::LockedOut { retry_after: LOCKOUT_SECS };
                }
                EntryOutcome::Rejected
            }
        }
    }

    /// Devices currently waiting, for the tray to show "a device is asking to pair".
    pub fn pending(&self, now: u64) -> Vec<String> {
        self.requests
            .values()
            .filter(|r| r.expires_at > now)
            .map(|r| r.device_id.clone())
            .collect()
    }

    pub fn cancel(&mut self, device_id: &str) {
        self.requests.remove(device_id);
    }

    fn expire(&mut self, now: u64) {
        self.requests.retain(|_, r| r.expires_at > now);
    }
}

/// Exactly six ASCII digits.
pub fn is_valid_code(code: &str) -> bool {
    code.len() == 6 && code.bytes().all(|b| b.is_ascii_digit())
}

/// Compares without leaking where the mismatch was through timing.
///
/// A six-digit code over a LAN is not realistically attackable this way, and
/// the lockout above is the defence that matters. This costs nothing, though,
/// and a comparison of secrets that short-circuits is the kind of thing that
/// gets copied into somewhere it does matter.
fn constant_time_eq(a: &[u8], b: &[u8]) -> bool {
    if a.len() != b.len() {
        return false;
    }
    let mut diff = 0u8;
    for (x, y) in a.iter().zip(b.iter()) {
        diff |= x ^ y;
    }
    diff == 0
}

// The code itself is generated on the device, in firmware: it is shown on the
// device's screen, and a code the daemon invented would have to travel over the
// network before the user could read it, which is exactly what the out-of-band
// step is there to avoid.

/// A device token: 32 random bytes, hex. Long enough that guessing is not a
/// consideration, printable so it can live in NVS and in a JSON file.
pub fn token_from_bytes(bytes: [u8; 32]) -> String {
    let mut s = String::with_capacity(64);
    for b in bytes {
        s.push_str(&format!("{b:02x}"));
    }
    s
}

/// Fresh randomness from the OS. The only impure function in the file, kept
/// separate so everything above it can be tested.
pub fn random_token() -> anyhow::Result<String> {
    let mut bytes = [0u8; 32];
    getrandom::fill(&mut bytes)?;
    Ok(token_from_bytes(bytes))
}

#[cfg(test)]
mod tests {
    use super::*;

    const T0: u64 = 1_700_000_000;

    #[test]
    fn the_happy_path_pairs_the_right_device() {
        let mut p = Pairing::new();
        assert!(p.device_requested("nev-abc", "123456", T0));
        assert_eq!(
            p.user_entered("123456", T0 + 10),
            EntryOutcome::Granted { device_id: "nev-abc".into() }
        );
    }

    #[test]
    fn a_code_is_single_use() {
        // Otherwise a code read over someone's shoulder pairs a second device
        // later, and the user has no way to know.
        let mut p = Pairing::new();
        p.device_requested("nev-abc", "123456", T0);
        assert!(matches!(p.user_entered("123456", T0), EntryOutcome::Granted { .. }));
        assert_eq!(p.user_entered("123456", T0), EntryOutcome::Rejected);
    }

    #[test]
    fn the_right_device_is_paired_when_two_are_asking() {
        let mut p = Pairing::new();
        p.device_requested("nev-one", "111111", T0);
        p.device_requested("nev-two", "222222", T0);
        assert_eq!(
            p.user_entered("222222", T0),
            EntryOutcome::Granted { device_id: "nev-two".into() }
        );
        // The other is untouched and can still pair.
        assert_eq!(p.pending(T0), vec!["nev-one".to_string()]);
    }

    #[test]
    fn a_stale_request_no_longer_pairs() {
        let mut p = Pairing::new();
        p.device_requested("nev-abc", "123456", T0);
        assert_eq!(p.user_entered("123456", T0 + REQUEST_TTL_SECS + 1), EntryOutcome::Rejected);
    }

    #[test]
    fn a_device_reconnecting_replaces_its_own_request() {
        let mut p = Pairing::new();
        p.device_requested("nev-abc", "111111", T0);
        p.device_requested("nev-abc", "222222", T0 + 5);
        assert_eq!(p.user_entered("111111", T0 + 6), EntryOutcome::Rejected);
        assert_eq!(p.pending(T0 + 6).len(), 1, "the old code must not linger as a second slot");
    }

    #[test]
    fn guessing_locks_out() {
        let mut p = Pairing::new();
        p.device_requested("nev-abc", "123456", T0);
        for _ in 0..MAX_FAILED_ENTRIES - 1 {
            assert_eq!(p.user_entered("000000", T0), EntryOutcome::Rejected);
        }
        assert!(matches!(p.user_entered("000000", T0), EntryOutcome::LockedOut { .. }));
        // And the correct code is refused while locked out, or the lockout is
        // only an inconvenience to an attacker who then gets it right.
        assert!(matches!(p.user_entered("123456", T0 + 1), EntryOutcome::LockedOut { .. }));

        // The lockout outlasts the request on purpose: sitting out five minutes
        // leaves nothing to pair with, so the user starts the attempt again
        // from the device rather than finding a stale code still live.
        let later = T0 + LOCKOUT_SECS + 1;
        assert_eq!(p.user_entered("123456", later), EntryOutcome::Rejected);
        p.device_requested("nev-abc", "123456", later);
        assert!(matches!(p.user_entered("123456", later), EntryOutcome::Granted { .. }));
    }

    #[test]
    fn a_success_clears_the_failure_count() {
        let mut p = Pairing::new();
        p.device_requested("nev-abc", "123456", T0);
        p.user_entered("000000", T0);
        p.user_entered("000000", T0);
        assert!(matches!(p.user_entered("123456", T0), EntryOutcome::Granted { .. }));

        p.device_requested("nev-abc", "654321", T0);
        for _ in 0..MAX_FAILED_ENTRIES - 1 {
            assert_eq!(p.user_entered("000000", T0), EntryOutcome::Rejected);
        }
        assert_eq!(p.user_entered("654321", T0), EntryOutcome::Granted { device_id: "nev-abc".into() });
    }

    #[test]
    fn a_malformed_code_is_never_accepted() {
        let mut p = Pairing::new();
        for bad in ["", "12345", "1234567", "12345a", "     6", "12 456"] {
            assert!(!p.device_requested("nev-abc", bad, T0), "{bad:?} was accepted");
        }
        // Nothing is pending, so an empty entry cannot match an empty request.
        assert_eq!(p.user_entered("", T0), EntryOutcome::Rejected);
    }

    #[test]
    fn a_token_is_64_hex_characters() {
        let t = token_from_bytes([0xab; 32]);
        assert_eq!(t.len(), 64);
        assert!(t.chars().all(|c| c.is_ascii_hexdigit()));
        assert_eq!(random_token().unwrap().len(), 64);
        assert_ne!(random_token().unwrap(), random_token().unwrap());
    }
}
