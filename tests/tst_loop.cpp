#include <doctest/doctest.h>
#include "net/loop.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

struct FakeDriver : yt::LoopDriver {
    std::mutex mx; std::condition_variable cv; bool woken = false; int waits = 0; int lastWait = -2;
    void wait(int ms) override {
        std::unique_lock<std::mutex> l(mx); ++waits; lastWait = ms;
        cv.wait_for(l, std::chrono::milliseconds(ms), [&] { return woken; }); woken = false;
    }
    void wake() override { { std::lock_guard<std::mutex> l(mx); woken = true; } cv.notify_one(); }
};
static void drain(yt::net::Loop &l, std::function<bool()> done, int ms = 2000) {
    auto end = yt::net::Loop::Clock::now() + std::chrono::milliseconds(ms);
    while (!done() && yt::net::Loop::Clock::now() < end) l.runOnce(10);
}
TEST_CASE("post runs on the loop, in order, never inline") {
    FakeDriver drv; yt::net::Loop l;
    SUBCASE("cv") {}
    SUBCASE("driver") { l.setDriver(&drv); }
    std::vector<int> v;
    l.post([&] { v.push_back(1); l.post([&] { v.push_back(3); }); });
    l.post([&] { v.push_back(2); });
    CHECK(v.empty());
    drain(l, [&] { return v.size() == 3; });
    CHECK(v == std::vector<int>{1, 2, 3});
}
TEST_CASE("post from another thread wakes a blocked idle wait") {
    FakeDriver drv; yt::net::Loop l;
    SUBCASE("cv") {}
    SUBCASE("driver") { l.setDriver(&drv); }
    std::atomic<bool> ran{false};
    std::thread t([&] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); l.post([&] { ran = true; }); });
    auto t0 = yt::net::Loop::Clock::now();
    l.runOnce(5000);
    if (!ran) l.runOnce(0);
    t.join();
    CHECK(ran);
    CHECK(yt::net::Loop::Clock::now() - t0 < std::chrono::milliseconds(2000));
}
TEST_CASE("timers fire in deadline order, cancel works, wait is bounded by the next timer") {
    FakeDriver drv; yt::net::Loop l; l.setDriver(&drv);
    std::vector<int> v;
    l.after(30, [&] { v.push_back(2); });
    uint64_t gone = l.after(10, [&] { v.push_back(99); });
    l.after(10, [&] { v.push_back(1); });
    l.cancelTimer(gone);
    l.runOnce(1000);
    CHECK(drv.lastWait <= 10);
    drain(l, [&] { return v.size() == 2; });
    CHECK(v == std::vector<int>{1, 2});
}
TEST_CASE("onLoopThread is true only inside runOnce's thread") {
    yt::net::Loop l; bool inside = false;
    l.post([&] { inside = l.onLoopThread(); });
    l.runOnce(0);
    CHECK(inside);
    bool other = true;
    std::thread([&] { other = l.onLoopThread(); }).join();
    CHECK_FALSE(other);
}
TEST_CASE("run() exits on quit()") {
    yt::net::Loop l;
    std::thread t([&] { l.run(); });
    l.post([&] { l.quit(); });
    t.join();
    CHECK(true);
}
