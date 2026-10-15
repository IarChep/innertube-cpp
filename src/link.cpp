// yt::Link — the URL shapes YouTube hands out (watch, youtu.be, embed, shorts, live,
// playlist, channel, @handle, /c/, /user/) and bare ids, recognized by plain string
// checks: no regex, no network, ids byte-exact.
#include <yt/link.h>
namespace yt {

namespace {

constexpr size_t npos = std::string_view::npos;

bool idChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}
bool isIdText(std::string_view s)
{
    if (s.empty()) return false;
    for (char c : s) if (!idChar(c)) return false;
    return true;
}
bool isVideoId(std::string_view s) { return s.size() == 11 && isIdText(s); }
bool isChannelId(std::string_view s) { return s.size() == 24 && s.substr(0, 2) == "UC" && isIdText(s); }
bool isPlaylistId(std::string_view s)
{
    if (s == "LL" || s == "WL") return true;               // Liked / Watch Later
    if (s.size() < 12 || !isIdText(s)) return false;
    for (std::string_view p : {"PL", "RD", "UU", "OL", "FL"})
        if (s.substr(0, p.size()) == p) return true;
    return false;
}
// A handle or legacy name: anything up to the next separator (handles may be
// percent-encoded Unicode — kept byte-exact for the server to resolve).
bool isNameText(std::string_view s)
{
    if (s.empty()) return false;
    for (unsigned char c : s) if (c <= 0x20 || c == 0x7f || c == '/' || c == '?' || c == '#' || c == '&') return false;
    return true;
}

char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c; }
bool iequals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (lower(a[i]) != lower(b[i])) return false;
    return true;
}
bool istarts(std::string_view s, std::string_view prefix)
{
    return s.size() >= prefix.size() && iequals(s.substr(0, prefix.size()), prefix);
}

std::string_view trim(std::string_view s)
{
    while (!s.empty() && (unsigned char) s.front() <= 0x20) s.remove_prefix(1);
    while (!s.empty() && (unsigned char) s.back() <= 0x20) s.remove_suffix(1);
    return s;
}

// The value of `key` in "a=1&b=2"; empty when absent.
std::string_view param(std::string_view query, std::string_view key)
{
    while (!query.empty()) {
        const size_t amp = query.find('&');
        const std::string_view kv = query.substr(0, amp);
        const size_t eq = kv.find('=');
        if (eq != npos && kv.substr(0, eq) == key) return kv.substr(eq + 1);
        if (amp == npos) break;
        query.remove_prefix(amp + 1);
    }
    return std::string_view();
}

// "90", "90s", "1m30s", "1h2m3s" → seconds; 0 when malformed.
int64_t secondsOf(std::string_view t)
{
    int64_t total = 0, n = 0;
    bool digits = false;
    for (char c : t) {
        if (c >= '0' && c <= '9') {
            n = n * 10 + (c - '0');
            if (n > 100000000) return 0;
            digits = true;
            continue;
        }
        if (!digits) return 0;
        if (c == 'h') total += n * 3600;
        else if (c == 'm') total += n * 60;
        else if (c == 's') total += n;
        else return 0;
        n = 0;
        digits = false;
    }
    return total + n;                                 // a trailing bare number is seconds
}

// The path segment right after `prefix` ("/shorts/ID/x" → "ID"); empty when the path
// does not start with it.
std::string_view segment(std::string_view path, std::string_view prefix)
{
    if (path.substr(0, prefix.size()) != prefix) return std::string_view();
    path.remove_prefix(prefix.size());
    return path.substr(0, path.find('/'));
}

std::optional<Link> make(LinkKind kind, std::string_view id, int64_t start = 0)
{
    Link l;
    l.kind = kind;
    l.id = std::string(id);
    l.startSecs = start;
    return l;
}

std::optional<Link> parseBare(std::string_view s)
{
    if (s.front() == '@')
        return isNameText(s.substr(1)) ? make(LinkKind::Handle, s.substr(1)) : std::nullopt;
    if (isVideoId(s)) return make(LinkKind::Video, s);
    if (isChannelId(s)) return make(LinkKind::Channel, s);
    if (isPlaylistId(s)) return make(LinkKind::Playlist, s);
    return std::nullopt;
}

} // namespace

