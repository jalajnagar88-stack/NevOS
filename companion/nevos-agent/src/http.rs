//! A minimal HTTP/1.1 client that only ever talks to the loopback interface.
//!
//! NEVOS has no cloud backend, so this crate has exactly one job: POST to a
//! model server running on the same machine and stream the reply back. That
//! makes a full HTTP stack — connection pools, redirects, TLS, proxies — dead
//! weight, and the TLS part is the expensive half of it.
//!
//! Refusing anything but loopback is also the enforcement point for the
//! product's central promise. `NEVOS_OLLAMA_HOST=some.remote.host` would
//! quietly turn every transcript in the house into an outbound request; here it
//! is a startup error instead. The check lives in the transport rather than in
//! the caller because that is the layer a mistake has to get past.
use anyhow::{anyhow, bail, Context, Result};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;
use tokio::sync::mpsc;

/// Is this a name that can only resolve to this machine?
///
/// Deliberately a string test rather than a DNS lookup: a lookup would let a
/// hostile or misconfigured resolver answer `127.0.0.1` for a remote name at
/// check time and something else at connect time. Only literals that cannot
/// mean anything but loopback are accepted.
pub fn is_loopback_host(host: &str) -> bool {
    if host.eq_ignore_ascii_case("localhost") {
        return true;
    }
    if host == "::1" || host == "[::1]" {
        return true;
    }
    // 127.0.0.0/8, in full: every octet must parse, and only the first is fixed.
    let mut parts = host.split('.');
    let Some(first) = parts.next() else {
        return false;
    };
    if first != "127" {
        return false;
    }
    let rest: Vec<&str> = parts.collect();
    if rest.len() != 3 {
        return false;
    }
    rest.iter().all(|p| !p.is_empty() && p.parse::<u8>().is_ok())
}

/// Decodes `Transfer-Encoding: chunked`.
///
/// Written as a `push`-driven state machine with no socket in it so the nasty
/// cases — a size line split across two reads, a chunk split across three —
/// are unit tests rather than a thing you hope the network never does.
#[derive(Debug, Default)]
pub struct ChunkedDecoder {
    buf: Vec<u8>,
    /// Bytes still expected in the chunk being read, `None` while reading a size line.
    remaining: Option<usize>,
    done: bool,
}

impl ChunkedDecoder {
    pub fn new() -> Self {
        Self::default()
    }

    /// True once the terminating zero-length chunk has been seen.
    pub fn is_done(&self) -> bool {
        self.done
    }

    /// Feeds raw bytes in, returns whatever body bytes became available.
    pub fn push(&mut self, bytes: &[u8]) -> Result<Vec<u8>> {
        self.buf.extend_from_slice(bytes);
        let mut out = Vec::new();

        loop {
            match self.remaining {
                None => {
                    if self.done {
                        // Trailers after the final chunk. We have no use for them.
                        self.buf.clear();
                        return Ok(out);
                    }
                    let Some(eol) = find_crlf(&self.buf) else {
                        // Guard against a peer that never sends a newline.
                        if self.buf.len() > 64 {
                            bail!("chunked: size line longer than 64 bytes");
                        }
                        return Ok(out);
                    };
                    let line = &self.buf[..eol];
                    // A chunk extension (";name=value") is legal and ignorable.
                    let size_hex = line.split(|b| *b == b';').next().unwrap_or(line);
                    let size_str = std::str::from_utf8(size_hex)
                        .context("chunked: size line is not utf-8")?
                        .trim();
                    let size = usize::from_str_radix(size_str, 16)
                        .map_err(|_| anyhow!("chunked: bad size line {size_str:?}"))?;
                    self.buf.drain(..eol + 2);
                    if size == 0 {
                        self.done = true;
                    } else {
                        self.remaining = Some(size);
                    }
                }
                Some(n) => {
                    // The chunk plus its trailing CRLF must all be present before
                    // we can move on; take what we can and wait for the rest.
                    let take = n.min(self.buf.len());
                    out.extend_from_slice(&self.buf[..take]);
                    self.buf.drain(..take);
                    let left = n - take;
                    if left > 0 {
                        self.remaining = Some(left);
                        return Ok(out);
                    }
                    if self.buf.len() < 2 {
                        self.remaining = Some(0);
                        return Ok(out);
                    }
                    if &self.buf[..2] != b"\r\n" {
                        bail!("chunked: chunk not terminated by CRLF");
                    }
                    self.buf.drain(..2);
                    self.remaining = None;
                }
            }
        }
    }
}

