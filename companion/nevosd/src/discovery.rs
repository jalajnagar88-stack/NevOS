//! Telling the device where the daemon is.
//!
//! The device is on someone's home Wi-Fi with a DHCP address, and so is this
//! machine. Typing an IP address into a robot with no keyboard is not a setup
//! flow, so the daemon advertises itself over mDNS and the device connects to
//! whatever it finds.
//!
//! Discovery is explicitly not authentication. Anything on the network can
//! claim to be a NEVOS daemon; that is what the pairing code exists to catch.
//! Nothing here should ever be treated as proof of anything.
use anyhow::{Context, Result};
use mdns_sd::{ServiceDaemon, ServiceInfo};

/// The service type. `_nevos._tcp` is not registered with IANA — it is a local
/// name, which is normal for this and costs nothing as long as it is specific.
pub const SERVICE_TYPE: &str = "_nevos._tcp.local.";

/// Holds the advertisement. Dropping it withdraws the service, so the daemon
/// keeps it alive for as long as it is running.
pub struct Advertisement {
    daemon: ServiceDaemon,
    fullname: String,
}

impl Advertisement {
    /// Advertises this daemon on every interface, on `port`.
    ///
    /// `instance` is what the device shows the user when more than one machine
    /// answers ("kitchen mac", "studio desktop"), so it is the computer's name
    /// rather than anything generated.
    pub fn start(instance: &str, port: u16, protocol_version: u16) -> Result<Self> {
        let daemon = ServiceDaemon::new().context("starting the mDNS responder")?;

        // Instance names travel in DNS labels: strip what cannot appear in one
        // rather than letting the responder refuse a perfectly ordinary
        // computer name like "Jo's MacBook Pro".
        let instance = sanitise_instance(instance);
        let host = format!("{instance}.local.");

        let properties = [
            ("proto", protocol_version.to_string()),
            ("name", instance.clone()),
            ("path", "/ws".to_string()),
        ];

        let info = ServiceInfo::new(SERVICE_TYPE, &instance, &host, "", port, &properties[..])
            .context("building the mDNS record")?
            // Addresses are filled in from the interfaces and kept current, so
            // a laptop moving between Wi-Fi and Ethernet stays findable.
            .enable_addr_auto();

        let fullname = info.get_fullname().to_string();
        daemon.register(info).context("registering the mDNS service")?;
        tracing::info!(service = %fullname, port, "advertising on the local network");

        Ok(Self { daemon, fullname })
    }

}

impl Drop for Advertisement {
    fn drop(&mut self) {
        // Withdraw rather than going silent: a stale record sends the device to
        // a port nobody is listening on, and it waits out a timeout for each
        // reconnection attempt.
        let _ = self.daemon.unregister(&self.fullname);
        let _ = self.daemon.shutdown();
    }
}

/// Reduces a computer name to something that fits in a DNS label.
fn sanitise_instance(name: &str) -> String {
    let cleaned: String = name
        .chars()
        .map(|c| if c.is_ascii_alphanumeric() || c == '-' { c } else { '-' })
        .collect();
    let trimmed = cleaned.trim_matches('-').to_string();
    let limited: String = trimmed.chars().take(63).collect();
    if limited.is_empty() {
        "nevos".to_string()
    } else {
        limited
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_normal_computer_name_survives() {
        assert_eq!(sanitise_instance("studio-mac"), "studio-mac");
    }

    #[test]
    fn spaces_and_punctuation_become_hyphens() {
        assert_eq!(sanitise_instance("Jo's MacBook Pro"), "Jo-s-MacBook-Pro");
    }

    #[test]
    fn a_name_that_is_all_punctuation_still_produces_a_label() {
        assert_eq!(sanitise_instance("!!!"), "nevos");
        assert_eq!(sanitise_instance(""), "nevos");
    }

    #[test]
    fn a_long_name_is_cut_to_a_label() {
        assert_eq!(sanitise_instance(&"a".repeat(200)).len(), 63);
    }
}