std::optional<Link> Link::parse(std::string_view text)
{
    std::string_view s = trim(text);
    if (s.empty()) return std::nullopt;
    if (s.find('/') == npos) return parseBare(s);

    const size_t scheme = s.find("://");
    if (scheme != npos) {
        const std::string_view name = s.substr(0, scheme);
        if (!iequals(name, "https") && !iequals(name, "http")) return std::nullopt;
        s.remove_prefix(scheme + 3);
    } else if (s.substr(0, 2) == "//") {
        s.remove_prefix(2);
    }
    const size_t hostEnd = s.find_first_of("/?#");
    std::string_view host = s.substr(0, hostEnd);
    std::string_view rest = hostEnd == npos ? std::string_view() : s.substr(hostEnd);
    if (const size_t colon = host.find(':'); colon != npos) {     // an explicit :port
        const std::string_view port = host.substr(colon + 1);
        if (port.empty() || port.find_first_not_of("0123456789") != npos) return std::nullopt;
        host = host.substr(0, colon);
    }
    for (std::string_view sub : {"www.", "m.", "music."})
        if (istarts(host, sub)) { host.remove_prefix(sub.size()); break; }
    rest = rest.substr(0, rest.find('#'));
    const size_t q = rest.find('?');
    const std::string_view path = rest.substr(0, q);
    const std::string_view query = q == npos ? std::string_view() : rest.substr(q + 1);
    int64_t start = secondsOf(param(query, "t"));
    if (start == 0) start = secondsOf(param(query, "start"));

    if (iequals(host, "youtu.be")) {
        const std::string_view id = segment(path, "/");
        return isVideoId(id) ? make(LinkKind::Video, id, start) : std::nullopt;
    }
    if (!iequals(host, "youtube.com") && !iequals(host, "youtube-nocookie.com")) return std::nullopt;

    if (path == "/watch" || path == "/watch/") {
        const std::string_view v = param(query, "v");
        if (!v.empty()) return isVideoId(v) ? make(LinkKind::Video, v, start) : std::nullopt;
        const std::string_view list = param(query, "list");
        return isPlaylistId(list) ? make(LinkKind::Playlist, list) : std::nullopt;
    }
    if (path == "/playlist" || path == "/playlist/") {
        const std::string_view list = param(query, "list");
        return isPlaylistId(list) ? make(LinkKind::Playlist, list) : std::nullopt;
    }
    struct Prefix { const char *path; LinkKind kind; };
    static const Prefix kVideoPaths[] = {
        {"/shorts/", LinkKind::Short}, {"/live/", LinkKind::Live},
        {"/embed/", LinkKind::Video}, {"/v/", LinkKind::Video},
    };
    for (const Prefix &p : kVideoPaths) {
        const std::string_view id = segment(path, p.path);
        if (!id.empty()) return isVideoId(id) ? make(p.kind, id, start) : std::nullopt;
    }
    if (const std::string_view id = segment(path, "/channel/"); !id.empty())
        return isChannelId(id) ? make(LinkKind::Channel, id) : std::nullopt;
    if (const std::string_view name = segment(path, "/@"); !name.empty())
        return isNameText(name) ? make(LinkKind::Handle, name) : std::nullopt;
    if (const std::string_view name = segment(path, "/c/"); !name.empty())
        return isNameText(name) ? make(LinkKind::Custom, name) : std::nullopt;
    if (const std::string_view name = segment(path, "/user/"); !name.empty())
        return isNameText(name) ? make(LinkKind::User, name) : std::nullopt;
    return std::nullopt;
}

std::string Link::url() const
{
    const std::string base = "https://www.youtube.com";
    switch (kind) {
    case LinkKind::Video:    return base + "/watch?v=" + id;
    case LinkKind::Short:    return base + "/shorts/" + id;
    case LinkKind::Live:     return base + "/live/" + id;
    case LinkKind::Playlist: return base + "/playlist?list=" + id;
    case LinkKind::Channel:  return base + "/channel/" + id;
    case LinkKind::Handle:   return base + "/@" + id;
    case LinkKind::Custom:   return base + "/c/" + id;
    case LinkKind::User:     return base + "/user/" + id;
    }
    return base;
}

}
