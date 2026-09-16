//! The two listeners.
//!
//! They are separate on purpose and the split is load-bearing. The device link
//! is on the LAN, because the robot has to reach it from across the room. The
//! control API — which lists your notes, and can erase all of them — is bound
//! to loopback, because nothing on the network has any business calling it.
//! One server on one port would have meant the purge endpoint answering the
//! whole house.
use anyhow::{Context, Result};
use axum::extract::ws::{Message, WebSocket, WebSocketUpgrade};
use axum::extract::State;
use axum::http::StatusCode;
use axum::response::{IntoResponse, Response};
use axum::routing::{get, post};
use axum::{Json, Router};
use futures_util::{SinkExt, StreamExt};
use nevos_agent::{AgentEvent, AgentRequest, Turn};
use nevos_proto::{
    frame_split, AgentDone, AgentToken, MoodHint, Notification, TranscriptFinal, TranscriptPartial,
};
use nevos_store::{now_unix, Record, RecordKind};
use tokio::sync::mpsc::Sender as MpscSender;
use serde::{Deserialize, Serialize};
use std::net::SocketAddr;
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::net::TcpListener;
use tokio::sync::mpsc;
use tokio::task::JoinHandle;

use crate::pairing::EntryOutcome;
use crate::session::{Action, Out, Session, SAMPLE_RATE};
use crate::state::Daemon;

/// A device that has said nothing at all for this long is gone, whatever the
/// socket claims. A dropped Wi-Fi link does not always close cleanly, and a
/// half-open connection would keep a paired device listed as present.
const IDLE_TIMEOUT: Duration = Duration::from_secs(90);

/// The wire cap is 512 bytes per token frame; this leaves room for the rest of
/// the CBOR array without having to reason about its exact encoded size.
const TOKEN_CHUNK_BYTES: usize = 384;

/// How much audio a long capture transcribes at a time.
///
/// Long enough that whisper has sentences to work with rather than fragments —
/// it is much worse at three-second slices than at thirty-second ones — and
/// short enough that words appear on the device while the meeting is still
/// happening. It also bounds what is in memory: thirty seconds is under a
/// megabyte, where the whole meeting would be a hundred.
const SEGMENT_SECS: usize = 30;
const SEGMENT_SAMPLES: usize = SAMPLE_RATE as usize * SEGMENT_SECS;

/// A pairing attempt the user never completes. The device is told, so it can
/// stop showing a code that will not work rather than waiting indefinitely.
const PAIRING_TIMEOUT: Duration = Duration::from_secs(crate::pairing::REQUEST_TTL_SECS);

// ---------------------------------------------------------------------------
// Device listener
// ---------------------------------------------------------------------------

pub async fn run_device_listener(state: Arc<Daemon>, addr: SocketAddr) -> Result<u16> {
    let app = Router::new()
        .route("/ws", get(ws_upgrade))
        .route("/health", get(|| async { "nevos" }))
        .with_state(state);

    let listener = TcpListener::bind(addr).await.with_context(|| format!("binding {addr}"))?;
    let port = listener.local_addr()?.port();
    tokio::spawn(async move {
        if let Err(e) = axum::serve(listener, app).await {
            tracing::error!(error = %e, "device listener stopped");
        }
    });
    Ok(port)
}

async fn ws_upgrade(ws: WebSocketUpgrade, State(state): State<Arc<Daemon>>) -> Response {
    ws.on_upgrade(move |socket| connection(socket, state))
}

