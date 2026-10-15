// demoTsSource without FFmpeg, for the embedded dvbstool build (tools/arm/Makefile): the payload is the plain test packets.
#include "dect2/demo_ts.h"
#include "dect2/dvbt_gen.h"

namespace dect2 {
std::function<void(uint8_t*)> demoTsSource(double) { return dvbt::testTsSource(); }
}
