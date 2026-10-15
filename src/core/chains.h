// The request layer: one function per logical InnerTube operation (single- or
// multi-step), each over the core::IHttp seam. No Glaze here — the parsers are
// called inside chains.cpp.
//
// Contract for every chain: it runs entirely on the IHttp's (engine) thread; `done`
// is invoked at most once, from that thread. The JobToken is advisory — a
// multi-step chain checks live(job) between steps and stops early (without calling
// done) when the job was canceled; the transport gates its own deliveries too.
#ifndef YT_CORE_CHAINS_H
#define YT_CORE_CHAINS_H
#include <yt/result.h>
#include <yt/types.h>
#include <yt/player.h>
#include "core/cursor.h"
#include "core/ihttp.h"
#include "core/job.h"
#include <functional>
#include <string>
#include <vector>
namespace yt::core {

struct VideoListSpec {
    enum Kind { Browse, Search } kind = Browse;
    std::string browseId, params;                       // Browse (params: a tab selector)
    std::string query;                                  // Search
    SearchSort sort = SearchSort::Relevance;
};
enum ActionKind { Subscribe, Unsubscribe, Like, Dislike, RemoveLike };

struct DeviceCodeGrant { std::string deviceCode, userCode, verificationUrl; int intervalSecs = 5, expiresInSecs = 0; };
struct TokenGrant {
    std::string accessToken, refreshToken, error;
    int expiresInSecs = 0;                   // 0 = not reported
    bool transportOk = false;
    std::string transportError;
    long httpStatus = 0;
};

// Every list chain delivers a Page<T> whose cursor names the endpoint and the client that
// served it; fetchMore continues it with exactly those (YouTube continues a list only for
// the client that started it — a signed-in feed is TVHTML5 on every page).
void fetchVideoList(IHttp &, const VideoListSpec &, const JobToken &, Callback<Page<Video>> done);
// Two steps: /next by videoId discovers the comments continuation, which is page 1.
// No comments panel = comments turned off: an empty page with `disabled`.
void fetchComments(IHttp &, std::string videoId, const JobToken &, Callback<Page<Comment>> done);
void fetchUserSearch(IHttp &, std::string query, const JobToken &, Callback<Page<Channel>> done);
// A channel-list feed (FEchannels — the manage-subscriptions grid).
void fetchChannelList(IHttp &, std::string browseId, const JobToken &, Callback<Page<Channel>> done);
void fetchPlaylists(IHttp &, std::string resourceId, std::string params, const JobToken &, Callback<Page<Playlist>> done);
void fetchPlaylistSearch(IHttp &, std::string query, const JobToken &, Callback<Page<Playlist>> done);
// The next page of a list. A false cursor (the last page) fails with InvalidArgument and
// sends nothing.
void fetchMore(IHttp &, const Cursor<Video> &, const JobToken &, Callback<Page<Video>> done);
void fetchMore(IHttp &, const Cursor<Channel> &, const JobToken &, Callback<Page<Channel>> done);
void fetchMore(IHttp &, const Cursor<Playlist> &, const JobToken &, Callback<Page<Playlist>> done);
void fetchMore(IHttp &, const Cursor<Comment> &, const JobToken &, Callback<Page<Comment>> done);
void fetchWatch(IHttp &, std::string videoId, const JobToken &, Callback<WatchResult> done);
// The /player client walk (spec §2): WEB seeds visitorData when missing, then VISIONOS
// → ANDROID_VR → TVHTML5 (signed in) → IOS, each asked only while a kind in
// request.want is still missing. Every answer that carried a wanted kind becomes a
// Source. Fails only when no client produced a parseable answer (or, with empty
// sources, when TV got a 401 — the Client then refreshes the bearer and retries).
void fetchPlayer(IHttp &, PlayerRequest request, const JobToken &, Callback<Player> done);
// The caption URL with fmt=json3 set (an existing fmt value replaced, else appended);
// every other byte stays as the server issued it.
std::string json3Url(std::string url);
// One caption track as cues: a raw GET of json3Url(track.url). An empty 200 is no cues.
void fetchCaptions(IHttp &, CaptionTrack track, const JobToken &, Callback<std::vector<Cue>> done);
void fetchChannelById(IHttp &, std::string channelId, const JobToken &, Callback<Channel> done);
void fetchChannelByUrl(IHttp &, std::string url, const JobToken &, Callback<Channel> done);
// The viewer's subscribe state — an AUTHED TVHTML5 browse (WEB is anonymous).
void fetchChannelSubscribed(IHttp &, std::string channelId, const JobToken &, Callback<bool> done);
void fetchAccount(IHttp &, const JobToken &, Callback<Account> done);
void submitAction(IHttp &, ActionKind, std::string targetId, const JobToken &, Callback<Done> done);
void postComment(IHttp &, std::string createParams, std::string text, const JobToken &, Callback<Done> done);
// add=true: id = videoId; add=false: id = the entry's setVideoId handle. WL|LL|PL….
void editPlaylist(IHttp &, std::string playlistId, bool add, std::string id, const JobToken &, Callback<Done> done);
// Like + dislike counts from returnyoutubedislikeapi.com (YouTube hides dislikes).
void fetchDislikes(IHttp &, std::string videoId, const JobToken &, Callback<Votes> done);
void fetchSearchSuggestions(IHttp &, std::string query, const JobToken &, Callback<std::vector<std::string>> done);
// OAuth device-code flow (form posts, no context). One poll per call; the retry
// loop lives in Auth.
void oauthDeviceCode(IHttp &, const JobToken &, Callback<DeviceCodeGrant> done);
void oauthPollToken(IHttp &, std::string deviceCode, const JobToken &, std::function<void(const TokenGrant &)> done);
void oauthRefresh(IHttp &, std::string refreshToken, const JobToken &, std::function<void(const TokenGrant &)> done);
// YouTube's QR image for a device code (mdx/handoff as TVHTML5): the PNG bytes from its
// data: URL, or from one raw GET of a plain https URL. Anything that is not a PNG fails.
void fetchSignInQr(IHttp &, std::string userCode, const JobToken &, Callback<std::string> done);

}
#endif
