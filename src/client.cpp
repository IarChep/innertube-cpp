#include "engine.h"
#include <yt/testing.h>
#include "core/chains.h"
#include "net/httpclient.h"
#include <future>
#include <stdexcept>

namespace yt {
namespace detail {

namespace {
long httpStatusOf(const PlayerInfo &p) { return p.streams.error ? p.streams.error->httpStatus : 0; }
template <class T> long httpStatusOf(const Result<T> &r) { return r.error ? r.error->httpStatus : 0; }

template <class Arg>
using ChainOf = std::function<void(core::IHttp &, const core::JobToken &, std::function<void(const Arg &)>)>;

// One attempt of a chain. A signed-in 401 forces a bearer refresh and re-runs the
// chain exactly once (retried=true); every other result is delivered. Plain recursion
// through captures — no self-referencing closure that would leak when a canceled
// chain never calls back.
template <class Arg>
void runAttempt(Engine *e, std::shared_ptr<JobHandle> h, ChainOf<Arg> chain,
                std::function<void(const Arg &)> cb, bool retried)
{
    if (!core::live(h->token) || !e->http) return;
    chain(*e->http, h->token, [e, h, chain, cb, retried](const Arg &r) {
        if (!retried && httpStatusOf(r) == 401 && e->signedIn() && core::live(h->token)) {
            e->bearerExpiry = Engine::Clock::time_point{};
            e->ensureBearer([e, h, chain, cb]() { runAttempt<Arg>(e, h, chain, cb, true); });
            return;
        }
        e->deliver(h, [cb, r]() { cb(r); });
    });
}
}

std::shared_ptr<Engine> Engine::boot(std::shared_ptr<Engine> e)
{
    if (!e->opts.ownThread) {
        e->setup();
        return e;
    }
    std::promise<void> ready;
    std::future<void> ok = ready.get_future();
    e->thread = std::thread([e, &ready]() {
        try {
            e->setup();
        } catch (...) {
            ready.set_exception(std::current_exception());
            return;
        }
        ready.set_value();
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
        http = hc;
    }
    Session &s = http->session();
    s.hl = state.hl;
    s.gl = state.gl;
    s.visitorData = state.visitorData;
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
    if (opts.deliver) opts.deliver(std::move(task));
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

template <class Arg>
Job Engine::start(std::function<void(core::IHttp &, const core::JobToken &, std::function<void(const Arg &)>)> chain,
                  std::function<void(const Arg &)> cb)
{
    auto h = std::make_shared<JobHandle>();
    h->engine = weak_from_this();
    Engine *e = this;
    loop.post([e, h, chain, cb]() {
        e->ensureBearer([e, h, chain, cb]() { runAttempt<Arg>(e, h, chain, cb, false); });
    });
    return makeJob(h);
}

} // namespace detail

using detail::Engine;

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

bool Job::active() const { return m_h && core::live(m_h->token) && !m_h->finished; }

Client::Client(ClientOptions options)
{
    auto e = std::make_shared<Engine>();
    e->opts = std::move(options);
    m_engine = Engine::boot(std::move(e));
    m_auth.reset(new Auth(m_engine.get()));
}

Client::Client(std::shared_ptr<detail::Engine> engine) : m_engine(std::move(engine)), m_auth(new Auth(m_engine.get())) {}

Client::~Client()
{
    if (!m_engine) return;
    Engine *e = m_engine.get();
    {
        std::lock_guard<std::recursive_mutex> l(e->gate->mx);
        e->gate->closed = true;                      // no delivery starts after this
    }
    if (!e->opts.ownThread) { e->teardown(); return; }
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
    if (e && !e->opts.ownThread) e->loop.runOnce(maxWaitMs);
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

namespace {
template <class T> using Chain = std::function<void(core::IHttp &, const core::JobToken &, Callback<T>)>;
}

Job Client::browse(BrowseRequest req, Callback<VideoPage> done)
{
    return m_engine->start<Result<VideoPage>>([req](core::IHttp &h, const core::JobToken &t, Callback<VideoPage> d) {
        core::VideoListSpec s;
        s.kind = core::VideoListSpec::Browse;
        s.browseId = req.browseId; s.params = req.params; s.page = req.page;
        core::fetchVideoList(h, s, t, std::move(d));
    }, std::move(done));
}

Job Client::search(SearchRequest req, Callback<VideoPage> done)
{
    return m_engine->start<Result<VideoPage>>([req](core::IHttp &h, const core::JobToken &t, Callback<VideoPage> d) {
        core::VideoListSpec s;
        s.kind = core::VideoListSpec::Search;
        s.query = req.query; s.order = req.order;
        core::fetchVideoList(h, s, t, std::move(d));
    }, std::move(done));
}

Job Client::watch(std::string videoId, Callback<WatchResult> done)
{
    return m_engine->start<Result<WatchResult>>([videoId](core::IHttp &h, const core::JobToken &t, Callback<WatchResult> d) {
        core::fetchWatch(h, videoId, t, std::move(d));
    }, std::move(done));
}

Job Client::comments(std::string videoId, std::string page, Callback<CommentPage> done)
{
    return m_engine->start<Result<CommentPage>>([videoId, page](core::IHttp &h, const core::JobToken &t, Callback<CommentPage> d) {
        core::fetchComments(h, videoId, page, t, std::move(d));
    }, std::move(done));
}

Job Client::player(std::string videoId, std::function<void(const PlayerInfo &)> done)
{
    return m_engine->start<PlayerInfo>([videoId](core::IHttp &h, const core::JobToken &t, std::function<void(const PlayerInfo &)> d) {
        core::fetchPlayer(h, videoId, t, std::move(d));
    }, std::move(done));
}

Job Client::channel(std::string channelId, Callback<Channel> done)
{
    return m_engine->start<Result<Channel>>([channelId](core::IHttp &h, const core::JobToken &t, Callback<Channel> d) {
        core::fetchChannelById(h, channelId, t, std::move(d));
    }, std::move(done));
}

Job Client::channelByUrl(std::string url, Callback<Channel> done)
{
    return m_engine->start<Result<Channel>>([url](core::IHttp &h, const core::JobToken &t, Callback<Channel> d) {
        core::fetchChannelByUrl(h, url, t, std::move(d));
    }, std::move(done));
}

Job Client::channelSubscribed(std::string channelId, Callback<bool> done)
{
    return m_engine->start<Result<bool>>([channelId](core::IHttp &h, const core::JobToken &t, Callback<bool> d) {
        core::fetchChannelSubscribed(h, channelId, t, std::move(d));
    }, std::move(done));
}

Job Client::channelList(std::string browseId, std::string page, Callback<ChannelPage> done)
{
    return m_engine->start<Result<ChannelPage>>([browseId, page](core::IHttp &h, const core::JobToken &t, Callback<ChannelPage> d) {
        core::fetchChannelList(h, browseId, page, t, std::move(d));
    }, std::move(done));
}

Job Client::searchChannels(std::string query, Callback<ChannelPage> done)
{
    return m_engine->start<Result<ChannelPage>>([query](core::IHttp &h, const core::JobToken &t, Callback<ChannelPage> d) {
        core::fetchUserSearch(h, query, t, std::move(d));
    }, std::move(done));
}

Job Client::playlists(std::string resourceId, std::string page, std::string params, Callback<PlaylistPage> done)
{
    return m_engine->start<Result<PlaylistPage>>([resourceId, page, params](core::IHttp &h, const core::JobToken &t, Callback<PlaylistPage> d) {
        core::fetchPlaylists(h, resourceId, page, params, t, std::move(d));
    }, std::move(done));
}

Job Client::searchPlaylists(std::string query, Callback<PlaylistPage> done)
{
    return m_engine->start<Result<PlaylistPage>>([query](core::IHttp &h, const core::JobToken &t, Callback<PlaylistPage> d) {
        core::fetchPlaylistSearch(h, query, t, std::move(d));
    }, std::move(done));
}

Job Client::account(Callback<Account> done)
{
    return m_engine->start<Result<Account>>([](core::IHttp &h, const core::JobToken &t, Callback<Account> d) {
        core::fetchAccount(h, t, std::move(d));
    }, std::move(done));
}

Job Client::action(Action action, std::string targetId, Callback<Done> done)
{
    core::ActionKind kind = core::Like;
    switch (action) {
    case Action::Subscribe: kind = core::Subscribe; break;
    case Action::Unsubscribe: kind = core::Unsubscribe; break;
    case Action::Like: kind = core::Like; break;
    case Action::Dislike: kind = core::Dislike; break;
    case Action::RemoveLike: kind = core::RemoveLike; break;
    }
    return m_engine->start<Result<Done>>([kind, targetId](core::IHttp &h, const core::JobToken &t, Callback<Done> d) {
        core::submitAction(h, kind, targetId, t, std::move(d));
    }, std::move(done));
}

Job Client::postComment(std::string createParams, std::string text, Callback<Done> done)
{
    return m_engine->start<Result<Done>>([createParams, text](core::IHttp &h, const core::JobToken &t, Callback<Done> d) {
        core::postComment(h, createParams, text, t, std::move(d));
    }, std::move(done));
}

Job Client::editPlaylist(std::string playlistId, bool add, std::string id, Callback<Done> done)
{
    return m_engine->start<Result<Done>>([playlistId, add, id](core::IHttp &h, const core::JobToken &t, Callback<Done> d) {
        core::editPlaylist(h, playlistId, add, id, t, std::move(d));
    }, std::move(done));
}

Job Client::votes(std::string videoId, Callback<Votes> done)
{
    return m_engine->start<Result<Votes>>([videoId](core::IHttp &h, const core::JobToken &t, Callback<Votes> d) {
        core::fetchDislikes(h, videoId, t, std::move(d));
    }, std::move(done));
}

Job Client::suggestions(std::string query, Callback<std::vector<std::string>> done)
{
    return m_engine->start<Result<std::vector<std::string>>>(
        [query](core::IHttp &h, const core::JobToken &t, Callback<std::vector<std::string>> d) {
            core::fetchSearchSuggestions(h, query, t, std::move(d));
        }, std::move(done));
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
    e->baseUrlOverride = std::move(baseUrl);
    return Engine::makeClient(Engine::boot(std::move(e)));
}

void setClock(Client &client, std::function<std::chrono::steady_clock::time_point()> now)
{
    Engine::of(client)->now = std::move(now);
}

}
}
