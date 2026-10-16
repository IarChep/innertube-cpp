// Channel pages and channel lists: the header (legacy c4TabbedHeaderRenderer or the 2024+
// pageHeaderViewModel) with the channelMetadataRenderer fallback, channel renderers
// anywhere, the subscribe state and a resolved browseId (rendererparser.h).
#include "continuation.h"
#include "rendererinternal.h"

namespace yt {

Channel parseChannel(Value response)
{
    Channel u;
    const Value c4 = response.path("header", "c4TabbedHeaderRenderer");
    // --- Legacy c4TabbedHeaderRenderer (flat shape) ---
    if (c4) {
        const Value title = c4["title"];             // c4 uses a plain string title
        u.name = title.isString() ? title.string() : textOf(title);
        u.id = c4["channelId"].string();
        u.avatarUrl = lastThumbUrl(c4["avatar"]);
        u.subscriberText = textOf(c4["subscriberCountText"]);
        u.bannerUrl = lastThumbUrl(c4["banner"]);
        // Authed-only subscribe state; absent (unauthed) leaves subscribed false (R1).
        u.subscribed = c4.path("subscribeButton", "subscribeButtonRenderer", "subscribed").boolean();
    }
    // --- Current pageHeaderRenderer.content.pageHeaderViewModel (nested view-models):
    // 2024+ WEB channel headers moved name/avatar/subscriberCount here. Only when there
    // is no c4 header. ---
    const Value vm = c4 ? Value() : response.path("header", "pageHeaderRenderer", "content", "pageHeaderViewModel");
    if (vm) {
        if (u.name.empty()) u.name = vm.path("title", "dynamicTextViewModel", "text", "content").string();
        if (u.avatarUrl.empty())
            u.avatarUrl = lastSourceUrl(vm.path("image", "decoratedAvatarViewModel", "avatar", "avatarViewModel", "image"));
        if (u.bannerUrl.empty()) u.bannerUrl = lastSourceUrl(vm.path("banner", "imageBannerViewModel", "image"));
        // metadata.contentMetadataViewModel.metadataRows[].metadataParts[].text.content —
        // the rows carry the subscriber line, the @handle and "N videos" with no fixed
        // index, so pick each by its shape (contains "subscriber" / starts with '@' /
        // contains "video").
        if (u.subscriberText.empty())
            for (Value row : vm.path("metadata", "contentMetadataViewModel", "metadataRows").items())
                for (Value p : row["metadataParts"].items()) {
                    if (!p["text"]) continue;
                    const std::string txt = p.path("text", "content").string();
                    if (u.subscriberText.empty() && gj::containsCI(txt, "subscriber")) u.subscriberText = txt;
                    else if (u.handle.empty() && !txt.empty() && txt[0] == '@') u.handle = txt;
                    else if (u.videoCountText.empty() && gj::containsCI(txt, "video")) u.videoCountText = txt;
                }
    }
    // Fill gaps from channelMetadataRenderer (id/description/avatar).
    if (const Value m = response.path("metadata", "channelMetadataRenderer")) {
        if (u.id.empty()) u.id = m["externalId"].string();
        if (u.name.empty()) u.name = m["title"].string();
        u.description = m["description"].string();
        if (u.avatarUrl.empty()) u.avatarUrl = lastThumbUrl(m["avatar"]);
    }
    return u;
}

std::vector<Channel> parseUserList(Value response, std::string *nextToken)
{
    std::vector<Channel> out;
    std::string token;
    // tileRenderer: the authed TVHTML5 FEchannels grid ships channel TILES, not
    // gridChannelRenderer (the WEB shape). Both are leaves.
    response.find({"channelRenderer", "gridChannelRenderer", "tileRenderer", "continuationCommand",
                   "nextContinuationData", "reloadContinuationData"},
                  [&](std::string_view key, Value v) {
        if (isTokenKey(key)) {
            if (token.empty()) token = tokenOf(key, v);
            return Visit::Skip;
        }
        if (key != "tileRenderer") out.push_back(fromUserRenderer(v));
        // Only channel tiles become channels; a video tile in the grid is ignored.
        else if (v["contentType"].str() == "TILE_CONTENT_TYPE_CHANNEL") out.push_back(fromTileChannel(v));
        return Visit::SkipRest;
    });
    if (nextToken) *nextToken = token;
    return out;
}

Channel parseUserRenderer(Value r) { return fromUserRenderer(r); }

bool parseChannelSubscribed(Value response)
{
    return gj::first(response, "subscribeButtonRenderer")["subscribed"].boolean();
}

std::string parseResolvedBrowseId(Value response)
{
    return response.path("endpoint", "browseEndpoint", "browseId").string();
}

}
