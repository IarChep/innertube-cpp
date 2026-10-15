// json3 caption parsing: one partial read of events[] (see json3.h).
#include "json/json3.h"
#include "json/shapes/timedtext.h"
namespace yt {

std::vector<Cue> parseJson3(const std::string &body)
{
    tj::Root root{};
    gj::readJsonDoc(root, body);
    std::vector<Cue> out;
    if (!root.events) return out;
    for (const tj::Event &e : *root.events) {
        if (!e.segs) continue;                        // window/pen definitions
        std::string text;
        for (const tj::Seg &s : *e.segs) if (s.utf8) text += *s.utf8;
        if (text.find_first_not_of(" \n") == std::string::npos) continue;   // ASR line-append
        Cue c;
        c.startMs = gj::toInt64(e.tStartMs);
        c.endMs = c.startMs + gj::toInt64(e.dDurationMs);
        c.text = std::move(text);
        out.push_back(std::move(c));
    }
    return out;
}

}
