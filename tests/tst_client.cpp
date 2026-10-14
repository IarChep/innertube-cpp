// yt::Client — the public API: engine thread, executor delivery and the
// cancellation contract (spec §3.2).
#include <doctest/doctest.h>
#include <yt/client.h>
#include <yt/testing.h>
#include "conformance/loopback.h"
#include "fakehttp.h"
#include "faketransport.h"
#include "testutil.h"
#include <atomic>
#include <map>
#include <mutex>
#include <random>
#include <thread>

namespace {
// The loopback scenarios need a real backend; INNERTUBE_HTTP_BACKEND=none has none.
bool haveBackend() { return yt::makeDefaultTransport(yt::TransportConfig{}) != nullptr; }
#define REQUIRE_BACKEND() do { if (!haveBackend()) { MESSAGE("skipped: built without an HTTP backend"); return; } } while (0)
std::unique_ptr<yt::Client> mk(std::shared_ptr<FakeHttp> f, yt::ClientOptions o = {}) {
    return yt::testing::makeClientForTesting(std::move(o), f);
}
// A manual executor: deliveries queue up until run().
struct QueueExec {
    std::mutex mx;
    std::vector<std::function<void()>> q;
    yt::Executor exec() { return [this](std::function<void()> fn) { std::lock_guard<std::mutex> l(mx); q.push_back(std::move(fn)); }; }
    size_t size() { std::lock_guard<std::mutex> l(mx); return q.size(); }
    void run() {
        std::vector<std::function<void()>> b;
        { std::lock_guard<std::mutex> l(mx); b.swap(q); }
        for (auto &f : b) f();
    }
};
}

