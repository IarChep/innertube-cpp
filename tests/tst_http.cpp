// net::HttpClient — the transport-agnostic policy layer, driven over FakeTransport
// (no sockets): envelope ladder, coalescing, TTL cache, visitorData capture, and the
// security policy every backend inherits (schemes, redirects, body cap, timeouts,
// cancellation and late/foreign-thread callbacks).
#include <doctest/doctest.h>
#include "net/httpclient.h"
#include "faketransport.h"
#include <thread>

using yt::ClientId;
using yt::ErrorCode;
using yt::core::Reply;
using yt::net::HttpClient;

namespace {
struct Got {
    int calls = 0;
    Reply r;
    bool onLoop = false;
};
yt::core::HttpFn rec(Got &g, yt::net::Loop *loop = nullptr) {
    return [&g, loop](const Reply &r) { ++g.calls; g.r = r; if (loop) g.onLoop = loop->onLoopThread(); };
}
FakeExchange ok(const std::string &body, int status = 200) {
    FakeExchange x; x.status = status; x.chunks = {body}; return x;
}
FakeExchange redirect(int status, const std::string &location) {
    FakeExchange x; x.status = status; x.headers = {{"Location", location}}; return x;
}
const yt::HttpHeader *header(const yt::HttpRequest &r, const char *name) {
    for (const auto &h : r.headers) if (h.name == name) return &h;
    return nullptr;
}
struct Fx {
    yt::net::Loop loop;
    FakeTransport ft;
    yt::net::HttpOptions o;
    std::unique_ptr<HttpClient> http;
    explicit Fx(FakeTransport::Mode m = FakeTransport::Mode::Inline) : ft(m) { o.baseUrl = "https://yt.test/youtubei/v1/"; }
    HttpClient &h() { if (!http) http = std::make_unique<HttpClient>(loop, ft, o); return *http; }
    void pump() { for (int i = 0; i < 12; ++i) { loop.runOnce(0); ft.complete(); } loop.runOnce(0); }
    void runFor(int ms, const std::function<bool()> &until) {
        auto end = yt::net::Loop::Clock::now() + std::chrono::milliseconds(ms);
        while (!until() && yt::net::Loop::Clock::now() < end) { loop.runOnce(5); ft.complete(); }
    }
};
const char *kBrowse = "https://yt.test/youtubei/v1/browse";
}

