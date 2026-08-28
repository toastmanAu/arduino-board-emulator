// Real-mDNS backing for the ESPmDNS shim.
//
// Strategy: spawn `avahi-publish-service` (and `avahi-publish -a` for the
// hostname A record) as child processes. Each `MDNS.addService()` becomes a
// long-running child that holds the announcement open. On end() / destructor
// we SIGTERM all children so the announcement is withdrawn cleanly.
//
// This deliberately avoids linking libavahi-client (which would add a build
// dep and pkg-config plumbing). Spawning a process per service has overhead
// but sketches typically publish 1–2 services total, so it's fine.
//
// Failure modes:
//   - avahi-daemon not running     → children exit silently; MDNS calls
//                                    still return true (sketch keeps booting)
//   - avahi-publish-service missing → fork still succeeds; execvp fails in
//                                    the child; same end-user behaviour
//   - BOARDGHOST_MDNS=off           → skip entirely

#include <cstdint>
#include "ESPmDNS.h"
#include "WiFi.h"   // for the host IP we publish in A records

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <string>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace boardghost_internal {

class MDnsImpl {
public:
    ~MDnsImpl() { end(); }

    bool begin(const std::string& hostname) {
        if (disabled()) return true;
        end();  // idempotent — restart from scratch
        hostname_ = hostname;
        // Publish an A record so <hostname>.local resolves to our host IP.
        // Without this, addService() advertisements appear with the host's
        // own hostname rather than the sketch's chosen one.
        std::string ip = WiFi.localIP().toString().c_str();
        if (!hostname_.empty() && !ip.empty() && ip != "127.0.0.1") {
            spawn({"avahi-publish", "-a", hostname_ + ".local", ip});
        }
        return true;
    }

    void end() {
        for (pid_t pid : children_) {
            kill(pid, SIGTERM);
            // Reap with a short blocking wait so we don't leak zombies, but
            // don't hang forever if the child ignores SIGTERM.
            int status = 0;
            for (int i = 0; i < 10; ++i) {
                if (waitpid(pid, &status, WNOHANG) == pid) break;
                usleep(10 * 1000);
            }
        }
        children_.clear();
        hostname_.clear();
    }

    bool addService(const std::string& service,
                    const std::string& proto,
                    uint16_t           port) {
        if (disabled()) return true;
        if (service.empty() || proto.empty()) return false;
        // avahi-publish-service expects the service type as `_<svc>._<proto>`.
        std::string type = "_" + service + "._" + proto;
        std::string name = hostname_.empty() ? service : hostname_;
        spawn({"avahi-publish-service", name, type, std::to_string(port)});
        return true;
    }

    // espota discovery record. TXT keys mirror what the ESP32 core publishes;
    // the IDE keys on `tcp_check` / `auth_upload`. avahi-publish-service takes
    // TXT entries as trailing key=value args.
    void enableArduino(uint16_t port, bool auth) {
        if (disabled()) return;
        std::string name = hostname_.empty() ? "esp32" : hostname_;
        spawn({"avahi-publish-service", name, "_arduino._tcp",
               std::to_string(port),
               "tcp_check=no", "ssh_upload=no", "board=esp32",
               std::string("auth_upload=") + (auth ? "yes" : "no")});
    }

private:
    static bool disabled() {
        const char* env = std::getenv("BOARDGHOST_MDNS");
        return env && (std::strcmp(env, "off") == 0 || std::strcmp(env, "0") == 0);
    }

    void spawn(std::vector<std::string> argv) {
        pid_t pid = fork();
        if (pid < 0) return;
        if (pid == 0) {
            // Tie our lifetime to the parent (sketch) — if the parent dies
            // for any reason (SIGKILL, segfault, debugger detach), the
            // kernel will send us SIGTERM. Without this, an abruptly-killed
            // sketch leaves avahi-publish-service zombies advertising the
            // service name forever (until next reboot or manual cleanup).
            prctl(PR_SET_PDEATHSIG, SIGTERM);
            // Edge case: parent already died between fork and prctl. Detect
            // by getppid; if it's 1 (reparented to init), bail.
            if (getppid() == 1) _exit(0);
            // Child — silence avahi's chatty stdout/stderr; we don't want
            // the sketch's log polluted with "Established under name 'foo'".
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) {
                dup2(devnull, 1);
                dup2(devnull, 2);
                close(devnull);
            }
            std::vector<char*> cargs;
            cargs.reserve(argv.size() + 1);
            for (auto& s : argv) cargs.push_back(const_cast<char*>(s.c_str()));
            cargs.push_back(nullptr);
            execvp(cargs[0], cargs.data());
            _exit(127);   // execvp failed (avahi-publish not on PATH)
        }
        children_.push_back(pid);
    }

    std::vector<pid_t> children_;
    std::string        hostname_;
};

}  // namespace boardghost_internal

namespace bgi = boardghost_internal;

MDNSResponder::MDNSResponder()  : impl_(std::make_unique<bgi::MDnsImpl>()) {}
MDNSResponder::~MDNSResponder() = default;

bool MDNSResponder::begin(const char* hostname) {
    return impl_->begin(hostname ? hostname : "");
}
void MDNSResponder::end() { impl_->end(); }
bool MDNSResponder::addService(const char* svc, const char* proto, uint16_t port) {
    return impl_->addService(svc ? svc : "", proto ? proto : "", port);
}
void MDNSResponder::enableArduino(uint16_t port, bool auth) {
    impl_->enableArduino(port, auth);
}

MDNSResponder MDNS;
