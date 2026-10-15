// DVB-T2 / DVB-S2 style IRA LDPC codes defined by 360-bit-group address tables.
#pragma once
#include <cstdint>
#include <vector>

namespace dect2 {

class LdpcCode {
public:
    // `rows[r]` lists the parity-check addresses of the 360 information bits of group r (value 0 terminated by `len`).
    LdpcCode(int k, int n, const std::vector<std::vector<int>>& rows);
    int k() const { return k_; }
    int n() const { return n_; }

    // Systematic encode: bits[0..k) in, bits[k..n) out.
    void encode(std::vector<uint8_t>& bits) const;

    // Normalised min-sum decoding. llr > 0 means bit 0. Returns true if all parity checks are satisfied.
    // hard[0..n) receives the decided bits. `iters` gets the iteration count used.
    bool decode(const std::vector<float>& llr, int maxIter, std::vector<uint8_t>& hard, int* iters = nullptr) const;

    // Quasi-cyclic layer structure (used by the GPU decoder): layer r lists (group, shift) of the information columns.
    struct Conn { int group, shift; };
    const std::vector<std::vector<Conn>>& layers() const { return layers_; }
    int q() const { return q_; }
    const std::vector<int>& chkStart() const { return chkStart_; }   // check -> variable adjacency (for reference decoders in the benchmarks)
    const std::vector<int>& chkVar() const { return chkVar_; }
    int groups() const { return groups_; }

    // Fast layered normalised min-sum exploiting the quasi-cyclic (360) structure; same interface and conventions.
    bool decodeFast(const std::vector<float>& llr, int maxIter, std::vector<uint8_t>& hard, int* iters = nullptr, float alpha = 0.78f) const;

    // Same layered schedule as decodeFast on saturating 8-bit values (16 or 32 checks per vector instead of 4 or 8). The LLRs are scaled to the 8-bit
    // range from their own mean (about 16); the normalisation is 13/16, `alpha` is not used. A frame it does not decode but nearly does (few unsatisfied checks)
    // goes on to decodeFast; a damaged frame (a gap in the samples) does not, that would cost more than the frame is worth.
    bool decodeFast8(const std::vector<float>& llr, int maxIter, std::vector<uint8_t>& hard, int* iters = nullptr, float alpha = 0.78f) const;

private:
    bool decodeQ8Body(const std::vector<float>& llr, int maxIter, std::vector<uint8_t>& hard, int* iters, float alpha, int* unsat) const;
    bool decodeQ8Avx2(const std::vector<float>& llr, int maxIter, std::vector<uint8_t>& hard, int* iters, float alpha, int* unsat) const;
    // decodeFast compiled for the baseline and for AVX2 + FMA (picked at run time, dect2/simd.h)
    bool decodeFastBody(const std::vector<float>& llr, int maxIter, std::vector<uint8_t>& hard, int* iters, float alpha) const;
    bool decodeFastAvx2(const std::vector<float>& llr, int maxIter, std::vector<uint8_t>& hard, int* iters, float alpha) const;
    int k_, n_, m_;
    // check -> variable adjacency (CSR)
    std::vector<int> chkStart_, chkVar_;
    // quasi-cyclic layer structure: layer r lists (group, shift) of the information columns that reach it
    std::vector<std::vector<Conn>> layers_;
    int q_ = 0, groups_ = 0;
    // information-bit edges for encoding: parity index p, info index d
    std::vector<int> encP_, encD_;
};

} // namespace dect2
