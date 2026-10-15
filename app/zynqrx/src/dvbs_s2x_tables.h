// DVB-S2X LDPC address tables (see dvbs_s2x_tables.cpp)
#pragma once
#include <cstdint>

namespace dect2 {
namespace dvbs {

struct S2xLdpcTable {
    const char* id;          // "B.5": the table of the annex
    const char* code;        // LDPC code identifier: "26/45", "104/180", ...
    int nldpc, kldpc;
    const uint16_t* data;    // for each row: count, addresses...
    int rows;
    int entries;             // number of uint16_t in data
};
extern const S2xLdpcTable kS2xLdpcTables[];
extern const int kS2xLdpcTableCount;

} // namespace dvbs
} // namespace dect2