fn find_crlf(buf: &[u8]) -> Option<usize> {
    buf.windows(2).position(|w| w == b"\r\n")
}

/// Splits a byte stream into lines, holding a partial line until it completes.
///
/// Ollama answers a streaming request with newline-delimited JSON, and a chunk
/// boundary lands in the middle of a JSON object often enough that treating one
/// read as one message produces a parse error every few replies.
#[derive(Debug, Default)]
pub struct LineSplitter {
    buf: Vec<u8>,
    limit: usize,
}

impl LineSplitter {
    /// `limit` caps a single line, so a server that never sends a newline
    /// cannot grow this buffer without bound.
    pub fn new(limit: usize) -> Self {
        Self { buf: Vec::new(), limit }
    }

    pub fn push(&mut self, bytes: &[u8]) -> Result<Vec<String>> {
        self.buf.extend_from_slice(bytes);
        let mut lines = Vec::new();
        while let Some(pos) = self.buf.iter().position(|b| *b == b'\n') {
            let line: Vec<u8> = self.buf.drain(..pos + 1).collect();
            let text = String::from_utf8_lossy(&line).trim().to_string();
            if !text.is_empty() {
                lines.push(text);
            }
        }
        if self.buf.len() > self.limit {
            bail!("response line exceeded {} bytes", self.limit);
        }
        Ok(lines)
    }

    /// Returns whatever is left when the body ends without a final newline.
    ///
    /// Not a detail: a non-streaming endpoint answering with a single JSON
    /// object and a Content-Length sends no trailing newline at all, so without
    /// this the entire response is silently discarded.
    pub fn flush(&mut self) -> Option<String> {
        let text = String::from_utf8_lossy(&self.buf).trim().to_string();
        self.buf.clear();
        (!text.is_empty()).then_some(text)
    }
}

/// What a response's framing turned out to be.
enum Framing {
    Chunked,
    Length(usize),
    /// No length and no chunking: the body ends when the connection does.
    ToEof,
}

/// POSTs JSON to a loopback endpoint and streams the response body back as lines.
pub async fn post_json_lines(
    host: &str,
    port: u16,
    path: &str,
    body: &str,
    lines: mpsc::Sender<String>,
) -> Result<()> {
    request_lines("POST", host, port, path, Some(body), lines).await
}

/// GETs from a loopback endpoint and streams the response body back as lines.
pub async fn get_lines(host: &str, port: u16, path: &str, lines: mpsc::Sender<String>) -> Result<()> {
    request_lines("GET", host, port, path, None, lines).await
}

