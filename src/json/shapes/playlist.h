// Playlist page (browse of VL<id>) shapes: the header rides in the sidebar — title, the
// stats line ("182 videos"), the description and the owner, whose videoOwnerRenderer is
// the watch page's (parsers/playlistparser.cpp). Namespace rj.
#ifndef YT_SHAPES_PLAYLIST_H
#define YT_SHAPES_PLAYLIST_H
#include "json/shapes/watch.h"
#include <vector>
namespace yt::rj {

struct PlaylistThumbR { std::optional<gj::ThumbSet> thumbnail; };
struct PlaylistThumbW { std::optional<PlaylistThumbR> playlistVideoThumbnailRenderer; };
struct PlaylistPrimaryInfo {
    std::optional<gj::Text> title;
    std::optional<std::vector<gj::Text>> stats;     // ["182 videos", "1,045,670,652 views", "Last updated…"]
    std::optional<gj::Text> description;
    std::optional<PlaylistThumbW> thumbnailRenderer;
};
struct OwnerW { std::optional<OwnerR> videoOwnerRenderer; };
struct PlaylistSecondaryInfo { std::optional<OwnerW> videoOwner; };
struct PlaylistSidebarItem {
    std::optional<PlaylistPrimaryInfo> playlistSidebarPrimaryInfoRenderer;
    std::optional<PlaylistSecondaryInfo> playlistSidebarSecondaryInfoRenderer;
};
struct PlaylistSidebar { std::optional<std::vector<PlaylistSidebarItem>> items; };
struct PlaylistSidebarW { std::optional<PlaylistSidebar> playlistSidebarRenderer; };
struct PlaylistMeta { std::optional<std::string> title, description; };
struct PlaylistMetaW { std::optional<PlaylistMeta> playlistMetadataRenderer; };
struct PlaylistRoot {
    std::optional<PlaylistSidebarW> sidebar;
    std::optional<PlaylistMetaW> metadata;            // the fallback for title/description
};

}
#endif
