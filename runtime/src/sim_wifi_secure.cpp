// WiFiClientSecure backing: when BOARDGHOST_WITH_OPENSSL is on, wrap the
// base WiFiClient's POSIX socket in an OpenSSL session and route
// read/write through SSL_read / SSL_write. When OpenSSL is unavailable at
// build time, the override falls through to the base class so the sketch
// still compiles + runs without encryption (with a warning on connect).
//
// SNI is enabled so the server picks the right cert for the requested
// hostname. By default we use the system trust store
// (SSL_CTX_set_default_verify_paths). setCACert() pins a user-supplied PEM,
// setInsecure() disables peer verification entirely (common in sketches
// hitting self-signed / rapidly-rotating endpoints).

#include "WiFiClientSecure.h"
#include "sim_net.h"

#include <cstdio>
#include <cstring>

#ifdef BOARDGHOST_WITH_OPENSSL
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#endif

namespace boardghost_internal {

#ifdef BOARDGHOST_WITH_OPENSSL

struct TlsState {
    SSL_CTX* ctx = nullptr;
    SSL*     ssl = nullptr;
    ~TlsState() {
        if (ssl) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
        }
        if (ctx) SSL_CTX_free(ctx);
    }
};

// One-shot init guard. OpenSSL 1.1.0+ auto-inits, but we still want to be
// explicit so log lines and error strings show up cleanly.
static void ensure_openssl_init() {
    static bool inited = false;
    if (inited) return;
    inited = true;
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
}

#else

// Minimal placeholder so the unique_ptr in the header has a definition to
// reference even when OpenSSL isn't built in.
struct TlsState {};

#endif

}  // namespace boardghost_internal

namespace bgi = boardghost_internal;

WiFiClientSecure::WiFiClientSecure() = default;
WiFiClientSecure::~WiFiClientSecure() = default;

void WiFiClientSecure::setCACert(const char* cert_pem) {
    ca_cert_pem_ = cert_pem ? cert_pem : "";
}

void WiFiClientSecure::setCertificate(const char* cert_pem) {
    client_cert_pem_ = cert_pem ? cert_pem : "";
}

void WiFiClientSecure::setPrivateKey(const char* key_pem) {
    client_key_pem_ = key_pem ? key_pem : "";
}

void WiFiClientSecure::setInsecure() {
    insecure_ = true;
}

void WiFiClientSecure::setHandshakeTimeout(uint32_t ms) {
    handshake_ms_ = ms;
}