/// The one request path.
///
/// Returns once the body is complete. The caller gets lines through `lines`;
/// dropping the receiver ends the request, which is how a cancelled agent turn
/// stops the model mid-sentence instead of paying for the rest of it.
pub async fn request_lines(
    method: &str,
    host: &str,
    port: u16,
    path: &str,
    body: Option<&str>,
    lines: mpsc::Sender<String>,
) -> Result<()> {
    if !is_loopback_host(host) {
        bail!(
            "refusing to connect to {host:?}: the NEVOS agent only talks to loopback. \
             There is no cloud backend, and a non-local model server would send \
             everything the microphone hears off this machine."
        );
    }

    let mut sock = TcpStream::connect((host.trim_matches(|c| c == '[' || c == ']'), port))
        .await
        .with_context(|| format!("connecting to model server at {host}:{port}"))?;
    // Replies arrive a few tokens at a time; Nagle would batch them into a
    // stutter that is visible on a face animating at 30fps.
    let _ = sock.set_nodelay(true);

    let mut req = format!(
        "{method} {path} HTTP/1.1\r\n\
         Host: {host}:{port}\r\n\
         Connection: close\r\n"
    );
    if let Some(body) = body {
        req.push_str(&format!(
            "Content-Type: application/json\r\nContent-Length: {}\r\n",
            body.len()
        ));
    }
    req.push_str("\r\n");
    sock.write_all(req.as_bytes()).await?;
    if let Some(body) = body {
        sock.write_all(body.as_bytes()).await?;
    }
    sock.flush().await?;

    let mut raw = Vec::new();
    let mut chunk = [0u8; 4096];

    // Headers first. They are small, so reading them byte-window by byte-window
    // is fine, but a server that sends nothing but headers forever is not.
    let head_end = loop {
        if let Some(pos) = find_headers_end(&raw) {
            break pos;
        }
        if raw.len() > 16 * 1024 {
            bail!("model server sent more than 16 KiB of headers");
        }
        let n = sock.read(&mut chunk).await?;
        if n == 0 {
            bail!("model server closed the connection before sending headers");
        }
        raw.extend_from_slice(&chunk[..n]);
    };

    let head = String::from_utf8_lossy(&raw[..head_end]).to_string();
    let status = parse_status(&head)?;
    let framing = parse_framing(&head)?;

    let mut body_bytes: Vec<u8> = raw[head_end + 4..].to_vec();
    raw.clear();

    if status != 200 {
        // Read a bounded amount so the error says what the server objected to.
        while body_bytes.len() < 1024 {
            let n = sock.read(&mut chunk).await?;
            if n == 0 {
                break;
            }
            body_bytes.extend_from_slice(&chunk[..n]);
        }
        let detail = String::from_utf8_lossy(&body_bytes).trim().to_string();
        bail!("model server returned HTTP {status}: {detail}");
    }

    let mut decoder = ChunkedDecoder::new();
    let mut splitter = LineSplitter::new(1024 * 1024);
    let mut read_total = 0usize;

    // A plain function rather than a closure: the read loop also has to ask the
    // decoder whether the body has ended, and a closure capturing it mutably
    // would hold that borrow for the rest of the function.
    fn feed(
        framing: &Framing,
        decoder: &mut ChunkedDecoder,
        splitter: &mut LineSplitter,
        bytes: &[u8],
    ) -> Result<Vec<String>> {
        let decoded = match framing {
            Framing::Chunked => decoder.push(bytes)?,
            Framing::Length(_) | Framing::ToEof => bytes.to_vec(),
        };
        splitter.push(&decoded)
    }

    let first = std::mem::take(&mut body_bytes);
    read_total += first.len();
    for line in feed(&framing, &mut decoder, &mut splitter, &first)? {
        if lines.send(line).await.is_err() {
            return Ok(()); // Caller went away; stop generating.
        }
    }

    loop {
        if matches!(framing, Framing::Chunked) && decoder.is_done() {
            break;
        }
        if let Framing::Length(len) = framing {
            if read_total >= len {
                break;
            }
        }
        let n = sock.read(&mut chunk).await?;
        if n == 0 {
            break;
        }
        read_total += n;
        for line in feed(&framing, &mut decoder, &mut splitter, &chunk[..n])? {
            if lines.send(line).await.is_err() {
                return Ok(());
            }
        }
    }

    if let Some(last) = splitter.flush() {
        let _ = lines.send(last).await;
    }

    Ok(())
}

fn find_headers_end(buf: &[u8]) -> Option<usize> {
    buf.windows(4).position(|w| w == b"\r\n\r\n")
}

fn parse_status(head: &str) -> Result<u16> {
    let first = head.lines().next().unwrap_or_default();
    let code = first
        .split_whitespace()
        .nth(1)
        .ok_or_else(|| anyhow!("malformed status line {first:?}"))?;
    code.parse::<u16>()
        .map_err(|_| anyhow!("malformed status code in {first:?}"))
}