TEST_CASE("postSplicesContext") {
    Fx f; f.ft.script(kBrowse, ok("{\"contents\":{}}"));
    Got g;
    f.h().post("browse", ClientId::WEB, "{\"browseId\":\"FEx\"}", yt::core::newJob(), rec(g));
    f.pump();
    CHECK(g.calls == 1);
    CHECK(g.r.ok);
    REQUIRE(f.ft.started.size() == 1);
    const yt::HttpRequest &q = f.ft.started[0];
    CHECK(q.method == yt::HttpRequest::Method::Post);
    CHECK(q.url == "https://yt.test/youtubei/v1/browse?prettyPrint=false");
    CHECK(q.body.rfind("{\"context\":{\"client\":{\"clientName\":\"WEB\"", 0) == 0);
    CHECK(q.body.find("\"browseId\":\"FEx\"") != std::string::npos);
    REQUIRE(header(q, "X-YouTube-Client-Name") != nullptr);
    CHECK(header(q, "X-YouTube-Client-Name")->value == "1");
}
TEST_CASE("emptyBodyObjectSplicesToContextOnly") {
    Fx f; f.ft.script("https://yt.test/youtubei/v1/account", ok("{}"));
    Got g;
    f.h().post("account/accounts_list", ClientId::TVHTML5, "{}", yt::core::newJob(), rec(g));
    f.pump();
    REQUIRE(f.ft.started.size() == 1);
    const std::string &b = f.ft.started[0].body;
    CHECK(b.rfind("{\"context\":{", 0) == 0);
    CHECK(b.back() == '}');
    CHECK(b.find("},}") == std::string::npos);
}
TEST_CASE("neverReentrant") {
    Fx f; f.ft.script(kBrowse, ok("{}"));
    Got g;
    f.h().post("browse", ClientId::WEB, "{\"browseId\":\"A\"}", yt::core::newJob(), rec(g));
    CHECK(g.calls == 0);
    f.ft.complete();                 // transport finished, but the delivery is posted
    CHECK(g.calls == 0);
    f.pump();
    CHECK(g.calls == 1);
}
TEST_CASE("coalesces") {
    Fx f; f.ft.script(kBrowse, ok("{\"x\":1}"));
    Got a, b;
    f.h().post("browse", ClientId::WEB, "{\"browseId\":\"A\"}", yt::core::newJob(), rec(a));
    f.h().post("browse", ClientId::WEB, "{\"browseId\":\"A\"}", yt::core::newJob(), rec(b));
    f.pump();
    CHECK(f.ft.started.size() == 1);
    CHECK(a.calls == 1); CHECK(b.calls == 1);
    CHECK(a.r.body.get() == b.r.body.get());
}
TEST_CASE("ttlCache") {
    Fx f; f.ft.script(kBrowse, ok("{\"x\":1}"));
    Got a, b;
    f.h().post("browse", ClientId::WEB, "{\"browseId\":\"A\"}", yt::core::newJob(), rec(a));
    f.pump();
    f.h().post("browse", ClientId::WEB, "{\"browseId\":\"A\"}", yt::core::newJob(), rec(b));
    CHECK(b.calls == 0);             // cache hit is still asynchronous
    f.pump();
    CHECK(b.calls == 1);
    CHECK(b.r.ok);
    CHECK(f.ft.started.size() == 1);

    f.ft.script("https://yt.test/youtubei/v1/player", ok("{}"));
    f.ft.script("https://yt.test/youtubei/v1/player", ok("{}"));
    Got p1, p2;
    f.h().post("player", ClientId::IOS, "{\"videoId\":\"v\"}", yt::core::newJob(), rec(p1));
    f.pump();
    f.h().post("player", ClientId::IOS, "{\"videoId\":\"v\"}", yt::core::newJob(), rec(p2));
    f.pump();
    CHECK(f.ft.started.size() == 3);   // player is never cached
}
TEST_CASE("apiEnvelope") {
    Fx f; f.ft.script(kBrowse, ok("{\"error\":{\"code\":400,\"message\":\"bad\"}}", 400));
    Got g;
    f.h().post("browse", ClientId::WEB, "{}", yt::core::newJob(), rec(g));
    f.pump();
    CHECK_FALSE(g.r.ok);
    CHECK(g.r.code == ErrorCode::Api);
    CHECK(g.r.error == "bad");
    CHECK(g.r.httpStatus == 400);
    CHECK_FALSE(g.r.body->empty());
}
TEST_CASE("htmlInvalid") {
    Fx f; f.ft.script(kBrowse, ok("<html>consent</html>"));
    Got g;
    f.h().post("browse", ClientId::WEB, "{}", yt::core::newJob(), rec(g));
    f.pump();
    CHECK_FALSE(g.r.ok);
    CHECK(g.r.code == ErrorCode::InvalidResponse);
}
TEST_CASE("timeout") {
    Fx f; f.o.timeoutMs = 50;
    FakeExchange x; x.hang = true; f.ft.script(kBrowse, x);
    Got g;
    f.h().post("browse", ClientId::WEB, "{}", yt::core::newJob(), rec(g));
    f.runFor(1000, [&] { return g.calls > 0; });
    CHECK(g.calls == 1);
    CHECK(g.r.code == ErrorCode::Timeout);
    CHECK(g.r.error == "request timed out");
    CHECK(f.ft.canceled.size() == 1);
    f.pump();
    CHECK(g.calls == 1);             // the backend's late onDone(Canceled) is ignored
}
TEST_CASE("partialBodyIsErrorAndUncached") {
    Fx f;
    for (int i = 0; i < 2; ++i) {
        FakeExchange x; x.chunks = {"{\"contents\""}; x.end = {yt::TransportError::Reset, "partial"};
        f.ft.script(kBrowse, x);
    }
    Got a, b;
    f.h().post("browse", ClientId::WEB, "{}", yt::core::newJob(), rec(a));
    f.pump();
    CHECK_FALSE(a.r.ok);
    CHECK(a.r.code == ErrorCode::Transport);
    CHECK(a.r.error == "partial");
    f.h().post("browse", ClientId::WEB, "{}", yt::core::newJob(), rec(b));
    f.pump();
    CHECK(f.ft.started.size() == 2);
}
TEST_CASE("visitorDataAdopted") {
    Fx f;
    f.ft.script(kBrowse, ok("{\"responseContext\":{\"visitorData\":\"V1\"}}"));
    f.ft.script(kBrowse, ok("{}"));
    std::vector<std::string> sunk;
    f.h().setVisitorSink([&](const std::string &v) { sunk.push_back(v); });
    Got a, b;
    f.h().post("browse", ClientId::WEB, "{\"browseId\":\"A\"}", yt::core::newJob(), rec(a));
    f.pump();
    CHECK(a.r.ok);
    CHECK(a.r.visitorData == "V1");
    CHECK(sunk == std::vector<std::string>{"V1"});
    CHECK(f.h().session().visitorData == "V1");
    f.h().post("browse", ClientId::WEB, "{\"browseId\":\"B\"}", yt::core::newJob(), rec(b));
    f.pump();
    REQUIRE(f.ft.started.size() == 2);
    CHECK(f.ft.started[1].body.find("\"visitorData\":\"V1\"") != std::string::npos);
    REQUIRE(header(f.ft.started[1], "X-Goog-Visitor-Id") != nullptr);
    CHECK(header(f.ft.started[1], "X-Goog-Visitor-Id")->value == "V1");
}
TEST_CASE("credentialedRedirectNotFollowed") {
    Fx f; f.ft.script(kBrowse, redirect(302, "https://evil.test/"));
    f.h().setBearer("T");
    Got g;
    f.h().post("browse", ClientId::TVHTML5, "{}", yt::core::newJob(), rec(g));
    f.pump();
    REQUIRE(f.ft.started.size() == 1);
    REQUIRE(header(f.ft.started[0], "Authorization") != nullptr);
    CHECK_FALSE(g.r.ok);
    CHECK(g.r.httpStatus == 302);
}
TEST_CASE("redirectFollowedRelative") {
    Fx f;
    f.ft.script("https://a.test/x", redirect(302, "/y"));
    f.ft.script("https://a.test/y", ok("{\"ok\":1}"));
    Got g;
    f.h().get("https://a.test/x", yt::core::newJob(), rec(g));
    f.pump();
    REQUIRE(f.ft.started.size() == 2);
    CHECK(f.ft.started[1].url == "https://a.test/y");
    CHECK(g.calls == 1);
    CHECK(g.r.ok);
    CHECK(*g.r.body == "{\"ok\":1}");
}
// Final review #5: form posts carry client_secret / refresh_token in the BODY, which a
// 307/308 would replay to the Location's origin — forms never follow redirects.
TEST_CASE("formPostsNeverFollowRedirects") {
    for (int status : {301, 302, 303, 307, 308}) {
        CAPTURE(status);
        Fx f;
        f.ft.script("https://a.test/f", redirect(status, "https://elsewhere.test/collect"));
        Got g;
        f.h().postForm("https://a.test/f", {{"refresh_token", "SECRET"}}, yt::core::newJob(), rec(g));
        f.pump();
        CHECK(f.ft.started.size() == 1);
        CHECK_FALSE(g.r.ok);
        CHECK(g.r.httpStatus == status);
    }
}
TEST_CASE("redirectToFileRefused") {
    Fx f; f.ft.script("https://a.test/x", redirect(302, "file:///etc/passwd"));
    Got g;
    f.h().get("https://a.test/x", yt::core::newJob(), rec(g));
    f.pump();
    CHECK(f.ft.started.size() == 1);
    CHECK_FALSE(g.r.ok);
    CHECK(g.r.code == ErrorCode::Transport);
}
TEST_CASE("tooManyRedirects") {
    Fx f;
    for (int i = 0; i < 6; ++i) f.ft.script("https://a.test/r", redirect(302, "/r"));
    Got g;
    f.h().get("https://a.test/r", yt::core::newJob(), rec(g));
    f.pump();
    CHECK(f.ft.started.size() == 6);
    CHECK_FALSE(g.r.ok);
    CHECK(g.r.error == "too many redirects");
}
TEST_CASE("schemeRefused") {
    Fx f;
    Got g;
    f.h().get("file:///etc/hostname", yt::core::newJob(), rec(g));
    CHECK(g.calls == 0);
    f.pump();
    CHECK(f.ft.started.empty());
    CHECK(g.calls == 1);
    CHECK_FALSE(g.r.ok);
    CHECK(g.r.code == ErrorCode::Transport);
}
TEST_CASE("bodyCap") {
    Fx f; f.o.maxBodyBytes = 1024;
    f.ft.script("https://a.test/big", ok(std::string(4096, 'a')));
    Got g;
    f.h().get("https://a.test/big", yt::core::newJob(), rec(g));
    f.pump();
    CHECK_FALSE(g.r.ok);
    CHECK(g.r.error == "response too large");
}
TEST_CASE("getPreservesPercentEscapes") {
    Fx f;
    Got g;
    f.h().get("https://v.test/v?sig=a%2Cb%3D", yt::core::newJob(), rec(g));
    f.pump();
    REQUIRE(f.ft.started.size() == 1);
    CHECK(f.ft.started[0].url == "https://v.test/v?sig=a%2Cb%3D");
}
TEST_CASE("foreignThreadCallbacksLandOnLoop") {
    Fx f(FakeTransport::Mode::Threaded);
    f.ft.script("https://a.test/", ok("{}"));
    Got g;
    f.h().get("https://a.test/", yt::core::newJob(), rec(g, &f.loop));
    f.runFor(2000, [&] { return g.calls > 0; });
    CHECK(g.calls == 1);
    CHECK(g.onLoop);
}
TEST_CASE("abortDropsCallbacks") {
    Fx f;
    FakeExchange x; x.hang = true; f.ft.script(kBrowse, x);
    Got g;
    yt::core::JobToken job = yt::core::newJob();
    f.h().post("browse", ClientId::WEB, "{}", job, rec(g));
    f.pump();
    job->canceled = true;
    f.h().abort(job);
    f.pump();
    CHECK(g.calls == 0);
    CHECK(f.ft.canceled.size() == 1);
}
TEST_CASE("abortKeepsOtherCoalescedWaiter") {
    Fx f; f.ft.script(kBrowse, ok("{}"));
    Got a, b;
    yt::core::JobToken ja = yt::core::newJob();
    f.h().post("browse", ClientId::WEB, "{}", ja, rec(a));
    f.h().post("browse", ClientId::WEB, "{}", yt::core::newJob(), rec(b));
    ja->canceled = true;
    f.h().abort(ja);
    f.pump();
    CHECK(a.calls == 0);
    CHECK(b.calls == 1);
    CHECK(f.ft.canceled.empty());
}
TEST_CASE("lateCallbacksAfterDestroyIgnored") {
    Fx f(FakeTransport::Mode::Threaded);
    FakeExchange x; x.hang = true; f.ft.script("https://a.test/", x);
    Got g;
    f.h().get("https://a.test/", yt::core::newJob(), rec(g));
    f.http.reset();                                  // cancels -> the fake answers from its thread
    for (int i = 0; i < 20; ++i) { f.loop.runOnce(5); }
    CHECK(g.calls == 0);
    CHECK(f.ft.canceled.size() == 1);
}
TEST_CASE("cachedDeliveryAfterDestroyIgnored") {
    Fx f; f.ft.script(kBrowse, ok("{}"));
    Got a, b;
    f.h().post("browse", ClientId::WEB, "{}", yt::core::newJob(), rec(a));
    f.pump();
    f.h().post("browse", ClientId::WEB, "{}", yt::core::newJob(), rec(b));   // cache hit, posted
    f.http.reset();
    f.pump();
    CHECK(b.calls == 0);
}

