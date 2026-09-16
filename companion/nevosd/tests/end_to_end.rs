//! A fake device against a real daemon.
//!
//! Everything below the socket is the real thing: the real listeners, the real
//! session state machine, the real store writing real files. Only the model and
//! the transcriber are mocks, because the point is to prove the pipeline fits
//! together — pair, speak, be transcribed, ask, be answered — not to prove that
//! llama.cpp works.
//!
//! This is the test that would have caught every integration bug in this
//! milestone, and it runs on a machine with nothing installed.
use futures_util::{SinkExt, StreamExt};
use nevos_agent::MockAgent;
use nevos_proto::{
    frame_split, frame_wrap, peek_id, AgentDone, AgentRequest, AgentToken, AudioChunk, Hello,
    HelloAck, MoodHint, MsgId, Notification, Pair, PairResult, TranscriptFinal, PROTOCOL_VERSION,
};
use nevos_store::Store;
use nevos_stt::MockTranscriber;
use nevosd::state::Daemon;
use std::sync::Arc;
use std::time::Duration;
use tokio::net::TcpStream;
use tokio_tungstenite::tungstenite::Message;
use tokio_tungstenite::{connect_async, MaybeTlsStream, WebSocketStream};

type Socket = WebSocketStream<MaybeTlsStream<TcpStream>>;

struct Harness {
    device_port: u16,
    control_port: u16,
    state: Arc<Daemon>,
}

async fn start(tag: &str) -> Harness {
    let root = std::env::temp_dir().join(format!(
        "nevosd-e2e-{tag}-{}-{:?}",
        std::process::id(),
        std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()
    ));
    std::fs::create_dir_all(&root).unwrap();

    let state = Arc::new(Daemon::new(
        "test-mac",
        Store::open(&root).unwrap(),
        Arc::new(MockAgent::new("Glad to help. The kettle is on.")),
        Arc::new(MockTranscriber::new("put the kettle on")),
        false,
    ));

    let device_port =
        nevosd::server::run_device_listener(state.clone(), "127.0.0.1:0".parse().unwrap())
            .await
            .unwrap();
    let control_port =
        nevosd::server::run_control_listener(state.clone(), "127.0.0.1:0".parse().unwrap())
            .await
            .unwrap();

    Harness { device_port, control_port, state }
}

impl Harness {
    async fn connect(&self) -> Socket {
        let url = format!("ws://127.0.0.1:{}/ws", self.device_port);
        let (socket, _) = connect_async(&url).await.expect("device could not connect");
        socket
    }

    /// POSTs to the control API using the daemon's own loopback HTTP client —
    /// which is also, usefully, a second exercise of that code.
    async fn control(&self, path: &str, body: &str) -> String {
        let (tx, mut rx) = tokio::sync::mpsc::channel(16);
        let port = self.control_port;
        let path = path.to_string();
        let body = body.to_string();
        let task = tokio::spawn(async move {
            nevos_agent::http::post_json_lines("127.0.0.1", port, &path, &body, tx).await
        });
        let mut out = String::new();
        while let Some(line) = rx.recv().await {
            out.push_str(&line);
        }
        task.await.unwrap().expect("control request failed");
        out
    }

    async fn status(&self) -> serde_json::Value {
        let (tx, mut rx) = tokio::sync::mpsc::channel(16);
        let port = self.control_port;
        let task = tokio::spawn(async move {
            nevos_agent::http::get_lines("127.0.0.1", port, "/api/status", tx).await
        });
        let mut out = String::new();
        while let Some(line) = rx.recv().await {
            out.push_str(&line);
        }
        task.await.unwrap().unwrap();
        serde_json::from_str(&out).unwrap()
    }
}

async fn send(socket: &mut Socket, payload: Vec<u8>) {
    socket.send(Message::Binary(frame_wrap(&payload).unwrap().into())).await.unwrap();
}

