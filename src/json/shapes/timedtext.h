// Timedtext caption shapes, fmt=json3: events[] with segs[] (json/json3.cpp).
// Namespace tj.
#ifndef YT_SHAPES_TIMEDTEXT_H
#define YT_SHAPES_TIMEDTEXT_H
#include "json/shapes/common.h"
#include <string>
#include <vector>
namespace yt::tj {

struct Seg {
    std::optional<std::string> utf8;
};
struct Event {
    std::optional<gj::FlexInt> tStartMs;
    std::optional<gj::FlexInt> dDurationMs;
    std::optional<std::vector<Seg>> segs;
};
struct Root {
    std::optional<std::vector<Event>> events;
};

}
#endif
