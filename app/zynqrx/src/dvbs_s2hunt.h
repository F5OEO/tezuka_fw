// DVB-S2 frame synchronisation from a stored window of symbols: the PLHEADER is found by correlating the products of neighbouring symbols with
// those of the SOF and of the PLS code (which do not care about the carrier phase or a frequency offset of a few percent of the symbol rate), then
// each candidate is fitted with a maximum likelihood search over the 256 code words (DVB-S2 and S2X), the carrier frequency and the sense of the spectrum, and the
// frames are followed from it by the lengths the MODCODs give. Internal.
#pragma once
#include "dect2/ring.h"
#include <cstddef>
#include <vector>

namespace dect2 {
namespace dvbs {

struct S2HuntResult {
    bool ok = false;
    size_t pos = 0;             // index (in the window) of the first symbol of the header the receiver starts from (the same as firstPos)
    size_t firstPos = 0;        // the first verified header of a data frame that has another header one frame length later
    size_t lastPos = 0;         // the last header verified in the window
    int frameLen = 0;           // symbols per frame of the header at firstPos
    int modcod = 0;
    bool shortFrame = false, pilots = false;
    double phi = 0;             // carrier offset in radians per symbol (to a few thousandths; the receiver refines it from the headers)
    double theta = 0;           // carrier phase at firstPos, radians
    float plsScore = 0;         // correlation with the winning PLS code word, 1 = perfect
    float plsSecond = 0;
    float sofScore = 0;         // correlation with the SOF after the phase correction
    int headers = 0;            // headers found one frame apart from firstPos on (including the one at firstPos)
    float snrDb = 0;            // Es/N0 from the SOF symbols
    int candidates = 0;         // peaks of the SOF correlation that were examined
    bool inverted = false;      // the symbols are complex conjugates of what was sent (spectral inversion): feed conj(z) to the tracker
};

// Window of n symbols (three frames of the longest kind find a header and two more). About 60 complex multiply-adds per symbol plus a fit per candidate.
// sense: -1 tries both senses of the spectrum, 0 only the normal one, 1 only the inverted one. Once a signal has been decoded the sense is known and
// does not change (an LNB or a mixer stays what it is), so a hunt after a loss of lock needs half the work.
// onlyCode: -1 looks for every PLS code, otherwise only for that one (and the dummy frame): the stream is known to keep its MODCOD. Both the header fits
// (one code word instead of all) and the frame lengths tried (one instead of every length a frame can have) get cheaper by that factor.
// huntCodes: null looks for every PLS code, otherwise only for those that are true in this array of 256 (e.g. the QPSK and 8PSK MODCODs: the other
// modulations are not wanted, and the frame lengths and code words to try shrink with them).
S2HuntResult s2Hunt(const cf32* z, size_t n, int sense = -1, int onlyCode = -1, const bool* huntCodes = nullptr);

} // namespace dvbs
} // namespace dect2