// Security review: server-issued values (visitorData, OAuth tokens, Location) reach
// request headers / request lines; CR/LF/NUL in them would inject headers through any
// backend. The policy layer refuses them centrally.
TEST_CASE("visitorDataWithControlCharsNotAdopted") {
    Fx f;
    f.ft.script(kBrowse, ok("{\"responseContext\":{\"visitorData\":\"V1\\r\\nX-Evil: 1\"}}"));
    f.ft.script(kBrowse, ok("{}"));
    std::vector<std::string> sunk;
    f.h().setVisitorSink([&](const std::string &v) { sunk.push_back(v); });
    Got a, b;
    f.h().post("browse", ClientId::WEB, "{\"browseId\":\"A\"}", yt::core::newJob(), rec(a));
    f.pump();
    CHECK(f.h().session().visitorData.empty());
    CHECK(sunk.empty());
    f.h().post("browse", ClientId::WEB, "{\"browseId\":\"B\"}", yt::core::newJob(), rec(b));
    f.pump();
    REQUIRE(f.ft.started.size() == 2);
    CHECK(header(f.ft.started[1], "X-Goog-Visitor-Id") == nullptr);
}
TEST_CASE("headerValueWithCrLfRefused") {
    Fx f;
    f.h().setBearer("T\r\nX-Evil: 1");
    Got g;
    f.h().post("browse", ClientId::TVHTML5, "{}", yt::core::newJob(), rec(g));
    f.pump();
    CHECK(f.ft.started.empty());
    CHECK(g.calls == 1);
    CHECK_FALSE(g.r.ok);
    CHECK(g.r.code == ErrorCode::Transport);
}
TEST_CASE("urlWithControlCharsRefused") {
    Fx f;
    Got a, b;
    f.h().get(std::string("https://a.test/x\r\nX-Evil: 1"), yt::core::newJob(), rec(a));
    f.ft.script("https://a.test/r", redirect(302, "/y\r\nX-Evil: 1"));
    f.h().get("https://a.test/r", yt::core::newJob(), rec(b));
    f.pump();
    REQUIRE(f.ft.started.size() == 1);              // only the /r request went out
    CHECK(f.ft.started[0].url == "https://a.test/r");
    CHECK_FALSE(a.r.ok);
    CHECK_FALSE(b.r.ok);
    CHECK(b.r.code == ErrorCode::Transport);
    CHECK(f.loop.timerCount() == 0);                // the refused hop disarmed the timeout
}

