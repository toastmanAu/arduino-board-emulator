// mDNS advertisement for the mirror, so the Android companion / agents can
// discover a running instance without typing an IP. Advertises
// `_boardghost-mirror._tcp` via avahi-publish-service — the same spawn-a-child
// approach sim_mdns.cpp uses for the ESPmDNS shim, kept separate here so the
// HTTP module (sim_mirror.cpp) stays free of process/POSIX plumbing.
//
// Only used when the mirror binds the LAN (BOARDGHOST_MIRROR=lan + token);
// pointless on loopback. Best-effort: if avahi isn't running the child exits
// silently and the mirror still works, you just type the IP.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <string>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {
pid_t g_mdns_pid = -1;

bool mdns_disabled() {
    const char* env = std::getenv("BOARDGHOST_MDNS");
    return env && (std::strcmp(env, "off") == 0 || std::strcmp(env, "0") == 0);
}
}  // namespace

extern "C" void boardghost_mirror_mdns_advertise(uint16_t port) {
    if (mdns_disabled() || g_mdns_pid > 0) return;

    // Service instance name; TXT carries the feature-detection path so a
    // discovering client can GET it for live dims/format. Dims aren't in the
    // TXT because no display is registered yet at advertise time.
    std::string port_s = std::to_string(port);
    std::vector<std::string> argv = {
        "avahi-publish-service", "BoardGhost Mirror",
        "_boardghost-mirror._tcp", port_s, "path=/mirror/info",
    };

    pid_t pid = fork();
    if (pid < 0) return;
    if (pid == 0) {
        // Die with the parent so we never leak an advertisement past the sketch.
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() == 1) _exit(0);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, 1); dup2(devnull, 2); close(devnull); }
        std::vector<char*> cargs;
        cargs.reserve(argv.size() + 1);
        for (auto& a : argv) cargs.push_back(const_cast<char*>(a.c_str()));
        cargs.push_back(nullptr);
        execvp(cargs[0], cargs.data());
        _exit(127);  // execvp failed (avahi-publish-service missing)
    }
    g_mdns_pid = pid;
}

extern "C" void boardghost_mirror_mdns_stop(void) {
    if (g_mdns_pid <= 0) return;
    kill(g_mdns_pid, SIGTERM);
    int status = 0;
    for (int i = 0; i < 10; ++i) {
        if (waitpid(g_mdns_pid, &status, WNOHANG) == g_mdns_pid) break;
        usleep(10 * 1000);
    }
    g_mdns_pid = -1;
}