TEST_CASE("browse delivers a parsed page, never inline") {
    auto f = std::make_shared<FakeHttp>(); f->queue("browse", loadFixtureRaw("browse_feed.json"));
    auto c = mk(f);
    yt::Result<yt::VideoPage> got; bool called = false;
    yt::Job j = c->browse({"FEwhat_to_watch", "", ""}, [&](const yt::Result<yt::VideoPage> &r) { got = r; called = true; });
    CHECK_FALSE(called);
    CHECK(j.active());
    c->runOnce(0);
    CHECK(f->lastClientFor("browse") == (int)yt::ClientId::WEB);
    f->flush();
    CHECK(called);
    REQUIRE(got.ok());
    CHECK(got.value.items.size() == 2);
    CHECK_FALSE(j.active());
}
TEST_CASE("initial state seeds the session") {
    auto f = std::make_shared<FakeHttp>();
    yt::ClientOptions o;
    o.initial.hl = "de"; o.initial.gl = "AT"; o.initial.visitorData = "VD0";
    o.clients[yt::ClientId::IOS] = yt::ClientInfo{"IOS", "99.0", "UA", 5};
    auto c = mk(f, o);
    CHECK(f->session().hl == "de");
    CHECK(f->session().gl == "AT");
    CHECK(f->session().visitorData == "VD0");
    CHECK(f->session().clients[(int)yt::ClientId::IOS].version == "99.0");
}
TEST_CASE("cancelOnDeliveryThreadSuppresses") {
    auto f = std::make_shared<FakeHttp>(); f->queue("browse", "{}");
    QueueExec ex;
    yt::ClientOptions o; o.deliver = ex.exec();
    auto c = mk(f, o);
    int calls = 0;
    yt::Job j = c->browse({"FEx", "", ""}, [&](const yt::Result<yt::VideoPage> &) { ++calls; });
    c->runOnce(0); f->flush();
    REQUIRE(ex.size() == 1);            // the delivery is queued in the consumer's executor
    j.cancel();                         // ...and canceled on the delivery thread before it runs
    ex.run();
    CHECK(calls == 0);
    CHECK_FALSE(j.active());
}
TEST_CASE("deliveries go through the executor") {
    auto f = std::make_shared<FakeHttp>(); f->queue("browse", "{}");
    QueueExec ex;
    yt::ClientOptions o; o.deliver = ex.exec();
    auto c = mk(f, o);
    int calls = 0;
    c->browse({"FEx", "", ""}, [&](const yt::Result<yt::VideoPage> &) { ++calls; });
    c->runOnce(0); f->flush();
    CHECK(calls == 0);
    ex.run();
    CHECK(calls == 1);
}
TEST_CASE("setLocale reaches the session and clears the cache") {
    auto f = std::make_shared<FakeHttp>();
    std::vector<yt::SessionState> states;
    yt::ClientOptions o; o.onStateChanged = [&](const yt::SessionState &s) { states.push_back(s); };
    auto c = mk(f, o);
    c->setLocale("ru", "RU");
    c->runOnce(0);
    CHECK(f->session().hl == "ru");
    CHECK(f->session().gl == "RU");
    CHECK(f->clearCount >= 1);
    REQUIRE(states.size() == 1);
    CHECK(states[0].hl == "ru");
}
TEST_CASE("a runtime transport factory overrides the build-time backend") {
    FakeTransport *made = nullptr;
    yt::ClientOptions o;
    o.ownThread = false;
    o.net.ipResolve = yt::IpResolve::V4;
    o.transport = [&](const yt::TransportConfig &cfg) {
        CHECK(cfg.ipResolve == yt::IpResolve::V4);
        auto t = std::make_unique<FakeTransport>();
        made = t.get();
        return std::unique_ptr<yt::HttpTransport>(std::move(t));
    };
    yt::Client c(o);
    REQUIRE(made != nullptr);
    c.browse({"FEx", "", ""}, [](const yt::Result<yt::VideoPage> &) {});
    c.runOnce(0);
    REQUIRE(made->started.size() == 1);
    CHECK(made->started[0].url == "https://www.youtube.com/youtubei/v1/browse?prettyPrint=false");
}
TEST_CASE("visitorData capture surfaces via onStateChanged") {
    FakeTransport *ft = nullptr;
    std::vector<yt::SessionState> states;
    yt::ClientOptions o;
    o.ownThread = false;
    o.transport = [&](const yt::TransportConfig &) {
        auto t = std::make_unique<FakeTransport>(); ft = t.get();
        return std::unique_ptr<yt::HttpTransport>(std::move(t));
    };
    o.onStateChanged = [&](const yt::SessionState &s) { states.push_back(s); };
    yt::Client c(o);
    FakeExchange x; x.chunks = {"{\"responseContext\":{\"visitorData\":\"VX\"}}"};
    ft->script("https://www.youtube.com/youtubei/v1/browse", x);
    bool called = false;
    c.browse({"FEx", "", ""}, [&](const yt::Result<yt::VideoPage> &r) { called = true; CHECK(r.ok()); });
    for (int i = 0; i < 5; ++i) { c.runOnce(0); ft->complete(); }
    CHECK(called);
    REQUIRE_FALSE(states.empty());
    CHECK(states.back().visitorData == "VX");
}
TEST_CASE("no transport -> constructor throws") {
    yt::ClientOptions o;
    o.transport = [](const yt::TransportConfig &) { return std::unique_ptr<yt::HttpTransport>(); };
    SUBCASE("own thread") { o.ownThread = true; }
    SUBCASE("manual") { o.ownThread = false; }
    CHECK_THROWS_AS(yt::Client{o}, std::invalid_argument);
}
TEST_CASE("cancelFromOtherThreadAtMostOnce") {
    REQUIRE_BACKEND();
    yt::conformance::Loopback srv;
    srv.route("/youtubei/v1/browse", yt::conformance::Canned{200, "{}"});
    yt::ClientOptions o;   // own engine thread, inline delivery on it
    auto c = yt::testing::makeClientForTesting(o, srv.url("/youtubei/v1/"));
    std::mutex mx;
    std::map<int, int> calls;
    std::mt19937 rng(7);
    std::vector<yt::Job> jobs;
    for (int i = 0; i < 200; ++i) {
        jobs.push_back(c->browse({"FE" + std::to_string(i), "", ""},
            [&, i](const yt::Result<yt::VideoPage> &) { std::lock_guard<std::mutex> l(mx); ++calls[i]; }));
        const int delayUs = (int)(rng() % 3000);
        yt::Job j = jobs.back();
        std::thread([j, delayUs]() mutable { std::this_thread::sleep_for(std::chrono::microseconds(delayUs)); j.cancel(); }).join();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::lock_guard<std::mutex> l(mx);
    for (auto &kv : calls) CHECK(kv.second <= 1);
}
TEST_CASE("destroyWithInflight") {
    REQUIRE_BACKEND();
    yt::conformance::Loopback srv;
    srv.route("/youtubei/v1/browse", yt::conformance::Canned{200, "{}", {}, 300});
    std::atomic<int> calls{0};
    {
        yt::ClientOptions o;
        auto c = yt::testing::makeClientForTesting(o, srv.url("/youtubei/v1/"));
        for (int i = 0; i < 5; ++i)
            c->browse({"FE" + std::to_string(i), "", ""}, [&](const yt::Result<yt::VideoPage> &) { ++calls; });
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }   // ~Client while all five are in flight
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    CHECK(calls == 0);
}
TEST_CASE("queued deliveries are dropped once the client is gone") {
    auto f = std::make_shared<FakeHttp>(); f->queue("browse", "{}");
    QueueExec ex;
    yt::ClientOptions o; o.deliver = ex.exec();
    int calls = 0;
    {
        auto c = mk(f, o);
        c->browse({"FEx", "", ""}, [&](const yt::Result<yt::VideoPage> &) { ++calls; });
        c->runOnce(0); f->flush();
        REQUIRE(ex.size() == 1);
    }
    ex.run();
    CHECK(calls == 0);
}
TEST_CASE("every endpoint reaches its chain") {
    auto f = std::make_shared<FakeHttp>();
    auto c = mk(f);
    auto nop = [](auto &&) {};
    c->search({"q", "date"}, nop);
    c->watch("v", nop);
    c->comments("v", "", nop);
    c->player("v", [](const yt::PlayerInfo &) {});
    c->channel("UC", nop);
    c->channelByUrl("https://youtube.com/@x", nop);
    c->channelSubscribed("UC", nop);
    c->channelList("FEchannels", "", nop);
    c->searchChannels("q", nop);
    c->playlists("UC", "", "", nop);
    c->searchPlaylists("q", nop);
    c->account(nop);
    c->action(yt::Action::Like, "v", nop);
    c->postComment("P", "t", nop);
    c->editPlaylist("WL", true, "v", nop);
    c->votes("v", nop);
    c->suggestions("q", nop);
    c->runOnce(0);
    for (const char *ep : {"search", "next", "player", "browse", "navigation/resolve_url", "account/accounts_list",
                           "like/like", "comment/create_comment", "browse/edit_playlist"})
        CHECK_MESSAGE(f->lastClientFor(ep) != -1, ep);
}

// Final review C1: a callback destroying the Client in manual mode ran teardown inline,
// freeing the policy layer while it was still delivering to a coalesced waiter
// (watch + comments both POST the same /next).
TEST_CASE("destroying a manual-mode client from a callback is safe") {
    REQUIRE_BACKEND();
    yt::conformance::Loopback srv;
    srv.route("/youtubei/v1/next", yt::conformance::Canned{200,
        "{\"engagementPanels\":[{\"x\":{\"continuationCommand\":{\"token\":\"TOK\"}}}]}"});
    yt::ClientOptions o; o.ownThread = false;
    std::unique_ptr<yt::Client> c = yt::testing::makeClientForTesting(o, srv.url("/youtubei/v1/"));
    bool gone = false;
    int comments = 0;
    c->watch("vid", [&](const yt::Result<yt::WatchResult> &) { c.reset(); gone = true; });
    c->comments("vid", "", [&](const yt::Result<yt::CommentPage> &) { ++comments; });
    yt::Client *raw = c.get();
    for (int i = 0; i < 200 && !gone; ++i) raw->runOnce(10);
    CHECK(gone);
    CHECK(comments == 0);                 // nothing after the destructor
}
// Final review C2: Job::cancel() on another thread during ~Client posted to the loop
// and woke a driver (curl multi) that teardown was destroying.
TEST_CASE("cancel racing destruction") {
    REQUIRE_BACKEND();
    yt::conformance::Loopback srv;
    srv.route("/youtubei/v1/browse", yt::conformance::Canned{200, "{}", {}, 200});
    for (int it = 0; it < 100; ++it) {
        yt::ClientOptions o;
        auto c = yt::testing::makeClientForTesting(o, srv.url("/youtubei/v1/"));
        yt::Job j = c->browse({"FE", "", ""}, [](const yt::Result<yt::VideoPage> &) {});
        std::atomic<bool> stop{false};
        std::thread t([&] { while (!stop) j.cancel(); });
        c.reset();
        stop = true;
        t.join();
    }
    CHECK(true);
}
