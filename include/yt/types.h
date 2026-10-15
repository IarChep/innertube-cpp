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

namespace detail { struct CursorAccess; }

// Where a list continues. Opaque: hand it to Client::more. False on the last page.
template <class T> class Cursor {
public:
    explicit operator bool() const { return !m_token.empty(); }
private:
    friend struct detail::CursorAccess;
    std::string m_endpoint, m_token;       // the InnerTube request that continues the list
    int m_client = 0;                      // ClientId: the client that served page 1 serves the rest
};

// One page of a list. Every list in the API is a Page; Client::more fetches the next.
template <class T> struct Page {
    std::vector<T> items;
    Cursor<T> next;                        // false: this was the last page
    bool disabled = false;                 // comments only: turned off for this video
};

enum class Rating { None, Like, Dislike };                       // the viewer's rating of a video
enum class SearchSort { Relevance, UploadDate, Views, TopRated };
struct SearchOptions { SearchSort sort = SearchSort::Relevance; };

struct WatchResult  { Video primary; std::vector<Video> related; };
struct Votes        { std::optional<int64_t> likes, dislikes; };
}
#endif
