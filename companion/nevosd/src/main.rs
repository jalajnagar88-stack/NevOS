//! nevosd — the NEVOS companion daemon.
//!
//! It runs on the user's own computer and does the three things the device
//! cannot: transcribe speech, run a language model, and keep files. Every one
//! of those happens locally. There is no account, no API key and no cloud
//! service anywhere in this program, which is why a NEVOS on a desk costs
//! nothing to talk to and keeps working when the internet does not.
//!
//! Usage:
//!   nevosd [--port N] [--control-port N] [--data-dir PATH] [--name NAME]
//!          [--model NAME] [--mock] [--no-mdns] [--keep-audio]
use anyhow::{Context, Result};
use nevos_agent::{Agent, MockAgent, OllamaAgent};
use nevos_store::Store;
use nevos_stt::{MockTranscriber, Transcriber, Unavailable, WhisperCli};
use std::net::SocketAddr;
use std::path::PathBuf;
use std::sync::Arc;

use nevosd::state::Daemon;
use nevosd::{discovery, server};

/// The device link. Port 0 would work — the device finds it by mDNS — but a
/// fixed default makes a firewall rule something a person can write once.
const DEFAULT_PORT: u16 = 4821;
/// The tray's control API, on loopback.
const DEFAULT_CONTROL_PORT: u16 = 4822;

#[derive(Debug, Clone, PartialEq, Eq)]
struct Args {
    port: u16,
    control_port: u16,
    data_dir: Option<PathBuf>,
    name: Option<String>,
    model: Option<String>,
    mock: bool,
    mdns: bool,
    keep_audio: bool,
    help: bool,
}

impl Default for Args {
    fn default() -> Self {
        Self {
            port: DEFAULT_PORT,
            control_port: DEFAULT_CONTROL_PORT,
            data_dir: None,
            name: None,
            model: None,
            mock: false,
            mdns: true,
            // Off unless asked for. Text is what the features need; the audio
            // of someone's kitchen is the part worth not keeping by default.
            keep_audio: false,
            help: false,
        }
    }
}

const USAGE: &str = "\
nevosd — the NEVOS companion daemon (runs entirely on this machine)

  --port N           device link port (default 4821)
  --control-port N   tray control API, loopback only (default 4822)
  --data-dir PATH    where notes and transcripts live
  --name NAME        what the device shows for this computer
  --model NAME       local model to use (default llama3.2:3b via Ollama)
  --mock             mock agent and transcriber; needs nothing installed
  --no-mdns          do not advertise on the network
  --keep-audio       retain captured audio as well as the text
  -h, --help         this text

Environment:
  NEVOS_DATA_DIR       overrides --data-dir
  NEVOS_OLLAMA_HOST    must be a loopback address (default 127.0.0.1)
  NEVOS_OLLAMA_PORT    default 11434
  NEVOS_MODEL          overrides --model
  NEVOS_WHISPER_BIN    path to the whisper.cpp binary
  NEVOS_WHISPER_MODEL  path to a whisper model (.bin)
";

/// Hand-rolled because it is twenty lines and a dependency for this would be a
/// dependency to audit. Unknown flags are an error rather than ignored: a typo
/// in --keep-audio should not quietly discard the recording the user asked for.
fn parse_args(argv: &[String]) -> Result<Args> {
    let mut args = Args::default();
    let mut it = argv.iter();
    while let Some(arg) = it.next() {
        let mut value = |name: &str| -> Result<String> {
            it.next().cloned().with_context(|| format!("{name} needs a value"))
        };
        match arg.as_str() {
            "--port" => args.port = value("--port")?.parse().context("--port")?,
            "--control-port" => {
                args.control_port = value("--control-port")?.parse().context("--control-port")?
            }
            "--data-dir" => args.data_dir = Some(PathBuf::from(value("--data-dir")?)),
            "--name" => args.name = Some(value("--name")?),
            "--model" => args.model = Some(value("--model")?),
            "--mock" => args.mock = true,
            "--no-mdns" => args.mdns = false,
            "--keep-audio" => args.keep_audio = true,
            "-h" | "--help" => args.help = true,
            other => anyhow::bail!("unknown option {other:?}\n\n{USAGE}"),
        }
    }
    Ok(args)
}

/// This machine's name, as the device will show it.
fn machine_name() -> String {
    std::env::var("NEVOS_NAME")
        .ok()
        .or_else(|| std::env::var("HOSTNAME").ok())
        .or_else(|| {
            std::fs::read_to_string("/etc/hostname").ok().map(|s| s.trim().to_string())
        })
        .filter(|s| !s.is_empty())
        .unwrap_or_else(|| "this computer".to_string())
}

