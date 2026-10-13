// The scripted test backend itself: ordering, threading and cancel semantics that
// the policy tests (tst_http) rely on.
#include <doctest/doctest.h>
#include "faketransport.h"
#include <thread>

namespace {
struct Rec : yt::HttpResponseSink {
    std::vector<std::string> ev;
    std::thread::id tid;
    std::atomic<bool> done{false};
    void onHead(int status, std::vector<yt::HttpHeader>) override { tid = std::this_thread::get_id(); ev.push_back("head:" + std::to_string(status)); }
    bool onBody(const char *d, size_t n) override { ev.push_back("body:" + std::string(d, n)); return true; }
    void onDone(yt::TransportStatus s) override { ev.push_back("done:" + std::to_string((int)s.error)); tid = std::this_thread::get_id(); done = true; }
};
void waitDone(Rec &r) { for (int i = 0; i < 400 && !r.done; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
}

TEST_CASE("inline: scripted exchange is delivered in order, only on complete()") {
    FakeTransport ft;
    FakeExchange x; x.status = 200; x.chunks = {"ab", "cd"};
    ft.script("https://a.test/", x);
    auto r = std::make_shared<Rec>();
    yt::HttpRequest req; req.url = "https://a.test/x";
    ft.start(req, r);
    CHECK(r->ev.empty());
    ft.complete();
    CHECK(r->ev == std::vector<std::string>{"head:200", "body:ab", "body:cd", "done:0"});
    REQUIRE(ft.started.size() == 1);
    CHECK(ft.started[0].url == "https://a.test/x");
}
TEST_CASE("unscripted URL answers 404 with an empty body") {
    FakeTransport ft;
    auto r = std::make_shared<Rec>();
    yt::HttpRequest req; req.url = "https://nowhere.test/";
    ft.start(req, r);
    ft.complete();
    CHECK(r->ev == std::vector<std::string>{"head:404", "done:0"});
}
TEST_CASE("longest prefix wins and scripts are FIFO") {
    FakeTransport ft;
    FakeExchange a; a.status = 201; FakeExchange b; b.status = 202; FakeExchange c; c.status = 203;
    ft.script("https://a.test/", a);
    ft.script("https://a.test/deep", b);
    ft.script("https://a.test/deep", c);
    auto r1 = std::make_shared<Rec>(), r2 = std::make_shared<Rec>(), r3 = std::make_shared<Rec>();
    yt::HttpRequest q; q.url = "https://a.test/deep/1"; ft.start(q, r1); ft.start(q, r2);
    q.url = "https://a.test/other"; ft.start(q, r3);
    ft.complete();
    CHECK(r1->ev.front() == "head:202");
    CHECK(r2->ev.front() == "head:203");
    CHECK(r3->ev.front() == "head:201");
}
TEST_CASE("threaded: the sequence arrives from another thread") {
    FakeTransport ft(FakeTransport::Mode::Threaded);
    FakeExchange x; x.chunks = {"z"};
    ft.script("https://a.test/", x);
    auto r = std::make_shared<Rec>();
    yt::HttpRequest req; req.url = "https://a.test/";
    ft.start(req, r);
    waitDone(*r);
    CHECK(r->ev == std::vector<std::string>{"head:200", "body:z", "done:0"});
    CHECK(r->tid != std::this_thread::get_id());
}
TEST_CASE("cancel on a hanging exchange yields onDone(Canceled)") {
    SUBCASE("inline") {
        FakeTransport ft;
        FakeExchange x; x.hang = true; ft.script("https://h.test/", x);
        auto r = std::make_shared<Rec>();
        yt::HttpRequest req; req.url = "https://h.test/";
        uint64_t id = ft.start(req, r);
        ft.complete();
        CHECK(r->ev.empty());
        ft.cancel(id);
        ft.complete();
        CHECK(r->ev == std::vector<std::string>{"done:" + std::to_string((int)yt::TransportError::Canceled)});
        CHECK(ft.canceled == std::vector<uint64_t>{id});
    }
    SUBCASE("threaded") {
        FakeTransport ft(FakeTransport::Mode::Threaded);
        FakeExchange x; x.hang = true; ft.script("https://h.test/", x);
        auto r = std::make_shared<Rec>();
        yt::HttpRequest req; req.url = "https://h.test/";
        uint64_t id = ft.start(req, r);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        CHECK_FALSE(r->done);
        ft.cancel(id);
        waitDone(*r);
        CHECK(r->ev == std::vector<std::string>{"done:" + std::to_string((int)yt::TransportError::Canceled)});
    }
}
TEST_CASE("onBody returning false stops the body and ends Canceled") {
    struct Stop : Rec { bool onBody(const char *d, size_t n) override { Rec::onBody(d, n); return false; } };
    FakeTransport ft;
    FakeExchange x; x.chunks = {"1", "2"}; ft.script("https://a.test/", x);
    auto r = std::make_shared<Stop>();
    yt::HttpRequest req; req.url = "https://a.test/";
    ft.start(req, r);
    ft.complete();
    CHECK(r->ev == std::vector<std::string>{"head:200", "body:1", "done:" + std::to_string((int)yt::TransportError::Canceled)});
}
