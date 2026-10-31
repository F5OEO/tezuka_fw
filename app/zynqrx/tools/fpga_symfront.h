// The FPGA symbol front end (maia-hdl symfront IP, registers at 0x43C20000): NCO mixer, interpolator, Gardner timing loop and AGC in the
// fabric, so that the RX buffer carries one symbol per sample (zynqrx rx --iio --fpga-front). Its arithmetic is maia_hdl/symfront_model.py;
// the loop gains below are those of SymbolTimer::setLoop (dvbs_dsp.cpp), in the register format of the model.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

class FpgaSymFront {
public:
    static constexpr uint32_t kBase = 0x43C20000;
    enum Reg { kControl = 0x00, kNcoStep = 0x04, kKp = 0x08, kKi = 0x0c, kPower = 0x10, kSymbols = 0x14, kStatus = 0x18 };

    FpgaSymFront() = default;
    FpgaSymFront(const FpgaSymFront&) = delete;
    FpgaSymFront& operator=(const FpgaSymFront&) = delete;
    ~FpgaSymFront() { if (regs_) { enable(false); munmap((void*)regs_, 0x1000); } if (fd_ >= 0) close(fd_); }

    bool open() {
        fd_ = ::open("/dev/mem", O_RDWR | O_SYNC);
        if (fd_ < 0) { perror("fpga front end: /dev/mem"); return false; }
        void* p = mmap(nullptr, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, kBase);
        if (p == MAP_FAILED) { perror("fpga front end: mmap"); regs_ = nullptr; return false; }
        regs_ = (volatile uint32_t*)p;
        // a bitstream without the core reads all ones or zeros here: write a gain and read it back
        write(kKp, 0x123456);
        if (read(kKp) != 0x123456) { fprintf(stderr, "fpga front end: no symfront core at 0x%08x (bitstream without it?)\n", kBase); munmap(p, 0x1000); regs_ = nullptr; return false; }
        return true;
    }
    // carrierHz: where the carrier is relative to the LO (the mixer moves it to 0); fs: sample rate into the core (2 per symbol)
    void configure(double carrierHz, double fs, double rollOff) {
        enable(false);
        roll_ = rollOff;
        const int64_t step = std::llround(-carrierHz / fs * 4294967296.0);
        write(kNcoStep, (uint32_t)(step & 0xffffffff));
        setLoop(0.02);
    }
    void setLoop(double bnT, double zeta = 0.707) {
        const double kd = std::max(3.1 * roll_, 0.15);
        const double th = bnT / (zeta + 1.0 / (4.0 * zeta));
        const double d = 1.0 + 2.0 * zeta * th + th * th;
        write(kKp, (uint32_t)std::llround(4.0 * zeta * th / d / kd * 16777216.0));
        write(kKi, (uint32_t)std::llround(4.0 * th * th / d / kd * 16777216.0));
    }
    void enable(bool on) { if (regs_) write(kControl, on ? 1 : 0); }
    uint32_t symbols() const { return read(kSymbols); }
    double power() const { return (double)read(kPower) * 4.0; }
    bool inOverflow() const { return read(kStatus) & 1; }
    bool outOverflow() const { return read(kStatus) & 2; }

private:
    void write(Reg r, uint32_t v) { regs_[r / 4] = v; }
    uint32_t read(Reg r) const { return regs_[r / 4]; }
    int fd_ = -1;
    volatile uint32_t* regs_ = nullptr;
    double roll_ = 0.35;
};
