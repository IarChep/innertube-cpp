// innertube-cpp client API. C++17.
//
// Threading & cancellation contract:
//  1. Every Client, group (videos(), search(), …) and Auth method may be called from
//     any thread; it only posts work.
//  2. Network I/O, parsing and request logic run on the engine thread (owned by the
//     Client unless ClientOptions::ownThread is false).
//  3. Each callback runs exactly once or never; never if its Job was canceled before
//     the delivery ran. The cancellation gate runs on the delivery thread (inside
//     ClientOptions::deliver), so cancel() called ON the delivery thread guarantees no
//     later callback.
//  4. Callbacks are never invoked from inside the call that started them.
//  5. ~Client() cancels everything; no callback runs after it returns.
#ifndef YT_CLIENT_H
#define YT_CLIENT_H
#include <yt/auth.h>
#include <yt/catalog.h>
#include <yt/link.h>
#include <yt/log.h>
#include <yt/player.h>
#include <yt/result.h>
#include <yt/session.h>
#include <yt/transport.h>
#include <yt/types.h>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <vector>
namespace yt {

// Where callbacks run. Empty = on the engine thread. A GUI app passes a poster to its
// UI thread; it must run every task it is given, in order, exactly once.
using Executor = std::function<void(std::function<void()>)>;

struct ClientOptions {
    TransportConfig net;                   // backend hints: CA bundle, IP family, proxy, trace log
    TransportFactory transport;            // empty = makeDefaultTransport() (build-time backend)
    int timeoutMs = 20000;                 // per logical request, all redirect hops
    bool ownThread = true;                 // false: the consumer drives runOnce() on one thread
    Executor deliver;                      // where callbacks run (empty = engine thread)
    Logger log;                            // request-level diagnostics ("net", "player")
    SessionState initial;                  // hl/gl/visitorData/refreshToken/account to resume
    std::function<void(const SessionState &)> onStateChanged;   // persist it; runs like a callback
    std::map<ClientId, ClientInfo> clients;                      // client version/UA overrides
};

namespace detail { struct Engine; struct JobHandle; }

// A started operation. Copyable; an empty Job does nothing.
class Job {
public:
    Job() = default;
    void cancel();                         // any thread; see the contract above
    bool active() const;                   // started, not canceled, not yet delivered
private:
    friend struct detail::Engine;
    friend class Auth;
    explicit Job(std::shared_ptr<detail::JobHandle> h) : m_h(std::move(h)) {}
    std::shared_ptr<detail::JobHandle> m_h;
};

// Every group method has two forms: with a callback (returns a Job; the contract above) and
// without one (returns a std::future, fulfilled from the engine thread without
// ClientOptions::deliver; with ownThread == false someone must keep calling runOnce() or
// get() never returns, and get() on the engine thread itself — inside a callback — never
// returns either). A Client destroyed first releases the future with ErrorCode::Canceled.

// Videos: a watch page, every playable source, captions and dislike counts. `link` is
// anything that names a video: an 11-character id or a watch / youtu.be / shorts / live /
// embed URL (yt::Link). Another kind of link fails with ErrorCode::InvalidArgument.
class Videos {
public:
    Job get(std::string link, Callback<Video> done);               // details + related
    Job player(std::string link, Callback<Player> done);                 // every source (yt/player.h)
    Job player(std::string link, unsigned want, Callback<Player> done);  // want: Want* flags; 0 = metadata only
    Job captions(CaptionTrack track, Callback<std::vector<Cue>> done);   // a track from Player::captions
    Job votes(std::string link, Callback<Votes> done);                   // Return YouTube Dislike counts
    std::future<Result<Video>> get(std::string link);
    std::future<Result<Player>> player(std::string link);
    std::future<Result<Player>> player(std::string link, unsigned want);
    std::future<Result<std::vector<Cue>>> captions(CaptionTrack track);
    std::future<Result<Votes>> votes(std::string link);
    Videos(const Videos &) = delete;
    Videos &operator=(const Videos &) = delete;
private:
    friend class Client;
    explicit Videos(detail::Engine *e) : m_e(e) {}
    detail::Engine *m_e;
};

// Search: videos (sortable), channels and playlists, all paged; typing suggestions.
class Search {
public:
    Job videos(std::string query, Callback<Page<VideoItem>> done);
    Job videos(std::string query, SearchOptions options, Callback<Page<VideoItem>> done);
    Job channels(std::string query, Callback<Page<Channel>> done);
    Job playlists(std::string query, Callback<Page<Playlist>> done);
    Job suggestions(std::string prefix, Callback<std::vector<std::string>> done);
    std::future<Result<Page<VideoItem>>> videos(std::string query);
    std::future<Result<Page<VideoItem>>> videos(std::string query, SearchOptions options);
    std::future<Result<Page<Channel>>> channels(std::string query);
    std::future<Result<Page<Playlist>>> playlists(std::string query);
    std::future<Result<std::vector<std::string>>> suggestions(std::string prefix);
    Search(const Search &) = delete;
    Search &operator=(const Search &) = delete;
private:
    friend class Client;
    explicit Search(detail::Engine *e) : m_e(e) {}
    detail::Engine *m_e;
};

// Channels: `link` is a UC… id or a /channel/, /@handle, /c/ or /user/ URL (handles and
// legacy URLs cost one resolve request). The videos and playlists of a channel's tabs carry
// that channel's id and name on every page.
class Channels {
public:
    Job get(std::string link, Callback<Channel> done);            // signed in: `subscribed` is the viewer's state
    Job videos(std::string link, Callback<Page<VideoItem>> done);     // the Videos tab
    Job playlists(std::string link, Callback<Page<Playlist>> done);
    std::future<Result<Channel>> get(std::string link);
    std::future<Result<Page<VideoItem>>> videos(std::string link);
    std::future<Result<Page<Playlist>>> playlists(std::string link);
    Channels(const Channels &) = delete;
    Channels &operator=(const Channels &) = delete;
private:
    friend class Client;
    explicit Channels(detail::Engine *e) : m_e(e) {}
    detail::Engine *m_e;
};

// Playlists: `link` is a playlist id (PL…, RD…, UU…, OL…; "WL" Watch Later and "LL" Liked
// videos when signed in) or a /playlist?list= URL.
class Playlists {
public:
    Job get(std::string link, Callback<Playlist> done);          // header + first page of entries
    std::future<Result<Playlist>> get(std::string link);
    Playlists(const Playlists &) = delete;
    Playlists &operator=(const Playlists &) = delete;
private:
    friend class Client;
    explicit Playlists(detail::Engine *e) : m_e(e) {}
    detail::Engine *m_e;
};

// Feeds. home() is personal when signed in; subscriptions() and history() need sign-in.
class Feeds {
public:
    Job home(Callback<Page<VideoItem>> done);
    Job subscriptions(Callback<Page<VideoItem>> done);
    Job history(Callback<Page<VideoItem>> done);
    Job topic(std::string id, Callback<Page<VideoItem>> done);        // an id from yt::topicFeeds()
    std::future<Result<Page<VideoItem>>> home();
    std::future<Result<Page<VideoItem>>> subscriptions();
    std::future<Result<Page<VideoItem>>> history();
    std::future<Result<Page<VideoItem>>> topic(std::string id);
    Feeds(const Feeds &) = delete;
    Feeds &operator=(const Feeds &) = delete;
private:
    friend class Client;
    explicit Feeds(detail::Engine *e) : m_e(e) {}
    detail::Engine *m_e;
};

// Comments of a video (`link` as in Videos). Page::disabled = turned off for the video.
class Comments {
public:
    Job list(std::string link, Callback<Page<CommentThread>> done);
    Job post(std::string link, std::string text, Callback<Done> done);   // needs sign-in
    std::future<Result<Page<CommentThread>>> list(std::string link);
    std::future<Result<Done>> post(std::string link, std::string text);
    Comments(const Comments &) = delete;
    Comments &operator=(const Comments &) = delete;
private:
    friend class Client;
    explicit Comments(detail::Engine *e) : m_e(e) {}
    detail::Engine *m_e;
};

// The signed-in user: who they are, their ratings, subscriptions and playlists. Every call
// needs sign-in (Client::auth()).
class Account {
public:
    Job info(Callback<AccountInfo> done);
    Job rate(std::string videoLink, Rating rating, Callback<Done> done);   // Rating::None removes a like/dislike
    Job subscribe(std::string channelLink, Callback<Done> done);
    Job unsubscribe(std::string channelLink, Callback<Done> done);
    Job save(std::string videoLink, std::string playlistLink, Callback<Done> done);    // "WL" = Watch Later
    Job unsave(std::string videoLink, std::string playlistLink, Callback<Done> done);
    Job playlists(Callback<Page<Playlist>> done);                 // the user's own playlists
    Job subscriptions(Callback<Page<Channel>> done);              // the channels the user follows
    std::future<Result<AccountInfo>> info();
    std::future<Result<Done>> rate(std::string videoLink, Rating rating);
    std::future<Result<Done>> subscribe(std::string channelLink);
    std::future<Result<Done>> unsubscribe(std::string channelLink);
    std::future<Result<Done>> save(std::string videoLink, std::string playlistLink);
    std::future<Result<Done>> unsave(std::string videoLink, std::string playlistLink);
    std::future<Result<Page<Playlist>>> playlists();
    std::future<Result<Page<Channel>>> subscriptions();
    Account(const Account &) = delete;
    Account &operator=(const Account &) = delete;
private:
    friend class Client;
    explicit Account(detail::Engine *e) : m_e(e) {}
    detail::Engine *m_e;
};

// Raw InnerTube requests: the escape hatch for what the groups do not cover yet. UNSTABLE:
// YouTube changes these endpoints and their JSON without notice, and nothing here parses
// for you — prefer a group method, and ask for one when you need the same call twice.
class Raw {
public:
    // POST youtubei/v1/<endpoint> as `client`. The library adds the context (client
    // identity, hl/gl, visitorData), the bearer when signed in (it rides TVHTML5 only) and
    // its transport policy (timeout, one retry after a 401 refresh, the short cache of
    // browse/search/next). `endpoint`: "browse", "next", "account/accounts_list"… (path
    // segments of A-Z a-z 0-9 _). `json`: the body without "context", one JSON object ("{}"
    // for none). Delivers the response body; an "error" envelope or a non-2xx answer fails
    // as usual. A malformed endpoint or body is ErrorCode::InvalidArgument, sent nowhere.
    Job post(std::string endpoint, ClientId client, std::string json, Callback<std::string> done);
    std::future<Result<std::string>> post(std::string endpoint, ClientId client, std::string json);
    Raw(const Raw &) = delete;
    Raw &operator=(const Raw &) = delete;
private:
    friend class Client;
    explicit Raw(detail::Engine *e) : m_e(e) {}
    detail::Engine *m_e;
};

class Client {
public:
    explicit Client(ClientOptions options = {});   // throws std::invalid_argument without a transport
    ~Client();
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;

