// innertube-cpp client API. C++17.
//
// Threading & cancellation contract:
//  1. Every Client / Auth method may be called from any thread; it only posts work.
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
#include <yt/log.h>
#include <yt/player.h>
#include <yt/result.h>
#include <yt/session.h>
#include <yt/transport.h>
#include <yt/types.h>
#include <functional>
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

struct BrowseRequest { std::string browseId, params; };
struct SearchRequest { std::string query; SearchSort sort = SearchSort::Relevance; };
enum class Action { Subscribe, Unsubscribe, Like, Dislike, RemoveLike };

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

class Client {
public:
    explicit Client(ClientOptions options);   // throws std::invalid_argument without a transport
    ~Client();
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;

    Job browse(BrowseRequest request, Callback<Page<Video>> done);
    Job search(SearchRequest request, Callback<Page<Video>> done);
    Job watch(std::string videoId, Callback<WatchResult> done);
    Job comments(std::string videoId, Callback<Page<Comment>> done);
    // Every source YouTube serves for the video (see yt/player.h); request.want limits the
    // walk to the kinds the consumer can play.
    Job player(PlayerRequest request, Callback<Player> done);
    // One caption track (from Player::captions) as timed cues.
    Job captions(CaptionTrack track, Callback<std::vector<Cue>> done);
    Job channel(std::string channelId, Callback<Channel> done);
    Job channelByUrl(std::string url, Callback<Channel> done);
    Job channelSubscribed(std::string channelId, Callback<bool> done);
    Job channelList(std::string browseId, Callback<Page<Channel>> done);
    Job searchChannels(std::string query, Callback<Page<Channel>> done);
    Job playlists(std::string resourceId, std::string params, Callback<Page<Playlist>> done);
    Job searchPlaylists(std::string query, Callback<Page<Playlist>> done);
    Job account(Callback<Account> done);
    Job action(Action action, std::string targetId, Callback<Done> done);
    Job postComment(std::string videoId, std::string text, Callback<Done> done);
    Job editPlaylist(std::string playlistId, bool add, std::string id, Callback<Done> done);
    Job votes(std::string videoId, Callback<Votes> done);                    // returnyoutubedislike
    Job suggestions(std::string query, Callback<std::vector<std::string>> done);
    // The next page of any list (Page::next). The last page's cursor fails with
    // ErrorCode::InvalidArgument and sends nothing.
    Job more(const Cursor<Video> &next, Callback<Page<Video>> done);
    Job more(const Cursor<Channel> &next, Callback<Page<Channel>> done);
    Job more(const Cursor<Playlist> &next, Callback<Page<Playlist>> done);
    Job more(const Cursor<Comment> &next, Callback<Page<Comment>> done);

    Auth &auth() { return *m_auth; }
    void setLocale(std::string hl, std::string gl);   // empty keeps the current value; clears caches
    void runOnce(int maxWaitMs);                       // only when ownThread == false

private:
    friend struct detail::Engine;
    explicit Client(std::shared_ptr<detail::Engine> engine);
    std::shared_ptr<detail::Engine> m_engine;
    std::unique_ptr<Auth> m_auth;
};

}
#endif
