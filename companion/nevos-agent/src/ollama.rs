//! The Ollama backend: a model running on the user's own machine.
//!
//! Ollama was chosen as the default because installing it is one command on all
//! three desktop platforms and it manages the model files itself. Nothing here
//! depends on it beyond two HTTP endpoints, though, and the raw llama.cpp
//! server speaks a near-identical dialect — a second backend would be a copy of
//! this file with a different body shape, not a new abstraction.
//!
//! The protocol pieces that are easy to get wrong — building the request, and
//! reading one line of the streamed reply — are pure functions with tests. What
//! is left needs a model server to exercise, and that is deliberately the
//! smallest part of the file.
use crate::http;
use crate::mood;
use crate::{Agent, AgentEvent, AgentRequest, Role, Turn, SYSTEM_PROMPT};
use anyhow::{bail, Context, Result};
use async_trait::async_trait;
use serde_json::{json, Value};
use tokio::sync::mpsc;

/// Ollama's default port. Not configurable in Ollama itself without an env var,
/// so this is the number in practice.
pub const DEFAULT_PORT: u16 = 11434;

/// A small instruct model is the right default: it answers in under a second on
/// a laptop CPU, and the job here is two sentences of conversation, not
/// reasoning. The user can point at anything they have pulled.
pub const DEFAULT_MODEL: &str = "llama3.2:3b";

#[derive(Debug)]
pub struct OllamaAgent {
    host: String,
    port: u16,
    model: String,
    system: String,
    /// Hard cap on generated tokens. The prompt asks for two sentences; this is
    /// what happens when the model ignores it. Roughly 200 words.
    num_predict: u32,
    name: String,
}

impl OllamaAgent {
    pub fn new(host: impl Into<String>, port: u16, model: impl Into<String>) -> Result<Self> {
        let host = host.into();
        let model = model.into();
        // Checked here as well as in the transport so a bad configuration fails
        // at startup, where the user is looking, rather than on the first thing
        // they say to the robot.
        if !http::is_loopback_host(&host) {
            bail!(
                "NEVOS_OLLAMA_HOST is {host:?}, which is not this machine. The agent is \
                 local-only by design: point it at localhost, or run the model server here."
            );
        }
        let name = format!("ollama:{model}");
        Ok(Self {
            host,
            port,
            model,
            system: SYSTEM_PROMPT.to_string(),
            num_predict: 256,
            name,
        })
    }

    /// Reads NEVOS_OLLAMA_HOST, NEVOS_OLLAMA_PORT and NEVOS_MODEL.
    pub fn from_env() -> Result<Self> {
        let host = std::env::var("NEVOS_OLLAMA_HOST").unwrap_or_else(|_| "127.0.0.1".to_string());
        let port = match std::env::var("NEVOS_OLLAMA_PORT") {
            Ok(s) => s.parse().with_context(|| format!("NEVOS_OLLAMA_PORT={s:?}"))?,
            Err(_) => DEFAULT_PORT,
        };
        let model = std::env::var("NEVOS_MODEL").unwrap_or_else(|_| DEFAULT_MODEL.to_string());
        Self::new(host, port, model)
    }

    /// Overrides the character prompt. The notes app uses this to ask for a
    /// summary in a different voice without a second backend.
    pub fn with_system(mut self, system: impl Into<String>) -> Self {
        self.system = system.into();
        self
    }

    /// Asks the server which models it has.
    ///
    /// Called once at startup so the daemon can say "ollama is running but
    /// llama3.2:3b is not pulled" — by far the most likely way a first run
    /// fails, and one that is otherwise reported as a 404 on the first question
    /// the user asks out loud.
    pub async fn installed_models(&self) -> Result<Vec<String>> {
        let (tx, mut rx) = mpsc::channel(8);
        let host = self.host.clone();
        let port = self.port;
        let task =
            tokio::spawn(async move { http::get_lines(&host, port, "/api/tags", tx).await });

        let mut body = String::new();
        while let Some(line) = rx.recv().await {
            body.push_str(&line);
        }
        task.await??;

        let parsed: Value = serde_json::from_str(&body).context("parsing /api/tags")?;
        Ok(parsed["models"]
            .as_array()
            .map(|models| {
                models
                    .iter()
                    .filter_map(|m| m["name"].as_str().map(str::to_string))
                    .collect()
            })
            .unwrap_or_default())
    }