/// Reads one framed message, failing rather than hanging if none arrives.
async fn recv(socket: &mut Socket) -> (MsgId, Vec<u8>) {
    loop {
        let message = tokio::time::timeout(Duration::from_secs(5), socket.next())
            .await
            .expect("timed out waiting for the daemon")
            .expect("socket closed")
            .expect("socket error");
        let Message::Binary(bytes) = message else { continue };
        let (range, _) = frame_split(&bytes).unwrap().expect("a partial frame in one message");
        let payload = bytes[range].to_vec();
        let id = MsgId::from_u16(peek_id(&payload).unwrap()).expect("unknown message id");
        return (id, payload);
    }
}

fn hello(token: &str) -> Vec<u8> {
    Hello {
        protocol: PROTOCOL_VERSION,
        device_id: "nev-e2e".into(),
        firmware: "0.1.0".into(),
        token: token.into(),
    }
    .encode()
}

/// Pairs a fresh device and returns its token.
async fn pair(h: &Harness, socket: &mut Socket) -> String {
    send(socket, hello("")).await;
    let (id, payload) = recv(socket).await;
    assert_eq!(id, MsgId::HelloAck);
    let ack = HelloAck::decode(&payload).unwrap();
    assert!(!ack.accepted && ack.needs_pairing, "a new device must be told to pair");
    assert_eq!(ack.daemon_name, "test-mac");
    assert!(ack.unix_time > 1_600_000_000, "the device gets its clock from here");

    send(socket, Pair { code: "424242".into() }.encode()).await;

    // Nothing is granted until a human types the code. Wait long enough that a
    // daemon which pairs on its own would have done so by now.
    tokio::time::sleep(Duration::from_millis(150)).await;
    let status = h.status().await;
    assert_eq!(status["devices"].as_array().unwrap().len(), 0, "paired without a human");
    assert_eq!(status["pending_pairings"][0], "nev-e2e");

    let reply = h.control("/api/pair", r#"{"code":"424242"}"#).await;
    assert!(reply.contains("\"granted\":true"), "{reply}");

    let (id, payload) = recv(socket).await;
    assert_eq!(id, MsgId::PairResult);
    let result = PairResult::decode(&payload).unwrap();
    assert!(result.granted);
    assert_eq!(result.token.len(), 64);
    result.token
}

#[tokio::test]
async fn a_device_pairs_only_when_a_human_enters_the_code() {
    let h = start("pair").await;
    let mut socket = h.connect().await;
    let token = pair(&h, &mut socket).await;

    // And the token works on a fresh connection, which is what the device does
    // after every power cycle.
    drop(socket);
    let mut socket = h.connect().await;
    send(&mut socket, hello(&token)).await;
    let (id, payload) = recv(&mut socket).await;
    assert_eq!(id, MsgId::HelloAck);
    assert!(HelloAck::decode(&payload).unwrap().accepted);
}

#[tokio::test]
async fn a_wrong_code_pairs_nothing() {
    let h = start("wrong-code").await;
    let mut socket = h.connect().await;
    send(&mut socket, hello("")).await;
    recv(&mut socket).await;
    send(&mut socket, Pair { code: "424242".into() }.encode()).await;
    tokio::time::sleep(Duration::from_millis(100)).await;

    let (tx, mut rx) = tokio::sync::mpsc::channel(16);
    let port = h.control_port;
    let task = tokio::spawn(async move {
        nevos_agent::http::post_json_lines(
            "127.0.0.1",
            port,
            "/api/pair",
            r#"{"code":"000000"}"#,
            tx,
        )
        .await
    });
    while rx.recv().await.is_some() {}
    // The API answers 400, which the loopback client reports as an error.
    assert!(task.await.unwrap().is_err(), "a wrong code must not be a success");
    assert!(h.state.store.devices().unwrap().is_empty());
}

#[tokio::test]
async fn an_unpaired_device_cannot_ask_anything() {
    let h = start("unpaired").await;
    let mut socket = h.connect().await;
    send(&mut socket, hello("")).await;
    recv(&mut socket).await; // hello_ack

    send(
        &mut socket,
        AgentRequest { turn: 1, text: "what is the wifi password".into(), app: "agent".into() }
            .encode(),
    )
    .await;

    // The connection is closed rather than answered.
    let closed = tokio::time::timeout(Duration::from_secs(5), async {
        while let Some(Ok(message)) = socket.next().await {
            if let Message::Binary(bytes) = message {
                let (range, _) = frame_split(&bytes).unwrap().unwrap();
                let id = MsgId::from_u16(peek_id(&bytes[range]).unwrap()).unwrap();
                panic!("an unpaired device got {}", id.name());
            }
        }
    })
    .await;
    assert!(closed.is_ok(), "the daemon left an unpaired connection open");
}

#[tokio::test]
async fn speech_becomes_a_transcript_and_a_saved_note() {
    let h = start("speech").await;
    let mut socket = h.connect().await;
    pair(&h, &mut socket).await;

    // A second of 16 kHz audio, in chunks the size the device actually sends.
    let samples = vec![1234i16; 16_000];
    // The last chunk is a short one, as it always is in practice: an utterance
    // is not a whole number of buffers long.
    let blocks: Vec<&[i16]> = samples.chunks(2048).collect();
    let last = blocks.len() - 1;
    for (seq, block) in blocks.iter().copied().enumerate() {
        let mut pcm = Vec::with_capacity(block.len() * 2);
        for s in block {
            pcm.extend_from_slice(&s.to_le_bytes());
        }
        send(
            &mut socket,
            AudioChunk {
                seq: seq as u32,
                session: 1,
                r#final: seq == last,
                pcm,
            }
            .encode(),
        )
        .await;
    }

    let (id, payload) = recv(&mut socket).await;
    assert_eq!(id, MsgId::TranscriptFinal);
    assert_eq!(TranscriptFinal::decode(&payload).unwrap().text, "put the kettle on");

    // The note is on disk, and the capture is over so the indicator is dark.
    let mut tries = 0;
    let records = loop {
        let records = h.state.store.records(nevos_store::RecordKind::Note).unwrap();
        if !records.is_empty() || tries > 50 {
            break records;
        }
        tries += 1;
        tokio::time::sleep(Duration::from_millis(20)).await;
    };
    assert_eq!(records.len(), 1, "the transcript was not filed");
    assert_eq!(records[0].text, "put the kettle on");
    assert!(records[0].audio.is_none(), "audio must not be kept unless asked for");
    assert_eq!(h.status().await["mic_live"], false);
}

#[tokio::test]
async fn a_question_is_answered_in_pieces_with_a_face_to_match() {
    let h = start("agent").await;
    let mut socket = h.connect().await;
    pair(&h, &mut socket).await;

    send(
        &mut socket,
        AgentRequest { turn: 4, text: "is the kettle on?".into(), app: "agent".into() }.encode(),
    )
    .await;

    let mut reply = String::new();
    let mut tokens = 0;
    let mut mood = None;
    loop {
        let (id, payload) = recv(&mut socket).await;
        match id {
            MsgId::AgentToken => {
                let t = AgentToken::decode(&payload).unwrap();
                assert_eq!(t.turn, 4, "a token arrived for the wrong turn");
                reply.push_str(&t.text);
                tokens += 1;
            }
            MsgId::MoodHint => mood = Some(MoodHint::decode(&payload).unwrap()),
            MsgId::AgentDone => {
                let done = AgentDone::decode(&payload).unwrap();
                assert_eq!(done.turn, 4);
                assert_eq!(done.error, "", "the mock agent should not fail");
                break;
            }
            other => panic!("unexpected {}", other.name()),
        }
    }

    assert_eq!(reply, "Glad to help. The kettle is on.");
    assert!(tokens > 1, "the reply arrived in one lump instead of streaming");
    // "Glad to help" reads as happy, which is mood 2 in nev_mood_t.
    assert_eq!(mood.expect("no mood hint was sent").mood, 2);

    // And the exchange is remembered, so "is it still?" has something to refer to.
    let history = h.state.conversation_for("nev-e2e");
    assert_eq!(history.turns().len(), 2);
}

#[tokio::test]
async fn the_status_endpoint_states_where_the_work_happens() {
    // This is what the tray shows, and the claim it makes is the product's
    // central one. If it is ever wrong, it is worse than not showing it.
    let h = start("status").await;
    let status = h.status().await;
    assert_eq!(status["agent"]["local"], true);
    assert_eq!(status["transcriber"]["local"], true);
    assert_eq!(status["mic_live"], false);
    assert_eq!(status["keep_audio"], false);
    assert_eq!(status["daemon"], "test-mac");
}

#[tokio::test]
async fn purging_erases_the_notes_and_keeps_the_pairing() {
    let h = start("purge").await;
    let mut socket = h.connect().await;
    let token = pair(&h, &mut socket).await;

    h.state
        .store
        .save_record(&nevos_store::Record {
            id: "r1".into(),
            kind: nevos_store::RecordKind::Note,
            created_at: 1,
            text: "something private".into(),
            markers: vec![],
            audio: None,
        })
        .unwrap();

    let reply = h.control("/api/purge/all", "{}").await;
    assert!(reply.contains("removed"), "{reply}");
    assert!(h.state.store.records(nevos_store::RecordKind::Note).unwrap().is_empty());

    // The robot on the desk is still paired: erasing your notes should not also
    // mean setting the device up again.
    drop(socket);
    let mut socket = h.connect().await;
    send(&mut socket, hello(&token)).await;
    let (_, payload) = recv(&mut socket).await;
    assert!(HelloAck::decode(&payload).unwrap().accepted);
}

#[tokio::test]
async fn a_notification_reaches_a_paired_device() {
    let h = start("notify").await;
    let mut socket = h.connect().await;
    pair(&h, &mut socket).await;

    // Anything local can put a message on the robot's face: a build script, a
    // calendar hook, a cron job. It goes through the loopback API because the
    // sender is always something on this machine.
    let reply = h
        .control("/api/notify", r#"{"title":"Build finished","body":"all green"}"#)
        .await;
    assert!(reply.contains("\"sent_to\":1"), "{reply}");

    let (id, payload) = recv(&mut socket).await;
    assert_eq!(id, MsgId::Notification);
    let note = Notification::decode(&payload).unwrap();
    assert_eq!(note.title, "Build finished");
    assert_eq!(note.body, "all green");
}

#[tokio::test]
async fn an_unpaired_device_is_not_sent_notifications() {
    // A device on the network that has not been paired is not yet anyone's
    // robot, and whatever the notification says is none of its business.
    let h = start("notify-unpaired").await;
    let mut socket = h.connect().await;
    send(&mut socket, hello("")).await;
    recv(&mut socket).await; // hello_ack: needs pairing

    let reply = h.control("/api/notify", r#"{"title":"secret"}"#).await;
    assert!(reply.contains("\"sent_to\":0"), "{reply}");
}

#[tokio::test]
async fn one_note_can_be_deleted_without_deleting_the_rest() {
    let h = start("delete-one").await;
    for id in ["a", "b"] {
        h.state
            .store
            .save_record(&nevos_store::Record {
                id: id.into(),
                kind: nevos_store::RecordKind::Note,
                created_at: 1,
                text: format!("note {id}"),
                markers: vec![],
                audio: None,
            })
            .unwrap();
    }

    let reply = h.control("/api/records/delete", r#"{"kind":"note","id":"a"}"#).await;
    assert!(reply.contains("\"deleted\":true"), "{reply}");

    let left = h.state.store.records(nevos_store::RecordKind::Note).unwrap();
    assert_eq!(left.len(), 1);
    assert_eq!(left[0].id, "b");
}

#[tokio::test]
async fn the_control_panel_is_served_on_loopback() {
    let h = start("panel").await;
    let (tx, mut rx) = tokio::sync::mpsc::channel(64);
    let port = h.control_port;
    let task =
        tokio::spawn(async move { nevos_agent::http::get_lines("127.0.0.1", port, "/", tx).await });

    let mut page = String::new();
    while let Some(line) = rx.recv().await {
        page.push_str(&line);
    }
    task.await.unwrap().unwrap();

    assert!(page.contains("<!doctype html>"), "the panel is not being served");
    // The two things the panel exists for.
    assert!(page.contains("Microphone is off"));
    assert!(page.contains("Delete everything"));
}
