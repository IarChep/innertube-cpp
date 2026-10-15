// A YouTube link or id, recognized without the network: which screen it opens and the
// id that screen needs. Every Client method that takes a video, channel or playlist
// accepts the raw text and parses it the same way, so a consumer only needs this to
// decide which screen to open. C++17.
#ifndef YT_LINK_H
#define YT_LINK_H
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
namespace yt {

enum class LinkKind {
    Video,      // watch?v=, youtu.be/, embed/, v/, or a bare 11-character id
    Short,      // /shorts/<id>
    Live,       // /live/<id>
    Playlist,   // /playlist?list=, watch?list= without v=, or a bare PL… / RD… / UU… / OL… / LL / WL id
    Channel,    // /channel/UC…, or a bare UC… id
    Handle,     // /@name, or a bare @name
    Custom,     // /c/name (legacy custom URL; the server resolves it)
    User        // /user/name (legacy username URL; the server resolves it)
};

struct Link {
    LinkKind kind = LinkKind::Video;
    std::string id;              // video / playlist / channel id, or the name without '@' — byte-exact
    int64_t startSecs = 0;       // t= / start= on video links; 0 when absent

    // nullopt when the text is neither a YouTube link nor a recognizable id. Leading and
    // trailing whitespace (a pasted newline) is ignored.
    static std::optional<Link> parse(std::string_view text);
    // The canonical https://www.youtube.com URL this link stands for.
    std::string url() const;
};

}
#endif
