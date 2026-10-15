// A thread that takes blocks of symbols from a queue and commands in order. The receiver's front end (mixer, resampler, matched filter, timing) runs on the
// engine's analysis thread and hands its symbols to one of these, so that the carrier loop and the frame logic (DVB-S2) or the Viterbi decoder (DVB-S)
// do not have to fit into the same core as the front end. Internal.
#pragma once
#include "dect2/ring.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace dect2 {
namespace dvbs {

class SymbolThread {
public:
    using BlockFn = std::function<void(const cf32* z, size_t n)>;
    ~SymbolThread() { stop(); }

    // `maxSymbols`: how many symbols may wait in the queue; beyond that blocks are dropped (or, with `blocking`, push() waits)
    void start(BlockFn fn, size_t maxSymbols, bool blocking) {
        stop();
        fn_ = std::move(fn);
        maxSymbols_ = maxSymbols;
        blocking_ = blocking;
        stopFlag_ = false;
        dropped_ = 0;
        th_ = std::thread([this] { loop(); });
    }

    // Queues a block of symbols (copied). Returns false when it was dropped because the thread is too far behind.
    bool push(const cf32* z, size_t n) {
        std::unique_lock<std::mutex> lk(mu_);
        if (queued_ + n > maxSymbols_) {
            if (blocking_) spaceCv_.wait(lk, [&] { return queued_ + n <= maxSymbols_ || stopFlag_; });
            if (queued_ + n > maxSymbols_) { dropped_ += n; return false; }
        }
        Item it;
        if (!free_.empty()) { it.data = std::move(free_.back()); free_.pop_back(); }
        it.data.assign(z, z + n);
        queued_ += n;
        q_.push_back(std::move(it));
        lk.unlock();
        cv_.notify_one();
        return true;
    }

    // Runs `f` on the thread after everything queued so far
    void post(std::function<void()> f) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            Item it;
            it.cmd = std::move(f);
            q_.push_back(std::move(it));
        }
        cv_.notify_one();
    }

    // Finishes what is queued and joins the thread
    void stop() {
        if (!th_.joinable()) return;
        {
            std::lock_guard<std::mutex> lk(mu_);
            stopFlag_ = true;
        }
        cv_.notify_all();
        spaceCv_.notify_all();
        th_.join();
        q_.clear();
        queued_ = 0;
    }

    // Forgets what is queued (a frame search starts again)
    void clear() {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& it : q_) if (!it.cmd) free_.push_back(std::move(it.data));
        q_.clear();
        queued_ = 0;
        spaceCv_.notify_all();
    }

    // Returns when the thread has processed everything queued before the call
    void sync() {
        if (!th_.joinable()) return;
        std::mutex m;
        std::condition_variable c;
        bool done = false;
        post([&] { std::lock_guard<std::mutex> lk(m); done = true; c.notify_one(); });
        std::unique_lock<std::mutex> lk(m);
        c.wait(lk, [&] { return done; });
    }

    bool running() const { return th_.joinable(); }
    uint64_t dropped() const { std::lock_guard<std::mutex> lk(mu_); return dropped_; }
    size_t queued() const { std::lock_guard<std::mutex> lk(mu_); return queued_; }

private:
    struct Item {
        std::vector<cf32> data;
        std::function<void()> cmd;
    };
    void loop() {
        for (;;) {
            Item it;
            {
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait(lk, [&] { return !q_.empty() || stopFlag_; });
                if (q_.empty()) return;
                it = std::move(q_.front());
                q_.pop_front();
                if (!it.cmd) queued_ -= it.data.size();
            }
            spaceCv_.notify_one();
            if (it.cmd) it.cmd();
            else if (!it.data.empty()) fn_(it.data.data(), it.data.size());
            if (!it.cmd) {
                std::lock_guard<std::mutex> lk(mu_);
                if (free_.size() < 8) free_.push_back(std::move(it.data));
            }
        }
    }

    BlockFn fn_;
    std::thread th_;
    mutable std::mutex mu_;
    std::condition_variable cv_, spaceCv_;
    std::deque<Item> q_;
    std::vector<std::vector<cf32>> free_;
    size_t queued_ = 0, maxSymbols_ = 1 << 22;
    bool blocking_ = false, stopFlag_ = false;
    uint64_t dropped_ = 0;
};

} // namespace dvbs
} // namespace dect2