fn parse_framing(head: &str) -> Result<Framing> {
    let mut chunked = false;
    let mut length = None;
    for line in head.lines().skip(1) {
        let Some((name, value)) = line.split_once(':') else {
            continue;
        };
        let value = value.trim();
        if name.eq_ignore_ascii_case("transfer-encoding") && value.eq_ignore_ascii_case("chunked") {
            chunked = true;
        } else if name.eq_ignore_ascii_case("content-length") {
            length = Some(
                value
                    .parse::<usize>()
                    .map_err(|_| anyhow!("bad Content-Length {value:?}"))?,
            );
        }
    }
    // RFC 9112: chunked wins if both are present.
    Ok(if chunked {
        Framing::Chunked
    } else if let Some(n) = length {
        Framing::Length(n)
    } else {
        Framing::ToEof
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn loopback_literals_are_accepted() {
        for host in ["localhost", "LocalHost", "127.0.0.1", "127.1.2.3", "::1", "[::1]"] {
            assert!(is_loopback_host(host), "{host} should be loopback");
        }
    }

    #[test]
    fn anything_that_could_leave_the_machine_is_refused() {
        // The last two matter most: they are what a typo or an attacker writes.
        for host in [
            "example.com",
            "192.168.1.10",
            "0.0.0.0",
            "127.0.0.1.example.com",
            "localhost.example.com",
            "127.0.0.999",
            "",
        ] {
            assert!(!is_loopback_host(host), "{host} must not count as loopback");
        }
    }

    #[test]
    fn chunked_decodes_a_whole_body() {
        let mut d = ChunkedDecoder::new();
        let out = d.push(b"5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n").unwrap();
        assert_eq!(out, b"hello world");
        assert!(d.is_done());
    }

    #[test]
    fn chunked_survives_being_split_anywhere() {
        // The failure this guards against only shows up under load, when reads
        // stop landing on tidy boundaries. So: try every boundary.
        let wire = b"1a\r\nabcdefghijklmnopqrstuvwxyz\r\n3\r\n012\r\n0\r\n\r\n";
        for split in 1..wire.len() {
            let mut d = ChunkedDecoder::new();
            let mut got = d.push(&wire[..split]).unwrap();
            got.extend(d.push(&wire[split..]).unwrap());
            assert_eq!(got, b"abcdefghijklmnopqrstuvwxyz012", "split at {split}");
            assert!(d.is_done(), "split at {split}");
        }
    }

    #[test]
    fn chunked_rejects_a_bad_size_line() {
        let mut d = ChunkedDecoder::new();
        assert!(d.push(b"zz\r\nhello\r\n").is_err());
    }

    #[test]
    fn chunked_rejects_an_endless_size_line() {
        let mut d = ChunkedDecoder::new();
        assert!(d.push(&[b'a'; 128]).is_err());
    }

    #[test]
    fn chunk_extensions_are_ignored() {
        let mut d = ChunkedDecoder::new();
        let out = d.push(b"5;ext=1\r\nhello\r\n0\r\n\r\n").unwrap();
        assert_eq!(out, b"hello");
    }

    #[test]
    fn lines_are_held_until_complete() {
        let mut s = LineSplitter::new(1024);
        assert!(s.push(b"{\"a\":").unwrap().is_empty());
        assert_eq!(s.push(b"1}\n{\"b\":2}\n").unwrap(), vec!["{\"a\":1}", "{\"b\":2}"]);
    }

    #[test]
    fn a_body_with_no_trailing_newline_is_not_lost() {
        let mut s = LineSplitter::new(1024);
        assert!(s.push(b"{\"models\":[]}").unwrap().is_empty());
        assert_eq!(s.flush().unwrap(), "{\"models\":[]}");
        assert!(s.flush().is_none(), "flushing twice must not repeat the line");
    }

    #[test]
    fn an_endless_line_is_refused() {
        let mut s = LineSplitter::new(16);
        assert!(s.push(&[b'x'; 64]).is_err());
    }

    #[test]
    fn framing_prefers_chunked_over_length() {
        let head = "HTTP/1.1 200 OK\r\nContent-Length: 9\r\nTransfer-Encoding: chunked";
        assert!(matches!(parse_framing(head).unwrap(), Framing::Chunked));
        assert_eq!(parse_status(head).unwrap(), 200);
    }
}