/// One device connection, start to finish.
///
/// All the decisions live in `Session`; this function is the hands. Keeping it
/// that way is what lets the protocol be tested without a socket, so the loop
/// below should stay boring: read, decide, do.
async fn connection(socket: WebSocket, state: Arc<Daemon>) {
    let (mut sink, mut stream) = socket.split();
    let (out_tx, mut out_rx) = mpsc::channel::<Out>(64);

    // One writer, so the agent's token stream and the session's replies cannot
    // interleave halfway through a frame.
    let writer = tokio::spawn(async move {
        while let Some(out) = out_rx.recv().await {
            match out.to_frame() {
                Ok(frame) => {
                    if sink.send(Message::Binary(frame.into())).await.is_err() {
                        break;
                    }
                }
                Err(e) => tracing::error!(error = ?e, "refusing to send an unencodable message"),
            }
        }
    });

    let mut session = Session::new(&state.name);
    let mut grants = state.subscribe_grants();
    let mut to_devices = state.subscribe_to_devices();
    let mut buf: Vec<u8> = Vec::new();
    let mut turn: Option<JoinHandle<()>> = None;
    // Which turn the device is waiting on. An aborted task can still have a
    // token in flight — abort only takes effect at the next await point — so
    // the sending side checks this before every frame rather than trusting
    // that a cancelled turn has already stopped.
    let live_turn = Arc::new(AtomicU32::new(0));
    let mut was_capturing = false;
    let mut announced = false;
    let mut pairing_since: Option<Instant> = None;
    // The long capture in progress, if any: its session id and the channel its
    // task is listening on.
    let mut capture: Option<(u32, MpscSender<CaptureMsg>)> = None;

    loop {
        let actions = tokio::select! {
            incoming = stream.next() => {
                match incoming {
                    Some(Ok(Message::Binary(bytes))) => {
                        buf.extend_from_slice(&bytes);
                        match drain_frames(&mut buf, &mut session, &state) {
                            Ok(actions) => actions,
                            Err(e) => vec![Action::Close(e)],
                        }
                    }
                    Some(Ok(Message::Text(_))) => {
                        vec![Action::Close("device sent text on a binary protocol".into())]
                    }
                    Some(Ok(_)) => Vec::new(), // ping/pong/close handled by axum
                    Some(Err(e)) => {
                        tracing::debug!(error = %e, "socket error");
                        break;
                    }
                    None => break,
                }
            }
            grant = grants.recv() => {
                match grant {
                    Ok(g) if g.device_id == session.device_id() && session.is_awaiting_pairing() => {
                        tracing::info!(device = %g.device_id, "paired");
                        session.pair_granted(g.token)
                    }
                    Ok(_) => Vec::new(),
                    // Lagged means grants were issued faster than this
                    // connection read them; the device can reconnect.
                    Err(_) => Vec::new(),
                }
            }
            broadcast = to_devices.recv() => {
                match broadcast {
                    // Only a paired, connected device gets these: a device
                    // still showing a pairing code is not yet anyone's robot.
                    Ok(out) if session.is_ready() => vec![Action::Send(out)],
                    _ => Vec::new(),
                }
            }
            _ = tokio::time::sleep(IDLE_TIMEOUT) => {
                tracing::info!(device = %session.device_id(), "idle, closing");
                break;
            }
            _ = tokio::time::sleep(pairing_since.map_or(IDLE_TIMEOUT, |t| {
                PAIRING_TIMEOUT.saturating_sub(t.elapsed())
            })), if pairing_since.is_some() => {
                pairing_since = None;
                state.cancel_pairing(session.device_id());
                session.pair_refused("nobody entered the code")
            }
        };

        let mut closing = false;
        for action in actions {
            match action {
                Action::Send(out) => {
                    if out_tx.send(out).await.is_err() {
                        closing = true;
                    }
                }
                Action::Close(why) => {
                    tracing::info!(device = %session.device_id(), reason = %why, "closing");
                    closing = true;
                }
                Action::Seen => touch(&state, session.device_id()),
                Action::RequestPairing { device_id, code } => {
                    if state.device_requested_pairing(&device_id, &code, now_unix()) {
                        tracing::info!(device = %device_id, "waiting for the code to be entered");
                        pairing_since = Some(Instant::now());
                    }
                }
                Action::AudioGap { session: s, expected, got } => {
                    tracing::warn!(session = s, expected, got, "dropped audio; transcript has a hole");
                }
                Action::CaptureChunk { session: s, pcm, last } => {
                    let sender = match &capture {
                        Some((id, tx)) if *id == s => tx.clone(),
                        _ => {
                            // A bound, not an unbounded queue: if transcription
                            // falls behind, this fills, and the pressure is felt
                            // here rather than as memory quietly climbing.
                            let (ctx, crx) = mpsc::channel::<CaptureMsg>(64);
                            let st = state.clone();
                            let out = out_tx.clone();
                            tokio::spawn(async move { capture_task(st, out, s, crx).await });
                            tracing::info!(session = s, "capture started");
                            capture = Some((s, ctx.clone()));
                            ctx
                        }
                    };
                    if sender.send(CaptureMsg::Pcm(pcm)).await.is_err() {
                        capture = None;
                    } else if last {
                        let _ = sender.send(CaptureMsg::End).await;
                        capture = None;
                    }
                }
                Action::Marker { session: s, at_seconds } => {
                    if let Some((id, tx)) = &capture {
                        if *id == s {
                            let _ = tx.send(CaptureMsg::Marker(at_seconds)).await;
                        }
                    }
                }
                Action::Utterance { session: s, pcm } => {
                    let state = state.clone();
                    let tx = out_tx.clone();
                    tokio::spawn(async move { transcribe(state, tx, s, pcm).await });
                }
                Action::Ask { turn: n, text, app } => {
                    // The newest question wins; abandon whatever was running
                    // rather than letting two replies race onto one screen.
                    if let Some(handle) = turn.take() {
                        handle.abort();
                    }
                    live_turn.store(n, Ordering::SeqCst);
                    let state = state.clone();
                    let tx = out_tx.clone();
                    let device_id = session.device_id().to_string();
                    let live = live_turn.clone();
                    turn = Some(tokio::spawn(async move {
                        answer(state, tx, live, device_id, n, text, app).await
                    }));
                }
            }
        }

        if !announced && session.is_ready() {
            // Announced only once authenticated: an unpaired device on the
            // network is not "your robot is connected".
            tracing::info!(
                device = %session.device_id(),
                firmware = %session.firmware(),
                "device connected"
            );
            state.device_connected(session.device_id());
            announced = true;
            pairing_since = None;
        }

        // The live indicator follows the audio itself, not an intention to
        // record announced in advance.
        let capturing = session.is_capturing();
        if capturing != was_capturing {
            if capturing {
                state.capture_started();
            } else {
                state.capture_stopped();
            }
            was_capturing = capturing;
        }

        if closing {
            break;
        }
    }

    if was_capturing {
        state.capture_stopped();
    }
    // A meeting that ends because the device ran out of battery is still a
    // meeting. Closing the channel makes the task file what it has.
    if let Some((_, tx)) = capture.take() {
        let _ = tx.send(CaptureMsg::End).await;
    }
    // A device that disconnects mid-pairing takes its code with it. Leaving the
    // request open would let the code be entered minutes later, against a
    // device that is no longer there to confirm it.
    if session.is_awaiting_pairing() {
        state.cancel_pairing(session.device_id());
    }
    if announced {
        state.device_disconnected(session.device_id());
    }
    if let Some(handle) = turn {
        handle.abort();
    }
    drop(out_tx);
    let _ = writer.await;
}

