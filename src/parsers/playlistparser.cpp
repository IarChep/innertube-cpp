#include "rendererinternal.h"

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

}
