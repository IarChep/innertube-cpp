// The /player response parsers: one partial read per answer for the client walk
// (core/chains.cpp), the storyboard spec and the json3 caption format. Parsers never
// fail: malformed input yields absent fields / empty lists.
#ifndef YT_PLAYERPARSER_H
#define YT_PLAYERPARSER_H
#include <string>
#include <string_view>
#include <vector>
#include <yt/player.h>
namespace yt {

// The full view of one /player answer the client walk needs: one partial Glaze read,
// plus a bounded scan for the offline slate only when the stream is offline.
struct PlayerPart {
    PlayabilityStatus status;
    bool final = false;                   // ERROR / LIVE_STREAM_OFFLINE: no client will have streams
    VideoDetails details;
    std::vector<Format> formats;          // formats[] (muxed) then adaptiveFormats[]
    std::string hlsUrl, dashUrl, sabrUrl, sabrConfig;
    int64_t expiresInSecs = 0;
    std::vector<CaptionTrack> captions;
    std::string storyboardSpec;           // storyboards.playerStoryboardSpecRenderer.spec
    std::string watchtimeUrl, playbackUrl;
};
PlayerPart parsePlayerResponse(const std::string &body);
// "<template>|w#h#count#cols#rows#intervalMs#name#sigh|…" → levels; a level whose
// interval is 0 gets lengthSecs·1000 / count. Malformed levels are skipped.
std::vector<StoryboardLevel> parseStoryboardSpec(std::string_view spec, int64_t lengthSecs);
// A timedtext fmt=json3 body → cues. Window events and whitespace-only appends are skipped.
std::vector<Cue> parseJson3(const std::string &body);
}
#endif
