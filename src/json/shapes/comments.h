// Comment shapes: commentEntityPayload from frameworkUpdates (parsers/commentparser.cpp).
// Namespace rj.
#ifndef YT_SHAPES_COMMENTS_H
#define YT_SHAPES_COMMENTS_H
#include "json/shapes/common.h"
namespace yt::rj {

// -- commentEntityPayload
struct CommentProps {
    std::optional<gj::ContentText> content;
    std::optional<std::string> publishedTime;
};
struct CommentAuthor {
    std::optional<std::string> displayName;
};
struct CommentAvatar {
    std::optional<gj::Sources> image;
};
struct CommentPayload {
    std::optional<CommentProps> properties;
    std::optional<CommentAuthor> author;
    std::optional<CommentAvatar> avatar;
};

}
#endif
