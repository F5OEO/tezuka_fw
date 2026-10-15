// Metal compute Viterbi decoder for the DVB-S / DVB-T convolutional code (rate 1/2, K = 7, 171/133 octal, depunctured soft values).
// One threadgroup of 64 threads (one per trellis state) decodes one chunk; the chunks are the ones dvbt::Viterbi::decode makes (16384 steps with 192
// steps of overlap on each side), so a decode of many windows at once gives the bits that the CPU decoder gives, with the same 16 bit saturating
// path metrics. Only macOS has a backend for now; elsewhere available() is false and the CPU decoder is used.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace dect2 {

class GpuViterbi {
public:
    static GpuViterbi& instance();
    bool available() const;
    // Same contract as dvbt::Viterbi::decode (soft: pairs per trellis step, one decoded bit per byte out, path metric summed over the chunks).
    // Blocks until the GPU is done. false: nothing was decoded, use the CPU.
    bool decode(const std::vector<int8_t>& soft, std::vector<uint8_t>& bits, long* metric);

private:
    GpuViterbi();
    ~GpuViterbi();
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace dect2
