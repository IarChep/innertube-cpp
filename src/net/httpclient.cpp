#include "net/httpclient.h"
#include "core/log.h"
#include "innertube/contextbuilder.h"
#include "json/json.h"
#include "net/url.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <optional>

namespace yt::net {

using core::Reply;

namespace {

// Response-cache bound: a browsing session's worth (home + feeds + pages). Entries are
// raw body strings — a full cache is a few MB at worst.
constexpr size_t kMaxCacheEntries = 64;

// Per-endpoint cache TTLs (seconds). 0 = never cache: writes must always hit the
// network, and /player payloads carry short-lived stream URLs.
int cacheTtlSecs(std::string_view endpoint)
{
    if (endpoint == "account/accounts_list") return 900;
    if (endpoint == "browse" || endpoint == "search" || endpoint == "next"
        || endpoint == "navigation/resolve_url") return 180;
    return 0;
}

char lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }
bool iequals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (lower(a[i]) != lower(b[i])) return false;
    return true;
}
const HttpHeader *findHeader(const std::vector<HttpHeader> &h, std::string_view name)
{
    for (const HttpHeader &x : h) if (iequals(x.name, name)) return &x;
    return nullptr;
}
// Only credential-free GETs follow redirects. A request carrying credentials would
// replay them to whatever origin the Location names: the bearer / consent cookie in its
// headers, or — for OAuth form posts — client_secret / refresh_token in its body.
bool mayFollowRedirects(const HttpRequest &r)
{
    return r.method == HttpRequest::Method::Get
        && !findHeader(r.headers, "authorization") && !findHeader(r.headers, "cookie");
}
// Request-line / header hygiene, enforced here for every backend: CR, LF, NUL (any C0
// control but TAB, and DEL) in a header would let a server-issued value (visitorData,
// an OAuth token, a redirect Location) inject headers or split the request.
bool cleanHeaderPart(std::string_view s)
{
    for (unsigned char c : s) if ((c < 0x20 && c != '\t') || c == 0x7f) return false;
    return true;
}
// URLs carry no raw spaces/controls at all (servers send them %-encoded).
bool cleanUrl(std::string_view s)
{
    for (unsigned char c : s) if (c <= 0x20 || c == 0x7f) return false;
    return true;
}
bool cleanRequest(const HttpRequest &r)
{
    if (!cleanUrl(r.url)) return false;
    for (const HttpHeader &h : r.headers)
        if (h.name.empty() || !cleanHeaderPart(h.name) || !cleanHeaderPart(h.value)) return false;
    return true;
}
// visitorData is an opaque base64url-ish token; anything but visible ASCII is refused.
bool plausibleToken(std::string_view s)
{
    if (s.empty()) return false;
    for (unsigned char c : s) if (c < 0x21 || c > 0x7e) return false;
    return true;
}
bool isRedirect(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

// The envelope of a delivered JSON body (MeeTube's makeReply, minus its transport
// branches which the caller has already handled). The one pass over the body: it builds
// the JSON index the parsers then read, so a body that is not well-formed JSON (empty,
// an HTML consent page, cut short) fails here as InvalidResponse — it used to reach the
// parsers and come back as a spuriously empty (and cached) list.
void envelope(Reply &out, bool wantVisitor)
{
    std::shared_ptr<const json::Doc> doc = std::make_shared<const json::Doc>(out.body);
    if (!doc->ok()) {
        out.ok = false;
        out.code = ErrorCode::InvalidResponse;
        out.error = "invalid JSON response";
        return;
    }
    out.doc = doc;
    // A top-level "error" of ANY type fails the reply; its message is used only when it
    // is an object with a string message. The doc stays (OAuth reads its error code).
    if (const json::Value err = doc->root()["error"]) {
        out.ok = false;
        out.code = ErrorCode::Api;
        out.error = "InnerTube error";
        if (const json::Value m = err["message"]; m.isString()) out.error = m.string();
        return;
    }
    if (wantVisitor) out.visitorData = doc->root().path("responseContext", "visitorData").string();
    out.ok = true;
}

} // namespace

