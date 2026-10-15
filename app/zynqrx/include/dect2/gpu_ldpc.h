// Metal compute LDPC decoder: layered normalised min-sum, one threadgroup (360 threads, one per check of a layer) per FEC block.
#pragma once
#include "ldpc.h"
#include <cstddef>
#include <cstdint>
#include <memory>

namespace dect2 {

class GpuLdpc {
public:
    static GpuLdpc& instance();
    bool available() const;
    const char* deviceName() const;
#ifdef _WIN32
    size_t dedicatedMemoryMb() const;   // video memory of the chosen adapter that belongs to it (about 128 for integrated graphics)
#endif
    // Decodes `nb` blocks of n LLRs each (llr > 0 means bit 0). hard receives nb*n bits (one per byte),
    // ok[b] = all parity checks satisfied, iters[b] = iterations used. Blocks the caller until the GPU is done.
    bool decode(const LdpcCode& code, const float* llr, int nb, int maxIter, uint8_t* hard, uint8_t* ok, int* iters, float alpha = 0.78f);

private:
    bool selfTest();   // (Direct3D 11 build) decodes known blocks; the GPU decoder disables itself if the result is wrong
    GpuLdpc();
    ~GpuLdpc();
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace dect2
