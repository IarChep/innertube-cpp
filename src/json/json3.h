// Timedtext captions in fmt=json3 → timed cues (Client::captions). The format is
// YouTube's own caption JSON, not an InnerTube answer: it arrives through fetch() (no
// envelope), so this indexes the body itself. Never fails: anything malformed yields no cues.
#ifndef YT_JSON_JSON3_H
#define YT_JSON_JSON3_H
#include <yt/player.h>
#include <string>
#include <string_view>
#include <vector>
namespace yt {

// Window events and whitespace-only appends are skipped; an event's segments are
// joined; an event without dDurationMs ends where it starts.
std::vector<Cue> parseJson3(std::string_view body);

}
#endif
