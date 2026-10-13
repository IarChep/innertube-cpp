#include "conformance/transport_conformance.h"
#include "conformance/loopback.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace yt::conformance {
namespace {

using Clock = std::chrono::steady_clock;
using Fails = std::vector<std::string>;

void fail(Fails &f, const char *check, const std::string &why) { f.push_back(std::string(check) + ": " + why); }

const char *errName(TransportError e)
{
    switch (e) {
    case TransportError::None: return "None";
    case TransportError::Resolve: return "Resolve";
    case TransportError::Connect: return "Connect";
    case TransportError::Tls: return "Tls";
    case TransportError::Reset: return "Reset";
    case TransportError::Protocol: return "Protocol";
    case TransportError::Canceled: return "Canceled";
    case TransportError::Other: return "Other";
    }
    return "?";
}

char lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }
bool iequals(const std::string &a, const char *b)
{
    const std::string bs(b);
    if (a.size() != bs.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (lower(a[i]) != lower(bs[i])) return false;
    return true;
}

// Records one exchange as the backend reports it — from any thread — and notes every
// contract violation: overlapping calls, out-of-order calls, calls after onDone or
// after the transport was destroyed.
struct Rec : HttpResponseSink {
    std::mutex mx;
    int status = -1;
    std::vector<HttpHeader> headers;
    std::string body;
    int heads = 0, dones = 0, chunks = 0;
    bool order = true;
    TransportStatus st;
    int stopAfterChunks = -1;                  // return false once this many chunks arrived
    std::atomic<int> inCall{0};
    std::atomic<bool> overlap{false};
    std::atomic<bool> afterDestroy{false};
    const std::atomic<bool> *destroyed = nullptr;

    void enter()
    {
        if (inCall.fetch_add(1) != 0) overlap = true;
        if (destroyed && destroyed->load()) afterDestroy = true;
    }
    void leave() { inCall.fetch_sub(1); }

    void onHead(int s, std::vector<HttpHeader> h) override
    {
        enter();
        {
            std::lock_guard<std::mutex> l(mx);
            if (heads || dones) order = false;
            ++heads;
            status = s;
            headers = std::move(h);
        }
        leave();
    }
    bool onBody(const char *d, size_t n) override
    {
        enter();
        bool keep = true;
        {
            std::lock_guard<std::mutex> l(mx);
            if (dones || !heads) order = false;
            body.append(d, n);
            ++chunks;
            if (stopAfterChunks >= 0 && chunks > stopAfterChunks) keep = false;
        }
        leave();
        return keep;
    }
    void onDone(TransportStatus s) override
    {
        enter();
        {
            std::lock_guard<std::mutex> l(mx);
            ++dones;
            st = std::move(s);
        }
        leave();
    }
    bool isDone() { std::lock_guard<std::mutex> l(mx); return dones > 0; }
    std::string header(const char *name)
    {
        std::lock_guard<std::mutex> l(mx);
        for (const HttpHeader &h : headers) if (iequals(h.name, name)) return h.value;
        return std::string();
    }
};

struct Harness {
    std::unique_ptr<HttpTransport> t;
    explicit Harness(const TransportFactory &make) : t(make(TransportConfig{})) {}
    void pump(int ms)
    {
        if (!t) return;
        if (LoopDriver *d = t->driver()) d->wait(ms);
        else std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
    bool waitFor(const std::function<bool()> &pred, int ms = 5000)
    {
        const auto end = Clock::now() + std::chrono::milliseconds(ms);
        while (!pred()) {
            if (Clock::now() > end) return false;
            pump(10);
        }
        return true;
    }
    uint64_t start(HttpRequest::Method m, const std::string &url, const std::shared_ptr<Rec> &r,
                   std::vector<HttpHeader> headers = {}, std::string body = {})
    {
        HttpRequest q;
        q.method = m;
        q.url = url;
        q.headers = std::move(headers);
        q.body = std::move(body);
        return t->start(std::move(q), r);
    }
};

void get200Body(const TransportFactory &make, Loopback &srv, Fails &f)
{
    const char *n = "get200Body";
    srv.route("/a", Canned{200, "hello"});
    Harness h(make);
    auto r = std::make_shared<Rec>();
    h.start(HttpRequest::Method::Get, srv.url("/a"), r);
    if (!h.waitFor([&] { return r->isDone(); })) { fail(f, n, "no onDone within 5 s"); return; }
    std::lock_guard<std::mutex> l(r->mx);
    if (r->status != 200) fail(f, n, "status " + std::to_string(r->status));
    if (r->body != "hello") fail(f, n, "body '" + r->body + "'");
    if (r->st.error != TransportError::None) fail(f, n, std::string("ended ") + errName(r->st.error));
    if (!r->order || r->heads != 1 || r->dones != 1) fail(f, n, "callback order/count wrong");
}

void postBodyAndHeaders(const TransportFactory &make, Loopback &srv, Fails &f)
{
    const char *n = "postBodyAndHeaders";
    srv.route("/p", Canned{200, "{}"});
    Harness h(make);
    auto r = std::make_shared<Rec>();
    h.start(HttpRequest::Method::Post, srv.url("/p"), r,
            {{"Content-Type", "application/json"}, {"X-Test", "1"}}, "{\"x\":1}");
    if (!h.waitFor([&] { return r->isDone(); })) { fail(f, n, "no onDone within 5 s"); return; }
    const std::string req = srv.lastRequest("/p");
    if (req.rfind("POST ", 0) != 0) fail(f, n, "not sent as POST");
    if (req.find("{\"x\":1}") == std::string::npos) fail(f, n, "body missing");
    if (req.find("application/json") == std::string::npos) fail(f, n, "Content-Type missing");
    if (req.find("X-Test: 1") == std::string::npos && req.find("x-test: 1") == std::string::npos)
        fail(f, n, "custom header missing");
}

void responseHeaders(const TransportFactory &make, Loopback &srv, Fails &f)
{
    const char *n = "responseHeaders";
    srv.route("/h", Canned{200, "", {"X-Reply: v"}});
    Harness h(make);
    auto r = std::make_shared<Rec>();
    h.start(HttpRequest::Method::Get, srv.url("/h"), r);
    if (!h.waitFor([&] { return r->isDone(); })) { fail(f, n, "no onDone within 5 s"); return; }
    if (r->header("x-reply") != "v") fail(f, n, "X-Reply not delivered");
}

void noRedirectFollow(const TransportFactory &make, Loopback &srv, Fails &f)
{
    const char *n = "noRedirectFollow";
    srv.route("/r", Canned{302, "", {"Location: /t"}});
    srv.route("/t", Canned{200, "followed"});
    Harness h(make);
    auto r = std::make_shared<Rec>();
    h.start(HttpRequest::Method::Get, srv.url("/r"), r);
    if (!h.waitFor([&] { return r->isDone(); })) { fail(f, n, "no onDone within 5 s"); return; }
    std::lock_guard<std::mutex> l(r->mx);
    if (r->status != 302) fail(f, n, "status " + std::to_string(r->status) + " (redirect followed?)");
    if (srv.hits("/t") != 0) fail(f, n, "backend followed the redirect itself");
}

void urlByteExact(const TransportFactory &make, Loopback &srv, Fails &f)
{
    const char *n = "urlByteExact";
    srv.route("/v", Canned{200, "{}"});
    Harness h(make);
    auto r = std::make_shared<Rec>();
    h.start(HttpRequest::Method::Get, srv.url("/v?sig=a%2Cb%3D"), r);
    if (!h.waitFor([&] { return r->isDone(); })) { fail(f, n, "no onDone within 5 s"); return; }
    const std::string req = srv.lastRequest("/v");
    if (req.find("/v?sig=a%2Cb%3D ") == std::string::npos) fail(f, n, "request line altered: " + req.substr(0, req.find('\r')));
}

std::string pattern(size_t n)
{
    std::string s(n, '\0');
    for (size_t i = 0; i < n; ++i) s[i] = char(i % 251);
    return s;
}

void largeBodyInOrder(const TransportFactory &make, Loopback &srv, Fails &f)
{
    const char *n = "largeBodyInOrder";
    srv.route("/big", Canned{200, pattern(1 << 20)});
    Harness h(make);
    auto r = std::make_shared<Rec>();
    h.start(HttpRequest::Method::Get, srv.url("/big"), r);
    if (!h.waitFor([&] { return r->isDone(); }, 10000)) { fail(f, n, "no onDone within 10 s"); return; }
    std::lock_guard<std::mutex> l(r->mx);
    if (r->body != pattern(1 << 20)) fail(f, n, "body differs (" + std::to_string(r->body.size()) + " bytes)");
}

void onBodyFalseAborts(const TransportFactory &make, Loopback &srv, Fails &f)
{
    const char *n = "onBodyFalseAborts";
    srv.route("/big2", Canned{200, pattern(4 << 20)});
    Harness h(make);
    auto r = std::make_shared<Rec>();
    r->stopAfterChunks = 0;                                  // refuse the first chunk
    h.start(HttpRequest::Method::Get, srv.url("/big2"), r);
    if (!h.waitFor([&] { return r->isDone(); }, 10000)) { fail(f, n, "no onDone after onBody returned false"); return; }
    h.pump(50);
    std::lock_guard<std::mutex> l(r->mx);
    if (r->chunks != 1) fail(f, n, "onBody called " + std::to_string(r->chunks) + " times after refusing");
    if (r->st.error != TransportError::Canceled) fail(f, n, std::string("ended ") + errName(r->st.error) + ", expected Canceled");
}

void cancelMidTransfer(const TransportFactory &make, Loopback &srv, Fails &f)
{
    const char *n = "cancelMidTransfer";
    srv.route("/slow", Canned{200, "late", {}, 500});
    Harness h(make);
    auto r = std::make_shared<Rec>();
    const uint64_t id = h.start(HttpRequest::Method::Get, srv.url("/slow"), r);
    const auto until = Clock::now() + std::chrono::milliseconds(50);
    while (Clock::now() < until) h.pump(10);
    h.t->cancel(id);
    const auto end = Clock::now() + std::chrono::milliseconds(1000);
    while (Clock::now() < end) h.pump(10);
    std::lock_guard<std::mutex> l(r->mx);
    if (r->dones > 1) fail(f, n, "onDone called " + std::to_string(r->dones) + " times");
    if (!r->order) fail(f, n, "callbacks after onDone / out of order");
    if (r->body == "late") fail(f, n, "body delivered after cancel()");
}

void connectRefused(const TransportFactory &make, Loopback &, Fails &f)
{
    const char *n = "connectRefused";
    Harness h(make);
    auto r = std::make_shared<Rec>();
    h.start(HttpRequest::Method::Get, "http://127.0.0.1:" + std::to_string(Loopback::closedPort()) + "/", r);
    if (!h.waitFor([&] { return r->isDone(); })) { fail(f, n, "no onDone within 5 s"); return; }
    std::lock_guard<std::mutex> l(r->mx);
    if (r->st.error != TransportError::Connect) fail(f, n, std::string("ended ") + errName(r->st.error) + ", expected Connect");
    if (r->heads) fail(f, n, "onHead without a response");
}

void unresolvable(const TransportFactory &make, Loopback &, Fails &f)
{
    const char *n = "unresolvable";
    Harness h(make);
    auto r = std::make_shared<Rec>();
    h.start(HttpRequest::Method::Get, "http://nonexistent.invalid/", r);
    if (!h.waitFor([&] { return r->isDone(); }, 15000)) { fail(f, n, "no onDone within 15 s"); return; }
    std::lock_guard<std::mutex> l(r->mx);
    if (r->st.error != TransportError::Resolve) fail(f, n, std::string("ended ") + errName(r->st.error) + ", expected Resolve");
}

void truncatedBody(const TransportFactory &make, Loopback &srv, Fails &f)
{
    const char *n = "truncatedBody";
    srv.route("/trunc", Canned{200, "0123456789", {}, 0, 100});
    Harness h(make);
    auto r = std::make_shared<Rec>();
    h.start(HttpRequest::Method::Get, srv.url("/trunc"), r);
    if (!h.waitFor([&] { return r->isDone(); })) { fail(f, n, "no onDone within 5 s"); return; }
    std::lock_guard<std::mutex> l(r->mx);
    if (r->st.error == TransportError::None) fail(f, n, "a truncated body was reported as success");
}

void noExpectHeader(const TransportFactory &make, Loopback &srv, Fails &f)
{
    const char *n = "noExpectHeader";
    srv.route("/e", Canned{200, "{}"});
    Harness h(make);
    auto r = std::make_shared<Rec>();
    h.start(HttpRequest::Method::Post, srv.url("/e"), r, {{"Content-Type", "application/json"}}, std::string(2 << 20, 'x'));
    if (!h.waitFor([&] { return r->isDone(); })) { fail(f, n, "no onDone within 5 s"); return; }
    const std::string req = srv.lastRequest("/e");
    if (req.find("Expect:") != std::string::npos || req.find("expect:") != std::string::npos)
        fail(f, n, "sent an Expect header");
    if (req.find(std::string(2 << 20, 'x')) == std::string::npos) fail(f, n, "body missing");
}

void concurrent(const TransportFactory &make, Loopback &srv, Fails &f)
{
    const int kN = 32;
    for (int i = 0; i < kN; ++i) srv.route("/c" + std::to_string(i), Canned{200, "b" + std::to_string(i)});
    Harness h(make);
    std::vector<std::shared_ptr<Rec>> rs;
    for (int i = 0; i < kN; ++i) {
        rs.push_back(std::make_shared<Rec>());
        h.start(HttpRequest::Method::Get, srv.url("/c" + std::to_string(i)), rs.back());
    }
    const bool all = h.waitFor([&] { for (auto &r : rs) if (!r->isDone()) return false; return true; }, 15000);
    if (!all) fail(f, "concurrent32", "not every request completed within 15 s");
    for (int i = 0; i < kN; ++i) {
        std::lock_guard<std::mutex> l(rs[i]->mx);
        if (all && rs[i]->body != "b" + std::to_string(i)) fail(f, "concurrent32", "request " + std::to_string(i) + " got '" + rs[i]->body + "'");
        if (rs[i]->overlap) fail(f, "serializedCallbacks", "overlapping sink calls for one request");
        if (!rs[i]->order || rs[i]->dones > 1) fail(f, "serializedCallbacks", "out-of-order or repeated sink calls");
    }
}

void noCallbackAfterDestroy(const TransportFactory &make, Loopback &srv, Fails &f)
{
    const char *n = "noCallbackAfterDestroy";
    srv.route("/slow2", Canned{200, "late", {}, 300});
    std::atomic<bool> destroyed{false};
    auto r = std::make_shared<Rec>();
    r->destroyed = &destroyed;
    {
        Harness h(make);
        h.start(HttpRequest::Method::Get, srv.url("/slow2"), r);
        h.pump(20);
        h.t.reset();
        destroyed = true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    if (r->afterDestroy) fail(f, n, "sink called after the transport's destructor returned");
}

} // namespace

std::vector<std::string> runTransportConformance(const TransportFactory &make)
{
    Fails f;
    {
        Harness probe(make);
        if (!probe.t) { f.push_back("factory: returned no transport"); return f; }
    }
    Loopback srv;
    get200Body(make, srv, f);
    postBodyAndHeaders(make, srv, f);
    responseHeaders(make, srv, f);
    noRedirectFollow(make, srv, f);
    urlByteExact(make, srv, f);
    largeBodyInOrder(make, srv, f);
    onBodyFalseAborts(make, srv, f);
    cancelMidTransfer(make, srv, f);
    connectRefused(make, srv, f);
    unresolvable(make, srv, f);
    truncatedBody(make, srv, f);
    noExpectHeader(make, srv, f);
    concurrent(make, srv, f);
    noCallbackAfterDestroy(make, srv, f);
    return f;
}

}
