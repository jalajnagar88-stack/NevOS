//! A pretend NEVOS device, for working on the daemon and the control panel
//! without hardware.
//!
//! It pairs itself (reading its own code back through the control API, which is
//! what a person at the keyboard does), then streams audio for as long as it is
//! running. That last part is the point: the microphone indicator is the single
//! most important thing on the control panel, and it cannot be designed against
//! a state that never occurs.
//!
//!   cargo run -p nevosd --example fake_device
//!   cargo run -p nevosd --example fake_device -- --quiet   (pair, but no audio)
use futures_util::{SinkExt, StreamExt};
use nevos_proto::{
    frame_split, frame_wrap, peek_id, AudioChunk, Hello, HelloAck, MsgId, Pair, PairResult,
    PROTOCOL_VERSION,
};
use std::time::Duration;
use tokio_tungstenite::tungstenite::Message;

const DEVICE_PORT: u16 = 4821;
const CONTROL_PORT: u16 = 4822;
const CODE: &str = "424242";

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    let quiet = std::env::args().any(|a| a == "--quiet");
    // Three short utterances and stop, which leaves notes behind to look at.
    let dictate = std::env::args().any(|a| a == "--dictate");

    let (mut socket, _) =
        tokio_tungstenite::connect_async(format!("ws://127.0.0.1:{DEVICE_PORT}/ws")).await?;
    println!("connected");

    let hello = Hello {
        protocol: PROTOCOL_VERSION,
        device_id: "nev-fakedevice".into(),
        firmware: "0.1.0-fake".into(),
        token: std::env::var("NEVOS_FAKE_TOKEN").unwrap_or_default(),
    };
    send(&mut socket, hello.encode()).await?;

    let (id, payload) = recv(&mut socket).await?;
    anyhow::ensure!(id == MsgId::HelloAck, "expected hello_ack, got {}", id.name());
    let ack = HelloAck::decode(&payload)?;
    println!("daemon: {}", ack.daemon_name);

    if ack.needs_pairing {
        send(&mut socket, Pair { code: CODE.into() }.encode()).await?;
        println!("showing code {CODE} — entering it on the daemon");
        tokio::time::sleep(Duration::from_millis(200)).await;

        // Standing in for the person who reads the screen and types.
        let client = reqwest_lite::post(
            CONTROL_PORT,
            "/api/pair",
            &format!("{{\"code\":\"{CODE}\"}}"),
        )
        .await?;
        println!("control API said: {client}");

        let (id, payload) = recv(&mut socket).await?;
        anyhow::ensure!(id == MsgId::PairResult, "expected pair_result");
        let result = PairResult::decode(&payload)?;
        anyhow::ensure!(result.granted, "pairing refused: {}", result.reason);
        println!("paired. token: {}", result.token);
        println!("(set NEVOS_FAKE_TOKEN to reconnect without pairing)");
    }

    if quiet {
        println!("connected and idle; ctrl-c to stop");
        loop {
            tokio::time::sleep(Duration::from_secs(60)).await;
        }
    }

    // 20 ms of silence, 50 times a second, the way the device will.
    let pcm = vec![0u8; 320 * 2];

    if dictate {
        for session in 1..=3u32 {
            println!("utterance {session}");
            for seq in 0..50u32 {
                let last = seq == 49;
                send(
                    &mut socket,
                    AudioChunk { seq, session, r#final: last, pcm: pcm.clone() }.encode(),
                )
                .await?;
                tokio::time::sleep(Duration::from_millis(20)).await;
            }
            tokio::time::sleep(Duration::from_millis(400)).await;
        }
        println!("done; three notes should be on the control panel");
        return Ok(());
    }

    println!("streaming audio — the control panel's mic indicator should be live");
    let mut seq = 0u32;
    loop {
        send(
            &mut socket,
            AudioChunk { seq, session: 1, r#final: false, pcm: pcm.clone() }.encode(),
        )
        .await?;
        seq += 1;
        tokio::time::sleep(Duration::from_millis(20)).await;
    }
}

async fn send<S>(socket: &mut S, payload: Vec<u8>) -> anyhow::Result<()>
where
    S: SinkExt<Message> + Unpin,
    <S as futures_util::Sink<Message>>::Error: std::error::Error + Send + Sync + 'static,
{
    socket.send(Message::Binary(frame_wrap(&payload)?.into())).await?;
    Ok(())
}

async fn recv<S>(socket: &mut S) -> anyhow::Result<(MsgId, Vec<u8>)>
where
    S: StreamExt<Item = Result<Message, tokio_tungstenite::tungstenite::Error>> + Unpin,
{
    loop {
        let Some(message) = socket.next().await else {
            anyhow::bail!("the daemon closed the connection");
        };
        if let Message::Binary(bytes) = message? {
            let Some((range, _)) = frame_split(&bytes)? else { continue };
            let payload = bytes[range].to_vec();
            let id = MsgId::from_u16(peek_id(&payload)?).ok_or_else(|| {
                anyhow::anyhow!("the daemon sent a message this device does not know")
            })?;
            return Ok((id, payload));
        }
    }
}

/// A five-line HTTP POST, so this example does not drag in an HTTP client for
/// one call to a loopback port.
mod reqwest_lite {
    use tokio::io::{AsyncReadExt, AsyncWriteExt};
    use tokio::net::TcpStream;

    pub async fn post(port: u16, path: &str, body: &str) -> anyhow::Result<String> {
        let mut sock = TcpStream::connect(("127.0.0.1", port)).await?;
        let request = format!(
            "POST {path} HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n\
             Content-Length: {}\r\nConnection: close\r\n\r\n{body}",
            body.len()
        );
        sock.write_all(request.as_bytes()).await?;
        let mut reply = String::new();
        sock.read_to_string(&mut reply).await?;
        Ok(reply.rsplit("\r\n\r\n").next().unwrap_or_default().trim().to_string())
    }
}