/// Pulls every complete frame out of the buffer and runs it through the session.
fn drain_frames(
    buf: &mut Vec<u8>,
    session: &mut Session,
    state: &Arc<Daemon>,
) -> Result<Vec<Action>, String> {
    let mut actions = Vec::new();
    loop {
        match frame_split(buf) {
            Ok(Some((range, total))) => {
                let payload = buf[range].to_vec();
                actions.extend(session.on_frame(&payload, now_unix(), state.as_ref()));
                buf.drain(..total);
            }
            Ok(None) => return Ok(actions),
            // An over-long length prefix is not recoverable: we cannot know
            // where the next frame starts.
            Err(e) => return Err(format!("bad frame: {e:?}")),
        }
    }
}

/// Refreshes last_seen, but not on every ping.
///
/// A device pings every few seconds; rewriting the device file that often would
/// be a few thousand writes an hour for a field nothing reads in real time.
fn touch(state: &Arc<Daemon>, device_id: &str) {
    const REFRESH_SECS: u64 = 300;
    if device_id.is_empty() {
        return;
    }
    let now = now_unix();
    let Ok(devices) = state.store.devices() else { return };
    let Some(mut device) = devices.into_iter().find(|d| d.device_id == device_id) else {
        return;
    };
    if now.saturating_sub(device.last_seen) < REFRESH_SECS {
        return;
    }
    device.last_seen = now;
    if let Err(e) = state.store.upsert_device(device) {
        tracing::warn!(error = %e, "could not update last_seen");
    }
}

/// What the capture task is told.
enum CaptureMsg {
    Pcm(Vec<i16>),
    Marker(f32),
    End,
}