// What one transport exchange delivered. Written only by its sink (whose calls the
// HttpTransport contract serializes), read on the engine thread after onDone is posted.
struct HttpClient::Hop {
    int status = 0;
    std::vector<HttpHeader> headers;
    std::string body;
    bool tooLarge = false;
    TransportStatus st;
};

struct HttpClient::Exchange {
    HttpClient *owner = nullptr;
    HttpRequest req;                       // the current hop's request
    std::vector<Waiter> waiters;           // >1 = coalesced identical requests
    std::string coalesceKey;               // posts only
    std::string cacheKey;                  // empty = not cacheable
    int ttlSecs = 0;
    bool wantVisitor = false;              // scan responseContext.visitorData on finish?
    bool raw = false;                      // fetch(): no envelope check
    int hops = 0;                          // redirects followed so far
    uint64_t transportId = 0;
    std::shared_ptr<HopSink> sink;
    uint64_t timer = 0;
    bool done = false;
    uint64_t gen = 0;                      // HttpClient::m_gen when the exchange started
    Loop::Clock::time_point t0 = Loop::Clock::now();
};

// The library's receiver for one hop. Body accumulation + the size cap happen here,
// on whatever thread the backend calls from (calls for one request are serialized by
// contract); completion is always posted to the engine loop, so delivery is never
// re-entrant and never on a foreign thread. A late call after the exchange is gone
// (canceled, timed out, client destroyed) finds the weak_ptr expired and is dropped.
class HopSink final : public HttpResponseSink {
public:
    HopSink(Loop &loop, std::weak_ptr<HttpClient::Exchange> ex, int64_t cap)
        : m_loop(loop), m_ex(std::move(ex)), m_cap(cap), m_hop(std::make_shared<HttpClient::Hop>()) {}
    // Calls after onDone break the backend contract; they are dropped instead of racing
    // the engine thread, which owns the hop from the moment onDone posts it.
    void onHead(int status, std::vector<HttpHeader> headers) override
    {
        if (m_finished.load()) return;
        m_hop->status = status;
        m_hop->headers = std::move(headers);
    }
    bool onBody(const char *data, size_t size) override
    {
        if (m_abort.load() || m_finished.load()) return false;
        if ((int64_t)(m_hop->body.size() + size) > m_cap) { m_hop->tooLarge = true; return false; }
        m_hop->body.append(data, size);
        return true;
    }
    void onDone(TransportStatus status) override
    {
        if (m_finished.exchange(true)) return;
        m_hop->st = std::move(status);
        std::weak_ptr<HttpClient::Exchange> ex = m_ex;
        std::shared_ptr<HttpClient::Hop> hop = m_hop;
        m_loop.post([ex, hop]() {
            if (std::shared_ptr<HttpClient::Exchange> e = ex.lock()) e->owner->onHopDone(e, hop);
        });
    }
    void abort() { m_abort = true; }
    const std::shared_ptr<HttpClient::Hop> &hop() const { return m_hop; }
private:
    Loop &m_loop;
    std::weak_ptr<HttpClient::Exchange> m_ex;
    int64_t m_cap;
    std::shared_ptr<HttpClient::Hop> m_hop;
    std::atomic<bool> m_abort{false};
    std::atomic<bool> m_finished{false};
};

HttpClient::HttpClient(Loop &loop, HttpTransport &transport, HttpOptions opts)
    : m_loop(loop), m_transport(transport), m_opts(std::move(opts)) {}

HttpClient::~HttpClient()
{
    m_life.reset();                                  // posted cache hits / failures go dead
    for (const std::shared_ptr<Exchange> &ex : m_exchanges) {
        ex->done = true;
        if (ex->timer) m_loop.cancelTimer(ex->timer);
        if (ex->sink) ex->sink->abort();
        m_transport.cancel(ex->transportId);
    }
    m_exchanges.clear();                             // weak_ptrs in sinks/timers expire
    m_inflightByKey.clear();
}

