#ifndef YT_PLAYERPARSER_H
#define YT_PLAYERPARSER_H
#include <string>
#include <string_view>
#include <yt/types.h>
namespace yt {
// All functions take the raw /player response bytes (the whole document).
// Parses ready-to-play streams (HLS manifest + non-ciphered progressive formats).
// If sawCipheredOnly is given, it is set true when the response carried progressive
// formats but every one required signature decipher (all skipped) — letting the
// caller report that distinctly from "no streams at all".
//
// Each entry point has a std::string_view form (used by bench/tests on subtrees)
// and a const std::string & overload that reads with the NUL-terminated sentinel
// path (kInDoc). Production callers pass *r.body (a std::string) — an exact match
// for the overload, so they take the sentinel path automatically.
std::vector<Stream> parseStreams(std::string_view playerResponse, bool *sawCipheredOnly = 0);
std::vector<Stream> parseStreams(const std::string &playerResponse, bool *sawCipheredOnly = 0);
Video parseVideoDetails(std::string_view playerResponse);
Video parseVideoDetails(const std::string &playerResponse);
std::vector<Subtitle> parseCaptions(std::string_view playerResponse);
std::vector<Subtitle> parseCaptions(const std::string &playerResponse);
bool isPlayable(std::string_view playerResponse, std::string *reason);
bool isPlayable(const std::string &playerResponse, std::string *reason);

// One typed read of the whole /player document — all four sections at once.
struct PlayerResult {
    bool playable = true;               // playabilityStatus.status missing or "OK"
    std::string reason;                     // "<STATUS>: <reason>" when !playable
    std::vector<Stream> streams;          // hls first, then non-ciphered progressive
    bool cipheredOnly = false;          // formats present but every one ciphered
    // Diagnostics (set by parsePlayer; used by the player trace — MEETUBE_PLAYER_DEBUG):
    bool sabr = false;                  // serverAbrStreamingUrl present → SABR-only session
    int  formatsSeen = 0;               // muxed streamingData.formats[] count
    int  adaptiveSeen = 0;              // streamingData.adaptiveFormats[] count
    std::vector<Subtitle> captions;
    Video details;
};
PlayerResult parsePlayer(std::string_view playerResponse);
PlayerResult parsePlayer(const std::string &playerResponse);
}
#endif