    /// True when the configured model is one the server actually has.
    pub fn has_model(&self, installed: &[String]) -> bool {
        installed.iter().any(|m| {
            m == &self.model
                // "llama3.2:3b" and a pulled "llama3.2:3b" differ by an implicit
                // ":latest" often enough to be worth normalising.
                || m.split(':').next() == self.model.split(':').next()
        })
    }

    pub fn model(&self) -> &str {
        &self.model
    }

    pub fn endpoint(&self) -> String {
        format!("http://{}:{}", self.host, self.port)
    }

    /// Builds the /api/chat body. Pure, so the shape is a test rather than
    /// something discovered by reading a model's confused reply.
    fn build_body(&self, req: &AgentRequest, history: &[Turn]) -> String {
        let mut messages = vec![json!({ "role": "system", "content": self.system })];
        for turn in history {
            messages.push(json!({
                "role": match turn.role { Role::User => "user", Role::Assistant => "assistant" },
                "content": turn.text,
            }));
        }
        // The asking app is context, not conversation: it goes on the current
        // message rather than into the history, so it does not accumulate.
        let content = if req.app.is_empty() || req.app == "agent" {
            req.text.clone()
        } else {
            format!("[asked from the {} app] {}", req.app, req.text)
        };
        messages.push(json!({ "role": "user", "content": content }));

        json!({
            "model": self.model,
            "messages": messages,
            "stream": true,
            "options": { "num_predict": self.num_predict },
        })
        .to_string()
    }
}

/// One decoded line of a streaming reply.
#[derive(Debug, Default, PartialEq, Eq)]
pub struct StreamPiece {
    pub content: String,
    pub done: bool,
}

/// Parses one NDJSON line from /api/chat.
///
/// Ollama reports a failure as a 200 with an `error` field in the stream — a
/// missing model arrives this way — so a line that is not a token still has to
/// be inspected rather than skipped.
pub fn parse_stream_line(line: &str) -> Result<StreamPiece> {
    let value: Value = serde_json::from_str(line)
        .with_context(|| format!("model server sent a line that is not JSON: {line:?}"))?;

    if let Some(err) = value["error"].as_str() {
        bail!("model server: {err}");
    }

    Ok(StreamPiece {
        content: value["message"]["content"].as_str().unwrap_or_default().to_string(),
        done: value["done"].as_bool().unwrap_or(false),
    })
}

#[async_trait]
impl Agent for OllamaAgent {
    fn name(&self) -> &str {
        &self.name
    }

    fn is_local(&self) -> bool {
        // Guaranteed by the constructor and re-checked by the transport: this
        // agent cannot be pointed anywhere but loopback.
        true
    }

