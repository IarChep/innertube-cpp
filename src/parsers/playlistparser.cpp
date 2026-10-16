// Playlist lists (every renderer that is a playlist) and the playlist page header
// (rendererparser.h).
#include "continuation.h"
#include "rendererinternal.h"

namespace yt {

std::vector<Playlist> parsePlaylistList(Value response, std::string *nextToken)
{
    std::vector<Playlist> out;
    std::string token;
    response.find({"lockupViewModel", "tileRenderer", "playlistRenderer", "gridPlaylistRenderer",
                   "compactPlaylistRenderer", "continuationCommand", "nextContinuationData",
                   "reloadContinuationData"},
                  [&](std::string_view key, Value v) {
        if (isTokenKey(key)) {
            if (token.empty()) token = tokenOf(key, v);
            return Visit::Skip;
        }
        // lockupViewModel is a self-contained leaf; only the PLAYLIST content type
        // becomes a Playlist (video/other lockups are skipped). tileRenderer is the
        // TVHTML5 leaf (signed-in FElibrary); only PLAYLIST tiles become playlists — the
        // sibling video/channel tiles in the library are skipped.
        if (key == "lockupViewModel") {
            if (v["contentType"].str() == "LOCKUP_CONTENT_TYPE_PLAYLIST") out.push_back(fromPlaylistLockup(v));
        } else if (key == "tileRenderer") {
            if (v["contentType"].str() == "TILE_CONTENT_TYPE_PLAYLIST") out.push_back(fromTilePlaylist(v));
        } else {
            out.push_back(fromPlaylistRenderer(v));
        }
        return Visit::SkipRest;
    });
    if (nextToken) *nextToken = token;
    return out;
}

Playlist parsePlaylistRenderer(Value r) { return fromPlaylistRenderer(r); }

Playlist parsePlaylistPage(Value response)
{
    Playlist p;
    for (Value it : response.path("sidebar", "playlistSidebarRenderer", "items").items()) {
        if (const Value pri = it["playlistSidebarPrimaryInfoRenderer"]) {
            p.title = textOf(pri["title"]);
            if (const Value d = pri["description"]) p.description = textOf(d);
            // stats[0] is the video count; the label is localized, its number is not.
            if (const Value s = pri["stats"][0]) p.videoCount = digitsOf(textOf(s));
            if (const Value t = pri.path("thumbnailRenderer", "playlistVideoThumbnailRenderer", "thumbnail"))
                p.thumbnailUrl = lastThumbUrl(t);
        }
        if (const Value o = it.path("playlistSidebarSecondaryInfoRenderer", "videoOwner", "videoOwnerRenderer")) {
            p.owner.name = textOf(o["title"]);
            if (const Value t = o["thumbnail"]) p.owner.avatarUrl = lastThumbUrl(t);
            if (const Value id = o.path("navigationEndpoint", "browseEndpoint", "browseId")) p.owner.id = id.string();
        }
    }
    const Value m = response.path("metadata", "playlistMetadataRenderer");
    if (p.title.empty()) p.title = m["title"].string();
    if (p.description.empty()) p.description = m["description"].string();
    return p;
}

}
