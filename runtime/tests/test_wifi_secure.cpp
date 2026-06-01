// Phase 3 — WiFiClientSecure over OpenSSL.
//
// Drives a real TLS handshake against a localhost SSL server (cpp-httplib's
// SSLServer, which is conditionally compiled when CPPHTTPLIB_OPENSSL_SUPPORT
// is defined — that's what CMake sets when OpenSSL is found). The cert is a
// self-signed pair generated once at module init and held in memory so
// nothing touches disk.
//
// Skips entirely if OpenSSL isn't built in — the plain-TCP fallback in the
// .cpp already logs a warning, and there's no TLS path to assert against.

#include <gtest/gtest.h>
#include "WiFiClientSecure.h"

#ifdef BOARDGHOST_WITH_OPENSSL

#include "../third_party/cpp-httplib/httplib.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <random>
#include <string>
#include <thread>

namespace {

// Lazily-generated self-signed cert held in process memory. Same approach
// cpp-httplib's tests use. Returns refs valid for the test run lifetime.
struct SelfSignedPair {
    X509* cert = nullptr;
    EVP_PKEY* key = nullptr;
    std::string cert_pem;
    std::string key_pem;
    ~SelfSignedPair() {
        if (cert) X509_free(cert);
        if (key)  EVP_PKEY_free(key);
    }
};

SelfSignedPair& self_signed() {
    static SelfSignedPair p;
    if (p.cert) return p;
    p.key = EVP_RSA_gen(2048);
    p.cert = X509_new();
    ASN1_INTEGER_set(X509_get_serialNumber(p.cert), 1);
    X509_gmtime_adj(X509_getm_notBefore(p.cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(p.cert), 3600);
    X509_set_pubkey(p.cert, p.key);
    X509_NAME* name = X509_get_subject_name(p.cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
        (const unsigned char*)"localhost", -1, -1, 0);
    X509_set_issuer_name(p.cert, name);
    X509_sign(p.cert, p.key, EVP_sha256());

    // Serialise to PEM strings (cpp-httplib SSLServer wants PEM file paths,
    // so we'll dump these to tmpfile().)
    BIO* cbio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(cbio, p.cert);
    char* cdata = nullptr;
    long  clen  = BIO_get_mem_data(cbio, &cdata);
    p.cert_pem.assign(cdata, clen);
    BIO_free(cbio);

    BIO* kbio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(kbio, p.key, nullptr, nullptr, 0, nullptr, nullptr);
    char* kdata = nullptr;
    long  klen  = BIO_get_mem_data(kbio, &kdata);
    p.key_pem.assign(kdata, klen);
    BIO_free(kbio);

    return p;
}

uint16_t pick_port() {
    std::random_device rd;
    std::mt19937 rng(rd());
    return (uint16_t)std::uniform_int_distribution<int>(19000, 19999)(rng);
}

struct TlsServer {
    std::string cert_path;
    std::string key_path;
    std::unique_ptr<httplib::SSLServer> srv;
    std::thread th;

    TlsServer(uint16_t port) {
        auto& pair = self_signed();
        // cpp-httplib needs file paths for cert/key. tmpfile + write + path
        // is awkward; easier to write to /tmp with unique names.
        char tmpl1[] = "/tmp/bgh-cert-XXXXXX.pem";
        char tmpl2[] = "/tmp/bgh-key-XXXXXX.pem";
        int fd1 = mkstemps(tmpl1, 4);
        int fd2 = mkstemps(tmpl2, 4);
        write(fd1, pair.cert_pem.data(), pair.cert_pem.size()); close(fd1);
        write(fd2, pair.key_pem.data(),  pair.key_pem.size());  close(fd2);
        cert_path = tmpl1;
        key_path  = tmpl2;
        srv = std::make_unique<httplib::SSLServer>(cert_path.c_str(), key_path.c_str());
        srv->Get("/echo", [](const httplib::Request& req, httplib::Response& res) {
            res.set_content("TLS OK: " + req.get_param_value("msg"), "text/plain");
        });
        srv->bind_to_port("127.0.0.1", port);
        th = std::thread([this]() { srv->listen_after_bind(); });
        // Spin until the listener is actually accepting (handshake takes
        // longer than plain TCP because the cert + key need to load).
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        httplib::SSLClient probe("127.0.0.1", port);
        probe.enable_server_certificate_verification(false);
        probe.set_connection_timeout(0, 100 * 1000);
        while (std::chrono::steady_clock::now() < deadline) {
            if (probe.Get("/__probe")) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    ~TlsServer() {
        if (srv) srv->stop();
        if (th.joinable()) th.join();
        std::remove(cert_path.c_str());
        std::remove(key_path.c_str());
    }
};

}  // namespace

TEST(WiFiClientSecureReal, HandshakeAgainstLocalhostInsecure) {
    setenv("BOARDGHOST_NET", "real", 1);
    uint16_t port = pick_port();
    TlsServer server(port);

    WiFiClientSecure client;
    client.setInsecure();  // self-signed — skip verification
    client.setTimeout(2000);
    ASSERT_EQ(client.connect("127.0.0.1", port), 1)
        << "TLS handshake failed";
    EXPECT_TRUE(client.connected());

    const char* req = "GET /echo?msg=phase3 HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
    EXPECT_EQ(client.write((const uint8_t*)req, std::strlen(req)), std::strlen(req));

    // Read the response — we just need to confirm bytes flow over TLS and
    // that the response body contains our echoed payload.
    std::string sink;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        uint8_t buf[1024];
        int got = client.read(buf, sizeof(buf));
        if (got <= 0) {
            if (sink.find("TLS OK: phase3") != std::string::npos) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        sink.append((char*)buf, got);
        if (sink.find("TLS OK: phase3") != std::string::npos) break;
    }
    EXPECT_NE(sink.find("TLS OK: phase3"), std::string::npos)
        << "TLS response did not contain expected payload. got: " << sink;
    client.stop();
}

TEST(WiFiClientSecureReal, HandshakeWithPinnedCASucceeds) {
    setenv("BOARDGHOST_NET", "real", 1);
    uint16_t port = pick_port();
    TlsServer server(port);

    WiFiClientSecure client;
    client.setCACert(self_signed().cert_pem.c_str());  // pin our self-signed CA
    client.setTimeout(2000);
    // Cert was issued for CN=localhost. Connecting via "localhost" passes
    // both chain validation (pinned CA) and hostname verification.
    EXPECT_EQ(client.connect("localhost", port), 1);
    client.stop();
}

TEST(WiFiClientSecureReal, RejectsChainValidCertForWrongHostname) {
    // The dangerous case that hostname verification protects against: the
    // chain is valid (pinned CA) but the hostname in the cert (CN=localhost)
    // does NOT match the host we asked to connect to ("127.0.0.1" is an IP
    // literal, not the literal string "localhost"). Without hostname
    // verification a MITM with any chain-valid cert could intercept; with it
    // the handshake aborts.
    setenv("BOARDGHOST_NET", "real", 1);
    uint16_t port = pick_port();
    TlsServer server(port);

    WiFiClientSecure client;
    client.setCACert(self_signed().cert_pem.c_str());
    client.setTimeout(2000);
    EXPECT_EQ(client.connect("127.0.0.1", port), 0)
        << "expected hostname mismatch to fail handshake";
    EXPECT_FALSE(client.connected());

    // setInsecure() bypasses both checks — should now succeed.
    WiFiClientSecure permissive;
    permissive.setInsecure();
    permissive.setTimeout(2000);
    EXPECT_EQ(permissive.connect("127.0.0.1", port), 1);
    permissive.stop();
}

TEST(WiFiClientSecureReal, HandshakeFailsAgainstUntrustedSelfSigned) {
    setenv("BOARDGHOST_NET", "real", 1);
    uint16_t port = pick_port();
    TlsServer server(port);

    WiFiClientSecure client;
    // No setInsecure, no setCACert — server's self-signed cert isn't in the
    // system trust store, so verification should reject it.
    client.setTimeout(2000);
    EXPECT_EQ(client.connect("127.0.0.1", port), 0)
        << "expected handshake to fail without setInsecure/setCACert";
    EXPECT_FALSE(client.connected());
}

#else  // !BOARDGHOST_WITH_OPENSSL

TEST(WiFiClientSecureReal, SkippedWithoutOpenSSL) {
    GTEST_SKIP() << "Build without OpenSSL — Phase 3 tests not applicable";
}

#endif
