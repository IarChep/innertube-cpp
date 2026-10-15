// core::chains — the request layer (ported from MeeTube's core::chains, which in turn
// was a byte-for-byte port of cuteTube2's QObject request classes onto callbacks).
#include "core/chains.h"
#include <yt/link.h>
#include "core/log.h"
#include "core/pct.h"
#include "innertube/bodies.h"
#include "innertube/catalog.h"
#include "innertube/clientconfig.h"
#include "innertube/playerjs.h"
#include "json/json3.h"
#include "json/shapes/account.h"
#include "json/shapes/oauth.h"
#include "json/shapes/ryd.h"
#include "parsers/continuation.h"
#include "parsers/playerparser.h"
#include "parsers/rendererparser.h"
#include "parsers/suggestparser.h"
#include <chrono>
#include <memory>
#include <random>

namespace yt::core {

namespace {

Error errorOf(const Reply &r) { return Error{r.code, r.error, r.httpStatus}; }
Error unavailable(const char *what) { return Error{ErrorCode::Unavailable, what, 0}; }
std::string str(const std::optional<std::string> &s) { return s ? *s : std::string(); }
bool isPng(const std::string &b) { return b.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0; }

// Bearer-aware browse routing. The OAuth bearer is minted with the TV device-code
// credentials and rides ONLY the TVHTML5 client (WEB browse rejects it with 400);
// TV responses carry tileRenderer items, which parseVideoList handles.
//   feedRequiresAuth   — personalized feeds that always need the bearer → TV.
//   feedPersonalizable — personalized when signed in, generic otherwise.
bool feedRequiresAuth(const std::string &id)
{
    return id == "FEsubscriptions" || id == "FEhistory" || id == "FElibrary" || id == "FEchannels"
        || id == "VLLL" || id == "VLWL";   // the private Liked / Watch Later playlists
}
bool feedPersonalizable(const std::string &id) { return id == "FEwhat_to_watch"; }
ClientId clientForBrowse(const std::string &id, const Session &s)
{
    if (feedRequiresAuth(id)) return ClientId::TVHTML5;
    if (feedPersonalizable(id) && !s.bearer.empty()) return ClientId::TVHTML5;
    return ClientId::WEB;
}

// Spec S4: a private feed asked without a bearer would come back as an empty page (or a
// sign-in prompt); it fails with AuthRequired before anything is sent.
template <class T> bool refusedSignedOut(const std::string &id, IHttp &http, const Callback<Page<T>> &done)
{
    if (!feedRequiresAuth(id) || !http.session().bearer.empty()) return false;
    Result<Page<T>> out;
    out.error = Error{ErrorCode::AuthRequired, "sign-in required", 0};
    done(out);
    return true;
}

struct ActionSpec { const char *endpoint; bool channel; };
ActionSpec actionSpecFor(ActionKind kind)
{
    switch (kind) {
    case Subscribe:   return {"subscription/subscribe", true};
    case Unsubscribe: return {"subscription/unsubscribe", true};
    case Like:        return {"like/like", false};
    case Dislike:     return {"like/dislike", false};
    case RemoveLike:  return {"like/removelike", false};
    }
    return {"like/like", false};
}

// Search sort -> the base64 protobuf `params`.
std::string sortParam(SearchSort sort)
{
    switch (sort) {
    case SearchSort::UploadDate: return "CAISAhAB";
    case SearchSort::Views:      return "CAMSAhAB";
    case SearchSort::TopRated:   return "CAESAhAB";
    case SearchSort::Relevance:  break;
    }
    return std::string();
}

// One page of a list plus the cursor that continues it on the same endpoint and client.
template <class T, class Parse>
HttpFn pageOf(Callback<Page<T>> done, std::string endpoint, ClientId client, Parse parse)
{
    return [done, endpoint, client, parse](const Reply &r) {
        Result<Page<T>> out;
        if (!r.ok) { out.error = errorOf(r); done(out); return; }
        std::string next;
        out.value.items = parse(*r.body, &next);
        out.value.next = detail::CursorAccess::make<T>(endpoint, client, std::move(next));
        done(out);
    };
}

template <class T, class Parse>
void moreOf(IHttp &http, const Cursor<T> &c, const JobToken &job, Callback<Page<T>> done, Parse parse)
{
    if (!c) {
        Result<Page<T>> out;
        out.error = Error{ErrorCode::InvalidArgument, "no next page", 0};
        done(out);
        return;
    }
    const std::string endpoint = detail::CursorAccess::endpoint(c);
    const ClientId client = detail::CursorAccess::client(c);
    http.post(endpoint, client, bodies::continuation(detail::CursorAccess::token(c)), job,
              pageOf<T>(std::move(done), endpoint, client, parse));
}

const auto kParseVideos = [](const std::string &b, std::string *n) { return parseVideoList(b, n); };
const auto kParseUsers = [](const std::string &b, std::string *n) { return parseUserList(b, n); };
const auto kParsePlaylists = [](const std::string &b, std::string *n) { return parsePlaylistList(b, n); };
const auto kParseComments = [](const std::string &b, std::string *n) { return parseComments(b, n); };

HttpFn doneOf(Callback<Done> done)
{
    return [done](const Reply &r) {
        Result<Done> out;
        if (!r.ok) out.error = errorOf(r);
        done(out);
    };
}

TokenGrant tokenGrantOf(const Reply &r)
{
    TokenGrant g;
    g.transportOk = r.ok;
    g.httpStatus = r.httpStatus;
    if (!r.ok) g.transportError = r.error;
    oj::Token tok{};
    gj::readJsonDoc(tok, *r.body);
    g.accessToken = str(tok.access_token);
    g.refreshToken = str(tok.refresh_token);
    g.error = str(tok.error);
    g.expiresInSecs = (int) gj::toInt64(tok.expires_in);
    return g;
}

// The first comments page of a video, and the createCommentParams it carries (needed to
// post a comment).
void fetchCommentsFirst(IHttp &http, std::string videoId, const JobToken &job,
                        std::function<void(const Result<Page<Comment>> &, const std::string &createParams)> done)
{
    IHttp *h = &http;
    http.post("next", ClientId::WEB, bodies::nextVideo(videoId), job, [h, job, done](const Reply &r) {
        Result<Page<Comment>> out;
        if (!r.ok) { out.error = errorOf(r); done(out, std::string()); return; }
        const std::string token = findContinuationTokenUnder(*r.body, "engagementPanels");
        // No panel/token = comments disabled (e.g. made-for-kids): an empty, successful
        // page flagged `disabled` (distinct from a video with zero comments).
        if (token.empty()) { out.value.disabled = true; done(out, std::string()); return; }
        if (!live(job)) return;                      // canceled between steps
        h->post("next", ClientId::WEB, bodies::continuation(token), job, [done](const Reply &p) {
            Result<Page<Comment>> page;
            std::string createParams;
            if (!p.ok) { page.error = errorOf(p); done(page, createParams); return; }
            std::string next;
            page.value.items = parseComments(*p.body, &next, &createParams);
            page.value.next = detail::CursorAccess::make<Comment>("next", ClientId::WEB, std::move(next));
            done(page, createParams);
        });
    });
}

} // namespace

void fetchWatch(IHttp &http, std::string videoId, const JobToken &job, Callback<WatchResult> done)
{
    // Signed-in watch goes TVHTML5: only the bearer (and thus only TV) surfaces the
    // viewer's like/subscribe state in /next.
    const ClientId cid = http.session().bearer.empty() ? ClientId::WEB : ClientId::TVHTML5;
    http.post("next", cid, bodies::nextVideo(videoId), job, [videoId, done](const Reply &r) {
        Result<WatchResult> out;
        if (!r.ok) { out.error = errorOf(r); done(out); return; }
        parseWatchPage(*r.body, &out.value.primary, &out.value.related);
        out.value.primary.id = videoId;              // /next does not echo the id
        done(out);
    });
}

namespace {

// One /player walk (engine thread only).
struct PlayerWalk {
    PlayerRequest req;
    unsigned covered = 0;                // kinds already found
    std::vector<ClientId> steps;         // clients still to ask, front first
    bool seeded = false;                 // asked WEB for a visitorData in this walk
    bool botRetried = false;             // one visitorData reseed per walk
    bool jsTried = false;                // one lazy base.js read per walk
    bool jsReloaded = false;             // one re-read after a TV UNPLAYABLE
    bool anyParsed = false;
    bool sawUnauthorized = false;
    bool metadataOnly = false;           // want == 0: stop after the first answer
    Error lastError{ErrorCode::Transport, "no player response", 0};
    int bestRank = -1;
    Player out;
    Callback<Player> done;
};

std::string newCpn()
{
    static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    // Per engine thread: several Clients may run walks at once.
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::string s(16, 'A');
    for (char &c : s) c = kAlphabet[rng() % 64];
    return s;
}

int rankOf(Playability k)
{
    switch (k) {
    case Playability::Ok: return 5;
    case Playability::Upcoming: case Playability::LiveOffline: return 4;
    case Playability::Unplayable: case Playability::AgeRestricted: case Playability::LoginRequired: return 3;
    case Playability::BotCheck: return 2;
    case Playability::Unknown: return 1;
    }
    return 0;
}

// The kinds a parsed answer covers — decided on URLs, never on the status: an OK answer
// may carry formats without a single URL (SABR-only).
unsigned kindsOf(const PlayerPart &p)
{
    unsigned k = 0;
    bool video = false, audio = false;
    for (const Format &f : p.formats) {
        if (f.url.empty()) continue;
        if (f.hasVideo && f.hasAudio) k |= WantProgressive;
        else if (f.hasVideo) video = true;
        else if (f.hasAudio) audio = true;
    }
    if (video && audio) k |= WantAdaptive;
    if (!p.hlsUrl.empty()) k |= WantHls;
    if (!p.sabrUrl.empty()) k |= WantSabr;
    return k;
}

// Whether asking `c` can still pay off for the `missing` kinds.
bool worth(ClientId c, unsigned missing)
{
    if (c == ClientId::TVHTML5) return (missing & (WantSabr | WantAdaptive)) != 0;
    if (c == ClientId::IOS) return (missing & WantHls) != 0;
    return missing != 0;
}

bool needsVisitor(ClientId c) { return c == ClientId::VISIONOS || c == ClientId::ANDROID_VR; }

void merge(PlayerWalk &w, IHttp &http, ClientId c, PlayerPart &p)
{
    const unsigned kinds = kindsOf(p) & w.req.want;
    if (kinds) {
        Source s;
        s.client = c;
        s.userAgent = http.session().clients[(int) c].userAgent;
        s.formats = std::move(p.formats);
        s.hlsUrl = p.hlsUrl;
        s.dashUrl = p.dashUrl;
        s.sabrUrl = p.sabrUrl;
        s.sabrConfig = p.sabrConfig;
        if (p.expiresInSecs > 0) {
            const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            s.expiresAtSecs = now + p.expiresInSecs;
        }
        w.out.sources.push_back(std::move(s));
        w.covered |= kinds;
    }
    const int rank = rankOf(p.status.kind);
    if (rank > w.bestRank) { w.bestRank = rank; w.out.status = p.status; }
    if (w.out.details.id.empty() && !p.details.id.empty()) w.out.details = p.details;
    // WEB caption URLs need a PO token (they answer empty without one).
    if (w.out.captions.empty() && c != ClientId::WEB && !p.captions.empty()) w.out.captions = p.captions;
    if (w.out.storyboard.empty() && !p.storyboardSpec.empty())
        w.out.storyboard = parseStoryboardSpec(p.storyboardSpec, p.details.lengthSecs);
    // Prefer the TV (signed-in) videostats URLs: the view is attributed to the account.
    if (!p.watchtimeUrl.empty() && (w.out.watchtimeUrl.empty() || c == ClientId::TVHTML5)) {
        w.out.watchtimeUrl = p.watchtimeUrl;
        w.out.playbackUrl = p.playbackUrl;
    }
}

void finishWalk(const std::shared_ptr<PlayerWalk> &w)
{
    Result<Player> r;
    if (!w->anyParsed) {
        r.error = w->lastError;
        if (w->sawUnauthorized) r.error->httpStatus = 401;
    } else if (w->sawUnauthorized && w->out.sources.empty()) {
        r.error = Error{ErrorCode::AuthRequired, "player: bearer rejected", 401};
    } else {
        r.value = std::move(w->out);
    }
    w->done(r);
}

// Reads the live player build and its signatureTimestamp (/tv page, then base.js) unless
// they are known and `force` is false; runs `then` afterwards whatever happened — the TV
// request goes out without sts rather than not at all.
void ensurePlayerJs(IHttp &http, const JobToken &job, bool force, std::function<void()> then)
{
    if (!force && http.session().signatureTimestamp > 0) { then(); return; }
    IHttp *h = &http;
    std::vector<Header> ua = {{"User-Agent", http.session().clients[(int) ClientId::TVHTML5].userAgent}};
    http.fetch(Catalog::kTvPageUrl, std::move(ua), job, [h, job, then](const Reply &r) {
        if (!live(job)) return;
        const std::string id = r.ok ? playerIdIn(*r.body) : std::string();
        if (id.empty() || (id == h->session().playerId && h->session().signatureTimestamp > 0)) { then(); return; }
        h->fetch(playerScriptUrl(id), {}, job, [h, job, id, then](const Reply &js) {
            if (!live(job)) return;
            const int sts = js.ok ? signatureTimestampIn(*js.body) : 0;
            if (sts > 0) h->setPlayerJs(id, sts);
            then();
        });
    });
}

void playerStep(IHttp &http, std::shared_ptr<PlayerWalk> w, JobToken job);

// Step 0: one anonymous WEB /player, only for the visitorData its responseContext
// carries (HttpClient adopts it); its answer is never a source.
void seedVisitor(IHttp &http, std::shared_ptr<PlayerWalk> w, JobToken job)
{
    w->seeded = true;
    IHttp *h = &http;
    http.post("player", ClientId::WEB, bodies::player(w->req.videoId), job, [h, w, job](const Reply &r) {
        if (r.ok) {
            w->anyParsed = true;
            PlayerPart p = parsePlayerResponse(*r.body);
            // WEB formats are n-protected or SABR-only, and WEB answers UNPLAYABLE for
            // ordinary videos (live-verified): neither a source nor a verdict.
            p.formats.clear(); p.hlsUrl.clear(); p.sabrUrl.clear(); p.dashUrl.clear();
            p.status = PlayabilityStatus{};
            merge(*w, *h, ClientId::WEB, p);
        }
        if (!live(job)) return;
        playerStep(*h, w, job);
    });
}

void playerStep(IHttp &http, std::shared_ptr<PlayerWalk> w, JobToken job)
{
    const unsigned missing = w->req.want & ~w->covered;
    while (!w->steps.empty() && !worth(w->steps.front(), missing)) w->steps.erase(w->steps.begin());
    if (w->steps.empty()) { finishWalk(w); return; }
    const ClientId c = w->steps.front();
    if (needsVisitor(c) && http.session().visitorData.empty() && !w->seeded) { seedVisitor(http, w, job); return; }
    if (c == ClientId::TVHTML5 && !w->jsTried) {
        w->jsTried = true;
        IHttp *h = &http;
        ensurePlayerJs(http, job, false, [h, w, job]() { playerStep(*h, w, job); });
        return;
    }
    w->steps.erase(w->steps.begin());
    bodies::PlayerBody body;
    if (c == ClientId::TVHTML5) {
        body.tv = true;
        body.cpn = w->out.cpn;
        body.signatureTimestamp = http.session().signatureTimestamp;
    }
    IHttp *h = &http;
    http.post("player", c, bodies::player(w->req.videoId, body), job, [h, w, job, c](const Reply &r) {
        if (!r.ok) {
            w->lastError = errorOf(r);
            if (r.httpStatus == 401) w->sawUnauthorized = true;
            YTLOG(h->log(), "player") << "player" << h->session().clients[(int) c].name << "failed:" << r.error;
            if (live(job)) playerStep(*h, w, job);
            return;
        }
        w->anyParsed = true;
        PlayerPart p = parsePlayerResponse(*r.body);
        YTLOG(h->log(), "player") << "player" << h->session().clients[(int) c].name << p.status.status
            << "formats=" << p.formats.size() << "kinds=" << kindsOf(p) << "hls=" << !p.hlsUrl.empty()
            << "sabr=" << !p.sabrUrl.empty();
        // TV carries the bearer: LOGIN_REQUIRED there is about the account, not a bot wall.
        if (c == ClientId::TVHTML5 && p.status.kind == Playability::BotCheck)
            p.status.kind = Playability::LoginRequired;
        if (needsVisitor(c) && p.status.kind == Playability::BotCheck && !w->botRetried) {
            // A flagged visitorData: drop it, reseed, and ask the same client again.
            w->botRetried = true;
            w->seeded = false;
            h->resetVisitorData();
            w->steps.insert(w->steps.begin(), c);
            merge(*w, *h, c, p);
            if (live(job)) playerStep(*h, w, job);
            return;
        }
        if (c == ClientId::TVHTML5 && p.status.kind == Playability::Unplayable && !w->jsReloaded
            && !h->session().playerId.empty()) {
            // "The page needs to be reloaded" (localized): YouTube may have rotated the player
            // build under us — re-read it and, if it changed, ask TV once more.
            w->jsReloaded = true;
            const std::string before = h->session().playerId;
            ensurePlayerJs(*h, job, true, [h, w, job, before]() {
                if (h->session().playerId != before) w->steps.insert(w->steps.begin(), ClientId::TVHTML5);
                playerStep(*h, w, job);
            });
            return;
        }
        merge(*w, *h, c, p);
        if (p.final || w->metadataOnly) w->steps.clear();
        if (live(job)) playerStep(*h, w, job);
    });
}

} // namespace

void fetchPlayer(IHttp &http, PlayerRequest request, const JobToken &job, Callback<Player> done)
{
    auto w = std::make_shared<PlayerWalk>();
    w->req = std::move(request);
    w->done = std::move(done);
    w->out.cpn = newCpn();
    if (w->req.want == 0) { w->metadataOnly = true; w->req.want = WantAll; }
    w->steps = {ClientId::VISIONOS, ClientId::ANDROID_VR};
    if (!http.session().bearer.empty()) w->steps.push_back(ClientId::TVHTML5);
    w->steps.push_back(ClientId::IOS);
    playerStep(http, w, job);
}

std::string json3Url(std::string url)
{
    const size_t q = url.find('?');
    if (q == std::string::npos) return url + "?fmt=json3";
    for (size_t p = q; p != std::string::npos; p = url.find('&', p + 1)) {
        if (url.compare(p + 1, 4, "fmt=") != 0) continue;
        const size_t v = p + 5, end = url.find('&', v);
        url.replace(v, (end == std::string::npos ? url.size() : end) - v, "json3");
        return url;
    }
    return url + "&fmt=json3";
}

void fetchCaptions(IHttp &http, CaptionTrack track, const JobToken &job, Callback<std::vector<Cue>> done)
{
    http.fetch(json3Url(std::move(track.url)), {}, job, [done](const Reply &r) {
        Result<std::vector<Cue>> out;
        if (!r.ok) { out.error = errorOf(r); done(out); return; }
        out.value = parseJson3(*r.body);
        done(out);
    });
}

void fetchChannelById(IHttp &http, std::string channelId, const JobToken &job, Callback<Channel> done)
{
    http.post("browse", ClientId::WEB, bodies::browse(channelId, ""), job, [done](const Reply &r) {
        Result<Channel> out;
        if (!r.ok) { out.error = errorOf(r); done(out); return; }
        out.value = parseChannel(*r.body);
        if (out.value.id.empty() && out.value.name.empty()) out.error = unavailable("channel unavailable");
        done(out);
    });
}

void fetchChannelSubscribed(IHttp &http, std::string channelId, const JobToken &job, Callback<bool> done)
{
    http.post("browse", ClientId::TVHTML5, bodies::browse(channelId, ""), job, [done](const Reply &r) {
        Result<bool> out;
        if (!r.ok) { out.error = errorOf(r); done(out); return; }
        out.value = parseChannelSubscribed(*r.body);
        done(out);
    });
}

std::optional<std::string> videoIdOf(std::string_view link)
{
    const std::optional<Link> l = Link::parse(link);
    if (!l || (l->kind != LinkKind::Video && l->kind != LinkKind::Short && l->kind != LinkKind::Live))
        return std::nullopt;
    return l->id;
}

std::optional<std::string> playlistIdOf(std::string_view link)
{
    const std::optional<Link> l = Link::parse(link);
    if (!l || l->kind != LinkKind::Playlist) return std::nullopt;
    return l->id;
}

Error badLink(std::string_view link, const char *expected)
{
    return Error{ErrorCode::InvalidArgument, std::string("not a ") + expected + " link: " + std::string(link), 0};
}

void fetchChannelId(IHttp &http, std::string link, const JobToken &job, Callback<std::string> done)
{
    const std::optional<Link> l = Link::parse(link);
    Result<std::string> out;
    if (!l || l->kind == LinkKind::Video || l->kind == LinkKind::Short || l->kind == LinkKind::Live
        || l->kind == LinkKind::Playlist) {
        out.error = badLink(link, "channel");
        done(out);
        return;
    }
    if (l->kind == LinkKind::Channel) { out.value = l->id; done(out); return; }
    http.post("navigation/resolve_url", ClientId::WEB, bodies::resolveUrl(l->url()), job, [done](const Reply &r) {
        Result<std::string> id;
        if (!r.ok) { id.error = errorOf(r); done(id); return; }
        id.value = parseResolvedBrowseId(*r.body);
        if (id.value.rfind("UC", 0) != 0) { id.value.clear(); id.error = unavailable("could not resolve channel"); }
        done(id);
    });
}

void fetchChannel(IHttp &http, std::string link, const JobToken &job, Callback<Channel> done)
{
    IHttp *h = &http;
    fetchChannelId(http, std::move(link), job, [h, job, done](const Result<std::string> &id) {
        if (!id.ok()) { Result<Channel> out; out.error = id.error; done(out); return; }
        if (!live(job)) return;
        const std::string cid = id.value;
        fetchChannelById(*h, cid, job, [h, job, done, cid](const Result<Channel> &ch) {
            if (!ch.ok() || h->session().bearer.empty()) { done(ch); return; }
            if (!live(job)) return;
            fetchChannelSubscribed(*h, cid, job, [done, ch](const Result<bool> &s) {
                Result<Channel> out = ch;
                if (s.ok()) out.value.subscribed = s.value;
                done(out);
            });
        });
    });
}

void fetchChannelVideos(IHttp &http, std::string link, const JobToken &job, Callback<Page<Video>> done)
{
    IHttp *h = &http;
    fetchChannelId(http, std::move(link), job, [h, job, done](const Result<std::string> &id) {
        if (!id.ok()) { Result<Page<Video>> out; out.error = id.error; done(out); return; }
        if (!live(job)) return;
        VideoListSpec spec;
        spec.browseId = id.value;
        spec.params = Catalog::kChannelVideosTab;
        fetchVideoList(*h, spec, job, done);
    });
}

void fetchChannelPlaylists(IHttp &http, std::string link, const JobToken &job, Callback<Page<Playlist>> done)
{
    IHttp *h = &http;
    fetchChannelId(http, std::move(link), job, [h, job, done](const Result<std::string> &id) {
        if (!id.ok()) { Result<Page<Playlist>> out; out.error = id.error; done(out); return; }
        if (!live(job)) return;
        fetchPlaylists(*h, id.value, Catalog::kChannelPlaylistsTab, job, done);
    });
}

void channelAction(IHttp &http, std::string link, ActionKind kind, const JobToken &job, Callback<Done> done)
{
    IHttp *h = &http;
    fetchChannelId(http, std::move(link), job, [h, kind, job, done](const Result<std::string> &id) {
        if (!id.ok()) { Result<Done> out; out.error = id.error; done(out); return; }
        if (!live(job)) return;
        submitAction(*h, kind, id.value, job, done);
    });
}


void fetchVideoList(IHttp &http, const VideoListSpec &spec, const JobToken &job, Callback<Page<Video>> done)
{
    if (spec.kind == VideoListSpec::Search) {
        http.post("search", ClientId::WEB, bodies::search(spec.query, sortParam(spec.sort)), job,
                  pageOf<Video>(std::move(done), "search", ClientId::WEB, kParseVideos));
        return;
    }
    if (refusedSignedOut(spec.browseId, http, done)) return;
    const ClientId cid = clientForBrowse(spec.browseId, http.session());
    http.post("browse", cid, bodies::browse(spec.browseId, spec.params), job,
              pageOf<Video>(std::move(done), "browse", cid, kParseVideos));
}

void fetchComments(IHttp &http, std::string videoId, const JobToken &job, Callback<Page<Comment>> done)
{
    fetchCommentsFirst(http, std::move(videoId), job,
                       [done](const Result<Page<Comment>> &page, const std::string &) { done(page); });
}

void fetchUserSearch(IHttp &http, std::string query, const JobToken &job, Callback<Page<Channel>> done)
{
    http.post("search", ClientId::WEB, bodies::search(query, "EgIQAg=="), job,   // channels filter
              pageOf<Channel>(std::move(done), "search", ClientId::WEB, kParseUsers));
}

void fetchChannelList(IHttp &http, std::string browseId, const JobToken &job, Callback<Page<Channel>> done)
{
    if (refusedSignedOut(browseId, http, done)) return;
    const ClientId cid = clientForBrowse(browseId, http.session());
    http.post("browse", cid, bodies::browse(browseId, ""), job, pageOf<Channel>(std::move(done), "browse", cid, kParseUsers));
}

void fetchPlaylists(IHttp &http, std::string resourceId, std::string params, const JobToken &job,
                    Callback<Page<Playlist>> done)
{
    // Signed-in FElibrary (the user's own playlists) is feedRequiresAuth → TVHTML5.
    if (refusedSignedOut(resourceId, http, done)) return;
    const ClientId cid = clientForBrowse(resourceId, http.session());
    http.post("browse", cid, bodies::browse(resourceId, params), job,
              pageOf<Playlist>(std::move(done), "browse", cid, kParsePlaylists));
}

void fetchPlaylistSearch(IHttp &http, std::string query, const JobToken &job, Callback<Page<Playlist>> done)
{
    http.post("search", ClientId::WEB, bodies::search(query, "EgIQAw=="), job,   // playlists filter
              pageOf<Playlist>(std::move(done), "search", ClientId::WEB, kParsePlaylists));
}

void fetchMore(IHttp &http, const Cursor<Video> &c, const JobToken &job, Callback<Page<Video>> done)
{
    moreOf<Video>(http, c, job, std::move(done), kParseVideos);
}
void fetchMore(IHttp &http, const Cursor<Channel> &c, const JobToken &job, Callback<Page<Channel>> done)
{
    moreOf<Channel>(http, c, job, std::move(done), kParseUsers);
}
void fetchMore(IHttp &http, const Cursor<Playlist> &c, const JobToken &job, Callback<Page<Playlist>> done)
{
    moreOf<Playlist>(http, c, job, std::move(done), kParsePlaylists);
}
void fetchMore(IHttp &http, const Cursor<Comment> &c, const JobToken &job, Callback<Page<Comment>> done)
{
    moreOf<Comment>(http, c, job, std::move(done), kParseComments);
}

void fetchAccount(IHttp &http, const JobToken &job, Callback<AccountInfo> done)
{
    http.post("account/accounts_list", ClientId::TVHTML5, bodies::accountsList(), job, [done](const Reply &r) {
        Result<AccountInfo> out;
        if (!r.ok) { out.error = errorOf(r); done(out); return; }
        out.value = parseAccountsList(*r.body);
        if (out.value.name.empty() && out.value.channelId.empty()) out.error = unavailable("account unavailable");
        done(out);
    });
}

// Writes go TVHTML5: they need the bearer, which only rides the TV client.
void submitAction(IHttp &http, ActionKind kind, std::string targetId, const JobToken &job, Callback<Done> done)
{
    const ActionSpec spec = actionSpecFor(kind);
    std::string body = spec.channel ? bodies::subscribeChannels(targetId) : bodies::likeTarget(targetId);
    http.post(spec.endpoint, ClientId::TVHTML5, std::move(body), job, doneOf(done));
}

void postComment(IHttp &http, std::string videoId, std::string text, const JobToken &job, Callback<Done> done)
{
    IHttp *h = &http;
    fetchCommentsFirst(http, std::move(videoId), job,
                       [h, text, job, done](const Result<Page<Comment>> &page, const std::string &createParams) {
        Result<Done> out;
        if (!page.ok()) { out.error = page.error; done(out); return; }
        if (page.value.disabled || createParams.empty()) {
            out.error = unavailable("comments are turned off for this video");
            done(out);
            return;
        }
        if (!live(job)) return;
        h->post("comment/create_comment", ClientId::TVHTML5, bodies::createComment(createParams, text), job, doneOf(done));
    });
}

void editPlaylist(IHttp &http, std::string playlistId, PlaylistEdit edit, std::string videoId, const JobToken &job,
                  Callback<Done> done)
{
    std::string body = edit == AddVideo ? bodies::playlistAddVideo(playlistId, videoId)
                                        : bodies::playlistRemoveVideo(playlistId, videoId);
    http.post("browse/edit_playlist", ClientId::TVHTML5, std::move(body), job, doneOf(done));
}

void oauthDeviceCode(IHttp &http, const JobToken &job, Callback<DeviceCodeGrant> done)
{
    Form f = {{"client_id", Catalog::kOAuthClientId}, {"scope", Catalog::kOAuthScope}};
    http.postForm(Catalog::kDeviceCodeUrl, std::move(f), job, [done](const Reply &r) {
        Result<DeviceCodeGrant> out;
        if (!r.ok) { out.error = errorOf(r); done(out); return; }
        oj::DeviceCode dc{};
        gj::readJsonDoc(dc, *r.body);
        out.value.deviceCode = str(dc.device_code);
        out.value.userCode = str(dc.user_code);
        out.value.verificationUrl = str(dc.verification_url);
        if (out.value.verificationUrl.empty()) out.value.verificationUrl = str(dc.verification_uri);
        out.value.intervalSecs = (int) gj::toInt64(dc.interval);
        if (out.value.intervalSecs <= 0) out.value.intervalSecs = 5;
        out.value.expiresInSecs = (int) gj::toInt64(dc.expires_in);
        if (out.value.deviceCode.empty() || out.value.userCode.empty())
            out.error = Error{ErrorCode::Api, "device code request failed", r.httpStatus};
        done(out);
    });
}

void oauthPollToken(IHttp &http, std::string deviceCode, const JobToken &job, std::function<void(const TokenGrant &)> done)
{
    Form f = {{"client_id", Catalog::kOAuthClientId},
              {"client_secret", Catalog::kOAuthClientSecret},
              {"device_code", std::move(deviceCode)},
              {"grant_type", "urn:ietf:params:oauth:grant-type:device_code"}};
    http.postForm(Catalog::kTokenUrl, std::move(f), job, [done](const Reply &r) { done(tokenGrantOf(r)); });
}

void oauthRefresh(IHttp &http, std::string refreshToken, const JobToken &job, std::function<void(const TokenGrant &)> done)
{
    Form f = {{"client_id", Catalog::kOAuthClientId},
              {"client_secret", Catalog::kOAuthClientSecret},
              {"refresh_token", std::move(refreshToken)},
              {"grant_type", "refresh_token"}};
    http.postForm(Catalog::kTokenUrl, std::move(f), job, [done](const Reply &r) { done(tokenGrantOf(r)); });
}

void fetchSignInQr(IHttp &http, std::string userCode, const JobToken &job, Callback<std::string> done)
{
    IHttp *h = &http;
    http.post("mdx/handoff", ClientId::TVHTML5, bodies::mdxHandoff(userCode), job, [h, job, done](const Reply &r) {
        Result<std::string> out;
        if (!r.ok) { out.error = errorOf(r); done(out); return; }
        rj::Handoff hf{};
        gj::readJsonDoc(hf, *r.body);
        std::string url;
        if (hf.rapidQrRenderer && hf.rapidQrRenderer->qrCodeRenderer && hf.rapidQrRenderer->qrCodeRenderer->qrCodeImage
            && hf.rapidQrRenderer->qrCodeRenderer->qrCodeImage->thumbnails
            && !hf.rapidQrRenderer->qrCodeRenderer->qrCodeImage->thumbnails->empty())
            url = str(hf.rapidQrRenderer->qrCodeRenderer->qrCodeImage->thumbnails->front().url);
        static const std::string_view kData = "data:image/png;base64,";
        if (url.compare(0, kData.size(), kData) == 0) {
            if (!gj::base64Decode(std::string_view(url).substr(kData.size()), &out.value) || !isPng(out.value)) {
                out.value.clear();
                out.error = unavailable("sign-in QR is not a PNG");
            }
            done(out);
            return;
        }
        if (url.compare(0, 8, "https://") == 0) {
            if (!live(job)) return;
            h->fetch(url, {}, job, [done](const Reply &img) {
                Result<std::string> o;
                if (!img.ok) o.error = errorOf(img);
                else if (!isPng(*img.body)) o.error = unavailable("sign-in QR is not a PNG");
                else o.value = *img.body;
                done(o);
            });
            return;
        }
        out.error = unavailable("no sign-in QR in the answer");
        done(out);
    });
}

// A plain GET — no youtubei context. The envelope check passes non-YouTube JSON
// through (no top-level "error"); transport error / 404 HTML -> !ok -> graceful.
void fetchDislikes(IHttp &http, std::string videoId, const JobToken &job, Callback<Votes> done)
{
    http.get("https://returnyoutubedislikeapi.com/votes?videoId=" + pctEncode(videoId), job, [done](const Reply &r) {
        Result<Votes> out;
        if (!r.ok) { out.error = errorOf(r); done(out); return; }
        oj::Ryd v{};
        gj::readJsonDoc(v, *r.body);
        if (v.likes) out.value.likes = gj::toInt64(v.likes);
        if (v.dislikes) out.value.dislikes = gj::toInt64(v.dislikes);
        done(out);
    });
}

void fetchSearchSuggestions(IHttp &http, std::string query, const JobToken &job, Callback<std::vector<std::string>> done)
{
    const std::string hl = http.session().hl.empty() ? std::string("en") : http.session().hl;
    const std::string url = std::string(Catalog::kSuggestUrl) + "?client=firefox&ds=yt&hl=" + pctEncode(hl)
                          + "&q=" + pctEncode(query);
    http.get(url, job, [done](const Reply &r) {
        Result<std::vector<std::string>> out;
        if (!r.ok) { out.error = errorOf(r); done(out); return; }
        out.value = parseSuggestions(*r.body);
        done(out);
    });
}

}
