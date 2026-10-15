#include "engine.h"
#include <yt/testing.h>
#include "core/chains.h"
#include "net/httpclient.h"
#include <future>
#include <stdexcept>

namespace yt {
namespace detail {

namespace {
thread_local int tDirect = 0;                        // nesting depth of DirectDelivery on this thread
}
DirectDelivery::DirectDelivery() { ++tDirect; }
DirectDelivery::~DirectDelivery() { --tDirect; }
bool directDelivery() { return tDirect > 0; }

std::shared_ptr<Engine> Engine::boot(std::shared_ptr<Engine> e)
{
    if (!e->opts.ownThread) {
        e->setup();
        return e;
    }
    // Shared, not on this stack: the thread may still be inside set_value() when get()
    // returns and this frame unwinds.
    auto ready = std::make_shared<std::promise<void>>();
    std::future<void> ok = ready->get_future();
    e->thread = std::thread([e, ready]() {
        try {
            e->setup();
        } catch (...) {
            ready->set_exception(std::current_exception());
            return;
        }
        ready->set_value();
        e->loop.run();
    });
    try {
        ok.get();
    } catch (...) {
        e->thread.join();
        throw;
    }
    return e;
}

void Engine::setup()
{
    state = opts.initial;
    if (!http) {
        transport = opts.transport ? opts.transport(opts.net) : makeDefaultTransport(opts.net);
        if (!transport)
            throw std::invalid_argument("innertube: no HTTP transport (built with INNERTUBE_HTTP_BACKEND=none?)");
        loop.setDriver(transport->driver());
        net::HttpOptions ho;
        ho.timeoutMs = opts.timeoutMs;
        ho.log = opts.log;
        if (!baseUrlOverride.empty()) ho.baseUrl = baseUrlOverride;
        auto hc = std::make_shared<net::HttpClient>(loop, *transport, ho);
        hc->setVisitorSink([this](const std::string &vd) { state.visitorData = vd; notifyState(); });
        hc->setPlayerJsSink([this](const std::string &id, int sts) {
            state.playerId = id;
            state.signatureTimestamp = sts;
            notifyState();
        });
        http = hc;
    }
    Session &s = http->session();
    s.hl = state.hl;
    s.gl = state.gl;
    s.visitorData = state.visitorData;
    s.playerId = state.playerId;
    s.signatureTimestamp = state.signatureTimestamp;
    for (const auto &kv : opts.clients) s.clients[(int) kv.first] = kv.second;
    signedInFlag = signedIn();
}

void Engine::teardown()
{
    if (torn) return;
    torn = true;
    if (pollTimer) { loop.cancelTimer(pollTimer); pollTimer = 0; }
    http.reset();                                    // cancels its exchanges on the transport
    loop.setDriver(nullptr);
    transport.reset();
}

void Engine::deliver(const std::shared_ptr<JobHandle> &h, std::function<void()> fn, bool final)
{
    std::shared_ptr<Gate> g = gate;
    auto task = [g, h, fn, final]() {
        std::lock_guard<std::recursive_mutex> l(g->mx);
        if (g->closed || !core::live(h->token)) return;
        if (final) h->finished = true;
        fn();
    };
    if (opts.deliver && !h->direct) opts.deliver(std::move(task));
    else task();
}

void Engine::notifyState()
{
    if (!opts.onStateChanged) return;
    std::shared_ptr<Gate> g = gate;
    std::function<void(const SessionState &)> cb = opts.onStateChanged;
    SessionState snap = state;
    auto task = [g, cb, snap]() {
        std::lock_guard<std::recursive_mutex> l(g->mx);
        if (g->closed) return;
        cb(snap);
    };
    if (opts.deliver) opts.deliver(std::move(task));
    else task();
}

} // namespace detail

using detail::Engine;

namespace {
std::shared_ptr<Engine> engineFor(ClientOptions options)
{
    auto e = std::make_shared<Engine>();
    e->opts = std::move(options);
    return Engine::boot(std::move(e));
}
}

void Job::cancel()
{
    if (!m_h) return;
    m_h->token->canceled = true;
    if (std::shared_ptr<Engine> e = m_h->engine.lock()) {
        Engine *raw = e.get();
        core::JobToken t = m_h->token;
        e->loop.post([raw, t]() { if (raw->http) raw->http->abort(t); });
    }
}

bool Job::active() const
{
    return m_h && core::live(m_h->token) && !m_h->finished && !(m_h->gate && m_h->gate->closed);
}

Client::Client(ClientOptions options) : Client(engineFor(std::move(options))) {}

Client::Client(std::shared_ptr<detail::Engine> engine)
    : m_engine(std::move(engine)), m_auth(new Auth(m_engine.get())), m_videos(m_engine.get()),
      m_search(m_engine.get()), m_channels(m_engine.get()), m_playlists(m_engine.get()),
      m_feeds(m_engine.get()), m_comments(m_engine.get()), m_account(m_engine.get())
{
}

Client::~Client()
{
    if (!m_engine) return;
    Engine *e = m_engine.get();
    {
        std::lock_guard<std::recursive_mutex> l(e->gate->mx);
        e->gate->closed = true;                      // no delivery starts after this
    }
    if (!e->opts.ownThread) {
        // Inside runOnce (a callback destroyed the Client): the policy layer may still be
        // on the stack delivering to coalesced waiters — tear down when runOnce unwinds.
        if (e->runDepth > 0) e->teardownPending = true;
        else e->teardown();
        return;
    }
    e->loop.post([e]() { e->teardown(); e->loop.quit(); });
    if (e->thread.get_id() == std::this_thread::get_id())
        e->thread.detach();                          // destroyed from a callback on the engine thread:
                                                     // the thread's own reference keeps the engine alive
    else
        e->thread.join();
}

void Client::runOnce(int maxWaitMs)
{
    std::shared_ptr<Engine> e = m_engine;            // survives a Client destroyed by a callback
    if (!e || e->opts.ownThread) return;
    ++e->runDepth;
    e->loop.runOnce(maxWaitMs);
    if (--e->runDepth == 0 && e->teardownPending) {
        e->teardownPending = false;
        e->teardown();
    }
}

void Client::setLocale(std::string hl, std::string gl)
{
    Engine *e = m_engine.get();
    e->loop.post([e, hl, gl]() {
        if (!e->http) return;
        if (!hl.empty()) { e->state.hl = hl; e->http->session().hl = hl; }
        if (!gl.empty()) { e->state.gl = gl; e->http->session().gl = gl; }
        e->http->clearCache();                       // localized payloads must not outlive the old locale
        e->notifyState();
    });
}

Job Client::more(const Cursor<VideoItem> &next, Callback<Page<VideoItem>> done)
{
    return m_engine->start<Result<Page<VideoItem>>>([next](core::IHttp &h, const core::JobToken &t, Callback<Page<VideoItem>> d) {
        core::fetchMore(h, next, t, std::move(d));
    }, std::move(done));
}

Job Client::more(const Cursor<Channel> &next, Callback<Page<Channel>> done)
{
    return m_engine->start<Result<Page<Channel>>>([next](core::IHttp &h, const core::JobToken &t, Callback<Page<Channel>> d) {
        core::fetchMore(h, next, t, std::move(d));
    }, std::move(done));
}

Job Client::more(const Cursor<Playlist> &next, Callback<Page<Playlist>> done)
{
    return m_engine->start<Result<Page<Playlist>>>([next](core::IHttp &h, const core::JobToken &t, Callback<Page<Playlist>> d) {
        core::fetchMore(h, next, t, std::move(d));
    }, std::move(done));
}

Job Client::more(const Cursor<CommentThread> &next, Callback<Page<CommentThread>> done)
{
    return m_engine->start<Result<Page<CommentThread>>>([next](core::IHttp &h, const core::JobToken &t, Callback<Page<CommentThread>> d) {
        core::fetchMore(h, next, t, std::move(d));
    }, std::move(done));
}

std::future<Result<Page<VideoItem>>> Client::more(const Cursor<VideoItem> &next)
{
    return detail::futureOf<Page<VideoItem>>([&](Callback<Page<VideoItem>> cb) { return more(next, std::move(cb)); });
}
std::future<Result<Page<Channel>>> Client::more(const Cursor<Channel> &next)
{
    return detail::futureOf<Page<Channel>>([&](Callback<Page<Channel>> cb) { return more(next, std::move(cb)); });
}
std::future<Result<Page<Playlist>>> Client::more(const Cursor<Playlist> &next)
{
    return detail::futureOf<Page<Playlist>>([&](Callback<Page<Playlist>> cb) { return more(next, std::move(cb)); });
}
std::future<Result<Page<CommentThread>>> Client::more(const Cursor<CommentThread> &next)
{
    return detail::futureOf<Page<CommentThread>>([&](Callback<Page<CommentThread>> cb) { return more(next, std::move(cb)); });
}

namespace testing {

std::unique_ptr<Client> makeClientForTesting(ClientOptions options, std::shared_ptr<core::IHttp> http)
{
    auto e = std::make_shared<Engine>();
    e->opts = std::move(options);
    e->opts.ownThread = false;
    e->http = std::move(http);
    return Engine::makeClient(Engine::boot(std::move(e)));
}

std::unique_ptr<Client> makeClientForTesting(ClientOptions options, std::string baseUrl)
{
    auto e = std::make_shared<Engine>();
    e->opts = std::move(options);
    e->opts.net.noProxy = true;                      // the base is a loopback server
    e->baseUrlOverride = std::move(baseUrl);
    return Engine::makeClient(Engine::boot(std::move(e)));
}

void setClock(Client &client, std::function<std::chrono::steady_clock::time_point()> now)
{
    Engine::of(client)->now = std::move(now);
}

}
}
