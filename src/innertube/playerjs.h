// What is read out of YouTube's player JavaScript without running it: which player
// build is live (named by the /tv page) and that build's signatureTimestamp. Byte scans
// over ASCII markers — base.js is ~3 MB and a 1 GB device must not copy it around.
#ifndef YT_INNERTUBE_PLAYERJS_H
#define YT_INNERTUBE_PLAYERJS_H
#include <string>
#include <string_view>
namespace yt {
std::string playerIdIn(std::string_view html);          // "" when the page names none
int signatureTimestampIn(std::string_view js);          // 0 when absent
std::string playerScriptUrl(std::string_view playerId); // the build's player_ias base.js
}
#endif
