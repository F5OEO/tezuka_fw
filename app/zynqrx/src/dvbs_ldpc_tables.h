// DVB-S2 LDPC address tables (see dvbs_ldpc_tables.cpp)
#pragma once
#include <cstdint>

namespace dect2 {
namespace dvbs {

struct S2LdpcTable {
    const uint16_t* data;   // for each row: count, addresses...
    int rows;
    int entries;            // number of uint16_t in data
};
extern const S2LdpcTable kS2LdpcTables[2][11];   // [0 normal, 1 short][rate index]

} // namespace dvbs
} // namespace dect2
