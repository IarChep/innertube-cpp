// Comment shapes: commentEntityPayload from frameworkUpdates (parsers/commentparser.cpp).
// Namespace rj.
#ifndef YT_SHAPES_COMMENTS_H
#define YT_SHAPES_COMMENTS_H
#include "json/shapes/common.h"
namespace yt::rj {

// -- commentEntityPayload
struct CommentProps {
    std::optional<std::string> commentId;
    std::optional<gj::ContentText> content;
    std::optional<std::string> publishedTime;
};
struct CommentAuthor {
    std::optional<std::string> channelId;
    std::optional<std::string> displayName;
    std::optional<std::string> avatarThumbnailUrl;
    std::optional<bool> isCurrentUser;        // signed in: the viewer wrote it
};
struct CommentAvatar {
    std::optional<gj::Sources> image;
};
struct CommentToolbar {
    std::optional<std::string> likeCountNotliked;   // "291K", as displayed
    std::optional<std::string> replyCount;          // "675"
};
struct CommentPayload {
    std::optional<CommentProps> properties;
    std::optional<CommentAuthor> author;
    std::optional<CommentAvatar> avatar;
    std::optional<CommentToolbar> toolbar;
};

}
#endif
