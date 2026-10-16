// net::HttpClient — the policy layer over any yt::HttpTransport (engine thread only).
//
// Owns everything a backend must not decide: allowed schemes (http/https, also on
// every redirect hop), redirects (followed by the library, never with credentials),
// the body cap, timeouts, cancellation and late/foreign-thread callbacks. On top:
// the InnerTube transport policy carried over from MeeTube's core::Http — context
// splice, request coalescing, per-endpoint TTL cache, single envelope scan,
// visitorData capture, raw (non-JSON) fetches, memoized per-client context/headers.
#ifndef YT_NET_HTTPCLIENT_H
#define YT_NET_HTTPCLIENT_H
#include "core/ihttp.h"
#include "net/loop.h"
#include <yt/transport.h>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
namespace yt::net {

class HopSink;

struct HttpOptions {
    int timeoutMs = 20000;                    // whole logical request, all redirect hops
    int64_t maxBodyBytes = 32LL * 1024 * 1024;
    int maxRedirects = 5;
    std::string baseUrl = "https://www.youtube.com/youtubei/v1/";
    Logger log;
};

class HttpClient final : public core::IHttp {
public:
    // `loop` and `transport` must outlive this object (the Client destroys the
    // HttpClient first, then the transport, then the loop).
    HttpClient(Loop &loop, HttpTransport &transport, HttpOptions opts);
    ~HttpClient() override;   // cancels every in-flight exchange; no callback afterwards
    HttpClient(const HttpClient &) = delete;
    HttpClient &operator=(const HttpClient &) = delete;

    // Called on the engine thread when the server-issued visitorData is first adopted.
    void setVisitorSink(std::function<void(const std::string &)> sink) { m_visitorSink = std::move(sink); }
    // Called on the engine thread when a new base.js identity is adopted.
    void setPlayerJsSink(std::function<void(const std::string &, int)> sink) { m_playerJsSink = std::move(sink); }

    void post(std::string_view endpoint, ClientId, std::string bodyJson, const core::JobToken &, core::HttpFn) override;
    void postForm(std::string url, core::Form fields, const core::JobToken &, core::HttpFn) override;
    void get(std::string url, const core::JobToken &, core::HttpFn) override;
    void fetch(std::string url, std::vector<Header> headers, const core::JobToken &, core::HttpFn) override;
    void resetVisitorData() override;
    void setPlayerJs(std::string playerId, int signatureTimestamp) override;
    void abort(const core::JobToken &) override;
    Session &session() override { return m_session; }
    void clearCache() override;
    void setBearer(std::string bearer) override;
    const Logger &log() const override { return m_opts.log; }

    friend class HopSink;
    struct Exchange;          // one logical request across its redirect hops
    struct Hop;               // what one transport exchange delivered
private:
    // Each waiter has its own deadline: a request coalesced onto an older exchange still
    // gets the full timeout, not what is left of the first one's.
    struct Waiter { core::JobToken job; core::HttpFn fn; Loop::Clock::time_point deadline; };
    // The index is cached with the body: a hit is parsed without another pass.
    struct CacheEntry { std::shared_ptr<const std::string> body; std::shared_ptr<const json::Doc> doc; Loop::Clock::time_point expires; };

    std::shared_ptr<Exchange> newExchange(HttpRequest req, core::JobToken job, core::HttpFn fn);
    void launch(const std::shared_ptr<Exchange> &ex);
    void onHopDone(const std::shared_ptr<Exchange> &ex, const std::shared_ptr<Hop> &hop);
    void onTimeout(const std::shared_ptr<Exchange> &ex);
    void armTimer(const std::shared_ptr<Exchange> &ex);   // at the earliest waiter deadline
    Loop::Clock::time_point deadline() const;
    void complete(const std::shared_ptr<Exchange> &ex, core::Reply r);
    void forget(const std::shared_ptr<Exchange> &ex);
    void failAsync(const core::JobToken &job, core::HttpFn fn, std::string message);
    const std::string &cachedContext(ClientId id);
    const std::vector<HttpHeader> &cachedHeaders(ClientId id);

    Loop &m_loop;
    HttpTransport &m_transport;
    HttpOptions m_opts;
    Session m_session;
    std::function<void(const std::string &)> m_visitorSink;
    std::function<void(const std::string &, int)> m_playerJsSink;
    std::shared_ptr<char> m_life = std::make_shared<char>(0);   // weak-captured by posted closures
    uint64_t m_gen = 0;                                         // identity generation (bumped by clearCache)
    std::vector<std::shared_ptr<Exchange>> m_exchanges;         // in flight
    std::unordered_map<std::string, std::weak_ptr<Exchange>> m_inflightByKey;   // coalescing
    std::unordered_map<std::string, CacheEntry> m_cache;
    std::deque<std::string> m_cacheOrder;                       // FIFO bound
    static constexpr int kClientCount = kClientIdCount;
    std::string m_ctxCache[kClientCount];
    std::vector<HttpHeader> m_hdrCache[kClientCount];
    bool m_ctxValid[kClientCount] = {};
    bool m_hdrValid[kClientCount] = {};
};

}
#endif
