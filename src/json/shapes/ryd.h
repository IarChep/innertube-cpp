// Return YouTube Dislike votes (returnyoutubedislikeapi.com/votes, core/chains.cpp).
// Namespace oj — services outside InnerTube.
#ifndef YT_SHAPES_RYD_H
#define YT_SHAPES_RYD_H
#include "json/ytjson.h"
#include <optional>
namespace yt::oj {

struct Ryd {
    std::optional<gj::FlexInt> likes;
    std::optional<gj::FlexInt> dislikes;
};

}
#endif
