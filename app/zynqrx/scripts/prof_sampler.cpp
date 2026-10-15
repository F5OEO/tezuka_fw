// Optional PC sampler for the ARM build (make PROF=1): SIGPROF at 1 kHz of CPU time, all threads; writes "address count" lines to $DVBS_PROF at exit.
// Resolve with `arm-linux-nm -n dvbstool` (the PROF build is static and not position independent) - see tools/arm/prof_report.py.
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sys/time.h>
#include <ucontext.h>

namespace {
constexpr size_t kMax = 1 << 20;
unsigned long samples[kMax];
volatile size_t n = 0;
void onProf(int, siginfo_t*, void* ctx) {
    const size_t i = __sync_fetch_and_add(&n, 1);
    if (i < kMax) samples[i] = ((ucontext_t*)ctx)->uc_mcontext.arm_pc;
}
void dump() {
    const char* path = getenv("DVBS_PROF");
    if (!path) return;
    std::map<unsigned long, unsigned> h;
    for (size_t i = 0; i < n && i < kMax; i++) h[samples[i]]++;
    if (FILE* f = fopen(path, "w")) { for (auto& kv : h) fprintf(f, "%lx %u\n", kv.first, kv.second); fclose(f); }
}
struct Init {
    Init() {
        if (!getenv("DVBS_PROF")) return;
        struct sigaction sa = {};
        sa.sa_sigaction = onProf; sa.sa_flags = SA_SIGINFO | SA_RESTART;
        sigaction(SIGPROF, &sa, nullptr);
        itimerval it = {{0, 1000}, {0, 1000}};
        setitimer(ITIMER_PROF, &it, nullptr);
        atexit(dump);
    }
} init;
}
