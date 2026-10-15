// Builds without a Viterbi GPU backend (everything except macOS for now): the GPU decoder reports itself as unavailable and the CPU decoder is used.
#include "dect2/gpu_viterbi.h"

namespace dect2 {

struct GpuViterbi::Impl {};
GpuViterbi::GpuViterbi() : p_(new Impl) {}
GpuViterbi::~GpuViterbi() = default;
GpuViterbi& GpuViterbi::instance() { static GpuViterbi g; return g; }
bool GpuViterbi::available() const { return false; }
bool GpuViterbi::decode(const std::vector<int8_t>&, std::vector<uint8_t>&, long*) { return false; }

} // namespace dect2