    Videos &videos() { return m_videos; }
    Search &search() { return m_search; }
    Channels &channels() { return m_channels; }
    Playlists &playlists() { return m_playlists; }
    Feeds &feeds() { return m_feeds; }
    Comments &comments() { return m_comments; }
    Account &account() { return m_account; }
    Auth &auth() { return *m_auth; }
    Raw &raw() { return m_raw; }                     // unstable: see class Raw

    // The next page of any list (Page::next). The last page's cursor fails with
    // ErrorCode::InvalidArgument and sends nothing.
    Job more(const Cursor<VideoItem> &next, Callback<Page<VideoItem>> done);
    Job more(const Cursor<Channel> &next, Callback<Page<Channel>> done);
    Job more(const Cursor<Playlist> &next, Callback<Page<Playlist>> done);
    Job more(const Cursor<CommentThread> &next, Callback<Page<CommentThread>> done);
    std::future<Result<Page<VideoItem>>> more(const Cursor<VideoItem> &next);
    std::future<Result<Page<Channel>>> more(const Cursor<Channel> &next);
    std::future<Result<Page<Playlist>>> more(const Cursor<Playlist> &next);
    std::future<Result<Page<CommentThread>>> more(const Cursor<CommentThread> &next);

    void setLocale(std::string hl, std::string gl);   // empty keeps the current value; clears caches
    void runOnce(int maxWaitMs);                       // only when ownThread == false

private:
    friend struct detail::Engine;
    explicit Client(std::shared_ptr<detail::Engine> engine);
    std::shared_ptr<detail::Engine> m_engine;
    std::unique_ptr<Auth> m_auth;
    Videos m_videos;
    Search m_search;
    Channels m_channels;
    Playlists m_playlists;
    Feeds m_feeds;
    Comments m_comments;
    Account m_account;
    Raw m_raw;
};

}
#endif