// Security review: a response still in flight when the identity changes (clearCache on
// sign-in/sign-out) must not be served to the new identity — neither by coalescing nor
// from the cache.
TEST_CASE("responses in flight across clearCache are neither coalesced nor cached") {
    Fx f;
    f.ft.script(kBrowse, ok("{\"who\":\"A\"}"));
    f.ft.script(kBrowse, ok("{\"who\":\"B\"}"));
    Got a, b, c;
    f.h().post("browse", ClientId::WEB, "{\"browseId\":\"FEx\"}", yt::core::newJob(), rec(a));
    f.h().clearCache();                               // identity changed while A is in flight
    f.h().post("browse", ClientId::WEB, "{\"browseId\":\"FEx\"}", yt::core::newJob(), rec(b));
    f.pump();
    CHECK(f.ft.started.size() == 2);                  // B did not join A's exchange
    CHECK(*b.r.body == "{\"who\":\"B\"}");
    f.h().post("browse", ClientId::WEB, "{\"browseId\":\"FEx\"}", yt::core::newJob(), rec(c));
    f.pump();
    CHECK(f.ft.started.size() == 2);                  // served from cache...
    CHECK(*c.r.body == "{\"who\":\"B\"}");            // ...and it is B's, never A's
}

// Final review #6: only youtubei responses may seed the session's visitorData — a
// third-party GET (RYD, suggest) must not set the persisted YouTube identity.
TEST_CASE("thirdPartyGetDoesNotSetVisitorData") {
    Fx f;
    f.ft.script("https://ryd.test/votes", ok("{\"responseContext\":{\"visitorData\":\"EVIL\"}}"));
    std::vector<std::string> sunk;
    f.h().setVisitorSink([&](const std::string &v) { sunk.push_back(v); });
    Got g;
    f.h().get("https://ryd.test/votes?videoId=x", yt::core::newJob(), rec(g));
    f.pump();
    CHECK(g.r.ok);
    CHECK(f.h().session().visitorData.empty());
    CHECK(sunk.empty());
}