void HttpClient::clearCache()
{
    // clearCache marks an identity change (sign-in/out, locale): bump the generation so
    // exchanges still in flight are neither joined by new requests nor cached.
    ++m_gen;
    m_inflightByKey.clear();
    m_cache.clear();
    m_cacheOrder.clear();
    for (int i = 0; i < kClientCount; ++i) { m_ctxValid[i] = false; m_hdrValid[i] = false; }
}

void HttpClient::setBearer(std::string bearer)
{
    m_session.bearer = std::move(bearer);
    for (int i = 0; i < kClientCount; ++i) m_hdrValid[i] = false;
}

const std::string &HttpClient::cachedContext(ClientId id)
{
    const int i = (int) id;
    if (!m_ctxValid[i]) { m_ctxCache[i] = ContextBuilder::contextJson(id, m_session); m_ctxValid[i] = true; }
    return m_ctxCache[i];
}

const std::vector<HttpHeader> &HttpClient::cachedHeaders(ClientId id)
{
    const int i = (int) id;
    if (!m_hdrValid[i]) {
        m_hdrCache[i].clear();
        for (const Header &h : ContextBuilder::headers(id, m_session)) m_hdrCache[i].push_back({h.name, h.value});
        m_hdrValid[i] = true;
    }
    return m_hdrCache[i];
}

std::shared_ptr<HttpClient::Exchange> HttpClient::newExchange(HttpRequest req, core::JobToken job, core::HttpFn fn)
{
    std::shared_ptr<Exchange> ex = std::make_shared<Exchange>();
    ex->owner = this;
    ex->gen = m_gen;
    ex->req = std::move(req);
    ex->waiters.push_back({std::move(job), std::move(fn), deadline()});
    m_exchanges.push_back(ex);
    return ex;
}

void HttpClient::failAsync(const core::JobToken &job, core::HttpFn fn, std::string message)
{
    std::weak_ptr<char> life = m_life;
    m_loop.post([life, job, fn, message]() {
        if (life.expired() || !core::live(job)) return;
        Reply r;
        r.code = ErrorCode::Transport;
        r.error = message;
        fn(r);
    });
}

void HttpClient::post(std::string_view endpoint, ClientId client, std::string bodyJson,
                      const core::JobToken &job, core::HttpFn done)
{
    // Splice the context in: every bodies:: builder emits a JSON object, so the
    // payload is {"context":<ctx>[, <body members>]}.
    const std::string &ctx = cachedContext(client);
    std::string payload;
    payload.reserve(ctx.size() + bodyJson.size() + 16);
    payload += "{\"context\":";
    payload += ctx;
    if (bodyJson.size() > 2) {                       // not the empty object "{}"
        payload += ',';
        payload.append(bodyJson, 1, std::string::npos);
    } else {
        payload += '}';
    }

    // The full key is both the cache key (TTL>0) and the coalescing key; it covers
    // endpoint, client and the payload (browseId/continuation + hl/gl/visitorData).
    std::string key;
    key.reserve(endpoint.size() + payload.size() + 8);
    key.append(endpoint).append(1, '|').append(std::to_string((int) client)).append(1, '|').append(payload);
    const int ttl = cacheTtlSecs(endpoint);

    // (a) COALESCE: an identical request is in flight — attach as a waiter.
    auto inflight = m_inflightByKey.find(key);
    if (inflight != m_inflightByKey.end()) {
        if (std::shared_ptr<Exchange> ex = inflight->second.lock()) {
            if (!ex->done) { ex->waiters.push_back({job, std::move(done), deadline()}); return; }
        }
    }
    // (b) CACHE HIT: deliver asynchronously — never re-entrant from post().
    if (ttl > 0) {
        auto hit = m_cache.find(key);
        if (hit != m_cache.end() && hit->second.expires > Loop::Clock::now()) {
            std::weak_ptr<char> life = m_life;
            std::shared_ptr<const json::Doc> doc = hit->second.doc;
            std::shared_ptr<const std::string> body = hit->second.body;
            m_loop.post([life, job, done, body, doc]() {
                if (life.expired() || !core::live(job)) return;
                Reply r;
                r.ok = true;
                r.httpStatus = 200;
                r.body = body;
                r.doc = doc;
                done(r);
            });
            return;
        }
    }
    // (c) MISS.
    HttpRequest req;
    req.method = HttpRequest::Method::Post;
    req.url = m_opts.baseUrl;
    req.url.append(endpoint).append("?prettyPrint=false");
    req.headers = cachedHeaders(client);
    req.body = std::move(payload);
    std::shared_ptr<Exchange> ex = newExchange(std::move(req), job, std::move(done));
    ex->cacheKey = (ttl > 0) ? key : std::string();
    ex->ttlSecs = ttl;
    ex->wantVisitor = m_session.visitorData.empty();
    m_inflightByKey[key] = ex;
    ex->coalesceKey = std::move(key);
    launch(ex);
}

