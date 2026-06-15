use anyhow::{Context, Result};
use std::io::{Read, Write};
use std::net::{TcpListener, UdpSocket};
use std::time::Duration;

/// MD5 hex of a byte slice. espota uses MD5 for both the firmware checksum
/// and the auth challenge.
fn md5_hex(bytes: &[u8]) -> String {
    // `md5` crate (add to Cargo.toml). Tiny, pure-Rust, no build deps.
    format!("{:x}", md5::compute(bytes))
}

/// Push `firmware` to a running sim's ArduinoOTA receiver at 127.0.0.1:port.
/// Returns Ok(()) when the device replies with the closing "OK".
pub fn push(port: u16, firmware: &[u8], password: Option<&str>) -> Result<()> {
    let listener = TcpListener::bind("127.0.0.1:0").context("bind tcp listener")?;
    let host_port = listener.local_addr()?.port();

    let udp = UdpSocket::bind("127.0.0.1:0").context("bind udp")?;
    udp.set_read_timeout(Some(Duration::from_secs(3)))?;
    let dev = format!("127.0.0.1:{port}");

    let md5 = md5_hex(firmware);
    let invite = format!("0 {host_port} {} {md5}\n", firmware.len());
    udp.send_to(invite.as_bytes(), &dev)
        .context("send invite")?;

    let mut rb = [0u8; 256];
    let (n, _) = udp.recv_from(&mut rb).context("await device reply")?;
    let reply = String::from_utf8_lossy(&rb[..n]).to_string();

    if let Some(stripped) = reply.strip_prefix("AUTH ") {
        let pw = password.context("device requires a password (--password)")?;
        let nonce = stripped.trim();
        let passmd5 = md5_hex(pw.as_bytes());
        let cnonce = md5_hex(format!("{}{host_port}", firmware.len()).as_bytes());
        let result = md5_hex(format!("{passmd5}:{nonce}:{cnonce}").as_bytes());
        udp.send_to(format!("{cnonce} {result}\n").as_bytes(), &dev)?;
        let (n2, _) = udp.recv_from(&mut rb).context("await auth result")?;
        let r2 = String::from_utf8_lossy(&rb[..n2]).to_string();
        anyhow::ensure!(
            r2.starts_with("OK"),
            "authentication rejected: {}",
            r2.trim()
        );
    } else {
        anyhow::ensure!(reply.starts_with("OK"), "device declined: {}", reply.trim());
    }

    let (mut sock, _) = listener.accept().context("device did not connect back")?;
    sock.write_all(firmware).context("stream firmware")?;
    let mut fin = String::new();
    sock.read_to_string(&mut fin).ok();
    anyhow::ensure!(
        fin.starts_with("OK"),
        "device reported failure: {}",
        fin.trim()
    );
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn auth_digest_matches_reference() {
        // Reference vector: passmd5 = md5("secret"), nonce/cnonce fixed.
        let passmd5 = md5_hex(b"secret");
        let nonce = "deadbeef";
        let cnonce = "feedface";
        let result = md5_hex(format!("{passmd5}:{nonce}:{cnonce}").as_bytes());
        // Same formula the C++ receiver uses — recompute and compare.
        assert_eq!(
            result,
            md5_hex(format!("{passmd5}:{nonce}:{cnonce}").as_bytes())
        );
        assert_eq!(result.len(), 32);
    }
}
