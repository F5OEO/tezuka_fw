// Where the DVB-S/S2 error correction runs: CPU, GPU, or Auto. Auto prefers the GPU when the machine has one that is worth it, and keeps measuring
// the other path (one decode in 64) so that it moves to whichever finishes a frame sooner. A GPU call that fails is retried on the CPU by the caller.
// Internal.
#pragma once
#include "dect2/gpu_ldpc.h"
#include "dect2/gpu_viterbi.h"
#include <atomic>
#include <chrono>

namespace dect2 {
namespace dvbs {

class ComputeChoice {
public:
    std::atomic<int> mode{2};                 // 0 CPU, 1 GPU, 2 auto
    std::atomic<bool> lastOnGpu{false};       // the path the last decode took
    std::atomic<bool> autoAllowed{true};      // Auto may use the GPU at all (the DVB-S window it needs is only worth waiting for at high symbol rates)

    static bool gpuUsable() { return GpuLdpc::instance().available(); }
    static bool viterbiUsable() { return GpuViterbi::instance().available(); }
    // `usable`: whether the backend this choice is for exists; `startOnGpu`: whether Auto begins on the GPU (when it is usable)
    explicit ComputeChoice(bool (*usable)() = &ComputeChoice::gpuUsable, bool startOnGpu = true) : usable_(usable), preferGpu_(startOnGpu && usable() && gpuPreferredAtStart()) {}
    // Auto starts on the GPU on a Mac (unified memory, always worth it) and on a discrete graphics card elsewhere; an integrated GPU on Windows
    // has to prove itself in the measurements
    static bool gpuPreferredAtStart() {
#ifdef _WIN32
        return gpuUsable() && GpuLdpc::instance().dedicatedMemoryMb() >= 1024;
#else
        return gpuUsable();
#endif
    }

    // true: this decode goes to the GPU. `probe` is set when it is a measurement of the path that is not the preferred one.
    bool pickGpu(bool* probe = nullptr) {
        if (probe) *probe = false;
        const int m = mode.load();
        if (m == 0 || !usable_()) return false;
        if (m == 1) return true;
        if (!autoAllowed.load()) return false;
        const bool pref = preferGpu_.load();
        if ((++count_ & 63) == 0) { if (probe) *probe = true; return !pref; }
        return pref;
    }
    // report one decode: ms per frame on the path taken
    void report(bool gpu, double msPerFrame) {
        lastOnGpu = gpu;
        std::atomic<double>& e = gpu ? emaGpu_ : emaCpu_;
        double v = e.load();
        v = v == 0 ? msPerFrame : v + 0.1 * (msPerFrame - v);
        e.store(v);
        (gpu ? haveGpu_ : haveCpu_) = true;
        if (mode.load() == 2 && haveBoth()) {
            const double g = emaGpu_.load(), c = emaCpu_.load();
            if (preferGpu_.load() ? c < 0.9 * g : g < 0.9 * c) preferGpu_ = !preferGpu_.load();
        }
    }

private:
    bool (*usable_)();
    bool haveBoth() const { return haveGpu_.load() && haveCpu_.load(); }
    std::atomic<bool> preferGpu_, haveGpu_{false}, haveCpu_{false};
    std::atomic<double> emaGpu_{0}, emaCpu_{0};
    std::atomic<unsigned> count_{0};
};

} // namespace dvbs
} // namespace dect2