/// Runs one long capture from start to finish.
///
/// A task of its own because it outlives any single frame of audio and has to
/// do slow work — whisper on thirty seconds takes a few seconds — without the
/// connection loop waiting on it. Audio arriving meanwhile queues in the
/// channel, and a channel with a bound is how backpressure reaches the device
/// rather than the memory.
async fn capture_task(
    state: Arc<Daemon>,
    tx: mpsc::Sender<Out>,
    session: u32,
    mut rx: mpsc::Receiver<CaptureMsg>,
) {
    let id = format!("{}-meeting-{session}", now_unix());
    let started = now_unix();
    let mut segment: Vec<i16> = Vec::with_capacity(SEGMENT_SAMPLES);
    let mut text = String::new();
    let mut markers: Vec<f32> = Vec::new();
    let mut audio: Vec<i16> = Vec::new();
    let mut total_samples: usize = 0;

    /* Transcribes one segment and appends it. Returns false if the device has
     * gone, which ends the capture. */
    async fn flush(
        state: &Arc<Daemon>,
        tx: &mpsc::Sender<Out>,
        session: u32,
        segment: &mut Vec<i16>,
        text: &mut String,
    ) -> bool {
        if segment.is_empty() {
            return true;
        }
        let pcm = std::mem::take(segment);
        match state.stt.transcribe(&pcm, SAMPLE_RATE).await {
            Ok(t) if !t.text.trim().is_empty() => {
                if !text.is_empty() {
                    text.push(' ');
                }
                text.push_str(t.text.trim());
                // The device shows the latest line while the meeting runs. It
                // is a partial by the schema's definition — each one replaces
                // the last — so the newest is the only one worth keeping.
                tx.send(Out::TranscriptPartial(TranscriptPartial {
                    session,
                    text: truncate_chars(t.text.trim(), 500),
                }))
                .await
                .is_ok()
            }
            Ok(_) => true, // a silent segment: nothing said, nothing to add
            Err(e) => {
                // One failed segment must not end an hour-long meeting. The
                // gap is recorded in the text, because a transcript with an
                // unmarked hole is worse than one that admits to it.
                tracing::error!(error = %e, "a capture segment failed to transcribe");
                text.push_str(" […] ");
                true
            }
        }
    }

    while let Some(msg) = rx.recv().await {
        match msg {
            CaptureMsg::Pcm(pcm) => {
                total_samples += pcm.len();
                if state.keep_audio {
                    audio.extend_from_slice(&pcm);
                }
                segment.extend_from_slice(&pcm);
                if segment.len() >= SEGMENT_SAMPLES
                    && !flush(&state, &tx, session, &mut segment, &mut text).await
                {
                    return;
                }
            }
            CaptureMsg::Marker(at) => {
                tracing::info!(session, at, "marker");
                markers.push(at);
            }
            CaptureMsg::End => break,
        }
    }

    flush(&state, &tx, session, &mut segment, &mut text).await;

    let seconds = total_samples as f32 / SAMPLE_RATE as f32;
    tracing::info!(session, seconds, markers = markers.len(), "capture finished");

    let audio_name = if state.keep_audio && !audio.is_empty() {
        let wav = nevos_stt::wav_from_pcm(&audio, SAMPLE_RATE);
        match state.store.save_audio(&id, &wav) {
            Ok(path) => path.file_name().map(|n| n.to_string_lossy().to_string()),
            Err(e) => {
                tracing::warn!(error = %e, "could not save the capture audio");
                None
            }
        }
    } else {
        None
    };

    // Filed even when empty, so an hour of silence is a transcript that says
    // nothing rather than a meeting that vanished.
    let record = Record {
        id,
        kind: RecordKind::Transcript,
        created_at: started,
        text: text.trim().to_string(),
        markers,
        audio: audio_name,
    };
    if let Err(e) = state.store.save_record(&record) {
        tracing::error!(error = %e, "could not save the transcript");
    }

    let _ = tx
        .send(Out::TranscriptFinal(TranscriptFinal {
            session,
            text: truncate_chars(&record.text, 500),
            confidence: 0.0,
        }))
        .await;
}