void HttpClient::get(std::string url, const core::JobToken &job, core::HttpFn done)
{
    if (schemeOf(url).empty()) { failAsync(job, std::move(done), "unsupported URL scheme"); return; }
    HttpRequest req;
    req.url = std::move(url);                        // byte-exact: never re-encoded
    std::shared_ptr<Exchange> ex = newExchange(std::move(req), job, std::move(done));
    ex->wantVisitor = false;                         // only youtubei responses seed the identity
    launch(ex);
}

void HttpClient::fetch(std::string url, std::vector<Header> headers, const core::JobToken &job, core::HttpFn done)
{
    if (schemeOf(url).empty()) { failAsync(job, std::move(done), "unsupported URL scheme"); return; }
    std::string key = "GET|" + url;
    for (const Header &h : headers) key.append(1, '\n').append(h.name).append(1, ':').append(h.value);
    // Coalesce: two walks needing base.js share one ~3 MB download.
    auto inflight = m_inflightByKey.find(key);
    if (inflight != m_inflightByKey.end()) {
        if (std::shared_ptr<Exchange> ex = inflight->second.lock()) {
            if (!ex->done) { ex->waiters.push_back({job, std::move(done), deadline()}); return; }
        }
    }
    HttpRequest req;
    req.url = std::move(url);                        // byte-exact: never re-encoded
    for (Header &h : headers) req.headers.push_back({std::move(h.name), std::move(h.value)});
    std::shared_ptr<Exchange> ex = newExchange(std::move(req), job, std::move(done));
    ex->raw = true;
    m_inflightByKey[key] = ex;
    ex->coalesceKey = std::move(key);
    launch(ex);
}

void HttpClient::resetVisitorData()
{
    m_session.visitorData.clear();
    for (int i = 0; i < kClientCount; ++i) { m_ctxValid[i] = false; m_hdrValid[i] = false; }
    if (m_visitorSink) m_visitorSink(m_session.visitorData);
}

void HttpClient::setPlayerJs(std::string playerId, int signatureTimestamp)
{
    m_session.playerId = std::move(playerId);
    m_session.signatureTimestamp = signatureTimestamp;
    if (m_playerJsSink) m_playerJsSink(m_session.playerId, m_session.signatureTimestamp);
}

void HttpClient::postForm(std::string url, core::Form fields, const core::JobToken &job, core::HttpFn done)
{
    if (schemeOf(url).empty()) { failAsync(job, std::move(done), "unsupported URL scheme"); return; }
    HttpRequest req;
    req.method = HttpRequest::Method::Post;
    req.url = std::move(url);
    req.headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});
    req.body = core::formEncode(fields);
    std::shared_ptr<Exchange> ex = newExchange(std::move(req), job, std::move(done));
    ex->wantVisitor = false;                         // OAuth endpoints are not youtubei calls
    launch(ex);
}