// A non-2xx JSON answer without an "error" member used to pass as ok (and be cached).
TEST_CASE("non2xxWithoutErrorMemberFailsAndIsNotCached") {
    Fx f;
    f.ft.script(kBrowse, ok("{\"contents\":{}}", 503));
    f.ft.script(kBrowse, ok("{\"contents\":{}}"));
    Got a, b;
    f.h().post("browse", ClientId::WEB, "{}", yt::core::newJob(), rec(a));
    f.pump();
    CHECK_FALSE(a.r.ok);
    CHECK(a.r.code == ErrorCode::Api);
    CHECK(a.r.httpStatus == 503);
    CHECK(a.r.error == "HTTP 503");
    f.h().post("browse", ClientId::WEB, "{}", yt::core::newJob(), rec(b));
    f.pump();
    CHECK(f.ft.started.size() == 2);                // not served from the cache
    CHECK(b.r.ok);
}

// A backend breaking the contract (sink calls after onDone) must not touch the hop the
// engine thread already owns.
TEST_CASE("sinkCallsAfterOnDoneAreDropped") {
    struct Rogue : yt::HttpTransport {
        std::shared_ptr<yt::HttpResponseSink> sink;
        uint64_t start(yt::HttpRequest, std::shared_ptr<yt::HttpResponseSink> s) override { sink = s; return 1; }
        void cancel(uint64_t) override {}
    } t;
    yt::net::Loop loop;
    HttpClient h(loop, t, yt::net::HttpOptions{});
    Got g;
    h.get("https://a.test/", yt::core::newJob(), rec(g));
    t.sink->onHead(200, {});
    t.sink->onBody("{}", 2);
    t.sink->onDone({});
    t.sink->onHead(500, {});
    CHECK_FALSE(t.sink->onBody("garbage", 7));
    t.sink->onDone({yt::TransportError::Reset, "late"});
    for (int i = 0; i < 3; ++i) loop.runOnce(0);
    CHECK(g.calls == 1);
    CHECK(g.r.ok);
    CHECK(g.r.httpStatus == 200);
    CHECK(*g.r.body == "{}");
}

// A request coalesced onto an older exchange gets its own full timeout.
TEST_CASE("coalescedWaiterKeepsItsOwnTimeout") {
    Fx f; f.o.timeoutMs = 300;
    FakeExchange x; x.hang = true; f.ft.script(kBrowse, x);
    Got a, b;
    f.h().post("browse", ClientId::WEB, "{}", yt::core::newJob(), rec(a));
    f.runFor(200, [] { return false; });
    f.h().post("browse", ClientId::WEB, "{}", yt::core::newJob(), rec(b));   // joins, ~100 ms left
    f.runFor(1000, [&] { return a.calls > 0; });
    CHECK(a.calls == 1);
    CHECK(a.r.code == ErrorCode::Timeout);
    CHECK(b.calls == 0);                            // still has ~200 ms of its own
    CHECK(f.ft.canceled.empty());                   // the exchange lives on for b
    f.runFor(1000, [&] { return b.calls > 0; });
    CHECK(b.calls == 1);
    CHECK(b.r.code == ErrorCode::Timeout);
    CHECK(f.ft.canceled.size() == 1);
    CHECK(f.ft.started.size() == 1);
}
