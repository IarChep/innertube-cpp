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
namespace detail { struct CursorAccess; }

// Where a list continues. Opaque: hand it to Client::more. False on the last page.
template <class T> class Cursor {
public:
    explicit operator bool() const { return !m_token.empty(); }
private:
    friend struct detail::CursorAccess;
    std::string m_endpoint, m_token;       // the InnerTube request that continues the list
    int m_client = 0;                      // ClientId: the client that served page 1 serves the rest
    std::string m_ownerId, m_ownerName;    // a channel's own tab: the channel its items belong to
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

// A channel as other things point at it: a video's uploader, a comment's author, a
// playlist's owner.
struct ChannelRef {
    std::string id, name, avatarUrl;       // id: UC…; any part may be empty when YouTube omits it
    bool subscribed = false;               // the viewer's state; known on a signed-in watch page only
};

// A video as a list shows it (feeds, search, a channel's or playlist's videos, related).
// Texts are as YouTube displays them, localized by hl; the numbers are 0 when unknown.
struct VideoItem {
    std::string id, title, thumbnailUrl;
    std::string durationText, viewText, publishedText;   // "4:56", "1.2M views", "3 days ago"
    int64_t durationSecs = 0;              // from durationText
    int64_t viewCount = 0;                 // only where YouTube gives the exact number
    ChannelRef channel;
};

// A watch page: the video in full and its first page of related videos.
struct Video : VideoItem {
    std::string description, likeText;     // likeText: "8.8K", as displayed
    std::optional<int64_t> likeCount;      // exact, when the page carries it
    Rating myRating = Rating::None;        // signed in: the viewer's like/dislike
    Page<VideoItem> related;
};

struct Playlist {
    std::string id, title, description, thumbnailUrl;
    ChannelRef owner;
    int64_t videoCount = 0;
    Page<VideoItem> entries;               // playlists().get(): the first page; empty in lists
};

struct Channel {
    std::string id, name, handle, description, avatarUrl, bannerUrl;
    std::string subscriberText, videoCountText;          // "15.1M subscribers", "4.5K videos"
    bool subscribed = false;               // signed in: the viewer's state
};

struct Comment {
    std::string id, text, publishedText, likeText;   // likeText as displayed ("291K")
    ChannelRef author;
    bool mine = false;                     // signed in: written by the viewer
};
// A top-level comment and its replies.
struct CommentThread {
    Comment comment;
    std::string replyText;                 // "675", "1.2K": as displayed; empty = no replies
};

struct AccountInfo { std::string id, name, avatarUrl, handle, channelId; };
struct Votes       { std::optional<int64_t> likes, dislikes; };
}
#endif
