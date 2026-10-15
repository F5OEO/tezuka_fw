// A built-in test programme for the synthetic transmitters: a looping MPEG-2 test card with a beeping tone, muxed into a transport stream.
#pragma once
#include <cstdint>
#include <functional>

namespace dect2 {

// A packet source for dvbt::Generator / atsc::Generator. The clip is encoded once (a fraction of a second at start) and padded with null
// packets so that it fills `netBitrate` bits per second of the channel.
std::function<void(uint8_t*)> demoTsSource(double netBitrate);

} // namespace dect2
