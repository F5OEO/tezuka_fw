// Broadcast character sets to UTF-8, and a clean-up for text that leaves the program (channel-list submissions, files).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace dect2::text {
// DAB labels and dynamic labels in the complete EBU Latin repertoire (ETSI TS 101 756 Annex C, charset 0).
std::string ebuLatin(const uint8_t* p, size_t n);
// DAB charset field (EN 300 401 FIG 1 / dynamic label): 0 EBU Latin, 4 ISO 8859-1, 6 UCS-2 (big endian), 15 UTF-8; others as EBU Latin.
std::string dabCharset(int charset, const uint8_t* p, size_t n);
// DVB SI default table (EN 300 468 Annex A, table 00): ISO/IEC 6937, where 0xC1..0xCF are accents put before the letter they mark.
std::string iso6937(const uint8_t* p, size_t n);
// UCS-2 big endian.
std::string ucs2be(const uint8_t* p, size_t n);
// Valid UTF-8 with no control or invisible characters (C0, DEL, C1, soft hyphen, zero-width and direction marks, BOM, U+FFFD and
// unpaired surrogates dropped; tabs and line breaks become spaces), runs of spaces folded to one, trimmed.
std::string clean(const std::string& s);
void appendUtf8(std::string& out, uint32_t cp);
} // namespace dect2::text
