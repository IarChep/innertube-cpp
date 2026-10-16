// json3 caption parsing: the events[] of one document (see json3.h).
#include "json/json3.h"
#include <yt/json.h>
#include "json/ytjson.h"
namespace yt {

std::vector<Cue> parseJson3(std::string_view body)
{
    const json::Doc doc(body);
    std::vector<Cue> out;
    for (json::Value e : doc["events"].items()) {
        if (!e["segs"]) continue;                     // window/pen definitions
        std::string text;
        for (json::Value s : e["segs"].items()) text += s["utf8"].str();
        if (text.find_first_not_of(" \n") == std::string::npos) continue;   // ASR line-append
        Cue c;
        c.startMs = gj::flexInt(e["tStartMs"]);
        c.endMs = c.startMs + gj::flexInt(e["dDurationMs"]);
        c.text = std::move(text);
        out.push_back(std::move(c));
    }
    return out;
}

}
