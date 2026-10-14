#ifndef YT_NET_LOOP_H
#define YT_NET_LOOP_H
#include <yt/transport.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
namespace yt::net {
// Task queue + one-shot timers, run by whichever thread calls runOnce()/run() (the
// "engine thread"). Idle waits go to the installed LoopDriver (a loop-driven backend
// does its I/O there) or, without one, to a condition variable.
// post()/quit() are thread-safe; everything else is engine-thread only.
class Loop {
public:
    using Fn = std::function<void()>;
    using Clock = std::chrono::steady_clock;
    void setDriver(LoopDriver *d);                     // engine thread; nullptr = cv wait
    void post(Fn fn);                                  // any thread; wakes the idle wait
    uint64_t after(int ms, Fn fn);                     // one-shot timer
    void cancelTimer(uint64_t id);
    void runOnce(int maxWaitMs);                       // posted tasks, due timers, then idle wait
    void run();                                        // runOnce(1000) until quit()
    void quit();                                       // any thread
    size_t timerCount() const { return m_timers.size(); }   // engine thread (tests)
    bool onLoopThread() const { return m_owner.load() == std::this_thread::get_id(); }
private:
    std::atomic<LoopDriver *> m_driver{nullptr};
    std::mutex m_mx;
    std::condition_variable m_cv;
    std::vector<Fn> m_posted;                          // guarded by m_mx
    std::multimap<Clock::time_point, std::pair<uint64_t, Fn>> m_timers;
    uint64_t m_nextTimer = 1;
    std::atomic<bool> m_quit{false};
    std::atomic<std::thread::id> m_owner{};
};
}
#endif
