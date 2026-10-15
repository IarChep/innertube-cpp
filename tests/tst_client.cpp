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
#include <chrono>
#include <future>
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
    yt::Result<yt::Page<yt::Video>> got; bool called = false;
    yt::Job j = c->feeds().home([&](const yt::Result<yt::Page<yt::Video>> &r) { got = r; called = true; });
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
    yt::Job j = c->feeds().topic("FEx", [&](const yt::Result<yt::Page<yt::Video>> &) { ++calls; });
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
    c->feeds().topic("FEx", [&](const yt::Result<yt::Page<yt::Video>> &) { ++calls; });
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
    c.feeds().topic("FEx", [](const yt::Result<yt::Page<yt::Video>> &) {});
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
    c.feeds().topic("FEx", [&](const yt::Result<yt::Page<yt::Video>> &r) { called = true; CHECK(r.ok()); });
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
        jobs.push_back(c->feeds().topic("FE" + std::to_string(i),
            [&, i](const yt::Result<yt::Page<yt::Video>> &) { std::lock_guard<std::mutex> l(mx); ++calls[i]; }));
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
            c->feeds().topic("FE" + std::to_string(i), [&](const yt::Result<yt::Page<yt::Video>> &) { ++calls; });
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
        c->feeds().topic("FEx", [&](const yt::Result<yt::Page<yt::Video>> &) { ++calls; });
        c->runOnce(0); f->flush();
        REQUIRE(ex.size() == 1);
    }
    ex.run();
    CHECK(calls == 0);
}
TEST_CASE("every group method reaches its chain") {
    auto f = std::make_shared<FakeHttp>();
    auto c = mk(f);
    auto nop = [](auto &&) {};
    const std::string v = "https://youtu.be/dQw4w9WgXcQ", ch = "UCuAXFkgsw1L7xaCfnd5JJOw", pl = "PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI";
    c->videos().get(v, nop);
    c->videos().player(v, nop);
    c->videos().player(v, yt::WantHls, nop);
    c->videos().captions(yt::CaptionTrack{"https://y/t", "en", "English"}, nop);
    c->videos().votes(v, nop);
    c->search().videos("q", nop);
    c->search().channels("q", nop);
    c->search().playlists("q", nop);
    c->search().suggestions("q", nop);
    c->channels().get("@NASA", nop);
    c->channels().videos(ch, nop);
    c->channels().playlists(ch, nop);
    c->playlists().videos(pl, nop);
    c->feeds().home(nop);
    c->feeds().topic("UC-9-kyTW8ZkZNDHQJ6FgpwQ", nop);
    c->comments().list(v, nop);
    c->comments().post(v, "t", nop);
    c->account().info(nop);
    c->account().rate(v, yt::Rating::Like, nop);
    c->account().subscribe(ch, nop);
    c->account().save(v, pl, nop);
    c->runOnce(0);
    for (const char *ep : {"search", "next", "player", "browse", "navigation/resolve_url", "account/accounts_list",
                           "like/like", "subscription/subscribe", "browse/edit_playlist"})
        CHECK_MESSAGE(f->lastClientFor(ep) != -1, ep);
    CHECK_FALSE(f->fetched.empty());                  // captions
    CHECK_FALSE(f->lastGetUrl().empty());             // votes (RYD) and suggestions are plain GETs
}
// Review focus: a link of the wrong kind fails asynchronously, with no request.
TEST_CASE("a link of the wrong kind fails with InvalidArgument, never inline") {
    auto f = std::make_shared<FakeHttp>();
    auto c = mk(f);
    std::vector<yt::ErrorCode> codes;
    auto rec = [&](const auto &r) { codes.push_back(r.ok() ? yt::ErrorCode::Canceled : r.error->code); };
    c->videos().get("https://example.com/watch?v=dQw4w9WgXcQ", rec);
    c->playlists().videos("not a playlist", rec);
    c->account().save("dQw4w9WgXcQ", "dQw4w9WgXcQ", rec);       // a video is not a playlist
    c->channels().videos("https://youtu.be/dQw4w9WgXcQ", rec);
    CHECK(codes.empty());                             // never from inside the call
    for (int i = 0; i < 4; ++i) { c->runOnce(0); f->flush(); }
    REQUIRE(codes.size() == 4);
    for (yt::ErrorCode code : codes) CHECK(code == yt::ErrorCode::InvalidArgument);
    CHECK(f->sent.empty());
}
// Spec S4: a private feed signed out is AuthRequired, not an anonymous request that comes
// back as an empty page.
TEST_CASE("a feed that needs sign-in fails with AuthRequired when signed out, without a request") {
    auto f = std::make_shared<FakeHttp>();
    auto c = mk(f);
    std::vector<yt::ErrorCode> codes;
    auto rec = [&](const auto &r) { codes.push_back(r.ok() ? yt::ErrorCode::Canceled : r.error->code); };
    c->feeds().subscriptions(rec);
    c->feeds().history(rec);
    c->playlists().videos("WL", rec);
    c->account().playlists(rec);
    c->account().subscriptions(rec);
    for (int i = 0; i < 4; ++i) { c->runOnce(0); f->flush(); }
    REQUIRE(codes.size() == 5);
    for (yt::ErrorCode code : codes) CHECK(code == yt::ErrorCode::AuthRequired);
    CHECK(f->sent.empty());
}
TEST_CASE("Client::more continues a page") {
    auto f = std::make_shared<FakeHttp>();
    f->queue("browse", loadFixtureRaw("browse_feed.json"));
    f->queue("browse", loadFixtureRaw("browse_feed.json"));
    auto c = mk(f);
    yt::Result<yt::Page<yt::Video>> first, second;
    c->feeds().home([&](const yt::Result<yt::Page<yt::Video>> &r) { first = r; });
    for (int i = 0; i < 4; ++i) { c->runOnce(0); f->flush(); }
    REQUIRE(first.ok());
    REQUIRE(first.value.next);
    c->more(first.value.next, [&](const yt::Result<yt::Page<yt::Video>> &r) { second = r; });
    for (int i = 0; i < 4; ++i) { c->runOnce(0); f->flush(); }
    REQUIRE(second.ok());
    CHECK(f->sent.back() == "{\"continuation\":\"FEEDNEXT\"}");
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
    c->videos().get("vid11111111", [&](const yt::Result<yt::WatchResult> &) { c.reset(); gone = true; });
    c->comments().list("vid11111111", [&](const yt::Result<yt::Page<yt::Comment>> &) { ++comments; });
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
        yt::Job j = c->feeds().topic("FE", [](const yt::Result<yt::Page<yt::Video>> &) {});
        std::atomic<bool> stop{false};
        std::thread t([&] { while (!stop) j.cancel(); });
        c.reset();
        stop = true;
        t.join();
    }
    CHECK(true);
}

