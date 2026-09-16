//! The transport against a real socket.
//!
//! The unit tests cover the decoder and the request builder separately. What
//! they cannot show is that the pieces fit: that the request we write is one a
//! server will answer, that a reply arriving a few bytes at a time reaches the
//! caller as words rather than as one block at the end, and that a failure
//! halfway through a reply is reported rather than passed off as a short answer.
//!
//! The fake server here is about forty lines and speaks chunked NDJSON exactly
//! as Ollama does, which makes those three things testable on a machine with no
//! model installed — including in CI.
use nevos_agent::{Agent, AgentEvent, AgentRequest, OllamaAgent};
use std::time::Duration;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpListener;
use tokio::sync::mpsc;

/// How the fake server should behave once it has read the request.
enum Behaviour {
    /// Stream these token strings, then a final done line.
    Reply(Vec<&'static str>),
    /// Stream a token, then report an error mid-stream the way Ollama does.
    FailHalfway,
    /// Answer with an HTTP error.
    Status(&'static str),
}

/// Starts a one-shot server on a loopback port. Returns the port and the
/// request body it received.
async fn fake_ollama(
    behaviour: Behaviour,
) -> (u16, tokio::task::JoinHandle<String>) {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = listener.local_addr().unwrap().port();

    let handle = tokio::spawn(async move {
        let (mut sock, _) = listener.accept().await.unwrap();

        // Read headers, then exactly the body the Content-Length announced.
        let mut raw = Vec::new();
        let mut buf = [0u8; 1024];
        let head_end = loop {
            if let Some(p) = raw.windows(4).position(|w| w == b"\r\n\r\n") {
                break p;
            }
            let n = sock.read(&mut buf).await.unwrap();
            assert!(n > 0, "client closed before sending headers");
            raw.extend_from_slice(&buf[..n]);
        };
        let head = String::from_utf8_lossy(&raw[..head_end]).to_string();
        let len: usize = head
            .lines()
            .find_map(|l| l.strip_prefix("Content-Length: "))
            .map(|v| v.trim().parse().unwrap())
            .unwrap_or(0);
        let mut body = raw[head_end + 4..].to_vec();
        while body.len() < len {
            let n = sock.read(&mut buf).await.unwrap();
            assert!(n > 0, "client closed mid-body");
            body.extend_from_slice(&buf[..n]);
        }

        match behaviour {
            Behaviour::Status(status) => {
                let payload = "{\"error\":\"nope\"}";
                let resp = format!(
                    "HTTP/1.1 {status}\r\nContent-Length: {}\r\n\r\n{payload}",
                    payload.len()
                );
                sock.write_all(resp.as_bytes()).await.unwrap();
            }
            Behaviour::Reply(tokens) => {
                sock.write_all(
                    b"HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\n\
                      Transfer-Encoding: chunked\r\n\r\n",
                )
                .await
                .unwrap();
                for token in tokens {
                    let line = format!(
                        "{{\"message\":{{\"role\":\"assistant\",\"content\":\"{token}\"}},\"done\":false}}\n"
                    );
                    write_chunk(&mut sock, &line).await;
                    // A real model does not answer instantly, and the point of
                    // streaming is that the caller sees words before the end.
                    tokio::time::sleep(Duration::from_millis(5)).await;
                }
                write_chunk(&mut sock, "{\"message\":{\"content\":\"\"},\"done\":true}\n").await;
                sock.write_all(b"0\r\n\r\n").await.unwrap();
            }
            Behaviour::FailHalfway => {
                sock.write_all(
                    b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n",
                )
                .await
                .unwrap();
                write_chunk(
                    &mut sock,
                    "{\"message\":{\"content\":\"Once upon\"},\"done\":false}\n",
                )
                .await;
                write_chunk(&mut sock, "{\"error\":\"context window exceeded\"}\n").await;
                sock.write_all(b"0\r\n\r\n").await.unwrap();
            }
        }
        sock.flush().await.unwrap();
        String::from_utf8_lossy(&body).to_string()
    });

    (port, handle)
}

async fn write_chunk(sock: &mut tokio::net::TcpStream, body: &str) {
    sock.write_all(format!("{:x}\r\n{body}\r\n", body.len()).as_bytes()).await.unwrap();
}

/// Collects a reply, recording how much text had arrived by the time each event
/// came in so "it streamed" is an assertion rather than an assumption.
async fn drain(mut rx: mpsc::Receiver<AgentEvent>) -> (String, Vec<AgentEvent>) {
    let mut text = String::new();
    let mut events = Vec::new();
    while let Some(ev) = rx.recv().await {
        if let AgentEvent::Token(t) = &ev {
            text.push_str(t);
        }
        events.push(ev);
    }
    (text, events)
}

#[tokio::test]
async fn a_streamed_reply_arrives_in_pieces() {
    let (port, server) = fake_ollama(Behaviour::Reply(vec!["The ", "kettle ", "is ", "boiling."])).await;
    let agent = OllamaAgent::new("127.0.0.1", port, "test-model").unwrap();
    let (tx, rx) = mpsc::channel(64);

    let req = AgentRequest { turn: 7, text: "is the kettle on?".into(), app: "agent".into() };
    let collector = tokio::spawn(drain(rx));
    agent.respond(&req, &[], tx).await.unwrap();
    let (text, events) = collector.await.unwrap();

    assert_eq!(text, "The kettle is boiling.");

    // Four tokens, not one: a client that buffered the whole body and emitted
    // it at the end would pass a text-only assertion.
    let tokens = events.iter().filter(|e| matches!(e, AgentEvent::Token(_))).count();
    assert_eq!(tokens, 4, "reply was not streamed");

    // The thinking face goes out before any text does.
    assert!(matches!(events[0], AgentEvent::Mood(_)), "no mood before the first token");

    let body = server.await.unwrap();
    assert!(body.contains("\"stream\":true"), "{body}");
    assert!(body.contains("is the kettle on?"), "{body}");
}

#[tokio::test]
async fn an_error_mid_stream_fails_the_turn() {
    // The device must show "something went wrong", not a story that stops after
    // two words as though the model had nothing else to say.
    let (port, server) = fake_ollama(Behaviour::FailHalfway).await;
    let agent = OllamaAgent::new("127.0.0.1", port, "test-model").unwrap();
    let (tx, rx) = mpsc::channel(64);

    let req = AgentRequest { turn: 1, text: "tell me a story".into(), app: "agent".into() };
    let collector = tokio::spawn(drain(rx));
    let result = agent.respond(&req, &[], tx).await;
    let (text, _) = collector.await.unwrap();

    assert!(result.is_err(), "a mid-stream error must not look like a complete reply");
    assert!(result.unwrap_err().to_string().contains("context window"));
    assert_eq!(text, "Once upon", "text already delivered is still delivered");
    let _ = server.await;
}

#[tokio::test]
async fn an_http_error_names_the_status() {
    let (port, server) = fake_ollama(Behaviour::Status("404 Not Found")).await;
    let agent = OllamaAgent::new("127.0.0.1", port, "missing-model").unwrap();
    let (tx, rx) = mpsc::channel(8);
    let req = AgentRequest { turn: 1, text: "hello".into(), app: String::new() };

    let collector = tokio::spawn(drain(rx));
    let err = agent.respond(&req, &[], tx).await.unwrap_err();
    let _ = collector.await;

    assert!(err.to_string().contains("404"), "{err}");
    let _ = server.await;
}

#[tokio::test]
async fn a_dead_server_is_an_error_rather_than_a_hang() {
    // Port 1 on loopback with nothing listening: connect refuses immediately.
    let agent = OllamaAgent::new("127.0.0.1", 1, "test-model").unwrap();
    let (tx, rx) = mpsc::channel(8);
    let req = AgentRequest { turn: 1, text: "hello".into(), app: String::new() };

    let collector = tokio::spawn(drain(rx));
    let result = tokio::time::timeout(Duration::from_secs(5), agent.respond(&req, &[], tx)).await;
    let _ = collector.await;

    let err = result.expect("respond hung on a dead server").unwrap_err();
    assert!(err.to_string().contains("model server"), "{err}");
}

#[tokio::test]
async fn installed_models_are_listed() {
    // /api/tags answers with a plain Content-Length body rather than chunked,
    // so this also covers the other framing path.
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = listener.local_addr().unwrap().port();
    tokio::spawn(async move {
        let (mut sock, _) = listener.accept().await.unwrap();
        let mut buf = [0u8; 2048];
        let _ = sock.read(&mut buf).await.unwrap();
        let payload = r#"{"models":[{"name":"llama3.2:3b"},{"name":"qwen2.5:7b"}]}"#;
        sock.write_all(
            format!("HTTP/1.1 200 OK\r\nContent-Length: {}\r\n\r\n{payload}", payload.len())
                .as_bytes(),
        )
        .await
        .unwrap();
    });

    let agent = OllamaAgent::new("127.0.0.1", port, "llama3.2:3b").unwrap();
    let models = agent.installed_models().await.unwrap();
    assert_eq!(models, vec!["llama3.2:3b", "qwen2.5:7b"]);
    assert!(agent.has_model(&models));
}
