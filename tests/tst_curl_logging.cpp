// Security review: the curl backend's verbose trace must never carry credentials
// (Authorization / Cookie / Set-Cookie / Proxy-Authorization values).
#include <doctest/doctest.h>
#include "conformance/loopback.h"
#include "core/log.h"
#include <yt/transport.h>
#include <atomic>
#include <mutex>
#include <thread>

namespace {
struct Done : yt::HttpResponseSink {
    std::atomic<bool> done{false};
    void onHead(int, std::vector<yt::HttpHeader>) override {}
    bool onBody(const char *, size_t) override { return true; }
    void onDone(yt::TransportStatus) override { done = true; }
};
}

TEST_CASE("curl trace redacts credentials") {
    yt::conformance::Loopback srv;
    srv.route("/a", yt::conformance::Canned{200, "{}", {"Set-Cookie: SESSION=srvsecret"}});
    std::mutex mx;
    std::string trace;
    yt::TransportConfig cfg;
    cfg.log = [&](yt::LogLevel, std::string_view, std::string_view m) {
        std::lock_guard<std::mutex> l(mx); trace.append(m).append("\n");
    };
    std::unique_ptr<yt::HttpTransport> t = yt::makeDefaultTransport(cfg);
    auto sink = std::make_shared<Done>();
    yt::HttpRequest req;
    req.url = srv.url("/a");
    req.headers = {{"Authorization", "Bearer tok-SECRET"}, {"Cookie", "SOCS=cookieSECRET"}, {"X-Plain", "visible"}};
    t->start(req, sink);
    for (int i = 0; i < 300 && !sink->done; ++i) t->driver()->wait(10);
    REQUIRE(sink->done);
    std::lock_guard<std::mutex> l(mx);
    CHECK(trace.find("X-Plain: visible") != std::string::npos);    // tracing is on
    CHECK(trace.find("tok-SECRET") == std::string::npos);
    CHECK(trace.find("cookieSECRET") == std::string::npos);
    CHECK(trace.find("srvsecret") == std::string::npos);
    CHECK(trace.find("Authorization:") != std::string::npos);      // the header name may stay
}
