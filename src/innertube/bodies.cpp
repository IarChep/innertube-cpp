#include "bodies.h"
#include "innertube/catalog.h"
#include "json/ytjson.h"

namespace yt {
namespace bodies {

// Body shapes (Glaze reflection; std::nullopt members are omitted on write —
// glz's default skip_null_members — which is how the conditional fields work).
namespace bj {

struct Browse {
    std::optional<std::string> browseId;
    std::optional<std::string> params;
    std::optional<std::string> continuation;
};
struct Search {
    std::string query;
    std::optional<std::string> params;
};
struct Next {
    std::optional<std::string> videoId;
    std::optional<std::string> continuation;
};
struct ContentPlaybackContext {
    std::string html5Preference = "HTML5_PREF_WANTS";
    // From base.js (innertube/playerjs.*); omitted while unknown.
    std::optional<int> signatureTimestamp;
};
struct PlaybackContext {
    ContentPlaybackContext contentPlaybackContext;
};
struct Player {
    std::string videoId;
    bool contentCheckOk = true;
    bool racyCheckOk = true;
    std::optional<std::string> cpn;
    std::optional<PlaybackContext> playbackContext;   // TVHTML5 only (else "reloaded")
};
struct Resolve {
    std::string url;
};
struct ReadMask {
    bool returnOwner = true;
};
struct AccountsList {
    ReadMask accountReadMask;
};
struct Subscribe {
    std::vector<std::string> channelIds;
};
struct Target {
    std::string videoId;
};
struct Like {
    Target target;
};
struct CreateComment {
    std::string createCommentParams;
    std::string commentText;
};
struct EditAction {
    std::string action;
    std::optional<std::string> addedVideoId;   // omitted when nullopt (add only)
    std::optional<std::string> setVideoId;     // omitted when nullopt (remove only)
};
struct EditPlaylist {
    std::string playlistId;
    std::vector<EditAction> actions;
};

struct RapidQr {
    std::string qrPresetStyle = Catalog::kQrPresetStyle;
    std::string userCode;
    std::string rapidQrFeature = "RAPID_QR_FEATURE_DEFAULT";
};
struct HandoffQr {
    RapidQr rapidQrParams;
};
struct Mdx {
    HandoffQr handoffQrParams;
};

} // namespace bj

template <class T>
static std::string dump(const T &body)
{
    return glz::write_json(body).value_or(std::string("{}"));
}

static std::optional<std::string> opt(std::string_view s)
{
    if (s.empty()) return std::nullopt;
    return std::string(s);
}

std::string browse(std::string_view browseId, std::string_view params, std::string_view continuation)
{
    bj::Browse b;
    if (!continuation.empty()) {
        b.continuation = std::string(continuation);
    } else {
        b.browseId = std::string(browseId);
        // Tab selector (e.g. a channel's Videos tab) — continuations re-encode it.
        b.params = opt(params);
    }
    return dump(b);
}

std::string search(std::string_view query, std::string_view params)
{
    bj::Search b;
    b.query = std::string(query);
    if (!params.empty()) b.params = std::string(params);
    return dump(b);
}

std::string nextVideo(std::string_view videoId)
{
    bj::Next b;
    b.videoId = std::string(videoId);
    return dump(b);
}

std::string nextContinuation(std::string_view token)
{
    bj::Next b;
    b.continuation = std::string(token);
    return dump(b);
}

std::string player(std::string_view videoId, const PlayerBody &pb)
{
    bj::Player b;
    b.videoId = std::string(videoId);
    if (pb.tv) {
        b.cpn = pb.cpn;
        b.playbackContext = bj::PlaybackContext{};
        if (pb.signatureTimestamp > 0)
            b.playbackContext->contentPlaybackContext.signatureTimestamp = pb.signatureTimestamp;
    }
    return dump(b);
}

std::string resolveUrl(std::string_view url)
{
    bj::Resolve b;
    b.url = std::string(url);
    return dump(b);
}

std::string accountsList()
{
    return dump(bj::AccountsList{});
}

std::string subscribeChannels(std::string_view channelId)
{
    bj::Subscribe b;
    b.channelIds.push_back(std::string(channelId));
    return dump(b);
}

std::string likeTarget(std::string_view videoId)
{
    bj::Like b;
    b.target.videoId = std::string(videoId);
    return dump(b);
}

std::string createComment(std::string_view createCommentParams, std::string_view text)
{
    bj::CreateComment b;
    b.createCommentParams = std::string(createCommentParams);
    b.commentText = std::string(text);
    return dump(b);
}

std::string editPlaylist(std::string_view playlistId, bool add, std::string_view id)
{
    bj::EditAction a;
    if (add) {
        a.action = "ACTION_ADD_VIDEO";
        a.addedVideoId = std::string(id);
    } else {
        a.action = "ACTION_REMOVE_VIDEO";
        a.setVideoId = std::string(id);
    }
    bj::EditPlaylist b;
    b.playlistId = std::string(playlistId);
    b.actions.push_back(a);
    return dump(b);
}

std::string mdxHandoff(std::string_view userCode)
{
    bj::Mdx b;
    b.handoffQrParams.rapidQrParams.userCode = std::string(userCode);
    return dump(b);
}

}
}