/// Builds the agent, and says clearly what is wrong when it cannot.
///
/// A missing model must not stop the daemon: pairing, notes and the device's
/// own apps all work without one, and a daemon that refuses to start is much
/// harder to diagnose from a robot's screen than one that says "no model".
async fn build_agent(args: &Args) -> Arc<dyn Agent> {
    if args.mock {
        tracing::warn!("--mock: replies are canned and no model is running");
        return Arc::new(MockAgent::default());
    }

    let agent = match args.model.clone() {
        Some(model) => {
            let host =
                std::env::var("NEVOS_OLLAMA_HOST").unwrap_or_else(|_| "127.0.0.1".to_string());
            let port = std::env::var("NEVOS_OLLAMA_PORT")
                .ok()
                .and_then(|p| p.parse().ok())
                .unwrap_or(nevos_agent::ollama::DEFAULT_PORT);
            OllamaAgent::new(host, port, model)
        }
        None => OllamaAgent::from_env(),
    };

    let agent = match agent {
        Ok(a) => a,
        Err(e) => {
            tracing::error!(error = %e, "agent disabled");
            return Arc::new(MockAgent::new(
                "My model backend is misconfigured, so I cannot answer that yet.",
            ));
        }
    };

    match agent.installed_models().await {
        Ok(models) if agent.has_model(&models) => {
            tracing::info!(model = agent.model(), endpoint = agent.endpoint(), "model ready");
        }
        Ok(models) => {
            // By far the most common first-run failure, and worth naming
            // exactly rather than surfacing as a 404 mid-conversation.
            tracing::warn!(
                model = agent.model(),
                installed = ?models,
                "the configured model is not pulled: run `ollama pull {}`",
                agent.model()
            );
        }
        Err(e) => {
            tracing::warn!(
                error = %e,
                endpoint = agent.endpoint(),
                "no model server answered; start Ollama and the agent will work on the next question"
            );
        }
    }
    Arc::new(agent)
}

fn build_transcriber(args: &Args) -> Arc<dyn Transcriber> {
    if args.mock {
        return Arc::new(MockTranscriber::new("this is a mock transcript"));
    }
    let (Ok(binary), Ok(model)) =
        (std::env::var("NEVOS_WHISPER_BIN"), std::env::var("NEVOS_WHISPER_MODEL"))
    else {
        tracing::warn!("no transcriber configured; voice capture will report an error");
        return Arc::new(Unavailable);
    };

    let whisper = WhisperCli::new(binary, model);
    if let Err(e) = whisper.check() {
        tracing::warn!(error = %e, "whisper is configured but not usable");
        return Arc::new(Unavailable);
    }
    Arc::new(whisper)
}

#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "nevosd=info,nevos_agent=info".into()),
        )
        .init();

    let argv: Vec<String> = std::env::args().skip(1).collect();
    let args = parse_args(&argv)?;
    if args.help {
        print!("{USAGE}");
        return Ok(());
    }

    let root = args
        .data_dir
        .clone()
        .unwrap_or_else(nevos_store::default_root);
    let store = Store::open(&root).with_context(|| format!("opening {}", root.display()))?;
    let name = args.name.clone().unwrap_or_else(machine_name);

    let agent = build_agent(&args).await;
    let stt = build_transcriber(&args);
    tracing::info!(
        data_dir = %root.display(),
        agent = agent.name(),
        transcriber = stt.name(),
        keep_audio = args.keep_audio,
        "nevosd {}",
        env!("CARGO_PKG_VERSION")
    );
    // Stated once at startup as well as in the tray, because it is the claim
    // the whole design rests on and it should be checkable from a log file.
    if agent.is_local() && stt.is_local() {
        tracing::info!("everything runs on this machine; no audio or text leaves it");
    } else {
        tracing::warn!("a non-local backend is configured: data will leave this machine");
    }

    let daemon = Arc::new(Daemon::new(name.clone(), store, agent, stt, args.keep_audio));

    let device_port = server::run_device_listener(
        daemon.clone(),
        SocketAddr::from(([0, 0, 0, 0], args.port)),
    )
    .await?;
    let control_port = server::run_control_listener(
        daemon.clone(),
        SocketAddr::from(([127, 0, 0, 1], args.control_port)),
    )
    .await?;
    tracing::info!(device_port, control_port, "listening");

    // Held for the lifetime of the process: dropping it withdraws the record.
    let _advert = if args.mdns {
        match discovery::Advertisement::start(&name, device_port, nevos_proto::PROTOCOL_VERSION) {
            Ok(a) => Some(a),
            Err(e) => {
                // Not fatal: a device that already knows the address, or a
                // network that blocks multicast, still works.
                tracing::warn!(error = %e, "mDNS unavailable; the device must be pointed here manually");
                None
            }
        }
    } else {
        None
    };

    tokio::signal::ctrl_c().await?;
    tracing::info!("shutting down");
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn args(list: &[&str]) -> Result<Args> {
        parse_args(&list.iter().map(|s| s.to_string()).collect::<Vec<_>>())
    }

    #[test]
    fn no_arguments_is_the_default_configuration() {
        let a = args(&[]).unwrap();
        assert_eq!(a, Args::default());
        assert!(!a.keep_audio, "audio retention must be opt-in");
        assert!(a.mdns);
    }

    #[test]
    fn flags_are_parsed() {
        let a = args(&["--port", "1234", "--mock", "--no-mdns", "--keep-audio", "--name", "desk"])
            .unwrap();
        assert_eq!(a.port, 1234);
        assert!(a.mock);
        assert!(!a.mdns);
        assert!(a.keep_audio);
        assert_eq!(a.name.as_deref(), Some("desk"));
    }

    #[test]
    fn an_unknown_option_is_refused() {
        // Quietly ignoring it would mean --keep-audio-pls silently discarding
        // the recordings the user asked to keep.
        let err = args(&["--keep-audi"]).unwrap_err();
        assert!(err.to_string().contains("unknown option"), "{err}");
    }

    #[test]
    fn a_flag_missing_its_value_is_refused() {
        assert!(args(&["--port"]).is_err());
        assert!(args(&["--port", "not-a-number"]).is_err());
    }
}
