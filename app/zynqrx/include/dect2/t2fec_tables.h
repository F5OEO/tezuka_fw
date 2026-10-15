// Tables for the DVB-T2 data path (generated, see t2fec_tables.cpp).
#pragma once
#include <cstdint>

namespace dect2 {

struct LdpcTable { const char* name; int rows; int cols; const int* data; }; // row = [count, addr...] (zero padded)
extern const LdpcTable kLdpcTables[];
extern const int kNumLdpcTables;

extern const int k_twist16n[8], k_twist64n[12], k_twist256n[16], k_twist16s[8], k_twist64s[12], k_twist256s[8];
extern const int k_mux16[8], k_mux64[12], k_mux256[16], k_mux16_35[8], k_mux16_13[8], k_mux16_25[8];
extern const int k_mux64_35[12], k_mux64_13[12], k_mux64_25[12], k_mux256_35[16], k_mux256_23[16];
extern const int k_mux256s[8], k_mux256s_13[8], k_mux256s_25[8];

// integer constellation coordinates indexed by cell label (divide by sqrt(2), sqrt(10), sqrt(42), sqrt(170))
extern const int8_t kConstQpsk[4][2], kConstQam16[16][2], kConstQam64[64][2], kConstQam256[256][2];

extern const uint8_t k_polyn01[17], k_polyn02[17], k_polyn03[17], k_polyn04[17], k_polyn05[17], k_polyn06[17],
    k_polyn07[17], k_polyn08[17], k_polyn09[17], k_polyn10[17], k_polyn11[17], k_polyn12[17];
extern const uint8_t k_polys01[15], k_polys02[15], k_polys03[15], k_polys04[15], k_polys05[15], k_polys06[15],
    k_polys07[15], k_polys08[15], k_polys09[15], k_polys10[15], k_polys11[15], k_polys12[15];

} // namespace dect2