int WiFiClientSecure::connect(const char* host, uint16_t port) {
#ifndef BOARDGHOST_WITH_OPENSSL
    if (sim_net_mode() == BOARDGHOST_NET_REAL) {
        std::fprintf(stderr,
            "[boardghost] WiFiClientSecure: built without OpenSSL — "
            "falling back to PLAIN TCP. Build with BOARDGHOST_WITH_OPENSSL=ON "
            "to encrypt.\n");
    }
    return WiFiClient::connect(host, port);
#else
    // Fake/fail modes use base behaviour (no real network).
    if (sim_net_mode() != BOARDGHOST_NET_REAL) {
        return WiFiClient::connect(host, port);
    }

    // Do the TCP handshake via the base class first — that populates sock_.
    if (WiFiClient::connect(host, port) != 1 || !sock_ || sock_->fd < 0) {
        return 0;
    }

    bgi::ensure_openssl_init();

    auto state = std::make_unique<bgi::TlsState>();
    // TLS_client_method picks the highest mutually-supported version.
    state->ctx = SSL_CTX_new(TLS_client_method());
    if (!state->ctx) {
        WiFiClient::stop();
        return 0;
    }

    // Verification: by default load system roots (Ubuntu ships ca-certs at
    // /etc/ssl/certs); pin user CA if provided; or disable entirely.
    if (insecure_) {
        SSL_CTX_set_verify(state->ctx, SSL_VERIFY_NONE, nullptr);
    } else {
        SSL_CTX_set_verify(state->ctx, SSL_VERIFY_PEER, nullptr);
        bool loaded = false;
        if (!ca_cert_pem_.empty()) {
            BIO* bio = BIO_new_mem_buf(ca_cert_pem_.data(), (int)ca_cert_pem_.size());
            if (bio) {
                X509* ca = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
                BIO_free(bio);
                if (ca) {
                    X509_STORE* store = SSL_CTX_get_cert_store(state->ctx);
                    if (store && X509_STORE_add_cert(store, ca) == 1) loaded = true;
                    X509_free(ca);
                }
            }
        }
        if (!loaded) {
            SSL_CTX_set_default_verify_paths(state->ctx);
        }
    }

    // Optional mTLS — client presents cert + key.
    if (!client_cert_pem_.empty() && !client_key_pem_.empty()) {
        BIO* cbio = BIO_new_mem_buf(client_cert_pem_.data(), (int)client_cert_pem_.size());
        BIO* kbio = BIO_new_mem_buf(client_key_pem_.data(), (int)client_key_pem_.size());
        if (cbio && kbio) {
            X509*    cert = PEM_read_bio_X509(cbio, nullptr, nullptr, nullptr);
            EVP_PKEY* key = PEM_read_bio_PrivateKey(kbio, nullptr, nullptr, nullptr);
            if (cert) { SSL_CTX_use_certificate(state->ctx, cert); X509_free(cert); }
            if (key)  { SSL_CTX_use_PrivateKey(state->ctx, key);   EVP_PKEY_free(key); }
        }
        if (cbio) BIO_free(cbio);
        if (kbio) BIO_free(kbio);
    }

    state->ssl = SSL_new(state->ctx);
    if (!state->ssl) {
        WiFiClient::stop();
        return 0;
    }
    SSL_set_fd(state->ssl, sock_->fd);
    // SNI — server uses this to pick the right cert. Required for most
    // multi-tenant TLS endpoints (CloudFront, Cloudflare, etc.).
    if (host && *host) SSL_set_tlsext_host_name(state->ssl, host);
    // Hostname verification — without this, SSL_VERIFY_PEER only checks
    // chain validity, so a chain-valid cert for the wrong hostname would
    // pass and MITM is trivial. Skip when the user opted into insecure or
    // host is unset (IP literals don't carry a hostname to match).
    if (!insecure_ && host && *host) {
        X509_VERIFY_PARAM* param = SSL_get0_param(state->ssl);
        X509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
        if (X509_VERIFY_PARAM_set1_host(param, host, 0) != 1) {
            WiFiClient::stop();
            return 0;
        }
    }

    int rc = SSL_connect(state->ssl);
    if (rc != 1) {
        int err = SSL_get_error(state->ssl, rc);
        std::fprintf(stderr,
            "[boardghost] WiFiClientSecure: TLS handshake to %s:%u failed "
            "(err=%d). %s\n",
            host ? host : "?", port, err,
            insecure_ ? "" : "Consider setInsecure() for self-signed certs.");
        WiFiClient::stop();
        return 0;
    }
    tls_ = std::move(state);
    return 1;
#endif
}

void WiFiClientSecure::stop() {
#ifdef BOARDGHOST_WITH_OPENSSL
    tls_.reset();  // shuts down + frees SSL + CTX
#endif
    WiFiClient::stop();
}

int WiFiClientSecure::read(uint8_t* buf, size_t n) {
#ifdef BOARDGHOST_WITH_OPENSSL
    if (tls_ && tls_->ssl) {
        if (!buf || n == 0) return 0;
        int r = SSL_read(tls_->ssl, buf, (int)n);
        if (r <= 0) {
            int err = SSL_get_error(tls_->ssl, r);
            // ZERO_RETURN means the peer closed cleanly.
            if (err == SSL_ERROR_ZERO_RETURN || err == SSL_ERROR_SYSCALL) {
                stop();
                return 0;
            }
            return 0;
        }
        return r;
    }
#endif
    return WiFiClient::read(buf, n);
}

size_t WiFiClientSecure::write(uint8_t b) {
    return write(&b, 1);
}

size_t WiFiClientSecure::write(const uint8_t* buf, size_t n) {
#ifdef BOARDGHOST_WITH_OPENSSL
    if (tls_ && tls_->ssl) {
        if (!buf || n == 0) return 0;
        int w = SSL_write(tls_->ssl, buf, (int)n);
        if (w <= 0) return 0;
        return (size_t)w;
    }
#endif
    return WiFiClient::write(buf, n);
}
