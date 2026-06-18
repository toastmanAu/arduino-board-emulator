//! `boardghost run --mirror` support: enable the LAN mirror with one flag.
//!
//! When `--mirror` is passed the CLI generates a random token (unless one is
//! supplied), exports the `BOARDGHOST_MIRROR*` env the runtime reads, and prints
//! a pairing block (URL + token) for the agent / Android companion. The graphical
//! QR is intentionally deferred — it lands with the companion app so we don't add
//! a CLI dependency before it's needed.

use std::io::Read;

/// Generate a random hex token (32 chars = 128 bits) for the mirror gate.
///
/// Reads 16 bytes from the OS CSPRNG (`/dev/urandom`; this is a Linux-only
/// simulator) and FAILS LOUDLY on any error. A silent fall-back to a zeroed or
/// partially-filled buffer would mint a predictable token guarding a LAN
/// socket — strictly worse than aborting, so open/read errors panic with a
/// clear message rather than returning a weak token.
pub fn generate_token() -> String {
    let mut bytes = [0u8; 16];
    let mut f = std::fs::File::open("/dev/urandom")
        .expect("mirror token: cannot open /dev/urandom for entropy");
    f.read_exact(&mut bytes)
        .expect("mirror token: short read from /dev/urandom");
    let mut s = String::with_capacity(32);
    for b in bytes {
        s.push_str(&format!("{:02x}", b));
    }
    s
}

/// The feature-detection URL an agent or the companion app hits first.
pub fn mirror_url(ip: &str, port: u16) -> String {
    format!("http://{ip}:{port}/mirror/info")
}

/// Best-effort LAN IP discovery: open a UDP socket "towards" a public address
/// and read back the local address the OS would route through. No packet is
/// actually sent (UDP connect only sets the route). Returns None if it can't
/// be determined (caller falls back to a placeholder).
pub fn detect_lan_ip() -> Option<String> {
    let sock = std::net::UdpSocket::bind("0.0.0.0:0").ok()?;
    sock.connect("8.8.8.8:80").ok()?;
    Some(sock.local_addr().ok()?.ip().to_string())
}

/// The human-facing pairing block printed to stderr on `--mirror`.
pub fn pairing_banner(url: &str, token: &str) -> String {
    format!(
        "\n  ┌─ BoardGhost mirror (LAN) ───────────────────────────\n  \
         │  URL:   {url}\n  \
         │  Token: {token}\n  \
         │  Header: X-BoardGhost-Mirror: {token}\n  \
         └─────────────────────────────────────────────────────\n"
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn token_is_32_hex_chars() {
        let t = generate_token();
        assert_eq!(t.len(), 32);
        assert!(t.chars().all(|c| c.is_ascii_hexdigit()));
        assert_ne!(t, "0".repeat(32), "must never mint an all-zero token");
    }

    #[test]
    fn tokens_differ_between_calls() {
        // Astronomically unlikely to collide; guards against a constant stub.
        assert_ne!(generate_token(), generate_token());
    }

    #[test]
    fn url_targets_mirror_info() {
        assert_eq!(
            mirror_url("192.168.1.5", 18082),
            "http://192.168.1.5:18082/mirror/info"
        );
    }

    #[test]
    fn banner_carries_url_and_token() {
        let b = pairing_banner("http://x:1/mirror/info", "deadbeef");
        assert!(b.contains("http://x:1/mirror/info"));
        assert!(b.contains("deadbeef"));
        assert!(b.contains("X-BoardGhost-Mirror"));
    }
}
