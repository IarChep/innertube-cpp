// Public value types of innertube-cpp. UTF-8 std::string everywhere; server-issued
// URLs are kept byte-exact. C++17.
#ifndef YT_TYPES_H
#define YT_TYPES_H
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <yt/result.h>
namespace yt {
struct Video {
    std::string id, title, description, thumbnailUrl, largeThumbnailUrl, date, duration, url,
                streamUrl, channelId, channelName, avatarUrl, likeText, viewText;
    int64_t viewCount = 0;
    int likeStatus = 0;                      // 0 Indifferent, 1 Liked, 2 Disliked
    std::optional<int64_t> likeCount;        // authed /next only
    std::optional<int64_t> dislikeCount;     // RYD
    bool subscribed = false;                 // viewer's state for the channel (authed /next)
    bool downloadable = false;
};
struct Playlist { std::string id, title, description, thumbnailUrl, date, channelId, channelName; int videoCount = 0; };
struct Channel  { std::string id, name, description, thumbnailUrl, subscriberCount, bannerUrl, handle, videoCount; bool subscribed = false; };
struct Comment  { std::string id, body, date, channelId, author, thumbnailUrl, videoId; };
struct Account  { std::string id, name, thumbnailUrl, handle, channelId; };

struct VideoPage    { std::vector<Video> items; std::string next; };
struct PlaylistPage { std::vector<Playlist> items; std::string next; };
struct ChannelPage  { std::vector<Channel> items; std::string next; };
struct CommentPage  { std::vector<Comment> items; std::string next; std::string createCommentParams; bool disabled = false; };
struct WatchResult  { Video primary; std::vector<Video> related; };
struct Votes        { std::optional<int64_t> likes, dislikes; };
}
#endif
