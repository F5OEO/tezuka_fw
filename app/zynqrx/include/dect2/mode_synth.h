// Test signals for the modes added after FM: a real-time generator that the built-in "synthetic" source plays (and the tools write to files).
// Nothing is transmitted; the samples only exist in memory or in a file.
#pragma once
#include "ring.h"
#include "source.h"
#include <memory>

namespace dect2 {

class ModeSynth {
public:
    virtual ~ModeSynth() = default;
    virtual double sampleRate() const = 0;
    // The next n samples of an endless, continuous signal. Keep the peak below about 0.9 (the source rounds to 8 bits like a HackRF);
    // an OFDM or a continuous signal sits at an rms of about 0.2 to 0.25. Noise, carrier offset and so on come from the SynthConfig.
    virtual void generate(cf32* out, size_t n) = 0;
    // The user changed an option while it plays. The default does nothing: the source then starts a new generator.
    virtual bool configure(const SynthConfig&) { return false; }
};

// The generator of the mode with this engine standard code (8 and up), or nullptr when the mode has no test signal.
std::unique_ptr<ModeSynth> makeModeSynth(int stdMode, const SynthConfig& cfg, double sampleRate);

} // namespace dect2
