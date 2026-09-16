//! The NEVOS companion daemon, as a library.
//!
//! The binary in main.rs is thin: it parses arguments, chooses backends and
//! starts the two listeners. Everything else lives here so the integration
//! tests can start a real daemon in-process and talk to it over a real socket,
//! which is the only way to prove that the protocol, the pairing flow and the
//! agent pipeline fit together rather than each working in isolation.
pub mod discovery;
pub mod pairing;
pub mod server;
pub mod session;
pub mod state;
