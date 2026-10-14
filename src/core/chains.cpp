// core::chains — the request layer (ported from MeeTube's core::chains, which in turn
// was a byte-for-byte port of cuteTube2's QObject request classes onto callbacks).
#include "core/chains.h"
#include "core/log.h"
#include "core/pct.h"
#include "innertube/bodies.h"
#include "innertube/catalog.h"
#include "innertube/clientconfig.h"
#include "json/ytjson.h"
#include "parsers/continuation.h"
#include "parsers/playerparser.h"
#include "parsers/rendererparser.h"
#include "parsers/suggestparser.h"
#include <memory>

namespace yt::core {

// OAuth endpoint responses and the RYD body — fixed schemas, unknown keys skipped.
namespace oj {
struct DeviceCode {
    std::optional<std::string> device_code;
    std::optional<std::string> user_code;
    std::optional<std::string> verification_url;
    std::optional<std::string> verification_uri;
    std::optional<gj::FlexInt> interval;
};
struct Token {
    std::optional<std::string> access_token;
    std::optional<std::string> refresh_token;
    std::optional<std::string> error;
    std::optional<gj::FlexInt> expires_in;
};
struct Ryd { std::optional<gj::FlexInt> likes; std::optional<gj::FlexInt> dislikes; };
}

namespace {

Error errorOf(const Reply &r) { return Error{r.code, r.error, r.httpStatus}; }
Error unavailable(const char *what) { return Error{ErrorCode::Unavailable, what, 0}; }
std::string str(const std::optional<std::string> &s) { return s ? *s : std::string(); }

// Search sort -> the base64 protobuf `params`.
std::string sortParam(const std::string &order)
{
    if (order == "date")   return "CAISAhAB";
    if (order == "views")  return "CAMSAhAB";
    if (order == "rating") return "CAESAhAB";
    return std::string();   // relevance
}

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

// The shared "one page of a list" delivery.
template <class Page, class Parse>
HttpFn pageOf(Callback<Page> done, Parse parse)
{
    return [done, parse](const Reply &r) {
        Result<Page> out;
        if (!r.ok) { out.error = errorOf(r); done(out); return; }
        out.value.items = parse(*r.body, &out.value.next);
        done(out);
    };
}

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

} // namespace

void fetchVideoList(IHttp &http, const VideoListSpec &spec, const JobToken &job, Callback<VideoPage> done)
{
    auto parse = [](const std::string &body, std::string *next) { return parseVideoList(body, next); };
    if (spec.kind == VideoListSpec::Search) {
        http.post("search", ClientId::WEB, bodies::search(spec.query, sortParam(spec.order)), job,
                  pageOf<VideoPage>(done, parse));
        return;
    }
    const ClientId cid = clientForBrowse(spec.browseId, http.session());
    http.post("browse", cid, bodies::browse(spec.browseId, spec.params, spec.page), job,
              pageOf<VideoPage>(done, parse));
}

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

static void fetchCommentsPage(IHttp &http, const std::string &token, const JobToken &job, Callback<CommentPage> done)
{
    http.post("next", ClientId::WEB, bodies::nextContinuation(token), job, [done](const Reply &r) {
        Result<CommentPage> out;
        if (!r.ok) { out.error = errorOf(r); done(out); return; }
        out.value.items = parseComments(*r.body, &out.value.next, &out.value.createCommentParams);
        done(out);
    });
}

void fetchComments(IHttp &http, std::string videoId, std::string page, const JobToken &job, Callback<CommentPage> done)
{
    if (!page.empty()) { fetchCommentsPage(http, page, job, done); return; }
    // Step 1: /next by videoId discovers the comments-section continuation token.
    IHttp *h = &http;
    http.post("next", ClientId::WEB, bodies::nextVideo(videoId), job, [h, job, done](const Reply &r) {
        Result<CommentPage> out;
        if (!r.ok) { out.error = errorOf(r); done(out); return; }
        const std::string token = findContinuationTokenUnder(*r.body, "engagementPanels");
        // No panel/token = comments disabled (e.g. made-for-kids): an empty, successful
        // page flagged `disabled` (distinct from a video with zero comments).
        if (token.empty()) { out.value.disabled = true; done(out); return; }
        if (!live(job)) return;                      // canceled between steps
        fetchCommentsPage(*h, token, job, done);
    });
}

namespace {
struct PlayerAccum {
    bool haveCaptions = false;
    std::vector<Subtitle> captions;
    Error lastError;                                 // for the all-transport-failed caption path
};
}

// Walks the ordered /player client list until one yields streams.
static void playerTry(IHttp &http, std::string videoId, std::shared_ptr<std::vector<ClientId>> clients, size_t idx,
                      std::shared_ptr<PlayerAccum> acc, const JobToken &job, std::function<void(const PlayerInfo &)> done)
{
    const ClientId client = (*clients)[idx];
    // TVHTML5 needs playbackContext.contentPlaybackContext or YouTube answers
    // "UNPLAYABLE: The page needs to be reloaded"; the mobile clients stay minimal.
    const bool tvCtx = (client == ClientId::TVHTML5);
    IHttp *h = &http;
    http.post("player", client, bodies::player(videoId, tvCtx), job,
        [h, videoId, clients, idx, client, acc, job, done](const Reply &r) {
            const bool isLast = (idx + 1 >= clients->size());
            auto finish = [&](Result<std::vector<Stream>> streams) {
                PlayerInfo out;
                out.streams = std::move(streams);
                if (acc->haveCaptions || r.ok) out.captions.value = acc->captions;
                else out.captions.error = acc->lastError;   // every attempt transport-failed
                done(out);
            };
            if (r.ok) {
                const PlayerResult pr = parsePlayer(*r.body);
                if (h->log()) {
                    int prog = 0; bool hls = false, aud = false;
                    for (const Stream &s : pr.streams) {
                        if (s.id == "hls") hls = true;
                        else if (s.id == "audio") aud = true;
                        else if (s.width > 0) ++prog;
                    }
                    YTLOG(h->log(), "player") << "player" << h->session().clients[(int) client].name
                        << "playable=" << pr.playable << "streams=" << pr.streams.size()
                        << "hls=" << hls << "prog=" << prog << "audio=" << aud
                        << "formats=" << pr.formatsSeen << "adaptive=" << pr.adaptiveSeen
                        << "ciphered=" << pr.cipheredOnly << "sabr=" << pr.sabr << "reason=" << pr.reason;
                }
                // Captions from the first response that ACTUALLY has them: signed in, TV is
                // tried first but carries no captionTracks — an empty list must not latch.
                if (!acc->haveCaptions && !pr.captions.empty()) { acc->haveCaptions = true; acc->captions = pr.captions; }
                if (pr.playable) {
                    if (!pr.streams.empty()) {
                        Result<std::vector<Stream>> s; s.value = pr.streams;
                        PlayerInfo out;
                        out.streams = std::move(s);
                        out.captions.value = acc->captions;
                        done(out);
                        return;
                    }
                    if (isLast && pr.cipheredOnly) {
                        Result<std::vector<Stream>> s;
                        s.error = Error{ErrorCode::Unsupported, "streams require signature decipher (unsupported)", 0};
                        finish(std::move(s));
                        return;
                    }
                } else if (isLast) {
                    Result<std::vector<Stream>> s;
                    s.error = Error{ErrorCode::Unavailable, pr.reason, 0};
                    finish(std::move(s));
                    return;
                }
            } else {
                acc->lastError = errorOf(r);
                YTLOG(h->log(), "player") << "player" << h->session().clients[(int) client].name << "TRANSPORT FAIL:" << r.error;
            }
            if (isLast) {
                Result<std::vector<Stream>> s;
                s.error = r.ok ? Error{ErrorCode::Unavailable, "no playable streams", 0} : errorOf(r);
                PlayerInfo out;
                out.streams = std::move(s);
                if (acc->haveCaptions) out.captions.value = acc->captions;
                else if (r.ok) out.captions.value = {};
                else out.captions.error = acc->lastError;
                done(out);
                return;
            }
            if (!live(job)) return;                  // canceled between attempts
            playerTry(*h, videoId, clients, idx + 1, acc, job, done);
        });
}

void fetchPlayer(IHttp &http, std::string videoId, const JobToken &job, std::function<void(const PlayerInfo &)> done)
{
    auto acc = std::make_shared<PlayerAccum>();
    auto clients = std::make_shared<std::vector<ClientId>>();
    // TVHTML5 first when signed in: an AUTHENTICATED /player clears the "confirm you're
    // not a bot" gate that rejects anonymous ANDROID_VR requests.
    if (!http.session().bearer.empty()) clients->push_back(ClientId::TVHTML5);
    // ANDROID_VR (with the session's visitorData) returns ready-to-fetch progressive
    // URLs: server-signed, no n-param, no PoToken, no client-side decipher.
    clients->push_back(ClientId::ANDROID_VR);
    // IOS last: may carry an hlsManifestUrl; must stay visitorData-free (ContextBuilder).
    clients->push_back(ClientId::IOS);
    playerTry(http, std::move(videoId), clients, 0, acc, job, std::move(done));
}

void fetchChannelById(IHttp &http, std::string channelId, const JobToken &job, Callback<Channel> done)
{
    http.post("browse", ClientId::WEB, bodies::browse(channelId, "", ""), job, [done](const Reply &r) {
        Result<Channel> out;
        if (!r.ok) { out.error = errorOf(r); done(out); return; }
        out.value = parseChannel(*r.body);
        if (out.value.id.empty() && out.value.name.empty()) out.error = unavailable("channel unavailable");
        done(out);
    });
}

void fetchChannelByUrl(IHttp &http, std::string url, const JobToken &job, Callback<Channel> done)
{
    IHttp *h = &http;
    http.post("navigation/resolve_url", ClientId::WEB, bodies::resolveUrl(url), job, [h, job, done](const Reply &r) {
        Result<Channel> out;
        if (!r.ok) { out.error = errorOf(r); done(out); return; }
        const std::string browseId = parseResolvedBrowseId(*r.body);
        if (browseId.empty()) { out.error = unavailable("could not resolve channel"); done(out); return; }
        if (!live(job)) return;
        fetchChannelById(*h, browseId, job, done);
    });
}

void fetchChannelSubscribed(IHttp &http, std::string channelId, const JobToken &job, Callback<bool> done)
{
    http.post("browse", ClientId::TVHTML5, bodies::browse(channelId, "", ""), job, [done](const Reply &r) {
        Result<bool> out;
        if (!r.ok) { out.error = errorOf(r); done(out); return; }
        out.value = parseChannelSubscribed(*r.body);
        done(out);
    });
}

void fetchUserSearch(IHttp &http, std::string query, const JobToken &job, Callback<ChannelPage> done)
{
    http.post("search", ClientId::WEB, bodies::search(query, "EgIQAg=="), job,   // channels filter
              pageOf<ChannelPage>(done, [](const std::string &b, std::string *n) { return parseUserList(b, n); }));
}

void fetchChannelList(IHttp &http, std::string browseId, std::string page, const JobToken &job, Callback<ChannelPage> done)
{
    const ClientId cid = clientForBrowse(browseId, http.session());
    http.post("browse", cid, bodies::browse(browseId, "", page), job,
              pageOf<ChannelPage>(done, [](const std::string &b, std::string *n) { return parseUserList(b, n); }));
}

void fetchPlaylists(IHttp &http, std::string resourceId, std::string page, std::string params, const JobToken &job,
                    Callback<PlaylistPage> done)
{
    // Signed-in FElibrary (the user's own playlists) is feedRequiresAuth → TVHTML5.
    const ClientId cid = clientForBrowse(resourceId, http.session());
    http.post("browse", cid, bodies::browse(resourceId, params, page), job,
              pageOf<PlaylistPage>(done, [](const std::string &b, std::string *n) { return parsePlaylistList(b, n); }));
}

void fetchPlaylistSearch(IHttp &http, std::string query, const JobToken &job, Callback<PlaylistPage> done)
{
    http.post("search", ClientId::WEB, bodies::search(query, "EgIQAw=="), job,   // playlists filter
              pageOf<PlaylistPage>(done, [](const std::string &b, std::string *n) { return parsePlaylistList(b, n); }));
}

void fetchAccount(IHttp &http, const JobToken &job, Callback<Account> done)
{
    http.post("account/accounts_list", ClientId::TVHTML5, bodies::accountsList(), job, [done](const Reply &r) {
        Result<Account> out;
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

void postComment(IHttp &http, std::string createParams, std::string text, const JobToken &job, Callback<Done> done)
{
    http.post("comment/create_comment", ClientId::TVHTML5, bodies::createComment(createParams, text), job, doneOf(done));
}

void editPlaylist(IHttp &http, std::string playlistId, bool add, std::string id, const JobToken &job, Callback<Done> done)
{
    http.post("browse/edit_playlist", ClientId::TVHTML5, bodies::editPlaylist(playlistId, add, id), job, doneOf(done));
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
