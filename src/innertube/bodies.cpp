#include "bodies.h"
#include "innertube/catalog.h"
#include "json/json.h"

namespace yt {
namespace bodies {

// Every body is one small object written field by field; members that are absent for a
// call are simply not written (the old Glaze shapes' nullopt fields).
namespace {

// {"<key>": "<value>"} — most bodies are one string member.
std::string one(std::string_view key, std::string_view value)
{
    return json::Writer().beginObject().member(key, value).endObject().take();
}

} // namespace

std::string browse(std::string_view browseId, std::string_view params)
{
    json::Writer w;
    w.beginObject().member("browseId", browseId);
    if (!params.empty()) w.member("params", params);   // tab selector, e.g. a channel's Videos tab
    return w.endObject().take();
}

std::string search(std::string_view query, std::string_view params)
{
    json::Writer w;
    w.beginObject().member("query", query);
    if (!params.empty()) w.member("params", params);
    return w.endObject().take();
}

std::string nextVideo(std::string_view videoId) { return one("videoId", videoId); }

std::string continuation(std::string_view token) { return one("continuation", token); }

std::string player(std::string_view videoId, const PlayerBody &pb)
{
    json::Writer w;
    w.beginObject().member("videoId", videoId).member("contentCheckOk", true).member("racyCheckOk", true);
    if (pb.tv) {
        w.member("cpn", pb.cpn)
            .key("playbackContext").beginObject()
            .key("contentPlaybackContext").beginObject().member("html5Preference", "HTML5_PREF_WANTS");
        // From base.js (innertube/playerjs.*); omitted while unknown.
        if (pb.signatureTimestamp > 0) w.member("signatureTimestamp", pb.signatureTimestamp);
        w.endObject().endObject();
    }
    return w.endObject().take();
}

std::string resolveUrl(std::string_view url) { return one("url", url); }

std::string accountsList()
{
    return json::Writer().beginObject()
        .key("accountReadMask").beginObject().member("returnOwner", true).endObject()
        .endObject().take();
}

std::string subscribeChannels(std::string_view channelId)
{
    return json::Writer().beginObject().key("channelIds").beginArray().str(channelId).endArray().endObject().take();
}

std::string likeTarget(std::string_view videoId)
{
    return json::Writer().beginObject()
        .key("target").beginObject().member("videoId", videoId).endObject()
        .endObject().take();
}

std::string createComment(std::string_view createCommentParams, std::string_view text)
{
    return json::Writer().beginObject()
        .member("createCommentParams", createCommentParams).member("commentText", text)
        .endObject().take();
}

namespace {

// One edit_playlist action: {"playlistId", "actions": [{"action", "<idKey>": videoId}]}.
std::string editPlaylist(std::string_view playlistId, const char *action, const char *idKey, std::string_view videoId)
{
    return json::Writer().beginObject()
        .member("playlistId", playlistId)
        .key("actions").beginArray().beginObject().member("action", action).member(idKey, videoId).endObject().endArray()
        .endObject().take();
}

} // namespace

std::string playlistAddVideo(std::string_view playlistId, std::string_view videoId)
{
    return editPlaylist(playlistId, "ACTION_ADD_VIDEO", "addedVideoId", videoId);
}

std::string playlistRemoveVideo(std::string_view playlistId, std::string_view videoId)
{
    return editPlaylist(playlistId, "ACTION_REMOVE_VIDEO_BY_VIDEO_ID", "removedVideoId", videoId);
}

std::string mdxHandoff(std::string_view userCode)
{
    return json::Writer().beginObject()
        .key("handoffQrParams").beginObject()
        .key("rapidQrParams").beginObject()
        .member("qrPresetStyle", Catalog::kQrPresetStyle).member("userCode", userCode)
        .member("rapidQrFeature", "RAPID_QR_FEATURE_DEFAULT")
        .endObject().endObject()
        .endObject().take();
}

}
}