void HttpClient::launch(const std::shared_ptr<Exchange> &ex)
{
    if (!cleanRequest(ex->req)) {
        // Asynchronous like every other failure: never re-entrant from post()/get(). A
        // redirect hop lands here with the exchange's timer armed.
        ex->done = true;
        if (ex->timer) { m_loop.cancelTimer(ex->timer); ex->timer = 0; }
        forget(ex);
        const std::vector<Waiter> waiters = std::move(ex->waiters);
        std::weak_ptr<char> life = m_life;
        m_loop.post([life, waiters]() {
            if (life.expired()) return;
            Reply r;
            r.code = ErrorCode::Transport;
            r.error = "invalid request (control characters in URL or header)";
            for (const Waiter &w : waiters) if (core::live(w.job)) w.fn(r);
        });
        return;
    }
    ex->sink = std::make_shared<HopSink>(m_loop, ex, m_opts.maxBodyBytes);
    if (!ex->timer) armTimer(ex);                    // redirect hops keep the running timer
    ex->transportId = m_transport.start(ex->req, ex->sink);
}

Loop::Clock::time_point HttpClient::deadline() const
{
    return m_opts.timeoutMs > 0 ? Loop::Clock::now() + std::chrono::milliseconds(m_opts.timeoutMs)
                                : Loop::Clock::time_point::max();
}

void HttpClient::armTimer(const std::shared_ptr<Exchange> &ex)
{
    if (m_opts.timeoutMs <= 0 || ex->waiters.empty()) return;
    Loop::Clock::time_point first = ex->waiters.front().deadline;
    for (const Waiter &w : ex->waiters) first = std::min(first, w.deadline);
    const auto ms = std::chrono::ceil<std::chrono::milliseconds>(first - Loop::Clock::now()).count();
    std::weak_ptr<Exchange> wk = ex;
    ex->timer = m_loop.after((int) std::max<long long>(0, ms), [wk]() {
        if (std::shared_ptr<Exchange> e = wk.lock()) e->owner->onTimeout(e);
    });
}

void HttpClient::onTimeout(const std::shared_ptr<Exchange> &ex)
{
    if (ex->done) return;
    ex->timer = 0;
    Reply r;
    r.code = ErrorCode::Timeout;
    r.error = "request timed out";
    // Waiters that joined later keep the exchange going until their own deadline.
    const Loop::Clock::time_point now = Loop::Clock::now();
    std::vector<Waiter> expired, rest;
    for (Waiter &w : ex->waiters) (w.deadline <= now ? expired : rest).push_back(std::move(w));
    if (!rest.empty()) {
        ex->waiters = std::move(rest);
        armTimer(ex);
        for (const Waiter &w : expired) if (core::live(w.job)) w.fn(r);
        return;
    }
    ex->waiters = std::move(expired);
    ex->sink->abort();
    m_transport.cancel(ex->transportId);
    complete(ex, std::move(r));                      // the backend's late onDone is ignored
}

void HttpClient::onHopDone(const std::shared_ptr<Exchange> &ex, const std::shared_ptr<Hop> &hop)
{
    if (ex->done || !ex->sink || ex->sink->hop() != hop) return;   // stale hop
    Reply r;
    r.httpStatus = hop->status;
    r.body = std::make_shared<const std::string>(std::move(hop->body));
    if (hop->tooLarge) {
        r.error = "response too large";
        complete(ex, std::move(r));
        return;
    }
    // Any transport-level error fails the reply — even with bytes received: a truncated
    // body parsed as success would yield (and cache) a spurious empty feed.
    if (hop->st.error != TransportError::None) {
        r.error = hop->st.message.empty() ? std::string("transport error") : hop->st.message;
        complete(ex, std::move(r));
        return;
    }
    if (isRedirect(hop->status)) {
        const HttpHeader *loc = findHeader(hop->headers, "location");
        if (loc && mayFollowRedirects(ex->req)) {
            if (ex->hops >= m_opts.maxRedirects) { r.error = "too many redirects"; complete(ex, std::move(r)); return; }
            std::string next = resolveLocation(ex->req.url, loc->value);
            if (schemeOf(next).empty()) { r.error = "unsupported URL scheme"; complete(ex, std::move(r)); return; }
            ++ex->hops;
            ex->req.url = std::move(next);           // a GET stays a body-less GET on every hop
            launch(ex);
            return;
        }
        // Not followable (credentials, a POST) or Location-less: delivered as is — the
        // envelope check fails it.
    }
    if (ex->raw) {
        // HTML, JavaScript and images skip the JSON envelope; an error status still fails
        // rather than handing an error page to a byte scanner.
        r.ok = hop->status >= 200 && hop->status < 300;
        if (!r.ok) { r.code = ErrorCode::Api; r.error = "HTTP " + std::to_string(hop->status); }
        complete(ex, std::move(r));
        return;
    }
    envelope(r, ex->wantVisitor);
    // A non-2xx answer is a failure even when its JSON carries no "error" member — it is
    // never cached nor parsed as a (spuriously empty) result. The body stays populated.
    if (r.ok && (hop->status < 200 || hop->status >= 300)) {
        r.ok = false;
        r.code = ErrorCode::Api;
        r.error = "HTTP " + std::to_string(hop->status);
    }
    complete(ex, std::move(r));
}

