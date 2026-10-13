#include "net/loop.h"
#include <algorithm>
namespace yt::net {
void Loop::setDriver(LoopDriver *d) { m_driver = d; }
void Loop::post(Fn fn) {
    { std::lock_guard<std::mutex> l(m_mx); m_posted.push_back(std::move(fn)); }
    if (LoopDriver *d = m_driver.load()) d->wake(); else m_cv.notify_one();
}
uint64_t Loop::after(int ms, Fn fn) {
    const uint64_t id = m_nextTimer++;
    m_timers.emplace(Clock::now() + std::chrono::milliseconds(ms), std::make_pair(id, std::move(fn)));
    return id;
}
void Loop::cancelTimer(uint64_t id) {
    for (auto it = m_timers.begin(); it != m_timers.end(); ++it)
        if (it->second.first == id) { m_timers.erase(it); return; }
}
void Loop::runOnce(int maxWaitMs) {
    m_owner = std::this_thread::get_id();
    std::vector<Fn> batch;
    { std::lock_guard<std::mutex> l(m_mx); batch.swap(m_posted); }
    for (Fn &f : batch) f();
    while (!m_timers.empty() && m_timers.begin()->first <= Clock::now()) {
        Fn f = std::move(m_timers.begin()->second.second);
        m_timers.erase(m_timers.begin());
        f();
    }
    if (m_quit) return;
    int wait = maxWaitMs;
    if (!m_timers.empty()) {
        const auto d = std::chrono::duration_cast<std::chrono::milliseconds>(
                           m_timers.begin()->first - Clock::now()).count();
        wait = (int) std::max<long long>(0, std::min<long long>(wait, d));
    }
    LoopDriver *drv = m_driver.load();
    {
        std::unique_lock<std::mutex> l(m_mx);
        if (!m_posted.empty()) wait = 0;
        if (!drv) {
            m_cv.wait_for(l, std::chrono::milliseconds(wait), [&] { return !m_posted.empty() || m_quit.load(); });
            return;
        }
    }
    drv->wait(wait);    // loop-driven backend: its I/O + dispatch happen in here
}
void Loop::run() { while (!m_quit) runOnce(1000); }
void Loop::quit() {
    m_quit = true;
    if (LoopDriver *d = m_driver.load()) d->wake();
    else { std::lock_guard<std::mutex> l(m_mx); m_cv.notify_one(); }
}
}