/// Transcribes one utterance and files it.
async fn transcribe(state: Arc<Daemon>, tx: mpsc::Sender<Out>, audio_session: u32, pcm: Vec<i16>) {
    let seconds = pcm.len() as f32 / crate::session::SAMPLE_RATE as f32;
    let transcript = match state.stt.transcribe(&pcm, crate::session::SAMPLE_RATE).await {
        Ok(t) => t,
        Err(e) => {
            tracing::error!(error = %e, "transcription failed");
            // The device is told, rather than left waiting for a final that
            // never comes.
            let _ = tx
                .send(Out::TranscriptFinal(TranscriptFinal {
                    session: audio_session,
                    text: String::new(),
                    confidence: 0.0,
                }))
                .await;
            return;
        }
    };

    tracing::info!(seconds, text = %transcript.text, "transcribed");
    let _ = tx
        .send(Out::TranscriptFinal(TranscriptFinal {
            session: audio_session,
            text: truncate_chars(&transcript.text, 500),
            confidence: transcript.confidence,
        }))
        .await;

    if transcript.text.trim().is_empty() {
        return;
    }

    let id = format!("{}-{audio_session}", now_unix());
    let audio = if state.keep_audio {
        let wav = nevos_stt::wav_from_pcm(&pcm, crate::session::SAMPLE_RATE);
        match state.store.save_audio(&id, &wav) {
            Ok(path) => path.file_name().map(|n| n.to_string_lossy().to_string()),
            Err(e) => {
                tracing::warn!(error = %e, "could not save audio");
                None
            }
        }
    } else {
        None
    };

    let record = Record {
        id,
        kind: RecordKind::Note,
        created_at: now_unix(),
        text: transcript.text,
        markers: Vec::new(),
        audio,
    };
    if let Err(e) = state.store.save_record(&record) {
        tracing::error!(error = %e, "could not save the note");
    }
}

/// Runs one agent turn, streaming the reply to the device as it arrives.
async fn answer(
    state: Arc<Daemon>,
    tx: mpsc::Sender<Out>,
    live_turn: Arc<AtomicU32>,
    device_id: String,
    turn: u32,
    text: String,
    app: String,
) {
    let history = state.conversation_for(&device_id);
    let req = AgentRequest { turn, text: text.clone(), app };
    let (ev_tx, mut ev_rx) = mpsc::channel::<AgentEvent>(64);

    let agent = state.agent.clone();
    let history_turns = history.turns().to_vec();
    let worker =
        tokio::spawn(async move { agent.respond(&req, &history_turns, ev_tx).await });

    let mut reply = String::new();
    while let Some(event) = ev_rx.recv().await {
        match event {
            AgentEvent::Token(piece) => {
                if live_turn.load(Ordering::SeqCst) != turn {
                    return; // superseded while this token was in flight
                }
                reply.push_str(&piece);
                for chunk in nevos_agent::split_for_frames(&piece, TOKEN_CHUNK_BYTES) {
                    if tx
                        .send(Out::AgentToken(AgentToken { turn, text: chunk.to_string() }))
                        .await
                        .is_err()
                    {
                        return; // device gone
                    }
                }
            }
            AgentEvent::Mood(hint) if live_turn.load(Ordering::SeqCst) == turn => {
                let _ = tx
                    .send(Out::MoodHint(MoodHint {
                        mood: hint.mood.as_u8(),
                        intensity: hint.intensity,
                        duration_ms: hint.duration_ms,
                    }))
                    .await;
            }
            AgentEvent::Mood(_) => return,
        }
    }

    if live_turn.load(Ordering::SeqCst) != turn {
        return;
    }

    let error = match worker.await {
        Ok(Ok(())) => String::new(),
        Ok(Err(e)) => {
            tracing::error!(error = %e, "agent turn failed");
            // Shown on the device. Deliberately short and not a stack trace:
            // the person reading it is looking at a robot, not a terminal.
            short_error(&e)
        }
        Err(e) if e.is_cancelled() => return, // superseded by a newer turn
        Err(e) => {
            tracing::error!(error = %e, "agent task panicked");
            "the model backend crashed".to_string()
        }
    };

    if error.is_empty() && !reply.trim().is_empty() {
        state.push_turn(&device_id, Turn::user(text));
        state.push_turn(&device_id, Turn::assistant(reply));
    }
    let _ = tx.send(Out::AgentDone(AgentDone { turn, error })).await;
}

/// The last line of an error chain, capped to what the schema allows.
fn short_error(e: &anyhow::Error) -> String {
    let text = e.chain().last().map(|c| c.to_string()).unwrap_or_else(|| e.to_string());
    truncate_chars(&text, 120)
}