    async fn respond(
        &self,
        req: &AgentRequest,
        history: &[Turn],
        events: mpsc::Sender<AgentEvent>,
    ) -> Result<()> {
        // The face changes the moment the question lands, not when the first
        // token arrives. On a cold model that gap is over a second, and a still
        // face for a second reads as "it didn't hear me" — which makes people
        // repeat themselves, which makes it worse.
        let _ = events.send(AgentEvent::Mood(mood::while_thinking())).await;

        let body = self.build_body(req, history);
        let (tx, mut rx) = mpsc::channel::<String>(32);

        let host = self.host.clone();
        let port = self.port;
        let task = tokio::spawn(async move {
            http::post_json_lines(&host, port, "/api/chat", &body, tx).await
        });

        let mut full = String::new();
        let mut send_failed = false;
        while let Some(line) = rx.recv().await {
            let piece = match parse_stream_line(&line) {
                Ok(p) => p,
                Err(e) => {
                    // Stop the request before returning, or the model keeps
                    // generating into a channel nobody is reading.
                    drop(rx);
                    let _ = task.await;
                    return Err(e);
                }
            };
            if !piece.content.is_empty() {
                full.push_str(&piece.content);
                if events.send(AgentEvent::Token(piece.content)).await.is_err() {
                    send_failed = true;
                    break;
                }
            }
            if piece.done {
                break;
            }
        }
        drop(rx);
        // The transport returning an error matters even when we already have
        // text: a reply cut off by a dead server should not look complete.
        task.await??;

        if send_failed {
            return Ok(());
        }
        if let Some(hint) = mood::infer(&full) {
            let _ = events.send(AgentEvent::Mood(hint)).await;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn agent() -> OllamaAgent {
        OllamaAgent::new("127.0.0.1", DEFAULT_PORT, "test-model").unwrap()
    }

    #[test]
    fn a_remote_host_is_refused_at_construction() {
        let err = OllamaAgent::new("192.168.1.50", DEFAULT_PORT, "m").unwrap_err();
        assert!(err.to_string().contains("local-only"), "{err}");
    }

    #[test]
    fn the_system_prompt_leads_and_history_follows_in_order() {
        let a = agent();
        let req = AgentRequest { turn: 3, text: "and the other one?".into(), app: "agent".into() };
        let history = [Turn::user("what is the capital of France?"), Turn::assistant("Paris.")];
        let body: Value = serde_json::from_str(&a.build_body(&req, &history)).unwrap();

        let msgs = body["messages"].as_array().unwrap();
        assert_eq!(msgs.len(), 4);
        assert_eq!(msgs[0]["role"], "system");
        assert!(msgs[0]["content"].as_str().unwrap().contains("NEVOS"));
        assert_eq!(msgs[1]["role"], "user");
        assert_eq!(msgs[2]["role"], "assistant");
        assert_eq!(msgs[3]["content"], "and the other one?");
        assert_eq!(body["stream"], true);
        assert_eq!(body["model"], "test-model");
    }

    #[test]
    fn the_asking_app_is_attached_to_the_question_not_the_history() {
        let a = agent();
        let req = AgentRequest { turn: 1, text: "summarise this".into(), app: "notes".into() };
        let body: Value = serde_json::from_str(&a.build_body(&req, &[])).unwrap();
        let msgs = body["messages"].as_array().unwrap();
        assert_eq!(msgs.len(), 2, "no extra turn is invented for the app name");
        assert_eq!(msgs[1]["content"], "[asked from the notes app] summarise this");
    }

    #[test]
    fn a_token_line_yields_its_text() {
        let piece = parse_stream_line(
            r#"{"model":"m","message":{"role":"assistant","content":"Hel"},"done":false}"#,
        )
        .unwrap();
        assert_eq!(piece, StreamPiece { content: "Hel".into(), done: false });
    }

    #[test]
    fn the_final_line_carries_no_text_but_ends_the_turn() {
        let piece =
            parse_stream_line(r#"{"model":"m","message":{"content":""},"done":true}"#).unwrap();
        assert!(piece.done);
        assert!(piece.content.is_empty());
    }

    #[test]
    fn a_missing_model_is_an_error_not_an_empty_reply() {
        // Ollama reports this inside a 200 response, so it is only an error if
        // we look for it.
        let err = parse_stream_line(r#"{"error":"model 'nope' not found"}"#).unwrap_err();
        assert!(err.to_string().contains("not found"), "{err}");
    }

    #[test]
    fn a_line_that_is_not_json_is_an_error() {
        assert!(parse_stream_line("<html>502 Bad Gateway</html>").is_err());
    }

    #[test]
    fn an_implicit_latest_tag_still_counts_as_installed() {
        let a = OllamaAgent::new("localhost", DEFAULT_PORT, "llama3.2").unwrap();
        assert!(a.has_model(&["llama3.2:latest".to_string()]));
        assert!(!a.has_model(&["qwen2.5:3b".to_string()]));
        assert!(!a.has_model(&[]));
    }
}
