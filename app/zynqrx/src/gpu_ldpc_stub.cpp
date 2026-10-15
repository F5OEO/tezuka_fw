// Builds without a GPU backend (everything except macOS for now): the GPU decoder reports itself as unavailable and the engine's
// Auto/CPU modes use the CPU decoder. A Vulkan / OpenCL implementation can replace this file.
#include "dect2/gpu_ldpc.h"
#include <cstdint>

namespace dect2 {

struct GpuLdpc::Impl {};
GpuLdpc::GpuLdpc() : p_(new Impl) {}
GpuLdpc::~GpuLdpc() = default;
GpuLdpc& GpuLdpc::instance() { static GpuLdpc g; return g; }
bool GpuLdpc::available() const { return false; }
const char* GpuLdpc::deviceName() const { return "none"; }
bool GpuLdpc::decode(const LdpcCode&, const float*, int, int, uint8_t*, uint8_t*, int*, float) { return false; }

} // namespace dect2