/// Truncates on a character boundary, never mid-character.
fn truncate_chars(text: &str, max_chars: usize) -> String {
    if text.chars().count() <= max_chars {
        return text.to_string();
    }
    text.chars().take(max_chars.saturating_sub(1)).collect::<String>() + "…"
}

// ---------------------------------------------------------------------------
// Control listener (loopback only)
// ---------------------------------------------------------------------------

#[derive(Serialize)]
struct Status {
    daemon: String,
    version: String,
    data_dir: String,
    /// The one line the tray shows largest. True while audio is arriving.
    mic_live: bool,
    keep_audio: bool,
    agent: Backend,
    transcriber: Backend,
    devices: Vec<DeviceStatus>,
    pending_pairings: Vec<String>,
}

#[derive(Serialize)]
struct Backend {
    name: String,
    /// Whether this backend runs on this machine. The tray states it plainly.
    local: bool,
}

#[derive(Serialize)]
struct DeviceStatus {
    device_id: String,
    name: String,
    paired_at: u64,
    last_seen: u64,
    connected: bool,
}

#[derive(Deserialize)]
struct PairBody {
    code: String,
}

#[derive(Deserialize)]
struct ForgetBody {
    device_id: String,
}

#[derive(Deserialize)]
struct DeleteBody {
    #[serde(default)]
    kind: String,
    id: String,
}

#[derive(Deserialize)]
struct NotifyBody {
    title: String,
    #[serde(default)]
    body: String,
    #[serde(default)]
    urgent: bool,
}

#[derive(Deserialize)]
struct RecordsQuery {
    #[serde(default)]
    kind: String,
}

pub async fn run_control_listener(state: Arc<Daemon>, addr: SocketAddr) -> Result<u16> {
    if !addr.ip().is_loopback() {
        anyhow::bail!(
            "the control API must bind to loopback: {addr} would expose \
             notes, transcripts and the purge endpoint to the network"
        );
    }

    let app = Router::new()
        // The control panel itself: one file, embedded in the binary. A menu
        // bar app would be a nicer front door and is a separate question — this
        // is the page it would show, and it needs no toolchain to run.
        .route("/", get(page))
        .route("/api/status", get(status))
        .route("/api/pair", post(pair))
        .route("/api/forget", post(forget))
        .route("/api/records", get(records))
        .route("/api/records/delete", post(delete_record))
        .route("/api/notify", post(notify))
        .route("/api/purge/audio", post(purge_audio))
        .route("/api/purge/all", post(purge_all))
        .with_state(state);

    let listener = TcpListener::bind(addr).await.with_context(|| format!("binding {addr}"))?;
    let port = listener.local_addr()?.port();
    tokio::spawn(async move {
        if let Err(e) = axum::serve(listener, app).await {
            tracing::error!(error = %e, "control listener stopped");
        }
    });
    Ok(port)
}

/// The control panel. Embedded rather than read from disk so that `nevosd` is
/// still one file you can copy somewhere and run.
async fn page() -> Response {
    (
        [(axum::http::header::CONTENT_TYPE, "text/html; charset=utf-8")],
        include_str!("../ui/index.html"),
    )
        .into_response()
}

async fn status(State(state): State<Arc<Daemon>>) -> Json<Status> {
    let connected = state.connected_devices();
    let devices = state
        .store
        .devices()
        .unwrap_or_default()
        .into_iter()
        .map(|d| DeviceStatus {
            connected: connected.iter().any(|c| *c == d.device_id),
            device_id: d.device_id,
            name: d.name,
            paired_at: d.paired_at,
            last_seen: d.last_seen,
        })
        .collect();

    Json(Status {
        daemon: state.name.clone(),
        version: env!("CARGO_PKG_VERSION").to_string(),
        data_dir: state.store.root().display().to_string(),
        mic_live: state.is_capturing(),
        keep_audio: state.keep_audio,
        agent: Backend { name: state.agent.name().to_string(), local: state.agent.is_local() },
        transcriber: Backend { name: state.stt.name().to_string(), local: state.stt.is_local() },
        devices,
        pending_pairings: state.pending_pairings(now_unix()),
    })
}

