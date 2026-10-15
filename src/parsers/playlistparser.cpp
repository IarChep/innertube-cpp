#include "rendererinternal.h"
#include "json/shapes/playlist.h"

namespace yt {

using namespace gj;

struct PlaylistCollector : CollectorBase {
    std::vector<Playlist> *out;
    scan::Action what(std::string_view key, int)
    {
        if (consumed()) return scan::Action::Skip;
        if (key == "lockupViewModel" || key == "tileRenderer" || isPlaylistKind(key)) {
            consume();
            return scan::Action::Capture;
        }
        return scan::Action::Descend;
    }
    void capture(std::string_view key, std::string_view value, int)
    {
        // lockupViewModel is a self-contained leaf; only the PLAYLIST content type
        // becomes a Playlist (video/other lockups are skipped).
        if (key == "lockupViewModel") {
            rj::Lockup lm{};
            readJson(lm, value);
            if (lm.contentType && *lm.contentType == "LOCKUP_CONTENT_TYPE_PLAYLIST")
                out->push_back(fromPlaylistLockup(lm));
        }
        // tileRenderer is the TVHTML5 leaf (signed-in FElibrary); only PLAYLIST tiles
        // become playlists — the sibling video/channel tiles in the library are skipped.
        else if (key == "tileRenderer") {
            rj::Tile t{};
            readJson(t, value);
            if (t.contentType && *t.contentType == "TILE_CONTENT_TYPE_PLAYLIST")
                out->push_back(fromTilePlaylist(t));
        }
        else {
            rj::PlaylistR r{};
            readJson(r, value);
            out->push_back(fromPlaylistRenderer(r));
        }
    }
};

std::vector<Playlist> parsePlaylistList(std::string_view response, std::string *nextToken)
{
    std::vector<Playlist> out;
    if (nextToken) {
        WithToken<PlaylistCollector> w;
        w.inner.out = &out;
        scan::document(response, w);
        *nextToken = std::string(w.token);
    } else {
        PlaylistCollector c;
        c.out = &out;
        scan::document(response, c);
    }
    return out;
}

Playlist parsePlaylistRenderer(std::string_view r)
{
    rj::PlaylistR s{};
    readJson(s, r);
    return fromPlaylistRenderer(s);
}

Playlist parsePlaylistPage(const std::string &response)
{
    Playlist p;
    rj::PlaylistRoot root{};
    readJsonDoc(root, response);
    if (root.sidebar && root.sidebar->playlistSidebarRenderer && root.sidebar->playlistSidebarRenderer->items) {
        for (const rj::PlaylistSidebarItem &it : *root.sidebar->playlistSidebarRenderer->items) {
            if (const auto &pri = it.playlistSidebarPrimaryInfoRenderer) {
                p.title = str(textOf(pri->title));
                if (pri->description) p.description = str(textOf(*pri->description));
                // stats[0] is the video count; the label is localized, its number is not.
                if (pri->stats && !pri->stats->empty()) p.videoCount = digitsOf(str(textOf(pri->stats->front())));
                if (pri->thumbnailRenderer && pri->thumbnailRenderer->playlistVideoThumbnailRenderer
                    && pri->thumbnailRenderer->playlistVideoThumbnailRenderer->thumbnail)
                    p.thumbnailUrl = str(lastThumbUrl(*pri->thumbnailRenderer->playlistVideoThumbnailRenderer->thumbnail));
            }
            if (const auto &sec = it.playlistSidebarSecondaryInfoRenderer) {
                if (!sec->videoOwner || !sec->videoOwner->videoOwnerRenderer) continue;
                const rj::OwnerR &o = *sec->videoOwner->videoOwnerRenderer;
                p.owner.name = str(textOf(o.title));
                if (o.thumbnail) p.owner.avatarUrl = str(lastThumbUrl(*o.thumbnail));
                if (o.navigationEndpoint && o.navigationEndpoint->browseEndpoint)
                    p.owner.id = str(o.navigationEndpoint->browseEndpoint->browseId);
            }
        }
    }
    if (root.metadata && root.metadata->playlistMetadataRenderer) {
        const rj::PlaylistMeta &m = *root.metadata->playlistMetadataRenderer;
        if (p.title.empty()) p.title = str(m.title);
        if (p.description.empty()) p.description = str(m.description);
    }
    return p;
}

}
