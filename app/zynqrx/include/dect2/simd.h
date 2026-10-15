#pragma once
// Runtime-dispatched vector code for x86-64 (Linux, Windows, Intel Macs). The release programs are built for the x86-64 baseline (SSE2) so
// that they start on any 64-bit PC (issue #74); the hot loops are compiled a second time for AVX2 + FMA and picked at run time when the
// processor has them. ifunc / target_clones exist only on Linux, so the dispatch is done by hand:
//
//   DECT2_SIMD_BODY void fooBody(const float* a, int n) { ... }            // the code, written once
//   DECT2_AVX2_DISPATCH(void, foo, (const float* a, int n), (a, n))       // defines foo(): AVX2 copy or baseline copy
//
// The body is always inlined, into an AVX2 wrapper (compiled with target("avx2,fma")) and into the baseline path. Other targets compile the
// plain function. ARM uses NEON intrinsics where they exist (guarded by __ARM_NEON) and the compiler's own vectoriser elsewhere.
#if defined(__GNUC__) && defined(__x86_64__) && !defined(DECT2_NO_SIMD)
#define DECT2_X86_DISPATCH 1
#define DECT2_AVX2_TARGET __attribute__((target("avx2,fma")))
#define DECT2_SIMD_BODY static inline __attribute__((always_inline))
#define DECT2_SIMD_MEMBER_BODY inline __attribute__((always_inline))
namespace dect2 {
// true on a processor (and operating system) that runs AVX2 and FMA; computed once
inline bool cpuHasAvx2() {
    static const bool v = [] { __builtin_cpu_init(); return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma"); }();
    return v;
}
} // namespace dect2
#define DECT2_AVX2_DISPATCH(ret, name, params, args) \
    DECT2_AVX2_TARGET static ret name##Avx2 params { return name##Body args; } \
    ret name params { if (::dect2::cpuHasAvx2()) return name##Avx2 args; return name##Body args; }
#else
#define DECT2_AVX2_TARGET
#define DECT2_SIMD_BODY static inline
#define DECT2_SIMD_MEMBER_BODY inline
#define DECT2_AVX2_DISPATCH(ret, name, params, args) \
    ret name params { return name##Body args; }
#endif
