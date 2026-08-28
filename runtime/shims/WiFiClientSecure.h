#pragma once
#include <cstdint>
#include "WiFiClient.h"

#include <memory>
#include <string>

namespace boardghost_internal { class TlsState; }

// WiFiClientSecure adds TLS on top of WiFiClient's POSIX socket. When the sim
// is built with BOARDGHOST_WITH_OPENSSL, connect() does the TCP handshake
// then wraps the socket in an SSL session; read/write go through SSL_read /
// SSL_write. Cert/key setters configure the in-flight TLS context.
//
// Without OpenSSL (build option off) the class degrades to plain WiFiClient —
// the sketch still compiles but `setInsecure()` etc. are no-ops and the
// connection is unencrypted. Real-mode tests log a clear warning if this
// fallback ever triggers.
class WiFiClientSecure : public WiFiClient {
public:
    WiFiClientSecure();
    ~WiFiClientSecure() override;

    // ----- TLS configuration knobs (called before connect) -----
    void setCACert(const char* cert_pem);
    void setCertificate(const char* cert_pem);
    void setPrivateKey(const char* key_pem);
    // Disable peer verification entirely. Common in sketches that hit
    // self-signed or rapidly-rotating endpoints. Equivalent to curl's `-k`.
    void setInsecure();
    void setHandshakeTimeout(uint32_t ms);

    // ----- Stream / connection overrides -----
    int  connect(const char* host, uint16_t port) override;
    void stop() override;
    int  read(uint8_t* buf, size_t n) override;
    size_t write(uint8_t b) override;
    size_t write(const uint8_t* buf, size_t n) override;

private:
    std::unique_ptr<boardghost_internal::TlsState> tls_;
    std::string ca_cert_pem_;
    std::string client_cert_pem_;
    std::string client_key_pem_;
    bool        insecure_         = false;
    uint32_t    handshake_ms_     = 5000;
};
