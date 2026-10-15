// Watch page (/next) shapes: primary info, view count, like/dislike toggles in their
// legacy and view-model forms, the owner and the TV like-status entity
// (parsers/watchparser.cpp). Namespace rj.
#ifndef YT_SHAPES_WATCH_H
#define YT_SHAPES_WATCH_H
#include "json/shapes/renderers.h"
#include <map>
namespace yt::rj {

// -- watch page pieces (extracted from findRenderer() subtrees)
struct PrimaryInfo {
    std::optional<gj::Text> title;
};
struct ViewCountR {
    std::optional<gj::Text> viewCount;
};
struct ToggleButton {
    std::optional<gj::Text> defaultText;
    std::optional<bool> isToggled;            // authed /next: true when this side is engaged
};
struct ButtonVM {
    std::optional<std::string> title;
};
// Modern like/dislike toggle state: segmentedLikeDislikeButtonViewModel nests a
// like/dislikeButtonViewModel, each wrapping a toggleButtonViewModel carrying the
// engaged flag. Read from the toggleButtonViewModel extent.
struct ToggleStateVM {
    std::optional<bool> isToggled;
};
// Legacy like/dislike: menuRenderer.topLevelButtons[] — [0]=like, [1]=dislike,
// each a {"toggleButtonRenderer": {...}} object (mapped by its single key).
struct TogglePair {
    std::optional<std::vector<std::map<std::string, ToggleButton>>> topLevelButtons;
};
struct OwnerR {
    std::optional<gj::Text> title;
    std::optional<gj::ThumbSet> thumbnail;
    std::optional<ITCommand> navigationEndpoint;   // {browseEndpoint:{browseId}}
    // Authed /next only: the viewer's subscribe state for this channel (same shape as
    // the channel header's subscribeButton). Absent (unauthed) → subscribed stays false.
    std::optional<SubscribeButton> subscribeButton;
};

// frameworkUpdates.entityBatchUpdate.mutations[].payload.likeStatusEntity — the authed
// viewer's like/dislike on the video ("LIKE" / "DISLIKE" / "INDIFFERENT"). The TV /next
// carries the like STATE here (not in a toggleButton), so it restores like/dislike.
struct LikeStatusEntity { std::optional<std::string> likeStatus; };

}
#endif
