// Video lists: every renderer that is a video, anywhere in a browse / search / next
// answer, plus the first continuation token in document order (rendererparser.h).
#include "continuation.h"
#include "rendererinternal.h"

namespace yt {

std::string parseText(Value field)
{
    return textOf(field);
}

std::vector<VideoItem> parseVideoList(Value response, std::string *nextToken)
{
    std::vector<VideoItem> out;
    std::string token;
    response.find({"lockupViewModel", "tileRenderer", "videoRenderer", "compactVideoRenderer", "gridVideoRenderer",
                   "playlistVideoRenderer", "continuationCommand", "nextContinuationData", "reloadContinuationData"},
                  [&](std::string_view key, Value v) {
        if (isTokenKey(key)) {
            if (token.empty()) token = tokenOf(key, v);
            return Visit::Skip;
        }
        // lockupViewModel and tileRenderer are self-contained leaves; only the VIDEO
        // content type becomes a video (playlist/channel ones are skipped).
        if (key == "lockupViewModel") {
            if (v["contentType"].str() == "LOCKUP_CONTENT_TYPE_VIDEO") out.push_back(fromLockupVideo(v));
        } else if (key == "tileRenderer") {
            if (v["contentType"].str() == "TILE_CONTENT_TYPE_VIDEO") out.push_back(fromTile(v));
        } else {
            out.push_back(fromVideoRenderer(v));
        }
        return Visit::SkipRest;
    });
    if (nextToken) *nextToken = token;
    return out;
}

// Direct single-renderer entry points (tests + callers holding the inner object).
VideoItem parseVideoRenderer(Value r) { return fromVideoRenderer(r); }
VideoItem parseLockupViewModel(Value lm) { return fromLockupVideo(lm); }
VideoItem parseTileRenderer(Value t) { return fromTile(t); }

}