// Job::active() used to stay true for a job the destroyed Client will never deliver.
TEST_CASE("a job is inactive once its client is gone") {
    auto f = std::make_shared<FakeHttp>();
    f->queue("browse", "{}");
    auto c = mk(f, [] { yt::ClientOptions o; o.ownThread = false; return o; }());
    yt::Job j = c->feeds().topic("FEx", [](const yt::Result<yt::Page<yt::Video>> &) {});
    c->runOnce(0);
    CHECK(j.active());
    c.reset();
    CHECK_FALSE(j.active());
}
namespace {
template <class F> bool ready(F &f) { return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }
}

TEST_CASE("the future form delivers the result") {
    auto f = std::make_shared<FakeHttp>(); f->queue("browse", loadFixtureRaw("browse_feed.json"));
    auto c = mk(f);
    std::future<yt::Result<yt::Page<yt::Video>>> home = c->feeds().home();
    CHECK_FALSE(ready(home));                         // only posted, like the callback form
    for (int i = 0; i < 8 && !ready(home); ++i) { c->runOnce(0); f->flush(); }
    REQUIRE(ready(home));
    const yt::Result<yt::Page<yt::Video>> r = home.get();
    REQUIRE(r.ok());
    CHECK(r.value.items.size() == 2);
}
// Review focus: a GUI blocks its own (delivery) thread on get(): the result must not wait
// for that executor.
TEST_CASE("the future form does not go through the executor") {
    auto f = std::make_shared<FakeHttp>(); f->queue("browse", loadFixtureRaw("browse_feed.json"));
    QueueExec ex;
    yt::ClientOptions o; o.deliver = ex.exec();
    auto c = mk(f, o);
    auto home = c->feeds().home();
    for (int i = 0; i < 8 && !ready(home); ++i) { c->runOnce(0); f->flush(); }
    CHECK(ready(home));
    CHECK(ex.size() == 0);                            // nothing waits for the executor
}
// Review focus: a future must never hang: destroying the Client releases it as Canceled.
TEST_CASE("destroying the client releases a pending future as Canceled") {
    auto f = std::make_shared<FakeHttp>();
    auto c = mk(f);
    auto home = c->feeds().home();
    c->runOnce(0);                                    // the request is in flight in FakeHttp
    c.reset();
    f.reset();                                        // the transport drops its pending replies
    REQUIRE(ready(home));
    const auto r = home.get();
    REQUIRE_FALSE(r.ok());
    CHECK(r.error->code == yt::ErrorCode::Canceled);
}
TEST_CASE("futures for links, pages and more") {
    auto f = std::make_shared<FakeHttp>();
    f->queue("browse", loadFixtureRaw("browse_feed.json"));
    f->queue("browse", loadFixtureRaw("browse_feed.json"));
    auto c = mk(f);
    auto bad = c->videos().get("not a link");
    auto first = c->playlists().videos("PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI");
    for (int i = 0; i < 8 && !(ready(bad) && ready(first)); ++i) { c->runOnce(0); f->flush(); }
    REQUIRE(ready(bad));
    CHECK(bad.get().error->code == yt::ErrorCode::InvalidArgument);
    REQUIRE(ready(first));
    const auto page = first.get();
    REQUIRE(page.ok());
    CHECK(f->sent[0] == "{\"browseId\":\"VLPLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI\"}");
    auto second = c->more(page.value.next);
    for (int i = 0; i < 8 && !ready(second); ++i) { c->runOnce(0); f->flush(); }
    REQUIRE(ready(second));
    CHECK(second.get().ok());
}