void HttpClient::forget(const std::shared_ptr<Exchange> &ex)
{
    m_exchanges.erase(std::remove(m_exchanges.begin(), m_exchanges.end(), ex), m_exchanges.end());
    if (!ex->coalesceKey.empty()) {
        auto it = m_inflightByKey.find(ex->coalesceKey);
        if (it != m_inflightByKey.end() && it->second.lock() == ex) m_inflightByKey.erase(it);
    }
}

void HttpClient::complete(const std::shared_ptr<Exchange> &ex, Reply r)
{
    ex->done = true;
    if (ex->timer) { m_loop.cancelTimer(ex->timer); ex->timer = 0; }
    forget(ex);

    if (r.ok && !ex->cacheKey.empty() && ex->ttlSecs > 0 && ex->gen == m_gen) {
        if (!m_cache.count(ex->cacheKey)) m_cacheOrder.push_back(ex->cacheKey);
        m_cache[ex->cacheKey] = CacheEntry{r.body, r.doc, Loop::Clock::now() + std::chrono::seconds(ex->ttlSecs)};
        while (m_cacheOrder.size() > kMaxCacheEntries) {
            m_cache.erase(m_cacheOrder.front());
            m_cacheOrder.pop_front();
        }
    }
    // Adopt the server-issued visitorData from the first reply that carries one. The
    // memoized context/headers embed it, so they are rebuilt; the response cache is
    // keyed by the full payload and simply misses for the new identity.
    if (!r.visitorData.empty() && !plausibleToken(r.visitorData)) r.visitorData.clear();
    if (r.ok && m_session.visitorData.empty() && !r.visitorData.empty()) {
        m_session.visitorData = r.visitorData;
        for (int i = 0; i < kClientCount; ++i) { m_ctxValid[i] = false; m_hdrValid[i] = false; }
        if (m_visitorSink) m_visitorSink(m_session.visitorData);
    }
    YTLOG(m_opts.log, "net") << "DONE" << r.httpStatus << (r.ok ? std::string("ok") : r.error)
        << std::chrono::duration_cast<std::chrono::milliseconds>(Loop::Clock::now() - ex->t0).count() << "ms"
        << ex->req.url;

    // Deliver to every live waiter (a coalesced set may mix live and canceled ones).
    // Local copy: a callback may start or abort other requests.
    const std::vector<Waiter> waiters = std::move(ex->waiters);
    for (const Waiter &w : waiters)
        if (core::live(w.job)) w.fn(r);
}

void HttpClient::abort(const core::JobToken &job)
{
    std::vector<std::shared_ptr<Exchange>> orphaned;
    for (const std::shared_ptr<Exchange> &ex : m_exchanges) {
        std::vector<Waiter> &ws = ex->waiters;
        ws.erase(std::remove_if(ws.begin(), ws.end(), [&](const Waiter &w) { return w.job == job; }), ws.end());
        if (ws.empty()) orphaned.push_back(ex);
    }
    for (const std::shared_ptr<Exchange> &ex : orphaned) {
        ex->done = true;
        if (ex->timer) { m_loop.cancelTimer(ex->timer); ex->timer = 0; }
        ex->sink->abort();
        m_transport.cancel(ex->transportId);
        forget(ex);
    }
    // Not-yet-delivered cache hits for `job` are dropped by their live(job) gate.
}

}