async fn pair(State(state): State<Arc<Daemon>>, Json(body): Json<PairBody>) -> Response {
    match state.complete_pairing(&body.code, now_unix()) {
        Ok(EntryOutcome::Granted { device_id }) => {
            Json(serde_json::json!({ "granted": true, "device_id": device_id })).into_response()
        }
        Ok(EntryOutcome::Rejected) => (
            StatusCode::BAD_REQUEST,
            Json(serde_json::json!({ "granted": false, "reason": "no device is showing that code" })),
        )
            .into_response(),
        Ok(EntryOutcome::LockedOut { retry_after }) => (
            StatusCode::TOO_MANY_REQUESTS,
            Json(serde_json::json!({ "granted": false, "retry_after": retry_after })),
        )
            .into_response(),
        Err(e) => internal(e),
    }
}

async fn forget(State(state): State<Arc<Daemon>>, Json(body): Json<ForgetBody>) -> Response {
    match state.store.forget_device(&body.device_id) {
        Ok(existed) => {
            state.forget_conversation(&body.device_id);
            Json(serde_json::json!({ "forgotten": existed })).into_response()
        }
        Err(e) => internal(e),
    }
}

async fn records(
    State(state): State<Arc<Daemon>>,
    axum::extract::Query(q): axum::extract::Query<RecordsQuery>,
) -> Response {
    let kind = match q.kind.as_str() {
        "transcript" => RecordKind::Transcript,
        _ => RecordKind::Note,
    };
    match state.store.records(kind) {
        Ok(records) => Json(records).into_response(),
        Err(e) => internal(e),
    }
}

async fn delete_record(State(state): State<Arc<Daemon>>, Json(body): Json<DeleteBody>) -> Response {
    let kind = match body.kind.as_str() {
        "transcript" => RecordKind::Transcript,
        _ => RecordKind::Note,
    };
    match state.store.delete_record(kind, &body.id) {
        Ok(existed) => Json(serde_json::json!({ "deleted": existed })).into_response(),
        Err(e) => internal(e),
    }
}

/// Sends a notification to every connected device.
///
/// The daemon knows things the device cannot — a build finished, a meeting
/// starts in five minutes — and this is how a script on the user's own machine
/// puts one on the robot's face. It is on the loopback API rather than the
/// device link because the sender is always something local.
async fn notify(State(state): State<Arc<Daemon>>, Json(body): Json<NotifyBody>) -> Response {
    let sent = state.notify(Notification {
        title: truncate_chars(&body.title, 48),
        body: truncate_chars(&body.body, 160),
        urgent: body.urgent,
    });
    Json(serde_json::json!({ "sent_to": sent })).into_response()
}

async fn purge_audio(State(state): State<Arc<Daemon>>) -> Response {
    match state.store.purge_audio() {
        Ok(n) => Json(serde_json::json!({ "removed": n })).into_response(),
        Err(e) => internal(e),
    }
}

async fn purge_all(State(state): State<Arc<Daemon>>) -> Response {
    match state.purge_all() {
        Ok(n) => {
            tracing::warn!(removed = n, "purged everything at the user's request");
            Json(serde_json::json!({ "removed": n })).into_response()
        }
        Err(e) => internal(e),
    }
}

fn internal(e: anyhow::Error) -> Response {
    tracing::error!(error = %e, "control request failed");
    (StatusCode::INTERNAL_SERVER_ERROR, Json(serde_json::json!({ "error": e.to_string() })))
        .into_response()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_long_transcript_is_cut_on_a_character_boundary() {
        let text = "é".repeat(600);
        let cut = truncate_chars(&text, 500);
        assert_eq!(cut.chars().count(), 500);
        assert!(cut.ends_with('…'));
    }

    #[test]
    fn a_short_transcript_is_untouched() {
        assert_eq!(truncate_chars("hello", 500), "hello");
    }

    #[test]
    fn an_error_shown_on_the_device_is_the_cause_not_the_context() {
        let e = anyhow::anyhow!("connection refused").context("connecting to model server");
        assert_eq!(short_error(&e), "connection refused");
    }

    #[tokio::test]
    async fn the_control_api_refuses_to_bind_to_the_network() {
        // The one configuration mistake here that would publish someone's
        // notes to their entire network.
        let state = crate::state::tests_support::daemon("refuse-bind");
        let err = run_control_listener(state, "0.0.0.0:0".parse().unwrap()).await.unwrap_err();
        assert!(err.to_string().contains("loopback"), "{err}");
    }
}
