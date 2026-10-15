# API groups, links, pages and futures (R2–R5) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `yt::Client`'s flat, InnerTube-shaped methods with resource groups (`yt.videos()`, `.search()`, `.channels()`, `.playlists()`, `.feeds()`, `.comments()`, `.account()`) that accept any YouTube link, return every list as `yt::Page<T>` continued by one `yt.more(cursor)`, and offer a blocking `std::future` form next to every callback.

**Architecture:** Bottom-up in seven green steps. A pure `yt::Link` parser (Task 1) and the opaque `Page<T>`/`Cursor<T>` value types (Task 2) come first; the request chains in `core/chains.cpp` then produce pages with cursors and continue them (Task 3) and learn to take links and do the composite account/comment/channel requests (Task 4). The public facade switches to groups in one breaking step (Task 5) — thin wrappers in a new `src/groups.cpp` over the unchanged `Engine::start` — then gains future overloads delivered straight from the engine (Task 6). Task 7 rewrites the examples and docs and verifies all four build trees and the live service.

**Tech Stack:** C++23 implementation / C++17 public headers, Glaze (private), doctest, CMake + Ninja, libcurl backend.

**Spec:** [`docs/specs/2026-10-15-api-scenarios.md`](../specs/2026-10-15-api-scenarios.md) (scenarios S1–S14 and the owner's decisions), arguing from [`docs/specs/2026-10-15-api-usability-research.md`](../specs/2026-10-15-api-usability-research.md) (R2–R5). Read the scenarios' "Decisions" section first.

### Scope and deliberate deviations from the scenarios

In scope: R2 (groups), R3 (links everywhere), R4 (pages, cursors, no server strings), R5 (futures), plus the umbrella header `<yt/yt.h>` that S1 uses. Out of scope (later plans): R6 selection helpers (`choose`, `captionFor`, `cueAt`), R7 type split (`VideoItem`/`Video`, `Video::myRating`, `Channel::tabs`), R8 `raw()`, and every phase-2/3 feature the scenarios tag (bell, save states, replies, shorts/live tabs, playlist headers, history reporting). Where the scenarios name something this plan cannot honour yet, the API grows later instead of changing:

| Scenario | This plan | Why |
|---|---|---|
| S7 `playlists().get(link)` → `Playlist` with entries | `playlists().videos(link)` → `Page<Video>` | the playlist header is not parsed yet; `get()` is added with it, nothing changes |
| S11 `account().unsave(entry)` (a `PlaylistEntry` handle) | `account().unsave(videoLink, playlistLink)` | `ACTION_REMOVE_VIDEO_BY_VIDEO_ID` removes by video id — no hidden `setVideoId` handle needed |
| S4 `feeds().trending()` | `feeds().topic(id)` with ids from `yt::topicFeeds()` | YouTube removed Trending (2025); `catalog.cpp` already notes FE destinations reject a plain browse |
| S6 `channels().shorts/streams` | not added | shorts/live items need the phase-3 parsers |
| `yt::Account` (the identity struct) | renamed `yt::AccountInfo` | the group class is `yt::Account` (owner's decision: `account()`) |

## Global Constraints

- Public headers (`include/yt/`) compile as C++17: no `std::expected`, `<format>`, ranges, designated initializers. `tst_types` and `tst_link` build at C++17 to prove it.
- No Qt anywhere (`innertube_no_qt` ctest). No new third-party dependency. Glaze never in a public header.
- Layering (never reverse): `parsers → json`, `core/chains → parsers, innertube, json`, `net → json, innertube`, `client/auth → core, net`. Public headers include only public headers and the standard library.
- Every public method only posts work; a callback runs exactly once or never and never from inside the call that started it; multi-step chains check `live(job)` between steps and stop silently when canceled.
- URLs are byte-exact: never decode or re-encode a server-issued URL. Credentials never reach a log.
- No exceptions in the library except `Client`'s constructor. Asynchronous outcomes are `Result<T>`; keep `httpStatus`.
- Style: 4 spaces, `&`/`*` bind to the name, function-definition braces on their own line, `m_` members, `k` constants, C-style casts with a space (`(int) x`), `nullptr`, header guards `YT_<AREA>_<NAME>_H`, every file starts with a header comment (public headers end it with `C++17.`), no `TODO`/`FIXME`/commented-out code, comments explain *why*.
- Done = warning-free build and all ctests green in `build`, `build-asan`, `build-tsan`, `build-none`; transport suites green with `http_proxy=http://127.0.0.1:9`; every fix has a test that fails without it.
- Commit only when the repository owner has said so for this plan (AGENTS.md §10). Commit steps show the message; skip `git commit` if commits were not authorized. Trailer lines (both):
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and
  `Claude-Session: https://claude.ai/code/session_01CcpAWezZwfUf6YSVnLXjYq`.

## Review Focus

1. **Pasted links carry noise**: `?feature=share&v=…`, `youtu.be/ID?si=…`, an upper-case host, `m.`/`music.` hosts, a trailing newline from the clipboard. All must parse to the same id (tests in Task 1).
2. **An exhausted cursor handed to `more`**: `Page::next` is false on the last page; `more()` with it must fail with `ErrorCode::InvalidArgument` and send nothing — not post an empty continuation (test in Task 3).
3. **A search continuation must not resend the query**: the second page posts only `{continuation}` to `search`, or YouTube answers page 1 again (test in Task 3).
4. **A link of the wrong kind** (a video link to `channels()`, garbage to `playlists()`): `InvalidArgument`, delivered asynchronously, with no request sent (tests in Tasks 4 and 5).
5. **The future form never hangs**: a GUI's executor that is blocked (or never run) must not stop a future from completing, and destroying the `Client` must release a pending future with `ErrorCode::Canceled` (tests in Task 6).

---

## File map

| File | Task | Responsibility |
|---|---|---|
| `include/yt/link.h`, `src/link.cpp` (new) | 1 | `yt::Link`, `LinkKind`, `Link::parse`, `Link::url` |
| `include/yt/types.h`, `include/yt/result.h`, `src/core/cursor.h` (new) | 2 | `Cursor<T>`, `Page<T>`, `Rating`, `SearchSort`, `SearchOptions`, `ErrorCode::InvalidArgument`, `detail::CursorAccess` |
| `src/innertube/bodies.*` | 2, 4 | `continuation()`, `browse()` without continuation, `playlistAddVideo/RemoveVideo` |
| `src/core/chains.*` | 3, 4 | pages + `fetchMore`; link helpers; `fetchChannelId/Channel/ChannelVideos/ChannelPlaylists`, `channelAction`, `postComment(videoId)`, `editPlaylist(PlaylistEdit)` |
| `src/innertube/catalog.h` | 4 | channel tab params |
| `include/yt/client.h`, `src/client.cpp`, `src/engine.h`, `src/groups.cpp` (new) | 3, 5, 6 | the groups, `Client::more`, future overloads, direct delivery |
| `include/yt/yt.h` (new) | 7 | umbrella header |
| `examples/*`, `README.md`, `AGENTS.md`, `docs/specs/*` | 3, 5, 7 | consumers and docs |

Build/test commands (from the repo root):

```sh
cmake --build build -j 2>&1 | grep -E "warning|error"; cmake --build build -j
(cd build && ctest -j8 --output-on-failure -R '<suite>')
```

---

### Task 1: `yt::Link` — recognize any YouTube link without the network

**Files:**
- Create: `include/yt/link.h`, `src/link.cpp`, `tests/tst_link.cpp`
- Modify: `CMakeLists.txt` (add `src/link.cpp` after `src/player.cpp`), `tests/CMakeLists.txt` (add `it_test(tst_link STD17)` after `it_test(tst_types STD17)`)

**Interfaces:**
- Produces: `enum class yt::LinkKind { Video, Short, Live, Playlist, Channel, Handle, Custom, User };` `struct yt::Link { LinkKind kind; std::string id; int64_t startSecs; static std::optional<Link> parse(std::string_view); std::string url() const; };`

- [ ] **Step 1: Write the failing test** — create `tests/tst_link.cpp`:

```cpp
// yt::Link — every link shape a user pastes, parsed without the network. Built at C++17.
#include <doctest/doctest.h>
#include <yt/link.h>

using yt::Link;
using yt::LinkKind;

namespace {
bool is(const char *text, LinkKind kind, const char *id, long long start = 0)
{
    const std::optional<Link> l = Link::parse(text);
    return l && l->kind == kind && l->id == id && l->startSecs == start;
}
}

TEST_CASE("videoLinks") {
    CHECK(is("dQw4w9WgXcQ", LinkKind::Video, "dQw4w9WgXcQ"));
    CHECK(is("https://www.youtube.com/watch?v=dQw4w9WgXcQ", LinkKind::Video, "dQw4w9WgXcQ"));
    CHECK(is("https://youtube.com/watch?feature=share&v=dQw4w9WgXcQ&t=1m30s", LinkKind::Video, "dQw4w9WgXcQ", 90));
    CHECK(is("https://youtu.be/dQw4w9WgXcQ?si=AbCdEf&t=42", LinkKind::Video, "dQw4w9WgXcQ", 42));
    CHECK(is("HTTPS://WWW.YOUTUBE.COM/watch?v=dQw4w9WgXcQ", LinkKind::Video, "dQw4w9WgXcQ"));
    CHECK(is("  https://m.youtube.com/watch?v=dQw4w9WgXcQ\n", LinkKind::Video, "dQw4w9WgXcQ"));
    CHECK(is("https://music.youtube.com/watch?v=dQw4w9WgXcQ&list=RDAMVM", LinkKind::Video, "dQw4w9WgXcQ"));
    CHECK(is("youtube.com/watch?v=dQw4w9WgXcQ", LinkKind::Video, "dQw4w9WgXcQ"));
    CHECK(is("https://www.youtube-nocookie.com/embed/dQw4w9WgXcQ?start=10", LinkKind::Video, "dQw4w9WgXcQ", 10));
    CHECK(is("https://www.youtube.com/v/dQw4w9WgXcQ", LinkKind::Video, "dQw4w9WgXcQ"));
    CHECK(is("https://www.youtube.com/shorts/aqz-KE-bpKQ", LinkKind::Short, "aqz-KE-bpKQ"));
    CHECK(is("https://www.youtube.com/live/jfKfPfyJRdk?si=x", LinkKind::Live, "jfKfPfyJRdk"));
    CHECK(is("https://youtu.be/dQw4w9WgXcQ?t=1h2m3s", LinkKind::Video, "dQw4w9WgXcQ", 3723));
}
TEST_CASE("playlistAndChannelLinks") {
    CHECK(is("https://www.youtube.com/playlist?list=PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI", LinkKind::Playlist,
             "PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI"));
    CHECK(is("https://www.youtube.com/watch?list=PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI", LinkKind::Playlist,
             "PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI"));
    CHECK(is("PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI", LinkKind::Playlist, "PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI"));
    CHECK(is("WL", LinkKind::Playlist, "WL"));
    CHECK(is("LL", LinkKind::Playlist, "LL"));
    CHECK(is("https://www.youtube.com/channel/UCuAXFkgsw1L7xaCfnd5JJOw", LinkKind::Channel, "UCuAXFkgsw1L7xaCfnd5JJOw"));
    CHECK(is("UCuAXFkgsw1L7xaCfnd5JJOw", LinkKind::Channel, "UCuAXFkgsw1L7xaCfnd5JJOw"));
    CHECK(is("https://www.youtube.com/@NASA/videos", LinkKind::Handle, "NASA"));
    CHECK(is("@NASA", LinkKind::Handle, "NASA"));
    CHECK(is("https://www.youtube.com/c/LinusTechTips", LinkKind::Custom, "LinusTechTips"));
    CHECK(is("https://www.youtube.com/user/RickAstleyVEVO", LinkKind::User, "RickAstleyVEVO"));
}
TEST_CASE("notLinks") {
    CHECK_FALSE(Link::parse("").has_value());
    CHECK_FALSE(Link::parse("   ").has_value());
    CHECK_FALSE(Link::parse("dQw4w9WgXc").has_value());                         // 10 characters
    CHECK_FALSE(Link::parse("dQw4w9WgX!Q").has_value());                        // foreign character
    CHECK_FALSE(Link::parse("https://example.com/watch?v=dQw4w9WgXcQ").has_value());
    CHECK_FALSE(Link::parse("ftp://www.youtube.com/watch?v=dQw4w9WgXcQ").has_value());
    CHECK_FALSE(Link::parse("https://youtu.be/").has_value());
    CHECK_FALSE(Link::parse("https://www.youtube.com/watch?v=short").has_value());
    CHECK_FALSE(Link::parse("https://www.youtube.com/feed/history").has_value());
    CHECK_FALSE(Link::parse("@").has_value());
    CHECK_FALSE(Link::parse("rick astley").has_value());
}
TEST_CASE("linkUrl") {
    CHECK(Link::parse("dQw4w9WgXcQ")->url() == "https://www.youtube.com/watch?v=dQw4w9WgXcQ");
    CHECK(Link::parse("@NASA")->url() == "https://www.youtube.com/@NASA");
    CHECK(Link::parse("https://www.youtube.com/c/LinusTechTips")->url() == "https://www.youtube.com/c/LinusTechTips");
    CHECK(Link::parse("https://www.youtube.com/user/RickAstleyVEVO")->url() == "https://www.youtube.com/user/RickAstleyVEVO");
    CHECK(Link::parse("WL")->url() == "https://www.youtube.com/playlist?list=WL");
}
```

Add `it_test(tst_link STD17)` to `tests/CMakeLists.txt` after `it_test(tst_types STD17)`.

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake -S . -B build -G Ninja >/dev/null && cmake --build build -j`
Expected: `fatal error: yt/link.h: No such file or directory`.

- [ ] **Step 3: Create `include/yt/link.h`**

```cpp
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
```

- [ ] **Step 4: Create `src/link.cpp`**

```cpp
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
```

Add `src/link.cpp` to the `add_library(innertube …)` list in `CMakeLists.txt`, right after `src/player.cpp`.

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake -S . -B build -G Ninja >/dev/null && cmake --build build -j 2>&1 | grep -E "warning|error"; (cd build && ctest --output-on-failure -R tst_link)`
Expected: no warnings; PASS.

- [ ] **Step 6: Commit**

```bash
git add include/yt/link.h src/link.cpp tests/tst_link.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "lib: yt::Link — any YouTube link or id, parsed without the network" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CcpAWezZwfUf6YSVnLXjYq"
```

---

### Task 2: `Page<T>`, `Cursor<T>`, the new enums, and the continuation body

**Files:**
- Modify: `include/yt/types.h`, `include/yt/result.h`
- Create: `src/core/cursor.h`
- Modify: `src/innertube/bodies.h`, `src/innertube/bodies.cpp`, `src/core/chains.cpp` (call sites of the renamed builders)
- Test: `tests/tst_types.cpp`, `tests/tst_bodies.cpp`, `tests/tst_chains.cpp`

**Interfaces:**
- Produces (namespace `yt`, in `types.h`): `template <class T> class Cursor { explicit operator bool() const; };` `template <class T> struct Page { std::vector<T> items; Cursor<T> next; bool disabled = false; };` `enum class Rating { None, Like, Dislike };` `enum class SearchSort { Relevance, UploadDate, Views, TopRated };` `struct SearchOptions { SearchSort sort = SearchSort::Relevance; };`
- `ErrorCode::InvalidArgument` (appended after `Canceled`).
- `src/core/cursor.h`: `struct yt::detail::CursorAccess { template <class T> static Cursor<T> make(std::string endpoint, ClientId, std::string token); static … endpoint(c), client(c), token(c); };`
- `bodies::continuation(std::string_view token)` → `{"continuation":"…"}` (replaces `nextContinuation`); `bodies::browse(browseId, params)` (the continuation parameter is gone).

- [ ] **Step 1: Write the failing tests**

Append to `tests/tst_types.cpp`:

```cpp
TEST_CASE("Page and Cursor defaults") {
    yt::Page<yt::Video> p;
    CHECK(p.items.empty());
    CHECK_FALSE(p.next);                              // no next page
    CHECK_FALSE(p.disabled);
    CHECK(yt::SearchOptions{}.sort == yt::SearchSort::Relevance);
    yt::Rating r = yt::Rating::None;
    CHECK(r != yt::Rating::Like);
    CHECK(yt::ErrorCode::InvalidArgument != yt::ErrorCode::Canceled);
}
```

In `tests/tst_bodies.cpp` replace every `bodies::nextContinuation(` with `bodies::continuation(` and every three-argument `bodies::browse(a, b, "")` with `bodies::browse(a, b)`; delete any check that passes a non-empty third argument to `bodies::browse` and add:

```cpp
TEST_CASE("continuation body") {
    CHECK(bodies::continuation("TOK") == "{\"continuation\":\"TOK\"}");
    CHECK(bodies::browse("UCx", "EgZ2aWRlb3PyBgQKAjoA") == "{\"browseId\":\"UCx\",\"params\":\"EgZ2aWRlb3PyBgQKAjoA\"}");
    CHECK(bodies::browse("FEwhat_to_watch", "") == "{\"browseId\":\"FEwhat_to_watch\"}");
}
```

Append to `tests/tst_chains.cpp` (add `#include "core/cursor.h"` to its includes):

```cpp
TEST_CASE("cursorAccessRoundTrip") {
    const Cursor<Video> c = detail::CursorAccess::make<Video>("browse", ClientId::TVHTML5, "TOK");
    CHECK(bool(c));
    CHECK(detail::CursorAccess::endpoint(c) == "browse");
    CHECK(detail::CursorAccess::client(c) == ClientId::TVHTML5);
    CHECK(detail::CursorAccess::token(c) == "TOK");
    CHECK_FALSE(detail::CursorAccess::make<Video>("browse", ClientId::WEB, ""));   // no token = no next page
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build build -j 2>&1 | grep -E "error" | head -3`
Expected: `'Page' is not a member of 'yt'` (and `'continuation' is not a member of 'yt::bodies'`).

- [ ] **Step 3: Public types** — `include/yt/result.h`: append to `ErrorCode` after `Canceled`:

```cpp
    Canceled,
    InvalidArgument   // a link or argument the library cannot use (checked before any request)
```

(change the existing `Canceled` line to end with a comma). `include/yt/types.h`: after the `struct Account …` line add:

```cpp
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
```

- [ ] **Step 4: `src/core/cursor.h`**

```cpp
// Builds and reads the opaque yt::Cursor<T> (yt/types.h). Only the chains know which
// request continues a list: the endpoint and the client that served page 1 (signed-in
// feeds are TVHTML5, and YouTube only continues a list for the client that started it).
#ifndef YT_CORE_CURSOR_H
#define YT_CORE_CURSOR_H
#include <yt/catalog.h>
#include <yt/types.h>
#include <string>
namespace yt::detail {

struct CursorAccess {
    // An empty token yields the false cursor: there is no next page.
    template <class T> static Cursor<T> make(std::string endpoint, ClientId client, std::string token)
    {
        Cursor<T> c;
        if (token.empty()) return c;
        c.m_endpoint = std::move(endpoint);
        c.m_client = (int) client;
        c.m_token = std::move(token);
        return c;
    }
    template <class T> static const std::string &endpoint(const Cursor<T> &c) { return c.m_endpoint; }
    template <class T> static ClientId client(const Cursor<T> &c) { return (ClientId) c.m_client; }
    template <class T> static const std::string &token(const Cursor<T> &c) { return c.m_token; }
};

}
#endif
```

- [ ] **Step 5: Bodies** — `src/innertube/bodies.h`: replace the `browse` and `nextContinuation` declarations with

```cpp
// {browseId[,params]} — the first page of a browse; later pages are continuation().
std::string browse(std::string_view browseId, std::string_view params);
// {continuation} — the next page of any list (browse, search, next): the endpoint and the
// client of page 1 carry the rest.
std::string continuation(std::string_view token);
```

`src/innertube/bodies.cpp`: replace `browse` and `nextContinuation` with

```cpp
std::string browse(std::string_view browseId, std::string_view params)
{
    bj::Browse b;
    b.browseId = std::string(browseId);
    b.params = opt(params);                          // tab selector, e.g. a channel's Videos tab
    return dump(b);
}
```

```cpp
std::string continuation(std::string_view token)
{
    bj::Next b;
    b.continuation = std::string(token);
    return dump(b);
}
```

In `src/core/chains.cpp`: replace `bodies::nextContinuation(` with `bodies::continuation(`; replace `bodies::browse(spec.browseId, spec.params, spec.page)` with `spec.page.empty() ? bodies::browse(spec.browseId, spec.params) : bodies::continuation(spec.page)`; replace every other `bodies::browse(x, y, z)` call with `z.empty() ? bodies::browse(x, y) : bodies::continuation(z)` using the same argument expressions (these call sites are rewritten in Task 3; this keeps the tree green). `grep -n "bodies::browse\|nextContinuation" src` must then show only two-argument `browse` calls.

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cmake --build build -j 2>&1 | grep -E "warning|error"; (cd build && ctest -j8 --output-on-failure)`
Expected: no warnings; all suites PASS.

- [ ] **Step 7: Commit**

```bash
git add include/yt/types.h include/yt/result.h src/core/cursor.h src/innertube/bodies.h src/innertube/bodies.cpp src/core/chains.cpp tests/tst_types.cpp tests/tst_bodies.cpp tests/tst_chains.cpp
git commit -m "lib: Page<T> and opaque Cursor<T>; one continuation body for every list" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CcpAWezZwfUf6YSVnLXjYq"
```

---

### Task 3: every list is a `Page<T>` continued by `Client::more`

**Files:**
- Modify: `src/core/chains.h`, `src/core/chains.cpp` (list chains, `fetchMore`, comments first page)
- Modify: `include/yt/types.h` (drop `VideoPage`, `PlaylistPage`, `ChannelPage`, `CommentPage`)
- Modify: `include/yt/client.h`, `src/client.cpp` (transitional signatures + `Client::more`)
- Modify: `examples/yt-cli/main.cpp`, `examples/watch/main.cpp`, `examples/channel/main.cpp`
- Test: `tests/tst_chains.cpp`, `tests/tst_client.cpp`, `tests/tst_auth.cpp`

**Interfaces:**
- Consumes: Task 2 (`Page`, `Cursor`, `CursorAccess`, `bodies::continuation`, `SearchSort`).
- Produces in `core`:
  - `struct VideoListSpec { enum Kind { Browse, Search } kind = Browse; std::string browseId, params; std::string query; SearchSort sort = SearchSort::Relevance; };`
  - `void fetchVideoList(IHttp &, const VideoListSpec &, const JobToken &, Callback<Page<Video>>);`
  - `void fetchComments(IHttp &, std::string videoId, const JobToken &, Callback<Page<Comment>>);`
  - `void fetchUserSearch(IHttp &, std::string query, const JobToken &, Callback<Page<Channel>>);`
  - `void fetchChannelList(IHttp &, std::string browseId, const JobToken &, Callback<Page<Channel>>);`
  - `void fetchPlaylists(IHttp &, std::string resourceId, std::string params, const JobToken &, Callback<Page<Playlist>>);`
  - `void fetchPlaylistSearch(IHttp &, std::string query, const JobToken &, Callback<Page<Playlist>>);`
  - `void fetchMore(IHttp &, const Cursor<T> &, const JobToken &, Callback<Page<T>>);` for `T` = `Video`, `Channel`, `Playlist`, `Comment` (four overloads).
- Produces on `yt::Client` (transitional until Task 5): `browse(BrowseRequest{browseId, params}, Callback<Page<Video>>)`, `search(SearchRequest{query, sort}, Callback<Page<Video>>)`, `comments(videoId, Callback<Page<Comment>>)`, `channelList(browseId, Callback<Page<Channel>>)`, `searchChannels(q, Callback<Page<Channel>>)`, `playlists(resourceId, params, Callback<Page<Playlist>>)`, `searchPlaylists(q, Callback<Page<Playlist>>)`, and the final `Job more(const Cursor<T> &, Callback<Page<T>>)` × 4.

- [ ] **Step 1: Write the failing tests** — in `tests/tst_chains.cpp`:

Replace the tests `videoBrowseFeed`, `videoSearchVideos`, `videoBrowseContinuation`, `videoAuthedFeedBrowses`, `videoListTransportErrorKeepsCode`, `commentsTwoStep`, `commentsDisabled`, `commentsDirectContinuation`, `commentsCanceledBetweenSteps`, `playlistSearch`, `channelListBrowsesGrid`, `playlistBrowse`, `userSearch` with:

```cpp
// ---- lists: Page<T> + cursors ----
namespace {
template <class T> std::string tokenOf(const Cursor<T> &c) { return detail::CursorAccess::token(c); }
const char *kSearchPage1 =
    "{\"contents\":{\"twoColumnSearchResultsRenderer\":{\"primaryContents\":{\"sectionListRenderer\":{\"contents\":["
    "{\"itemSectionRenderer\":{\"contents\":[{\"videoRenderer\":{\"videoId\":\"aaa11111111\",\"title\":{\"runs\":[{\"text\":\"A\"}]}}}]}},"
    "{\"continuationItemRenderer\":{\"continuationEndpoint\":{\"continuationCommand\":{\"token\":\"SEARCH1\"}}}}]}}}}}";
const char *kSearchPage2 =
    "{\"onResponseReceivedCommands\":[{\"appendContinuationItemsAction\":{\"continuationItems\":["
    "{\"itemSectionRenderer\":{\"contents\":[{\"videoRenderer\":{\"videoId\":\"bbb22222222\",\"title\":{\"runs\":[{\"text\":\"B\"}]}}}]}}]}}]}";
}

TEST_CASE("videoBrowseFeed") {
    FakeHttp t; t.queue("browse", loadFixtureRaw("browse_feed.json"));
    VideoListSpec spec; spec.kind = VideoListSpec::Browse; spec.browseId = "FEwhat_to_watch";
    Cap<Page<Video>> c;
    fetchVideoList(t, spec, newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    REQUIRE(c.r.ok());
    REQUIRE(c.r.value.items.size() == 2);
    CHECK(c.r.value.items[0].id == "ccc33333333");
    CHECK(tokenOf(c.r.value.next) == "FEEDNEXT");
    CHECK(has(t.sent[0], "\"browseId\":\"FEwhat_to_watch\""));
}
TEST_CASE("a signed-in feed continues on the client that served page 1") {
    FakeHttp t; t.session().bearer = "tok";
    t.queue("browse", loadFixtureRaw("browse_feed.json"));
    t.queue("browse", loadFixtureRaw("browse_feed.json"));
    VideoListSpec spec; spec.browseId = "FEsubscriptions";
    Cap<Page<Video>> first, second;
    fetchVideoList(t, spec, newJob(), first.fn());
    t.flush();
    REQUIRE(first.r.ok());
    REQUIRE(first.r.value.next);
    fetchMore(t, first.r.value.next, newJob(), second.fn());
    t.flush();
    REQUIRE(second.r.ok());
    CHECK(t.sentClients == std::vector<int>{(int) ClientId::TVHTML5, (int) ClientId::TVHTML5});
    CHECK(t.sent[1] == "{\"continuation\":\"FEEDNEXT\"}");
}
// Review focus: the second search page posts only the continuation — resending the query
// makes YouTube answer page 1 again.
TEST_CASE("search pages continue through the search endpoint without the query") {
    FakeHttp t;
    t.queue("search", kSearchPage1);
    t.queue("search", kSearchPage2);
    VideoListSpec spec; spec.kind = VideoListSpec::Search; spec.query = "cats"; spec.sort = SearchSort::UploadDate;
    Cap<Page<Video>> first, second;
    fetchVideoList(t, spec, newJob(), first.fn());
    t.flush();
    REQUIRE(first.r.ok());
    CHECK(has(t.sent[0], "\"query\":\"cats\""));
    CHECK(has(t.sent[0], "\"params\":\"CAISAhAB\""));
    CHECK(tokenOf(first.r.value.next) == "SEARCH1");
    fetchMore(t, first.r.value.next, newJob(), second.fn());
    t.flush();
    REQUIRE(second.r.ok());
    REQUIRE(second.r.value.items.size() == 1);
    CHECK(second.r.value.items[0].id == "bbb22222222");
    CHECK(t.sent[1] == "{\"continuation\":\"SEARCH1\"}");
    CHECK(t.lastClientFor("search") == (int) ClientId::WEB);
    CHECK_FALSE(second.r.value.next);                 // the last page
}
// Review focus: an exhausted cursor fails and sends nothing.
TEST_CASE("more with no next page fails with InvalidArgument and sends nothing") {
    FakeHttp t;
    Cap<Page<Video>> c;
    fetchMore(t, Cursor<Video>{}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.calls == 1);
    REQUIRE_FALSE(c.r.ok());
    CHECK(c.r.error->code == ErrorCode::InvalidArgument);
    CHECK(t.sent.empty());
}
TEST_CASE("videoListTransportErrorKeepsCode") {
    FakeHttp t; t.queue("browse", "{\"error\":{}}", 401);
    VideoListSpec spec; spec.browseId = "FEsubscriptions";
    Cap<Page<Video>> c;
    fetchVideoList(t, spec, newJob(), c.fn());
    t.flush();
    REQUIRE_FALSE(c.r.ok());
    CHECK(c.r.error->code == ErrorCode::Api);
    CHECK(c.r.error->httpStatus == 401);
}
TEST_CASE("comments: two steps, then pages through next") {
    FakeHttp t;
    t.queue("next", loadFixtureRaw("next_for_comments.json"));
    t.queue("next", loadFixtureRaw("comments_page.json"));
    t.queue("next", loadFixtureRaw("comments_page.json"));
    Cap<Page<Comment>> c, more;
    fetchComments(t, "aaa11111111", newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(c.r.value.items.size() == 2);
    CHECK(tokenOf(c.r.value.next) == "MORE_COMMENTS");
    CHECK(has(t.sent[0], "\"videoId\":\"aaa11111111\""));
    CHECK(has(t.sent[1], "\"continuation\":\"COMMENTS_TOKEN\""));
    fetchMore(t, c.r.value.next, newJob(), more.fn());
    t.flush();
    REQUIRE(more.r.ok());
    CHECK(t.sent[2] == "{\"continuation\":\"MORE_COMMENTS\"}");
    CHECK(t.lastClientFor("next") == (int) ClientId::WEB);
}
TEST_CASE("commentsDisabled") {
    FakeHttp t; t.queue("next", "{}");
    Cap<Page<Comment>> c;
    fetchComments(t, "aaa11111111", newJob(), c.fn());
    t.flush();
    CHECK(t.sent.size() == 1);
    REQUIRE(c.r.ok());
    CHECK(c.r.value.items.empty());
    CHECK(c.r.value.disabled);
    CHECK_FALSE(c.r.value.next);
}
TEST_CASE("commentsCanceledBetweenSteps") {
    FakeHttp t;
    t.queue("next", loadFixtureRaw("next_for_comments.json"));
    t.queue("next", loadFixtureRaw("comments_page.json"));
    JobToken job = newJob();
    Cap<Page<Comment>> c;
    fetchComments(t, "aaa11111111", job, c.fn());
    job->canceled = true;
    t.flush();
    CHECK(c.calls == 0);
    CHECK(t.sent.size() == 1);
}
TEST_CASE("playlistSearch") {
    FakeHttp t;
    t.queue("search", "{\"contents\":[{\"playlistRenderer\":{\"playlistId\":\"PL1\",\"title\":{\"simpleText\":\"L\"}}}]}");
    Cap<Page<Playlist>> c;
    fetchPlaylistSearch(t, "foo", newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    REQUIRE(c.r.value.items.size() == 1);
    CHECK(c.r.value.items[0].id == "PL1");
    CHECK(has(t.sent[0], "\"params\":\"EgIQAw==\""));
}
TEST_CASE("channelListBrowsesGrid") {
    FakeHttp t; t.session().bearer = "tok";
    t.queue("browse",
            "{\"contents\":[{\"gridChannelRenderer\":{\"channelId\":\"UCsub1\","
            "\"title\":{\"simpleText\":\"Sub One\"}}},"
            "{\"gridChannelRenderer\":{\"channelId\":\"UCsub2\","
            "\"title\":{\"simpleText\":\"Sub Two\"}}}]}");
    Cap<Page<Channel>> c;
    fetchChannelList(t, "FEchannels", newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    REQUIRE(c.r.value.items.size() == 2);
    CHECK(c.r.value.items[0].id == "UCsub1");
    CHECK(has(t.sent[0], "\"browseId\":\"FEchannels\""));
    CHECK(t.lastClientFor("browse") == (int) ClientId::TVHTML5);
}
TEST_CASE("playlistBrowse") {
    FakeHttp t;
    t.queue("browse", "{\"contents\":[{\"playlistRenderer\":{\"playlistId\":\"PL9\",\"title\":{\"simpleText\":\"X\"}}}]}");
    Cap<Page<Playlist>> c;
    fetchPlaylists(t, "UCabc", "", newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    REQUIRE(c.r.value.items.size() == 1);
    CHECK(c.r.value.items[0].id == "PL9");
    CHECK(has(t.sent[0], "\"browseId\":\"UCabc\""));
}
```

Keep the existing `userSearch` test but change its `Cap<ChannelPage>` to `Cap<Page<Channel>>` (and any `.next` string comparison to `tokenOf(...)`).

In `tests/tst_client.cpp` and `tests/tst_auth.cpp`, migrate mechanically:

```sh
sed -i -E 's/yt::VideoPage/yt::Page<yt::Video>/g; s/yt::CommentPage/yt::Page<yt::Comment>/g; s/\{("[^"]*"( \+ std::to_string\(i\))?), "", ""\}/{\1, ""}/g' tests/tst_client.cpp tests/tst_auth.cpp
sed -i -E 's/c->comments\("([^"]*)", "", /c->comments("\1", /g' tests/tst_client.cpp
```

then in `tests/tst_client.cpp` "every endpoint reaches its chain" change `c->search({"q", "date"}, nop);` → `c->search({"q", yt::SearchSort::UploadDate}, nop);`, `c->channelList("FEchannels", "", nop);` → `c->channelList("FEchannels", nop);`, `c->playlists("UC", "", "", nop);` → `c->playlists("UC", "", nop);`, and add after it:

```cpp
TEST_CASE("Client::more continues a page") {
    auto f = std::make_shared<FakeHttp>();
    f->queue("browse", loadFixtureRaw("browse_feed.json"));
    f->queue("browse", loadFixtureRaw("browse_feed.json"));
    auto c = mk(f);
    yt::Result<yt::Page<yt::Video>> first, second;
    c->browse({"FEwhat_to_watch", ""}, [&](const yt::Result<yt::Page<yt::Video>> &r) { first = r; });
    for (int i = 0; i < 4; ++i) { c->runOnce(0); f->flush(); }
    REQUIRE(first.ok());
    REQUIRE(first.value.next);
    c->more(first.value.next, [&](const yt::Result<yt::Page<yt::Video>> &r) { second = r; });
    for (int i = 0; i < 4; ++i) { c->runOnce(0); f->flush(); }
    REQUIRE(second.ok());
    CHECK(f->sent.back() == "{\"continuation\":\"FEEDNEXT\"}");
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build build -j 2>&1 | grep -E "error" | head -3`
Expected: compile errors (`fetchMore` not declared; `fetchVideoList` / `Client::browse` callbacks still take `VideoPage`).

- [ ] **Step 3: `src/core/chains.h`** — add `#include "core/cursor.h"` after `"core/ihttp.h"`; replace `VideoListSpec` and the list declarations (`fetchVideoList`, `fetchComments`, `fetchUserSearch`, `fetchChannelList`, `fetchPlaylists`, `fetchPlaylistSearch`) with:

```cpp
struct VideoListSpec {
    enum Kind { Browse, Search } kind = Browse;
    std::string browseId, params;                       // Browse (params: a tab selector)
    std::string query;                                  // Search
    SearchSort sort = SearchSort::Relevance;
};

// Every list chain delivers a Page<T> whose cursor names the endpoint and the client that
// served it; fetchMore continues it with exactly those (YouTube continues a list only for
// the client that started it — a signed-in feed is TVHTML5 on every page).
void fetchVideoList(IHttp &, const VideoListSpec &, const JobToken &, Callback<Page<Video>> done);
// Two steps: /next by videoId discovers the comments continuation, which is page 1.
// No comments panel = comments turned off: an empty page with `disabled`.
void fetchComments(IHttp &, std::string videoId, const JobToken &, Callback<Page<Comment>> done);
void fetchUserSearch(IHttp &, std::string query, const JobToken &, Callback<Page<Channel>> done);
// A channel-list feed (FEchannels — the manage-subscriptions grid).
void fetchChannelList(IHttp &, std::string browseId, const JobToken &, Callback<Page<Channel>> done);
void fetchPlaylists(IHttp &, std::string resourceId, std::string params, const JobToken &, Callback<Page<Playlist>> done);
void fetchPlaylistSearch(IHttp &, std::string query, const JobToken &, Callback<Page<Playlist>> done);
// The next page of a list. A false cursor (the last page) fails with InvalidArgument and
// sends nothing.
void fetchMore(IHttp &, const Cursor<Video> &, const JobToken &, Callback<Page<Video>> done);
void fetchMore(IHttp &, const Cursor<Channel> &, const JobToken &, Callback<Page<Channel>> done);
void fetchMore(IHttp &, const Cursor<Playlist> &, const JobToken &, Callback<Page<Playlist>> done);
void fetchMore(IHttp &, const Cursor<Comment> &, const JobToken &, Callback<Page<Comment>> done);
```

- [ ] **Step 4: `src/core/chains.cpp`** — in the first anonymous namespace replace `sortParam` and `pageOf` with:

```cpp
// Search sort -> the base64 protobuf `params`.
std::string sortParam(SearchSort sort)
{
    switch (sort) {
    case SearchSort::UploadDate: return "CAISAhAB";
    case SearchSort::Views:      return "CAMSAhAB";
    case SearchSort::TopRated:   return "CAESAhAB";
    case SearchSort::Relevance:  break;
    }
    return std::string();
}

// One page of a list plus the cursor that continues it on the same endpoint and client.
template <class T, class Parse>
HttpFn pageOf(Callback<Page<T>> done, std::string endpoint, ClientId client, Parse parse)
{
    return [done, endpoint, client, parse](const Reply &r) {
        Result<Page<T>> out;
        if (!r.ok) { out.error = errorOf(r); done(out); return; }
        std::string next;
        out.value.items = parse(*r.body, &next);
        out.value.next = detail::CursorAccess::make<T>(endpoint, client, std::move(next));
        done(out);
    };
}

template <class T, class Parse>
void moreOf(IHttp &http, const Cursor<T> &c, const JobToken &job, Callback<Page<T>> done, Parse parse)
{
    if (!c) {
        Result<Page<T>> out;
        out.error = Error{ErrorCode::InvalidArgument, "no next page", 0};
        done(out);
        return;
    }
    const std::string endpoint = detail::CursorAccess::endpoint(c);
    const ClientId client = detail::CursorAccess::client(c);
    http.post(endpoint, client, bodies::continuation(detail::CursorAccess::token(c)), job,
              pageOf<T>(std::move(done), endpoint, client, parse));
}

const auto kParseVideos = [](const std::string &b, std::string *n) { return parseVideoList(b, n); };
const auto kParseUsers = [](const std::string &b, std::string *n) { return parseUserList(b, n); };
const auto kParsePlaylists = [](const std::string &b, std::string *n) { return parsePlaylistList(b, n); };
const auto kParseComments = [](const std::string &b, std::string *n) { return parseComments(b, n); };

// The first comments page of a video, and the createCommentParams it carries (needed to
// post a comment).
void fetchCommentsFirst(IHttp &http, std::string videoId, const JobToken &job,
                        std::function<void(const Result<Page<Comment>> &, const std::string &createParams)> done)
{
    IHttp *h = &http;
    http.post("next", ClientId::WEB, bodies::nextVideo(videoId), job, [h, job, done](const Reply &r) {
        Result<Page<Comment>> out;
        if (!r.ok) { out.error = errorOf(r); done(out, std::string()); return; }
        const std::string token = findContinuationTokenUnder(*r.body, "engagementPanels");
        // No panel/token = comments disabled (e.g. made-for-kids): an empty, successful
        // page flagged `disabled` (distinct from a video with zero comments).
        if (token.empty()) { out.value.disabled = true; done(out, std::string()); return; }
        if (!live(job)) return;                      // canceled between steps
        h->post("next", ClientId::WEB, bodies::continuation(token), job, [done](const Reply &p) {
            Result<Page<Comment>> page;
            std::string createParams;
            if (!p.ok) { page.error = errorOf(p); done(page, createParams); return; }
            std::string next;
            page.value.items = parseComments(*p.body, &next, &createParams);
            page.value.next = detail::CursorAccess::make<Comment>("next", ClientId::WEB, std::move(next));
            done(page, createParams);
        });
    });
}
```

Replace the bodies of the list chains and delete `fetchCommentsPage`:

```cpp
void fetchVideoList(IHttp &http, const VideoListSpec &spec, const JobToken &job, Callback<Page<Video>> done)
{
    if (spec.kind == VideoListSpec::Search) {
        http.post("search", ClientId::WEB, bodies::search(spec.query, sortParam(spec.sort)), job,
                  pageOf<Video>(std::move(done), "search", ClientId::WEB, kParseVideos));
        return;
    }
    const ClientId cid = clientForBrowse(spec.browseId, http.session());
    http.post("browse", cid, bodies::browse(spec.browseId, spec.params), job,
              pageOf<Video>(std::move(done), "browse", cid, kParseVideos));
}

void fetchComments(IHttp &http, std::string videoId, const JobToken &job, Callback<Page<Comment>> done)
{
    fetchCommentsFirst(http, std::move(videoId), job,
                       [done](const Result<Page<Comment>> &page, const std::string &) { done(page); });
}

void fetchUserSearch(IHttp &http, std::string query, const JobToken &job, Callback<Page<Channel>> done)
{
    http.post("search", ClientId::WEB, bodies::search(query, "EgIQAg=="), job,   // channels filter
              pageOf<Channel>(std::move(done), "search", ClientId::WEB, kParseUsers));
}

void fetchChannelList(IHttp &http, std::string browseId, const JobToken &job, Callback<Page<Channel>> done)
{
    const ClientId cid = clientForBrowse(browseId, http.session());
    http.post("browse", cid, bodies::browse(browseId, ""), job, pageOf<Channel>(std::move(done), "browse", cid, kParseUsers));
}

void fetchPlaylists(IHttp &http, std::string resourceId, std::string params, const JobToken &job,
                    Callback<Page<Playlist>> done)
{
    // Signed-in FElibrary (the user's own playlists) is feedRequiresAuth → TVHTML5.
    const ClientId cid = clientForBrowse(resourceId, http.session());
    http.post("browse", cid, bodies::browse(resourceId, params), job,
              pageOf<Playlist>(std::move(done), "browse", cid, kParsePlaylists));
}

void fetchPlaylistSearch(IHttp &http, std::string query, const JobToken &job, Callback<Page<Playlist>> done)
{
    http.post("search", ClientId::WEB, bodies::search(query, "EgIQAw=="), job,   // playlists filter
              pageOf<Playlist>(std::move(done), "search", ClientId::WEB, kParsePlaylists));
}

void fetchMore(IHttp &http, const Cursor<Video> &c, const JobToken &job, Callback<Page<Video>> done)
{
    moreOf<Video>(http, c, job, std::move(done), kParseVideos);
}
void fetchMore(IHttp &http, const Cursor<Channel> &c, const JobToken &job, Callback<Page<Channel>> done)
{
    moreOf<Channel>(http, c, job, std::move(done), kParseUsers);
}
void fetchMore(IHttp &http, const Cursor<Playlist> &c, const JobToken &job, Callback<Page<Playlist>> done)
{
    moreOf<Playlist>(http, c, job, std::move(done), kParsePlaylists);
}
void fetchMore(IHttp &http, const Cursor<Comment> &c, const JobToken &job, Callback<Page<Comment>> done)
{
    moreOf<Comment>(http, c, job, std::move(done), kParseComments);
}
```

`fetchCommentsFirst` and the `kParse*` constants live in the first anonymous namespace, so they must be declared before the public functions that use them (put them after `tokenGrantOf`).

- [ ] **Step 5: Public types and the transitional facade** — `include/yt/types.h`: delete `VideoPage`, `PlaylistPage`, `ChannelPage` and `CommentPage`.

`include/yt/client.h`: replace `BrowseRequest`/`SearchRequest` and the affected declarations:

```cpp
struct BrowseRequest { std::string browseId, params; };
struct SearchRequest { std::string query; SearchSort sort = SearchSort::Relevance; };
```

```cpp
    Job browse(BrowseRequest request, Callback<Page<Video>> done);
    Job search(SearchRequest request, Callback<Page<Video>> done);
    Job comments(std::string videoId, Callback<Page<Comment>> done);
    Job channelList(std::string browseId, Callback<Page<Channel>> done);
    Job searchChannels(std::string query, Callback<Page<Channel>> done);
    Job playlists(std::string resourceId, std::string params, Callback<Page<Playlist>> done);
    Job searchPlaylists(std::string query, Callback<Page<Playlist>> done);
    // The next page of any list (Page::next). The last page's cursor fails with
    // ErrorCode::InvalidArgument and sends nothing.
    Job more(const Cursor<Video> &next, Callback<Page<Video>> done);
    Job more(const Cursor<Channel> &next, Callback<Page<Channel>> done);
    Job more(const Cursor<Playlist> &next, Callback<Page<Playlist>> done);
    Job more(const Cursor<Comment> &next, Callback<Page<Comment>> done);
```

`src/client.cpp`: update the matching wrappers (same shape as their neighbours), e.g.

```cpp
Job Client::browse(BrowseRequest req, Callback<Page<Video>> done)
{
    return m_engine->start<Result<Page<Video>>>([req](core::IHttp &h, const core::JobToken &t, Callback<Page<Video>> d) {
        core::VideoListSpec s;
        s.kind = core::VideoListSpec::Browse;
        s.browseId = req.browseId; s.params = req.params;
        core::fetchVideoList(h, s, t, std::move(d));
    }, std::move(done));
}

Job Client::search(SearchRequest req, Callback<Page<Video>> done)
{
    return m_engine->start<Result<Page<Video>>>([req](core::IHttp &h, const core::JobToken &t, Callback<Page<Video>> d) {
        core::VideoListSpec s;
        s.kind = core::VideoListSpec::Search;
        s.query = req.query; s.sort = req.sort;
        core::fetchVideoList(h, s, t, std::move(d));
    }, std::move(done));
}

Job Client::comments(std::string videoId, Callback<Page<Comment>> done)
{
    return m_engine->start<Result<Page<Comment>>>([videoId](core::IHttp &h, const core::JobToken &t, Callback<Page<Comment>> d) {
        core::fetchComments(h, videoId, t, std::move(d));
    }, std::move(done));
}
```

```cpp
Job Client::channelList(std::string browseId, Callback<Page<Channel>> done)
{
    return m_engine->start<Result<Page<Channel>>>([browseId](core::IHttp &h, const core::JobToken &t, Callback<Page<Channel>> d) {
        core::fetchChannelList(h, browseId, t, std::move(d));
    }, std::move(done));
}

Job Client::searchChannels(std::string query, Callback<Page<Channel>> done)
{
    return m_engine->start<Result<Page<Channel>>>([query](core::IHttp &h, const core::JobToken &t, Callback<Page<Channel>> d) {
        core::fetchUserSearch(h, query, t, std::move(d));
    }, std::move(done));
}

Job Client::playlists(std::string resourceId, std::string params, Callback<Page<Playlist>> done)
{
    return m_engine->start<Result<Page<Playlist>>>(
        [resourceId, params](core::IHttp &h, const core::JobToken &t, Callback<Page<Playlist>> d) {
            core::fetchPlaylists(h, resourceId, params, t, std::move(d));
        }, std::move(done));
}

Job Client::searchPlaylists(std::string query, Callback<Page<Playlist>> done)
{
    return m_engine->start<Result<Page<Playlist>>>([query](core::IHttp &h, const core::JobToken &t, Callback<Page<Playlist>> d) {
        core::fetchPlaylistSearch(h, query, t, std::move(d));
    }, std::move(done));
}

Job Client::more(const Cursor<Video> &next, Callback<Page<Video>> done)
{
    return m_engine->start<Result<Page<Video>>>([next](core::IHttp &h, const core::JobToken &t, Callback<Page<Video>> d) {
        core::fetchMore(h, next, t, std::move(d));
    }, std::move(done));
}

Job Client::more(const Cursor<Channel> &next, Callback<Page<Channel>> done)
{
    return m_engine->start<Result<Page<Channel>>>([next](core::IHttp &h, const core::JobToken &t, Callback<Page<Channel>> d) {
        core::fetchMore(h, next, t, std::move(d));
    }, std::move(done));
}

Job Client::more(const Cursor<Playlist> &next, Callback<Page<Playlist>> done)
{
    return m_engine->start<Result<Page<Playlist>>>([next](core::IHttp &h, const core::JobToken &t, Callback<Page<Playlist>> d) {
        core::fetchMore(h, next, t, std::move(d));
    }, std::move(done));
}

Job Client::more(const Cursor<Comment> &next, Callback<Page<Comment>> done)
{
    return m_engine->start<Result<Page<Comment>>>([next](core::IHttp &h, const core::JobToken &t, Callback<Page<Comment>> d) {
        core::fetchMore(h, next, t, std::move(d));
    }, std::move(done));
}
```

- [ ] **Step 6: Examples** — `examples/yt-cli/main.cpp`: `client.browse({arg, "", ""}, …VideoPage…)` → `client.browse({arg, ""}, [&](const yt::Result<yt::Page<yt::Video>> &r) {`; `client.search({arg, "relevance"}, …)` → `client.search({arg}, [&](const yt::Result<yt::Page<yt::Video>> &r) {`; `printVideos(const yt::VideoPage &p)` → `printVideos(const yt::Page<yt::Video> &p)` and its last line → `if (p.next) std::printf("(more available)\n");`. `examples/channel/main.cpp`: `client.browse({c.id, "", ""}, [&](const yt::Result<yt::VideoPage> &p)` → `client.browse({c.id, ""}, [&](const yt::Result<yt::Page<yt::Video>> &p)`. `examples/watch/main.cpp`: `yt::Callback<yt::CommentPage>` → `yt::Callback<yt::Page<yt::Comment>>`, `client.comments(id, r.value.next, onComments)` → `client.more(r.value.next, onComments)`, `!r.value.next.empty()` → `r.value.next`, `client.comments(id, "", onComments)` → `client.comments(id, onComments)`, and in its header comment "continuation paging (CommentPage::next fed back as `page`)" → "paging (Page::next handed to Client::more)".

- [ ] **Step 7: Run all tests**

Run: `cmake --build build -j 2>&1 | grep -E "warning|error"; (cd build && ctest -j8 --output-on-failure)`
Expected: no warnings; all suites PASS. `grep -rn "VideoPage\|CommentPage\|PlaylistPage\|ChannelPage\|nextContinuation" include src tests examples` prints nothing.

- [ ] **Step 8: Commit**

```bash
git add -A include src tests examples
git commit -m "lib: every list is a Page<T>; Client::more continues any of them" -m "Search can be paged for the first time (search continuations); a signed-in feed
continues on the client that served page 1; the last page's cursor fails with
InvalidArgument instead of posting an empty continuation." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CcpAWezZwfUf6YSVnLXjYq"
```

---

### Task 4: chains that take links, and the composite account/comment/channel requests

**Files:**
- Modify: `src/core/chains.h`, `src/core/chains.cpp`, `src/innertube/bodies.h`, `src/innertube/bodies.cpp`, `src/innertube/catalog.h`
- Modify: `include/yt/client.h`, `src/client.cpp` (transitional: `postComment(videoId, text)`, `editPlaylist` over the new chain)
- Test: `tests/tst_chains.cpp`, `tests/tst_bodies.cpp`, `tests/tst_client.cpp`

**Interfaces:**
- Consumes: Task 1 (`yt::Link`), Task 3 (`Page`, `fetchVideoList`, `fetchPlaylists`, `fetchCommentsFirst`).
- Produces in `core` (`chains.h`):
  - `std::optional<std::string> videoIdOf(std::string_view link);` (kinds Video, Short, Live)
  - `std::optional<std::string> playlistIdOf(std::string_view link);`
  - `Error badLink(std::string_view link, const char *expected);` → `ErrorCode::InvalidArgument`, message `"not a <expected> link: <link>"`
  - `void fetchChannelId(IHttp &, std::string link, const JobToken &, Callback<std::string>);`
  - `void fetchChannel(IHttp &, std::string link, const JobToken &, Callback<Channel>);`
  - `void fetchChannelVideos(IHttp &, std::string link, const JobToken &, Callback<Page<Video>>);`
  - `void fetchChannelPlaylists(IHttp &, std::string link, const JobToken &, Callback<Page<Playlist>>);`
  - `void channelAction(IHttp &, std::string link, ActionKind, const JobToken &, Callback<Done>);` (Subscribe / Unsubscribe by link)
  - `void postComment(IHttp &, std::string videoId, std::string text, const JobToken &, Callback<Done>);` (replaces the createParams form)
  - `enum PlaylistEdit { AddVideo, RemoveVideo };` `void editPlaylist(IHttp &, std::string playlistId, PlaylistEdit, std::string videoId, const JobToken &, Callback<Done>);`
- `Catalog::kChannelVideosTab = "EgZ2aWRlb3PyBgQKAjoA"`, `Catalog::kChannelPlaylistsTab = "EglwbGF5bGlzdHPyBgQKAkIA"`.
- `bodies::playlistAddVideo(playlistId, videoId)`, `bodies::playlistRemoveVideo(playlistId, videoId)` (replace `bodies::editPlaylist`).

- [ ] **Step 1: Write the failing tests** — `tests/tst_bodies.cpp`: replace the `bodies::editPlaylist` checks with

```cpp
TEST_CASE("playlist edits") {
    CHECK(bodies::playlistAddVideo("WL", "vid42") ==
          "{\"playlistId\":\"WL\",\"actions\":[{\"action\":\"ACTION_ADD_VIDEO\",\"addedVideoId\":\"vid42\"}]}");
    CHECK(bodies::playlistRemoveVideo("PLabc", "vid42") ==
          "{\"playlistId\":\"PLabc\",\"actions\":[{\"action\":\"ACTION_REMOVE_VIDEO_BY_VIDEO_ID\",\"removedVideoId\":\"vid42\"}]}");
}
```

`tests/tst_chains.cpp`: delete `editPlaylistAdd`, `editPlaylistRemove`, `postCommentSucceeds`, `postCommentTransportFails`; append:

```cpp
// ---- links into ids ----
TEST_CASE("videoIdOf and playlistIdOf") {
    CHECK(videoIdOf("https://youtu.be/dQw4w9WgXcQ?si=x") == std::optional<std::string>("dQw4w9WgXcQ"));
    CHECK(videoIdOf("https://www.youtube.com/shorts/aqz-KE-bpKQ") == std::optional<std::string>("aqz-KE-bpKQ"));
    CHECK_FALSE(videoIdOf("@NASA").has_value());
    CHECK(playlistIdOf("https://www.youtube.com/playlist?list=PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI")
          == std::optional<std::string>("PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI"));
    CHECK(playlistIdOf("WL") == std::optional<std::string>("WL"));
    CHECK_FALSE(playlistIdOf("dQw4w9WgXcQ").has_value());
    const Error e = badLink("xyz", "video");
    CHECK(e.code == ErrorCode::InvalidArgument);
    CHECK(e.message == "not a video link: xyz");
}
TEST_CASE("a channel id needs no request; a handle is resolved") {
    FakeHttp t;
    t.queue("navigation/resolve_url", "{\"endpoint\":{\"browseEndpoint\":{\"browseId\":\"UCabcabcabcabcabcabcabca\"}}}");
    Cap<std::string> a, b;
    fetchChannelId(t, "UCuAXFkgsw1L7xaCfnd5JJOw", newJob(), a.fn());
    fetchChannelId(t, "https://www.youtube.com/@NASA/videos", newJob(), b.fn());
    t.flush();
    REQUIRE(a.r.ok());
    CHECK(a.r.value == "UCuAXFkgsw1L7xaCfnd5JJOw");
    REQUIRE(b.r.ok());
    CHECK(b.r.value == "UCabcabcabcabcabcabcabca");
    REQUIRE(t.sent.size() == 1);
    CHECK(has(t.sent[0], "\"url\":\"https://www.youtube.com/@NASA\""));
}
// Review focus: a link of the wrong kind fails without a request.
TEST_CASE("a video link is not a channel") {
    FakeHttp t;
    Cap<std::string> c;
    fetchChannelId(t, "https://youtu.be/dQw4w9WgXcQ", newJob(), c.fn());
    t.flush();
    REQUIRE_FALSE(c.r.ok());
    CHECK(c.r.error->code == ErrorCode::InvalidArgument);
    CHECK(t.sent.empty());
}
TEST_CASE("a resolve that names no channel is Unavailable") {
    FakeHttp t;
    t.queue("navigation/resolve_url", "{\"endpoint\":{\"browseEndpoint\":{\"browseId\":\"FEwhat_to_watch\"}}}");
    Cap<std::string> c;
    fetchChannelId(t, "@nobody", newJob(), c.fn());
    t.flush();
    REQUIRE_FALSE(c.r.ok());
    CHECK(c.r.error->code == ErrorCode::Unavailable);
}
TEST_CASE("signed in, a channel also carries the viewer's subscribe state") {
    FakeHttp t; t.session().bearer = "tok";
    t.queue("browse", "{\"header\":{\"c4TabbedHeaderRenderer\":{\"title\":\"Chan\",\"channelId\":\"UCuAXFkgsw1L7xaCfnd5JJOw\"}}}");
    t.queue("browse", "{\"subscribeButton\":{\"subscribeButtonRenderer\":{\"subscribed\":true}}}");
    Cap<Channel> c;
    fetchChannel(t, "UCuAXFkgsw1L7xaCfnd5JJOw", newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(c.r.value.id == "UCuAXFkgsw1L7xaCfnd5JJOw");
    CHECK(t.sentClients == std::vector<int>{(int) ClientId::WEB, (int) ClientId::TVHTML5});
}
TEST_CASE("anonymous, a channel is one request") {
    FakeHttp t;
    t.queue("browse", "{\"header\":{\"c4TabbedHeaderRenderer\":{\"title\":\"Chan\",\"channelId\":\"UCuAXFkgsw1L7xaCfnd5JJOw\"}}}");
    Cap<Channel> c;
    fetchChannel(t, "https://www.youtube.com/channel/UCuAXFkgsw1L7xaCfnd5JJOw", newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(t.sent.size() == 1);
}
TEST_CASE("channel tabs browse the channel with the tab params") {
    FakeHttp t;
    t.queue("navigation/resolve_url", "{\"endpoint\":{\"browseEndpoint\":{\"browseId\":\"UCabcabcabcabcabcabcabca\"}}}");
    t.queue("browse", loadFixtureRaw("browse_feed.json"));
    t.queue("browse", "{\"contents\":[{\"playlistRenderer\":{\"playlistId\":\"PL9\",\"title\":{\"simpleText\":\"X\"}}}]}");
    Cap<Page<Video>> v;
    Cap<Page<Playlist>> p;
    fetchChannelVideos(t, "@NASA", newJob(), v.fn());
    t.flush();
    fetchChannelPlaylists(t, "UCuAXFkgsw1L7xaCfnd5JJOw", newJob(), p.fn());
    t.flush();
    REQUIRE(v.r.ok());
    CHECK(has(t.sent[1], "\"browseId\":\"UCabcabcabcabcabcabcabca\""));
    CHECK(has(t.sent[1], (std::string("\"params\":\"") + Catalog::kChannelVideosTab + "\"").c_str()));
    REQUIRE(p.r.ok());
    CHECK(p.r.value.items.size() == 1);
    CHECK(has(t.sent[2], (std::string("\"params\":\"") + Catalog::kChannelPlaylistsTab + "\"").c_str()));
}
TEST_CASE("subscribe by link resolves the channel first") {
    FakeHttp t;
    t.queue("navigation/resolve_url", "{\"endpoint\":{\"browseEndpoint\":{\"browseId\":\"UCabcabcabcabcabcabcabca\"}}}");
    t.queue("subscription/subscribe", "{}");
    Cap<Done> c;
    channelAction(t, "@NASA", Subscribe, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(has(t.sent[1], "\"channelIds\":[\"UCabcabcabcabcabcabcabca\"]"));
    CHECK(t.lastClientFor("subscription/subscribe") == (int) ClientId::TVHTML5);
}
TEST_CASE("posting a comment finds its createCommentParams on the comments page") {
    FakeHttp t;
    t.queue("next", loadFixtureRaw("next_for_comments.json"));
    t.queue("next", "{\"x\":{\"createCommentParams\":\"CPARAMS\"}}");
    t.queue("comment/create_comment", "{}");
    Cap<Done> c;
    postComment(t, "aaa11111111", "hi", newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    REQUIRE(t.sent.size() == 3);
    CHECK(has(t.sent[2], "\"createCommentParams\":\"CPARAMS\""));
    CHECK(has(t.sent[2], "\"commentText\":\"hi\""));
    CHECK(t.lastClientFor("comment/create_comment") == (int) ClientId::TVHTML5);
}
TEST_CASE("posting where comments are off fails without a create request") {
    FakeHttp t; t.queue("next", "{}");
    Cap<Done> c;
    postComment(t, "aaa11111111", "hi", newJob(), c.fn());
    t.flush();
    REQUIRE_FALSE(c.r.ok());
    CHECK(c.r.error->code == ErrorCode::Unavailable);
    CHECK(t.sent.size() == 1);
}
TEST_CASE("playlist edits add and remove by video id") {
    FakeHttp t;
    t.queue("browse/edit_playlist", "{}");
    t.queue("browse/edit_playlist", "{}");
    Cap<Done> a, r;
    editPlaylist(t, "WL", AddVideo, "vid42", newJob(), a.fn());
    editPlaylist(t, "PLabc", RemoveVideo, "vid42", newJob(), r.fn());
    t.flush();
    CHECK(a.r.ok());
    CHECK(r.r.ok());
    CHECK(has(t.sent[0], "\"addedVideoId\":\"vid42\""));
    CHECK(has(t.sent[1], "\"removedVideoId\":\"vid42\""));
    CHECK_FALSE(has(t.sent[1], "setVideoId"));
    CHECK(t.lastClientFor("browse/edit_playlist") == (int) ClientId::TVHTML5);
}
```

In `tests/tst_client.cpp` "every endpoint reaches its chain": `c->postComment("P", "t", nop);` → `c->postComment("dQw4w9WgXcQ", "t", nop);` and remove `"comment/create_comment"` from the endpoint list (posting now first needs a comments page; the chain tests above cover it).

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build build -j 2>&1 | grep -E "error" | head -3`
Expected: `'playlistAddVideo' is not a member of 'yt::bodies'`, `'videoIdOf' was not declared`.

- [ ] **Step 3: Catalog and bodies** — `src/innertube/catalog.h`, inside `namespace Catalog` after `kQrPresetStyle`:

```cpp
    // Channel tab selectors (browse params; Troubadour, live-verified): the Videos and
    // Playlists tabs of a channel browse.
    static const char *const kChannelVideosTab    = "EgZ2aWRlb3PyBgQKAjoA";
    static const char *const kChannelPlaylistsTab = "EglwbGF5bGlzdHPyBgQKAkIA";
```

`src/innertube/bodies.h`: replace the `editPlaylist` declaration and comment with

```cpp
// browse/edit_playlist: ACTION_ADD_VIDEO {addedVideoId} / ACTION_REMOVE_VIDEO_BY_VIDEO_ID
// {removedVideoId}. Removal by video id needs no per-entry setVideoId handle.
std::string playlistAddVideo(std::string_view playlistId, std::string_view videoId);
std::string playlistRemoveVideo(std::string_view playlistId, std::string_view videoId);
```

`src/innertube/bodies.cpp`: in `bj::EditAction` replace `std::optional<std::string> setVideoId;` with `std::optional<std::string> removedVideoId;     // omitted when nullopt (remove only)`; replace the `editPlaylist` builder with

```cpp
std::string playlistAddVideo(std::string_view playlistId, std::string_view videoId)
{
    bj::EditAction a;
    a.action = "ACTION_ADD_VIDEO";
    a.addedVideoId = std::string(videoId);
    bj::EditPlaylist b;
    b.playlistId = std::string(playlistId);
    b.actions.push_back(a);
    return dump(b);
}

std::string playlistRemoveVideo(std::string_view playlistId, std::string_view videoId)
{
    bj::EditAction a;
    a.action = "ACTION_REMOVE_VIDEO_BY_VIDEO_ID";
    a.removedVideoId = std::string(videoId);
    bj::EditPlaylist b;
    b.playlistId = std::string(playlistId);
    b.actions.push_back(a);
    return dump(b);
}
```

- [ ] **Step 4: `src/core/chains.h`** — add `#include <yt/link.h>` and `#include <optional>`/`<string_view>`; replace the `postComment` and `editPlaylist` declarations and add the new ones:

```cpp
// The id inside whatever the user pasted (yt::Link rules); nullopt when the text is not
// a link of that kind. Videos: watch, youtu.be, embed, shorts and live links and bare ids.
std::optional<std::string> videoIdOf(std::string_view link);
std::optional<std::string> playlistIdOf(std::string_view link);
// The InvalidArgument error for a link that is not of the expected kind.
Error badLink(std::string_view link, const char *expected);

// The channel id behind a channel link: a UC… id directly (no request), a /@handle, /c/ or
// /user/ link through navigation/resolve_url. Anything else is InvalidArgument.
void fetchChannelId(IHttp &, std::string link, const JobToken &, Callback<std::string> done);
// The channel header; signed in, also the viewer's subscribe state (an authed TV browse —
// the WEB header does not carry it for a bearer session).
void fetchChannel(IHttp &, std::string link, const JobToken &, Callback<Channel> done);
// A channel's Videos / Playlists tab (Catalog::kChannel*Tab).
void fetchChannelVideos(IHttp &, std::string link, const JobToken &, Callback<Page<Video>> done);
void fetchChannelPlaylists(IHttp &, std::string link, const JobToken &, Callback<Page<Playlist>> done);
// Subscribe / Unsubscribe by channel link.
void channelAction(IHttp &, std::string link, ActionKind, const JobToken &, Callback<Done> done);
// A top-level comment: the video's comments page supplies the createCommentParams. Comments
// turned off fail with Unavailable.
void postComment(IHttp &, std::string videoId, std::string text, const JobToken &, Callback<Done> done);
enum PlaylistEdit { AddVideo, RemoveVideo };
// WL|LL|PL…, by video id.
void editPlaylist(IHttp &, std::string playlistId, PlaylistEdit, std::string videoId, const JobToken &, Callback<Done> done);
```

- [ ] **Step 5: `src/core/chains.cpp`** — replace `postComment` and `editPlaylist`, and add (after `fetchChannelSubscribed`):

```cpp
std::optional<std::string> videoIdOf(std::string_view link)
{
    const std::optional<Link> l = Link::parse(link);
    if (!l || (l->kind != LinkKind::Video && l->kind != LinkKind::Short && l->kind != LinkKind::Live))
        return std::nullopt;
    return l->id;
}

std::optional<std::string> playlistIdOf(std::string_view link)
{
    const std::optional<Link> l = Link::parse(link);
    if (!l || l->kind != LinkKind::Playlist) return std::nullopt;
    return l->id;
}

Error badLink(std::string_view link, const char *expected)
{
    return Error{ErrorCode::InvalidArgument, std::string("not a ") + expected + " link: " + std::string(link), 0};
}

void fetchChannelId(IHttp &http, std::string link, const JobToken &job, Callback<std::string> done)
{
    const std::optional<Link> l = Link::parse(link);
    Result<std::string> out;
    if (!l || l->kind == LinkKind::Video || l->kind == LinkKind::Short || l->kind == LinkKind::Live
        || l->kind == LinkKind::Playlist) {
        out.error = badLink(link, "channel");
        done(out);
        return;
    }
    if (l->kind == LinkKind::Channel) { out.value = l->id; done(out); return; }
    http.post("navigation/resolve_url", ClientId::WEB, bodies::resolveUrl(l->url()), job, [done](const Reply &r) {
        Result<std::string> id;
        if (!r.ok) { id.error = errorOf(r); done(id); return; }
        id.value = parseResolvedBrowseId(*r.body);
        if (id.value.rfind("UC", 0) != 0) { id.value.clear(); id.error = unavailable("could not resolve channel"); }
        done(id);
    });
}

void fetchChannel(IHttp &http, std::string link, const JobToken &job, Callback<Channel> done)
{
    IHttp *h = &http;
    fetchChannelId(http, std::move(link), job, [h, job, done](const Result<std::string> &id) {
        if (!id.ok()) { Result<Channel> out; out.error = id.error; done(out); return; }
        if (!live(job)) return;
        const std::string cid = id.value;
        fetchChannelById(*h, cid, job, [h, job, done, cid](const Result<Channel> &ch) {
            if (!ch.ok() || h->session().bearer.empty()) { done(ch); return; }
            if (!live(job)) return;
            fetchChannelSubscribed(*h, cid, job, [done, ch](const Result<bool> &s) {
                Result<Channel> out = ch;
                if (s.ok()) out.value.subscribed = s.value;
                done(out);
            });
        });
    });
}

void fetchChannelVideos(IHttp &http, std::string link, const JobToken &job, Callback<Page<Video>> done)
{
    IHttp *h = &http;
    fetchChannelId(http, std::move(link), job, [h, job, done](const Result<std::string> &id) {
        if (!id.ok()) { Result<Page<Video>> out; out.error = id.error; done(out); return; }
        if (!live(job)) return;
        VideoListSpec spec;
        spec.browseId = id.value;
        spec.params = Catalog::kChannelVideosTab;
        fetchVideoList(*h, spec, job, done);
    });
}

void fetchChannelPlaylists(IHttp &http, std::string link, const JobToken &job, Callback<Page<Playlist>> done)
{
    IHttp *h = &http;
    fetchChannelId(http, std::move(link), job, [h, job, done](const Result<std::string> &id) {
        if (!id.ok()) { Result<Page<Playlist>> out; out.error = id.error; done(out); return; }
        if (!live(job)) return;
        fetchPlaylists(*h, id.value, Catalog::kChannelPlaylistsTab, job, done);
    });
}

void channelAction(IHttp &http, std::string link, ActionKind kind, const JobToken &job, Callback<Done> done)
{
    IHttp *h = &http;
    fetchChannelId(http, std::move(link), job, [h, kind, job, done](const Result<std::string> &id) {
        if (!id.ok()) { Result<Done> out; out.error = id.error; done(out); return; }
        if (!live(job)) return;
        submitAction(*h, kind, id.value, job, done);
    });
}

void postComment(IHttp &http, std::string videoId, std::string text, const JobToken &job, Callback<Done> done)
{
    IHttp *h = &http;
    fetchCommentsFirst(http, std::move(videoId), job,
                       [h, text, job, done](const Result<Page<Comment>> &page, const std::string &createParams) {
        Result<Done> out;
        if (!page.ok()) { out.error = page.error; done(out); return; }
        if (page.value.disabled || createParams.empty()) {
            out.error = unavailable("comments are turned off for this video");
            done(out);
            return;
        }
        if (!live(job)) return;
        h->post("comment/create_comment", ClientId::TVHTML5, bodies::createComment(createParams, text), job, doneOf(done));
    });
}

void editPlaylist(IHttp &http, std::string playlistId, PlaylistEdit edit, std::string videoId, const JobToken &job,
                  Callback<Done> done)
{
    std::string body = edit == AddVideo ? bodies::playlistAddVideo(playlistId, videoId)
                                        : bodies::playlistRemoveVideo(playlistId, videoId);
    http.post("browse/edit_playlist", ClientId::TVHTML5, std::move(body), job, doneOf(done));
}
```

`unavailable()` takes `const char *`; `doneOf` and `fetchCommentsFirst` are in the first anonymous namespace (declared above these). `fetchCommentsFirst` must be defined before `postComment` (it is, from Task 3).

- [ ] **Step 6: Transitional facade** — `include/yt/client.h`: `Job postComment(std::string createParams, std::string text, Callback<Done> done);` → `Job postComment(std::string videoId, std::string text, Callback<Done> done);`. `src/client.cpp`: `Client::postComment` passes `videoId` to `core::postComment`; `Client::editPlaylist` calls `core::editPlaylist(h, playlistId, add ? core::AddVideo : core::RemoveVideo, id, t, std::move(d));`.

- [ ] **Step 7: Run all tests**

Run: `cmake --build build -j 2>&1 | grep -E "warning|error"; (cd build && ctest -j8 --output-on-failure)`
Expected: no warnings; all suites PASS.

- [ ] **Step 8: Commit**

```bash
git add src/core/chains.h src/core/chains.cpp src/innertube/bodies.h src/innertube/bodies.cpp src/innertube/catalog.h include/yt/client.h src/client.cpp tests/tst_chains.cpp tests/tst_bodies.cpp tests/tst_client.cpp
git commit -m "lib: chains take links; channel, comment and playlist requests compose their own lookups" -m "- A comment is posted by video id: the comments page supplies createCommentParams,
  which consumers used to carry between two calls.
- A playlist entry is removed by video id (ACTION_REMOVE_VIDEO_BY_VIDEO_ID); the
  setVideoId handle it needed was not reachable from the public API.
- Channels resolve /@handle, /c/ and /user/ links themselves and, signed in, carry
  the viewer's subscribe state." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CcpAWezZwfUf6YSVnLXjYq"
```

---

### Task 5: the resource groups replace the flat `Client`

**Files:**
- Modify: `include/yt/client.h` (groups; old methods and `BrowseRequest`/`SearchRequest`/`Action` removed), `include/yt/types.h` (`Account` → `AccountInfo`), `include/yt/session.h`, `include/yt/catalog.h` + `src/catalog.cpp` (drop `feedSections`, `searchTypes`, `SearchOrder`, `SearchType`)
- Modify: `src/engine.h` (`Engine::start` + `runAttempt` move here), `src/client.cpp` (Client constructors, `more`; old wrappers deleted)
- Create: `src/groups.cpp` (add to `CMakeLists.txt` after `src/client.cpp`)
- Modify (rename `Account` → `AccountInfo`): `src/auth.cpp`, `src/parsers/rendererparser.h`, `src/parsers/accountparser.cpp`, `src/core/chains.h`, `src/core/chains.cpp`
- Modify: `src/core/chains.h/.cpp` (delete `fetchChannelByUrl`, now unused)
- Modify: `examples/yt-cli/main.cpp`, `examples/watch/main.cpp`, `examples/channel/main.cpp`, `examples/suggest/main.cpp`
- Test: `tests/tst_client.cpp`, `tests/tst_auth.cpp`, `tests/tst_catalog.cpp`, `tests/tst_chains.cpp`, `tests/tst_parsers.cpp`

**Interfaces:**
- Consumes: Tasks 1–4.
- Produces (namespace `yt`, `client.h`), every method returning `Job`:
  - `Videos`: `get(link, Callback<WatchResult>)`, `player(link, Callback<Player>)`, `player(link, unsigned want, Callback<Player>)`, `captions(CaptionTrack, Callback<std::vector<Cue>>)`, `votes(link, Callback<Votes>)`
  - `Search`: `videos(query, Callback<Page<Video>>)`, `videos(query, SearchOptions, Callback<Page<Video>>)`, `channels(query, Callback<Page<Channel>>)`, `playlists(query, Callback<Page<Playlist>>)`, `suggestions(prefix, Callback<std::vector<std::string>>)`
  - `Channels`: `get(link, Callback<Channel>)`, `videos(link, Callback<Page<Video>>)`, `playlists(link, Callback<Page<Playlist>>)`
  - `Playlists`: `videos(link, Callback<Page<Video>>)`
  - `Feeds`: `home(cb)`, `subscriptions(cb)`, `history(cb)`, `topic(std::string id, cb)` — all `Callback<Page<Video>>`
  - `Comments`: `list(link, Callback<Page<Comment>>)`, `post(link, text, Callback<Done>)`
  - `Account`: `info(Callback<AccountInfo>)`, `rate(link, Rating, Callback<Done>)`, `subscribe(link, Callback<Done>)`, `unsubscribe(link, Callback<Done>)`, `save(videoLink, playlistLink, Callback<Done>)`, `unsave(videoLink, playlistLink, Callback<Done>)`, `playlists(Callback<Page<Playlist>>)`, `subscriptions(Callback<Page<Channel>>)`
  - `Client`: `explicit Client(ClientOptions options = {})`, accessors `videos() search() channels() playlists() feeds() comments() account() auth()`, `more(...)` × 4 (Task 3), `setLocale`, `runOnce`.
  - `struct AccountInfo { std::string id, name, thumbnailUrl, handle, channelId; };` (was `Account`).

- [ ] **Step 1: Write the failing tests** — `tests/tst_client.cpp`: replace "every endpoint reaches its chain" with

```cpp
TEST_CASE("every group method reaches its chain") {
    auto f = std::make_shared<FakeHttp>();
    auto c = mk(f);
    auto nop = [](auto &&) {};
    const std::string v = "https://youtu.be/dQw4w9WgXcQ", ch = "UCuAXFkgsw1L7xaCfnd5JJOw", pl = "PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI";
    c->videos().get(v, nop);
    c->videos().player(v, nop);
    c->videos().player(v, yt::WantHls, nop);
    c->videos().captions(yt::CaptionTrack{"https://y/t", "en", "English"}, nop);
    c->videos().votes(v, nop);
    c->search().videos("q", nop);
    c->search().channels("q", nop);
    c->search().playlists("q", nop);
    c->search().suggestions("q", nop);
    c->channels().get("@NASA", nop);
    c->channels().videos(ch, nop);
    c->channels().playlists(ch, nop);
    c->playlists().videos(pl, nop);
    c->feeds().home(nop);
    c->feeds().topic("UC-9-kyTW8ZkZNDHQJ6FgpwQ", nop);
    c->comments().list(v, nop);
    c->comments().post(v, "t", nop);
    c->account().info(nop);
    c->account().rate(v, yt::Rating::Like, nop);
    c->account().subscribe(ch, nop);
    c->account().save(v, pl, nop);
    c->runOnce(0);
    for (const char *ep : {"search", "next", "player", "browse", "navigation/resolve_url", "account/accounts_list",
                           "like/like", "subscription/subscribe", "browse/edit_playlist"})
        CHECK_MESSAGE(f->lastClientFor(ep) != -1, ep);
    CHECK_FALSE(f->fetched.empty());                  // captions
    CHECK_FALSE(f->lastGetUrl().empty());             // votes (RYD) and suggestions are plain GETs
}
// Review focus: a link of the wrong kind fails asynchronously, with no request.
TEST_CASE("a link of the wrong kind fails with InvalidArgument, never inline") {
    auto f = std::make_shared<FakeHttp>();
    auto c = mk(f);
    std::vector<yt::ErrorCode> codes;
    auto rec = [&](const auto &r) { codes.push_back(r.ok() ? yt::ErrorCode::Canceled : r.error->code); };
    c->videos().get("https://example.com/watch?v=dQw4w9WgXcQ", rec);
    c->playlists().videos("not a playlist", rec);
    c->account().save("dQw4w9WgXcQ", "dQw4w9WgXcQ", rec);       // a video is not a playlist
    c->channels().videos("https://youtu.be/dQw4w9WgXcQ", rec);
    CHECK(codes.empty());                             // never from inside the call
    for (int i = 0; i < 4; ++i) { c->runOnce(0); f->flush(); }
    REQUIRE(codes.size() == 4);
    for (yt::ErrorCode code : codes) CHECK(code == yt::ErrorCode::InvalidArgument);
    CHECK(f->sent.empty());
}
TEST_CASE("a signed-out feed that needs sign-in still goes to the TV client") {
    auto f = std::make_shared<FakeHttp>();
    auto c = mk(f);
    c->feeds().subscriptions([](const yt::Result<yt::Page<yt::Video>> &) {});
    c->feeds().history([](const yt::Result<yt::Page<yt::Video>> &) {});
    c->runOnce(0);
    REQUIRE(f->sent.size() == 2);
    CHECK(f->sent[0] == "{\"browseId\":\"FEsubscriptions\"}");
    CHECK(f->sent[1] == "{\"browseId\":\"FEhistory\"}");
    CHECK(f->lastClientFor("browse") == (int) yt::ClientId::TVHTML5);
}
```

Migrate the remaining old calls in `tests/tst_client.cpp` and `tests/tst_auth.cpp`:

```sh
sed -i -E 's/->browse\(\{"FEsubscriptions", ""\}, /->feeds().subscriptions(/g; s/->browse\(\{"FEhistory", ""\}, /->feeds().history(/g; s/->browse\(\{"FEwhat_to_watch", ""\}, /->feeds().home(/g; s/->browse\(\{("[^"]*"( \+ std::to_string\(i\))?), ""\}, /->feeds().topic(\1, /g' tests/tst_client.cpp tests/tst_auth.cpp
sed -i -E 's/c->watch\("vid", /c->videos().get("vid11111111", /; s/c->comments\("vid", /c->comments().list("vid11111111", /' tests/tst_client.cpp
sed -i -E 's/r\.c->player\(\{"vid"\}, /r.c->videos().player("vid11111111", /' tests/tst_auth.cpp
```

`grep -n "c->browse\|c->watch\|c->comments(\|c->player(\|c->more(" tests/tst_client.cpp tests/tst_auth.cpp` must then list only `c->more(` (kept: `Client::more` is final). In "Client::more continues a page" change `c->browse({"FEwhat_to_watch", ""}, …)` to `c->feeds().home(…)` if the sed did not.

`tests/tst_catalog.cpp`: delete the checks on `feedSections()` and `searchTypes()` (keep `topicFeeds()` and the client-identity cases). `tests/tst_chains.cpp`: delete `userResolveThenBrowse`, `userResolveFails` (their behaviour is covered by the `fetchChannelId` tests); rename `Cap<Account>` → `Cap<AccountInfo>`. `tests/tst_parsers.cpp`: `const Account a` → `const AccountInfo a`.

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build build -j 2>&1 | grep -E "error" | head -3`
Expected: `'class yt::Client' has no member named 'videos'`.

- [ ] **Step 3: Rename `Account` → `AccountInfo`**

```sh
sed -i -E 's/\bAccount\b/AccountInfo/g' include/yt/types.h include/yt/session.h src/auth.cpp src/parsers/rendererparser.h src/parsers/accountparser.cpp src/core/chains.h src/core/chains.cpp
```

then check `git diff --stat` touches only those files and `grep -n "AccountInfoItem" src` prints nothing (the `rj::AccountItem` shape must be untouched — `\b` stops at `AccountItem`).

- [ ] **Step 4: Move the dispatch template into `src/engine.h`** — cut `httpStatusOf`, `ChainOf`, `runAttempt` (with their comments) and the `Engine::start` template definition out of `src/client.cpp` and paste them at the end of `src/engine.h`, inside `namespace yt::detail` after `struct Engine`, as `inline`/templates:

```cpp
// ---- dispatch (used by client.cpp and groups.cpp) ----

template <class T> long httpStatusOf(const Result<T> &r) { return r.error ? r.error->httpStatus : 0; }

template <class Arg>
using ChainOf = std::function<void(core::IHttp &, const core::JobToken &, std::function<void(const Arg &)>)>;

// One attempt of a chain. … (keep the existing comment verbatim)
template <class Arg>
void runAttempt(Engine *e, std::shared_ptr<JobHandle> h, ChainOf<Arg> chain,
                std::function<void(const Arg &)> cb, bool retried)
{
    … (existing body, unchanged)
}

template <class Arg>
Job Engine::start(std::function<void(core::IHttp &, const core::JobToken &, std::function<void(const Arg &)>)> chain,
                  std::function<void(const Arg &)> cb)
{
    … (existing body, unchanged)
}
```

The bodies move verbatim — only their location changes — because `groups.cpp` must
instantiate `Engine::start` too; `client.cpp` keeps including `engine.h`.

- [ ] **Step 5: `include/yt/client.h`** — add `#include <yt/link.h>` after `<yt/catalog.h>`. Delete `BrowseRequest`, `SearchRequest`, `enum class Action`. Replace `class Client` with the groups and the new `Client`:

```cpp
namespace detail { struct Engine; struct JobHandle; }

// Videos: a watch page, every playable source, captions and dislike counts. `link` is
// anything that names a video: an 11-character id or a watch / youtu.be / shorts / live /
// embed URL (yt::Link). Another kind of link fails with ErrorCode::InvalidArgument.
class Videos {
public:
    Job get(std::string link, Callback<WatchResult> done);               // details + related
    Job player(std::string link, Callback<Player> done);                 // every source (yt/player.h)
    Job player(std::string link, unsigned want, Callback<Player> done);  // want: Want* flags; 0 = metadata only
    Job captions(CaptionTrack track, Callback<std::vector<Cue>> done);   // a track from Player::captions
    Job votes(std::string link, Callback<Votes> done);                   // Return YouTube Dislike counts
    Videos(const Videos &) = delete;
    Videos &operator=(const Videos &) = delete;
private:
    friend class Client;
    explicit Videos(detail::Engine *e) : m_e(e) {}
    detail::Engine *m_e;
};

// Search: videos (sortable), channels and playlists, all paged; typing suggestions.
class Search {
public:
    Job videos(std::string query, Callback<Page<Video>> done);
    Job videos(std::string query, SearchOptions options, Callback<Page<Video>> done);
    Job channels(std::string query, Callback<Page<Channel>> done);
    Job playlists(std::string query, Callback<Page<Playlist>> done);
    Job suggestions(std::string prefix, Callback<std::vector<std::string>> done);
    Search(const Search &) = delete;
    Search &operator=(const Search &) = delete;
private:
    friend class Client;
    explicit Search(detail::Engine *e) : m_e(e) {}
    detail::Engine *m_e;
};

// Channels: `link` is a UC… id or a /channel/, /@handle, /c/ or /user/ URL (handles and
// legacy URLs cost one resolve request).
class Channels {
public:
    Job get(std::string link, Callback<Channel> done);            // signed in: `subscribed` is the viewer's state
    Job videos(std::string link, Callback<Page<Video>> done);     // the Videos tab
    Job playlists(std::string link, Callback<Page<Playlist>> done);
    Channels(const Channels &) = delete;
    Channels &operator=(const Channels &) = delete;
private:
    friend class Client;
    explicit Channels(detail::Engine *e) : m_e(e) {}
    detail::Engine *m_e;
};

// Playlists: `link` is a playlist id (PL…, RD…, UU…, OL…; "WL" Watch Later and "LL" Liked
// videos when signed in) or a /playlist?list= URL.
class Playlists {
public:
    Job videos(std::string link, Callback<Page<Video>> done);
    Playlists(const Playlists &) = delete;
    Playlists &operator=(const Playlists &) = delete;
private:
    friend class Client;
    explicit Playlists(detail::Engine *e) : m_e(e) {}
    detail::Engine *m_e;
};

// Feeds. home() is personal when signed in; subscriptions() and history() need sign-in.
class Feeds {
public:
    Job home(Callback<Page<Video>> done);
    Job subscriptions(Callback<Page<Video>> done);
    Job history(Callback<Page<Video>> done);
    Job topic(std::string id, Callback<Page<Video>> done);        // an id from yt::topicFeeds()
    Feeds(const Feeds &) = delete;
    Feeds &operator=(const Feeds &) = delete;
private:
    friend class Client;
    explicit Feeds(detail::Engine *e) : m_e(e) {}
    detail::Engine *m_e;
};

// Comments of a video (`link` as in Videos). Page::disabled = turned off for the video.
class Comments {
public:
    Job list(std::string link, Callback<Page<Comment>> done);
    Job post(std::string link, std::string text, Callback<Done> done);   // needs sign-in
    Comments(const Comments &) = delete;
    Comments &operator=(const Comments &) = delete;
private:
    friend class Client;
    explicit Comments(detail::Engine *e) : m_e(e) {}
    detail::Engine *m_e;
};

// The signed-in user: who they are, their ratings, subscriptions and playlists. Every call
// needs sign-in (Client::auth()).
class Account {
public:
    Job info(Callback<AccountInfo> done);
    Job rate(std::string videoLink, Rating rating, Callback<Done> done);   // Rating::None removes a like/dislike
    Job subscribe(std::string channelLink, Callback<Done> done);
    Job unsubscribe(std::string channelLink, Callback<Done> done);
    Job save(std::string videoLink, std::string playlistLink, Callback<Done> done);    // "WL" = Watch Later
    Job unsave(std::string videoLink, std::string playlistLink, Callback<Done> done);
    Job playlists(Callback<Page<Playlist>> done);                 // the user's own playlists
    Job subscriptions(Callback<Page<Channel>> done);              // the channels the user follows
    Account(const Account &) = delete;
    Account &operator=(const Account &) = delete;
private:
    friend class Client;
    explicit Account(detail::Engine *e) : m_e(e) {}
    detail::Engine *m_e;
};

class Client {
public:
    explicit Client(ClientOptions options = {});   // throws std::invalid_argument without a transport
    ~Client();
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;

    Videos &videos() { return m_videos; }
    Search &search() { return m_search; }
    Channels &channels() { return m_channels; }
    Playlists &playlists() { return m_playlists; }
    Feeds &feeds() { return m_feeds; }
    Comments &comments() { return m_comments; }
    Account &account() { return m_account; }
    Auth &auth() { return *m_auth; }

    // The next page of any list (Page::next). The last page's cursor fails with
    // ErrorCode::InvalidArgument and sends nothing.
    Job more(const Cursor<Video> &next, Callback<Page<Video>> done);
    Job more(const Cursor<Channel> &next, Callback<Page<Channel>> done);
    Job more(const Cursor<Playlist> &next, Callback<Page<Playlist>> done);
    Job more(const Cursor<Comment> &next, Callback<Page<Comment>> done);

    void setLocale(std::string hl, std::string gl);   // empty keeps the current value; clears caches
    void runOnce(int maxWaitMs);                       // only when ownThread == false

private:
    friend struct detail::Engine;
    explicit Client(std::shared_ptr<detail::Engine> engine);
    std::shared_ptr<detail::Engine> m_engine;
    std::unique_ptr<Auth> m_auth;
    Videos m_videos;
    Search m_search;
    Channels m_channels;
    Playlists m_playlists;
    Feeds m_feeds;
    Comments m_comments;
    Account m_account;
};
```

Also update the threading-contract comment at the top of `client.h`: in item 1 "Every Client / Auth method" → "Every Client, group (videos(), search(), …) and Auth method".

- [ ] **Step 6: `src/client.cpp`** — delete every old `Client::` operation wrapper (`browse` … `suggestions`) and the `Chain` alias; keep `Client::more` (Task 3). Replace the two constructors with:

```cpp
namespace {
std::shared_ptr<Engine> engineFor(ClientOptions options)
{
    auto e = std::make_shared<Engine>();
    e->opts = std::move(options);
    return Engine::boot(std::move(e));
}
}

Client::Client(ClientOptions options) : Client(engineFor(std::move(options))) {}

Client::Client(std::shared_ptr<detail::Engine> engine)
    : m_engine(std::move(engine)), m_auth(new Auth(m_engine.get())), m_videos(m_engine.get()),
      m_search(m_engine.get()), m_channels(m_engine.get()), m_playlists(m_engine.get()),
      m_feeds(m_engine.get()), m_comments(m_engine.get()), m_account(m_engine.get())
{
}
```

(`engineFor` sits in the anonymous namespace after `using detail::Engine;`.)

- [ ] **Step 7: Create `src/groups.cpp`** and add it to `CMakeLists.txt` after `src/client.cpp`:

```cpp
// The resource groups of yt::Client (client.h): thin wrappers that turn a link into an id
// and hand one chain to Engine::start. No request logic (core/chains.cpp) and no state —
// every group is a view of the one Engine.
#include "engine.h"
#include "core/chains.h"

namespace yt {

namespace {
using detail::Engine;

template <class T> using Chain = std::function<void(core::IHttp &, const core::JobToken &, Callback<T>)>;

template <class T> Job run(Engine *e, Chain<T> chain, Callback<T> done)
{
    return e->start<Result<T>>(std::move(chain), std::move(done));
}

template <class T> void fail(const Callback<T> &done, Error error)
{
    Result<T> r;
    r.error = std::move(error);
    done(r);
}

Job feed(Engine *e, std::string browseId, Callback<Page<Video>> done)
{
    return run<Page<Video>>(e, [browseId](core::IHttp &h, const core::JobToken &t, Callback<Page<Video>> d) {
        core::VideoListSpec s;
        s.browseId = browseId;
        core::fetchVideoList(h, s, t, std::move(d));
    }, std::move(done));
}

Job action(Engine *e, std::string videoLink, core::ActionKind kind, Callback<Done> done)
{
    return run<Done>(e, [videoLink, kind](core::IHttp &h, const core::JobToken &t, Callback<Done> d) {
        const std::optional<std::string> id = core::videoIdOf(videoLink);
        if (!id) return fail(d, core::badLink(videoLink, "video"));
        core::submitAction(h, kind, *id, t, std::move(d));
    }, std::move(done));
}

Job edit(Engine *e, std::string videoLink, std::string playlistLink, core::PlaylistEdit what, Callback<Done> done)
{
    return run<Done>(e, [videoLink, playlistLink, what](core::IHttp &h, const core::JobToken &t, Callback<Done> d) {
        const std::optional<std::string> video = core::videoIdOf(videoLink);
        if (!video) return fail(d, core::badLink(videoLink, "video"));
        const std::optional<std::string> list = core::playlistIdOf(playlistLink);
        if (!list) return fail(d, core::badLink(playlistLink, "playlist"));
        core::editPlaylist(h, *list, what, *video, t, std::move(d));
    }, std::move(done));
}
}

// ---- Videos ----

Job Videos::get(std::string link, Callback<WatchResult> done)
{
    return run<WatchResult>(m_e, [link](core::IHttp &h, const core::JobToken &t, Callback<WatchResult> d) {
        const std::optional<std::string> id = core::videoIdOf(link);
        if (!id) return fail(d, core::badLink(link, "video"));
        core::fetchWatch(h, *id, t, std::move(d));
    }, std::move(done));
}

Job Videos::player(std::string link, Callback<Player> done)
{
    return player(std::move(link), WantAll, std::move(done));
}

Job Videos::player(std::string link, unsigned want, Callback<Player> done)
{
    return run<Player>(m_e, [link, want](core::IHttp &h, const core::JobToken &t, Callback<Player> d) {
        const std::optional<std::string> id = core::videoIdOf(link);
        if (!id) return fail(d, core::badLink(link, "video"));
        core::fetchPlayer(h, PlayerRequest{*id, want}, t, std::move(d));
    }, std::move(done));
}

Job Videos::captions(CaptionTrack track, Callback<std::vector<Cue>> done)
{
    return run<std::vector<Cue>>(m_e, [track](core::IHttp &h, const core::JobToken &t, Callback<std::vector<Cue>> d) {
        core::fetchCaptions(h, track, t, std::move(d));
    }, std::move(done));
}

Job Videos::votes(std::string link, Callback<Votes> done)
{
    return run<Votes>(m_e, [link](core::IHttp &h, const core::JobToken &t, Callback<Votes> d) {
        const std::optional<std::string> id = core::videoIdOf(link);
        if (!id) return fail(d, core::badLink(link, "video"));
        core::fetchDislikes(h, *id, t, std::move(d));
    }, std::move(done));
}

// ---- Search ----

Job Search::videos(std::string query, Callback<Page<Video>> done)
{
    return videos(std::move(query), SearchOptions{}, std::move(done));
}

Job Search::videos(std::string query, SearchOptions options, Callback<Page<Video>> done)
{
    return run<Page<Video>>(m_e, [query, options](core::IHttp &h, const core::JobToken &t, Callback<Page<Video>> d) {
        core::VideoListSpec s;
        s.kind = core::VideoListSpec::Search;
        s.query = query;
        s.sort = options.sort;
        core::fetchVideoList(h, s, t, std::move(d));
    }, std::move(done));
}

Job Search::channels(std::string query, Callback<Page<Channel>> done)
{
    return run<Page<Channel>>(m_e, [query](core::IHttp &h, const core::JobToken &t, Callback<Page<Channel>> d) {
        core::fetchUserSearch(h, query, t, std::move(d));
    }, std::move(done));
}

Job Search::playlists(std::string query, Callback<Page<Playlist>> done)
{
    return run<Page<Playlist>>(m_e, [query](core::IHttp &h, const core::JobToken &t, Callback<Page<Playlist>> d) {
        core::fetchPlaylistSearch(h, query, t, std::move(d));
    }, std::move(done));
}

Job Search::suggestions(std::string prefix, Callback<std::vector<std::string>> done)
{
    return run<std::vector<std::string>>(m_e,
        [prefix](core::IHttp &h, const core::JobToken &t, Callback<std::vector<std::string>> d) {
            core::fetchSearchSuggestions(h, prefix, t, std::move(d));
        }, std::move(done));
}

// ---- Channels ----

Job Channels::get(std::string link, Callback<Channel> done)
{
    return run<Channel>(m_e, [link](core::IHttp &h, const core::JobToken &t, Callback<Channel> d) {
        core::fetchChannel(h, link, t, std::move(d));
    }, std::move(done));
}

Job Channels::videos(std::string link, Callback<Page<Video>> done)
{
    return run<Page<Video>>(m_e, [link](core::IHttp &h, const core::JobToken &t, Callback<Page<Video>> d) {
        core::fetchChannelVideos(h, link, t, std::move(d));
    }, std::move(done));
}

Job Channels::playlists(std::string link, Callback<Page<Playlist>> done)
{
    return run<Page<Playlist>>(m_e, [link](core::IHttp &h, const core::JobToken &t, Callback<Page<Playlist>> d) {
        core::fetchChannelPlaylists(h, link, t, std::move(d));
    }, std::move(done));
}

// ---- Playlists ----

Job Playlists::videos(std::string link, Callback<Page<Video>> done)
{
    return run<Page<Video>>(m_e, [link](core::IHttp &h, const core::JobToken &t, Callback<Page<Video>> d) {
        const std::optional<std::string> id = core::playlistIdOf(link);
        if (!id) return fail(d, core::badLink(link, "playlist"));
        core::VideoListSpec s;
        s.browseId = "VL" + *id;                     // a playlist's page is the browse of VL<id>
        core::fetchVideoList(h, s, t, std::move(d));
    }, std::move(done));
}

// ---- Feeds ----

Job Feeds::home(Callback<Page<Video>> done) { return feed(m_e, "FEwhat_to_watch", std::move(done)); }
Job Feeds::subscriptions(Callback<Page<Video>> done) { return feed(m_e, "FEsubscriptions", std::move(done)); }
Job Feeds::history(Callback<Page<Video>> done) { return feed(m_e, "FEhistory", std::move(done)); }
Job Feeds::topic(std::string id, Callback<Page<Video>> done) { return feed(m_e, std::move(id), std::move(done)); }

// ---- Comments ----

Job Comments::list(std::string link, Callback<Page<Comment>> done)
{
    return run<Page<Comment>>(m_e, [link](core::IHttp &h, const core::JobToken &t, Callback<Page<Comment>> d) {
        const std::optional<std::string> id = core::videoIdOf(link);
        if (!id) return fail(d, core::badLink(link, "video"));
        core::fetchComments(h, *id, t, std::move(d));
    }, std::move(done));
}

Job Comments::post(std::string link, std::string text, Callback<Done> done)
{
    return run<Done>(m_e, [link, text](core::IHttp &h, const core::JobToken &t, Callback<Done> d) {
        const std::optional<std::string> id = core::videoIdOf(link);
        if (!id) return fail(d, core::badLink(link, "video"));
        core::postComment(h, *id, text, t, std::move(d));
    }, std::move(done));
}

// ---- Account ----

Job Account::info(Callback<AccountInfo> done)
{
    return run<AccountInfo>(m_e, [](core::IHttp &h, const core::JobToken &t, Callback<AccountInfo> d) {
        core::fetchAccount(h, t, std::move(d));
    }, std::move(done));
}

Job Account::rate(std::string videoLink, Rating rating, Callback<Done> done)
{
    const core::ActionKind kind = rating == Rating::Like ? core::Like
                                : rating == Rating::Dislike ? core::Dislike : core::RemoveLike;
    return action(m_e, std::move(videoLink), kind, std::move(done));
}

Job Account::subscribe(std::string channelLink, Callback<Done> done)
{
    return run<Done>(m_e, [channelLink](core::IHttp &h, const core::JobToken &t, Callback<Done> d) {
        core::channelAction(h, channelLink, core::Subscribe, t, std::move(d));
    }, std::move(done));
}

Job Account::unsubscribe(std::string channelLink, Callback<Done> done)
{
    return run<Done>(m_e, [channelLink](core::IHttp &h, const core::JobToken &t, Callback<Done> d) {
        core::channelAction(h, channelLink, core::Unsubscribe, t, std::move(d));
    }, std::move(done));
}

Job Account::save(std::string videoLink, std::string playlistLink, Callback<Done> done)
{
    return edit(m_e, std::move(videoLink), std::move(playlistLink), core::AddVideo, std::move(done));
}

Job Account::unsave(std::string videoLink, std::string playlistLink, Callback<Done> done)
{
    return edit(m_e, std::move(videoLink), std::move(playlistLink), core::RemoveVideo, std::move(done));
}

Job Account::playlists(Callback<Page<Playlist>> done)
{
    return run<Page<Playlist>>(m_e, [](core::IHttp &h, const core::JobToken &t, Callback<Page<Playlist>> d) {
        core::fetchPlaylists(h, "FElibrary", "", t, std::move(d));
    }, std::move(done));
}

Job Account::subscriptions(Callback<Page<Channel>> done)
{
    return run<Page<Channel>>(m_e, [](core::IHttp &h, const core::JobToken &t, Callback<Page<Channel>> d) {
        core::fetchChannelList(h, "FEchannels", t, std::move(d));
    }, std::move(done));
}

}
```

- [ ] **Step 8: Remove what the groups replaced** — `src/core/chains.h/.cpp`: delete `fetchChannelByUrl` (unused now). `include/yt/catalog.h` + `src/catalog.cpp`: delete `SearchOrder`, `SearchType`, `searchTypes()`, `feedSections()` and their comments (the header comment's "and the search types" goes too). Run:

```sh
grep -rnE "BrowseRequest|SearchRequest|Action::|channelByUrl|channelSubscribed|searchTypes|feedSections|fetchChannelByUrl|\bAccount\b[^I(]" include src tests examples
```

Expected: only `class Account` / `Account &account()` / `Account(const Account` in `client.h` and the `Account::` definitions in `groups.cpp`.

- [ ] **Step 9: Examples** — rewrite the four examples onto the groups (callback form; Task 7 turns `yt-cli` into the blocking form):

`examples/channel/main.cpp`, replace the body of `main` after the argument check:

```cpp
    yt::ClientOptions o;
    o.ownThread = false;
    yt::Client client(o);
    int pending = 2, rc = 0;
    auto fail = [&](const yt::Error &e) { std::fprintf(stderr, "error: %s\n", e.message.c_str()); rc = 1; };

    // Two independent requests: the header and the Videos tab both accept the same link.
    client.channels().get(argv[1], [&](const yt::Result<yt::Channel> &r) {
        --pending;
        if (!r.ok()) return fail(*r.error);
        const yt::Channel &c = r.value;
        std::printf("%s (%s) %s · %s · %s\n", c.name.c_str(), c.id.c_str(), c.handle.c_str(),
                    c.subscriberCount.c_str(), c.videoCount.c_str());
    });
    client.channels().videos(argv[1], [&](const yt::Result<yt::Page<yt::Video>> &p) {
        --pending;
        if (!p.ok()) return fail(*p.error);
        for (const yt::Video &v : p.value.items)
            std::printf("  %-12s %-8s %s\n", v.id.c_str(), v.duration.c_str(), v.title.c_str());
    });

    while (pending > 0) client.runOnce(100);
    return rc;
```

and its header comment: "yt-channel — one link, two requests in flight: the channel header and its Videos tab.\n//\n//   yt-channel <link>     e.g. https://www.youtube.com/@NASA, @NASA or UC…". `examples/watch/main.cpp`: `client.watch(id, …)` → `client.videos().get(id, …)`, `client.votes(id, …)` → `client.videos().votes(id, …)`, `client.comments(id, onComments)` → `client.comments().list(id, onComments)`; usage text "<videoId>" → "<link>". `examples/suggest/main.cpp`: `client.suggestions(prefix, …)` → `client.search().suggestions(prefix, …)`. `examples/yt-cli/main.cpp`: `client.browse({arg, ""}, …)` → `client.feeds().topic(arg, …)`; `client.search({arg}, …)` → `client.search().videos(arg, …)`; `client.player({arg}, …)` → `client.videos().player(arg, …)`; `client.player({arg, 0}, …)` → `client.videos().player(arg, 0, …)`; `client.captions(pick, …)` → `client.videos().captions(pick, …)`.

- [ ] **Step 10: Run all tests**

Run: `cmake -S . -B build -G Ninja >/dev/null && cmake --build build -j 2>&1 | grep -E "warning|error"; (cd build && ctest -j8 --output-on-failure)`
Expected: no warnings; all suites PASS (tst_types still builds at C++17 with the new `client.h` through its includes, if it includes it; `tst_link` too).

- [ ] **Step 11: Commit**

```bash
git add -A include src tests examples CMakeLists.txt
git commit -m "lib: resource groups replace the flat Client" -m "yt.videos(), .search(), .channels(), .playlists(), .feeds(), .comments() and
.account() accept any YouTube link and return Page<T> for every list. BrowseRequest,
SearchRequest, Action, channelByUrl/channelSubscribed and the catalog's
searchTypes/feedSections are gone; yt::Account (the identity) is now AccountInfo." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CcpAWezZwfUf6YSVnLXjYq"
```

---

### Task 6: the blocking form — a `std::future` next to every callback

**Files:**
- Modify: `src/engine.h` (`JobHandle::direct`, `DirectDelivery`, `PromiseBox`, `futureOf`), `src/client.cpp` (`Engine::deliver`, `newHandle` flag, `Client::more` futures)
- Modify: `include/yt/client.h` (future overloads), `src/groups.cpp`
- Test: `tests/tst_client.cpp`

**Interfaces:**
- Consumes: Task 5 (every group method's callback form).
- Produces: for every callback method `Job m(args…, Callback<T> done)` in `Videos`, `Search`, `Channels`, `Playlists`, `Feeds`, `Comments`, `Account` and `Client::more`, an overload `std::future<Result<T>> m(args…)`. `Auth` is unchanged (two callbacks).

- [ ] **Step 1: Write the failing tests** — append to `tests/tst_client.cpp` (add `#include <future>` and `#include <chrono>`):

```cpp
namespace {
template <class F> bool ready(F &f) { return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }
}

TEST_CASE("the future form delivers the result") {
    auto f = std::make_shared<FakeHttp>(); f->queue("browse", loadFixtureRaw("browse_feed.json"));
    auto c = mk(f);
    std::future<yt::Result<yt::Page<yt::Video>>> home = c->feeds().home();
    CHECK_FALSE(ready(home));                         // only posted, like the callback form
    for (int i = 0; i < 8 && !ready(home); ++i) { c->runOnce(0); f->flush(); }
    REQUIRE(ready(home));
    const yt::Result<yt::Page<yt::Video>> r = home.get();
    REQUIRE(r.ok());
    CHECK(r.value.items.size() == 2);
}
// Review focus: a GUI blocks its own (delivery) thread on get(): the result must not wait
// for that executor.
TEST_CASE("the future form does not go through the executor") {
    auto f = std::make_shared<FakeHttp>(); f->queue("browse", loadFixtureRaw("browse_feed.json"));
    QueueExec ex;
    yt::ClientOptions o; o.deliver = ex.exec();
    auto c = mk(f, o);
    auto home = c->feeds().home();
    for (int i = 0; i < 8 && !ready(home); ++i) { c->runOnce(0); f->flush(); }
    CHECK(ready(home));
    CHECK(ex.size() == 0);                            // nothing waits for the executor
}
// Review focus: a future must never hang: destroying the Client releases it as Canceled.
TEST_CASE("destroying the client releases a pending future as Canceled") {
    auto f = std::make_shared<FakeHttp>();
    auto c = mk(f);
    auto home = c->feeds().home();
    c->runOnce(0);                                    // the request is in flight in FakeHttp
    c.reset();
    f.reset();                                        // the transport drops its pending replies
    REQUIRE(ready(home));
    const auto r = home.get();
    REQUIRE_FALSE(r.ok());
    CHECK(r.error->code == yt::ErrorCode::Canceled);
}
TEST_CASE("futures for links, pages and more") {
    auto f = std::make_shared<FakeHttp>();
    f->queue("browse", loadFixtureRaw("browse_feed.json"));
    f->queue("browse", loadFixtureRaw("browse_feed.json"));
    auto c = mk(f);
    auto bad = c->videos().get("not a link");
    auto first = c->playlists().videos("PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI");
    for (int i = 0; i < 8 && !(ready(bad) && ready(first)); ++i) { c->runOnce(0); f->flush(); }
    REQUIRE(ready(bad));
    CHECK(bad.get().error->code == yt::ErrorCode::InvalidArgument);
    REQUIRE(ready(first));
    const auto page = first.get();
    REQUIRE(page.ok());
    CHECK(f->sent[0] == "{\"browseId\":\"VLPLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI\"}");
    auto second = c->more(page.value.next);
    for (int i = 0; i < 8 && !ready(second); ++i) { c->runOnce(0); f->flush(); }
    REQUIRE(ready(second));
    CHECK(second.get().ok());
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build build -j 2>&1 | grep -E "error" | head -3`
Expected: `no matching function for call to 'yt::Feeds::home()'`.

- [ ] **Step 3: `src/engine.h`** — add `#include <future>` to the includes; in `struct JobHandle` add after `finished`:

```cpp
    bool direct = false;                             // future form: delivered without the executor
```

and before `struct Engine`:

```cpp
// While alive on a thread, the Job that thread starts is a future-form call: its result is
// delivered straight from the engine thread, never through ClientOptions::deliver — a GUI
// that blocks its own UI (delivery) thread on future::get() would otherwise wait forever.
struct DirectDelivery {
    DirectDelivery();
    ~DirectDelivery();
    DirectDelivery(const DirectDelivery &) = delete;
    DirectDelivery &operator=(const DirectDelivery &) = delete;
};
bool directDelivery();                               // inside a DirectDelivery scope on this thread

// The promise behind a future-form call. Fulfilled once, from the engine thread; if the
// call dies undelivered (Client destroyed, request dropped) the last owner releases the
// future with ErrorCode::Canceled instead of leaving get() blocked forever.
template <class T> struct PromiseBox {
    std::promise<Result<T>> promise;
    std::atomic<bool> set{false};
    void fulfil(const Result<T> &r)
    {
        if (!set.exchange(true)) promise.set_value(r);
    }
    ~PromiseBox()
    {
        if (set.exchange(true)) return;
        Result<T> r;
        r.error = Error{ErrorCode::Canceled, "the client was destroyed before the call completed", 0};
        promise.set_value(r);
    }
};
```

and after the `Engine::start` template (end of file, still in `yt::detail`):

```cpp
// The future form of an operation: `start` is the callback form, started inside a
// DirectDelivery scope.
template <class T, class Start> std::future<Result<T>> futureOf(Start start)
{
    auto box = std::make_shared<PromiseBox<T>>();
    std::future<Result<T>> f = box->promise.get_future();
    DirectDelivery direct;
    start(Callback<T>([box](const Result<T> &r) { box->fulfil(r); }));
    return f;
}
```

In `Engine::newHandle()` add before `return h;`: `h->direct = directDelivery();`.

- [ ] **Step 4: `src/client.cpp`** — at the top of `namespace detail` add:

```cpp
namespace {
thread_local int tDirect = 0;                        // nesting depth of DirectDelivery on this thread
}
DirectDelivery::DirectDelivery() { ++tDirect; }
DirectDelivery::~DirectDelivery() { --tDirect; }
bool directDelivery() { return tDirect > 0; }
```

In `Engine::deliver` replace the last two lines with:

```cpp
    if (opts.deliver && !h->direct) opts.deliver(std::move(task));
    else task();
```

Add the `Client::more` futures after the callback forms:

```cpp
std::future<Result<Page<Video>>> Client::more(const Cursor<Video> &next)
{
    return detail::futureOf<Page<Video>>([&](Callback<Page<Video>> cb) { return more(next, std::move(cb)); });
}
std::future<Result<Page<Channel>>> Client::more(const Cursor<Channel> &next)
{
    return detail::futureOf<Page<Channel>>([&](Callback<Page<Channel>> cb) { return more(next, std::move(cb)); });
}
std::future<Result<Page<Playlist>>> Client::more(const Cursor<Playlist> &next)
{
    return detail::futureOf<Page<Playlist>>([&](Callback<Page<Playlist>> cb) { return more(next, std::move(cb)); });
}
std::future<Result<Page<Comment>>> Client::more(const Cursor<Comment> &next)
{
    return detail::futureOf<Page<Comment>>([&](Callback<Page<Comment>> cb) { return more(next, std::move(cb)); });
}
```

- [ ] **Step 5: `include/yt/client.h`** — add `#include <future>`; add to the comment above `class Videos`: "Every method has two forms: with a callback (returns a Job; the contract above) and without one (returns a std::future, fulfilled from the engine thread without ClientOptions::deliver; with ownThread == false someone must keep calling runOnce() or get() never returns). A Client destroyed first releases the future with ErrorCode::Canceled." Then add after the callback forms of each class:

```cpp
    // Videos
    std::future<Result<WatchResult>> get(std::string link);
    std::future<Result<Player>> player(std::string link);
    std::future<Result<Player>> player(std::string link, unsigned want);
    std::future<Result<std::vector<Cue>>> captions(CaptionTrack track);
    std::future<Result<Votes>> votes(std::string link);
```

```cpp
    // Search
    std::future<Result<Page<Video>>> videos(std::string query);
    std::future<Result<Page<Video>>> videos(std::string query, SearchOptions options);
    std::future<Result<Page<Channel>>> channels(std::string query);
    std::future<Result<Page<Playlist>>> playlists(std::string query);
    std::future<Result<std::vector<std::string>>> suggestions(std::string prefix);
```

```cpp
    // Channels
    std::future<Result<Channel>> get(std::string link);
    std::future<Result<Page<Video>>> videos(std::string link);
    std::future<Result<Page<Playlist>>> playlists(std::string link);
```

```cpp
    // Playlists
    std::future<Result<Page<Video>>> videos(std::string link);
```

```cpp
    // Feeds
    std::future<Result<Page<Video>>> home();
    std::future<Result<Page<Video>>> subscriptions();
    std::future<Result<Page<Video>>> history();
    std::future<Result<Page<Video>>> topic(std::string id);
```

```cpp
    // Comments
    std::future<Result<Page<Comment>>> list(std::string link);
    std::future<Result<Done>> post(std::string link, std::string text);
```

```cpp
    // Account
    std::future<Result<AccountInfo>> info();
    std::future<Result<Done>> rate(std::string videoLink, Rating rating);
    std::future<Result<Done>> subscribe(std::string channelLink);
    std::future<Result<Done>> unsubscribe(std::string channelLink);
    std::future<Result<Done>> save(std::string videoLink, std::string playlistLink);
    std::future<Result<Done>> unsave(std::string videoLink, std::string playlistLink);
    std::future<Result<Page<Playlist>>> playlists();
    std::future<Result<Page<Channel>>> subscriptions();
```

```cpp
    // Client
    std::future<Result<Page<Video>>> more(const Cursor<Video> &next);
    std::future<Result<Page<Channel>>> more(const Cursor<Channel> &next);
    std::future<Result<Page<Playlist>>> more(const Cursor<Playlist> &next);
    std::future<Result<Page<Comment>>> more(const Cursor<Comment> &next);
```

(The `// Videos` etc. lines are not code: put each block inside its class, after the callback declarations.)

- [ ] **Step 6: `src/groups.cpp`** — add after each callback definition its future form; every one is the same one-liner over its callback form, e.g.:

```cpp
std::future<Result<WatchResult>> Videos::get(std::string link)
{
    return detail::futureOf<WatchResult>([&](Callback<WatchResult> cb) { return get(std::move(link), std::move(cb)); });
}
std::future<Result<Player>> Videos::player(std::string link)
{
    return detail::futureOf<Player>([&](Callback<Player> cb) { return player(std::move(link), std::move(cb)); });
}
std::future<Result<Player>> Videos::player(std::string link, unsigned want)
{
    return detail::futureOf<Player>([&](Callback<Player> cb) { return player(std::move(link), want, std::move(cb)); });
}
std::future<Result<std::vector<Cue>>> Videos::captions(CaptionTrack track)
{
    return detail::futureOf<std::vector<Cue>>([&](Callback<std::vector<Cue>> cb) { return captions(std::move(track), std::move(cb)); });
}
std::future<Result<Votes>> Videos::votes(std::string link)
{
    return detail::futureOf<Votes>([&](Callback<Votes> cb) { return votes(std::move(link), std::move(cb)); });
}
std::future<Result<Page<Video>>> Search::videos(std::string query)
{
    return detail::futureOf<Page<Video>>([&](Callback<Page<Video>> cb) { return videos(std::move(query), std::move(cb)); });
}
std::future<Result<Page<Video>>> Search::videos(std::string query, SearchOptions options)
{
    return detail::futureOf<Page<Video>>([&](Callback<Page<Video>> cb) { return videos(std::move(query), options, std::move(cb)); });
}
std::future<Result<Page<Channel>>> Search::channels(std::string query)
{
    return detail::futureOf<Page<Channel>>([&](Callback<Page<Channel>> cb) { return channels(std::move(query), std::move(cb)); });
}
std::future<Result<Page<Playlist>>> Search::playlists(std::string query)
{
    return detail::futureOf<Page<Playlist>>([&](Callback<Page<Playlist>> cb) { return playlists(std::move(query), std::move(cb)); });
}
std::future<Result<std::vector<std::string>>> Search::suggestions(std::string prefix)
{
    return detail::futureOf<std::vector<std::string>>(
        [&](Callback<std::vector<std::string>> cb) { return suggestions(std::move(prefix), std::move(cb)); });
}
std::future<Result<Channel>> Channels::get(std::string link)
{
    return detail::futureOf<Channel>([&](Callback<Channel> cb) { return get(std::move(link), std::move(cb)); });
}
std::future<Result<Page<Video>>> Channels::videos(std::string link)
{
    return detail::futureOf<Page<Video>>([&](Callback<Page<Video>> cb) { return videos(std::move(link), std::move(cb)); });
}
std::future<Result<Page<Playlist>>> Channels::playlists(std::string link)
{
    return detail::futureOf<Page<Playlist>>([&](Callback<Page<Playlist>> cb) { return playlists(std::move(link), std::move(cb)); });
}
std::future<Result<Page<Video>>> Playlists::videos(std::string link)
{
    return detail::futureOf<Page<Video>>([&](Callback<Page<Video>> cb) { return videos(std::move(link), std::move(cb)); });
}
std::future<Result<Page<Video>>> Feeds::home()
{
    return detail::futureOf<Page<Video>>([&](Callback<Page<Video>> cb) { return home(std::move(cb)); });
}
std::future<Result<Page<Video>>> Feeds::subscriptions()
{
    return detail::futureOf<Page<Video>>([&](Callback<Page<Video>> cb) { return subscriptions(std::move(cb)); });
}
std::future<Result<Page<Video>>> Feeds::history()
{
    return detail::futureOf<Page<Video>>([&](Callback<Page<Video>> cb) { return history(std::move(cb)); });
}
std::future<Result<Page<Video>>> Feeds::topic(std::string id)
{
    return detail::futureOf<Page<Video>>([&](Callback<Page<Video>> cb) { return topic(std::move(id), std::move(cb)); });
}
std::future<Result<Page<Comment>>> Comments::list(std::string link)
{
    return detail::futureOf<Page<Comment>>([&](Callback<Page<Comment>> cb) { return list(std::move(link), std::move(cb)); });
}
std::future<Result<Done>> Comments::post(std::string link, std::string text)
{
    return detail::futureOf<Done>([&](Callback<Done> cb) { return post(std::move(link), std::move(text), std::move(cb)); });
}
std::future<Result<AccountInfo>> Account::info()
{
    return detail::futureOf<AccountInfo>([&](Callback<AccountInfo> cb) { return info(std::move(cb)); });
}
std::future<Result<Done>> Account::rate(std::string videoLink, Rating rating)
{
    return detail::futureOf<Done>([&](Callback<Done> cb) { return rate(std::move(videoLink), rating, std::move(cb)); });
}
std::future<Result<Done>> Account::subscribe(std::string channelLink)
{
    return detail::futureOf<Done>([&](Callback<Done> cb) { return subscribe(std::move(channelLink), std::move(cb)); });
}
std::future<Result<Done>> Account::unsubscribe(std::string channelLink)
{
    return detail::futureOf<Done>([&](Callback<Done> cb) { return unsubscribe(std::move(channelLink), std::move(cb)); });
}
std::future<Result<Done>> Account::save(std::string videoLink, std::string playlistLink)
{
    return detail::futureOf<Done>(
        [&](Callback<Done> cb) { return save(std::move(videoLink), std::move(playlistLink), std::move(cb)); });
}
std::future<Result<Done>> Account::unsave(std::string videoLink, std::string playlistLink)
{
    return detail::futureOf<Done>(
        [&](Callback<Done> cb) { return unsave(std::move(videoLink), std::move(playlistLink), std::move(cb)); });
}
std::future<Result<Page<Playlist>>> Account::playlists()
{
    return detail::futureOf<Page<Playlist>>([&](Callback<Page<Playlist>> cb) { return playlists(std::move(cb)); });
}
std::future<Result<Page<Channel>>> Account::subscriptions()
{
    return detail::futureOf<Page<Channel>>([&](Callback<Page<Channel>> cb) { return subscriptions(std::move(cb)); });
}
```

- [ ] **Step 7: Run all tests (and the sanitizers, which watch the PromiseBox handover)**

Run: `cmake --build build -j 2>&1 | grep -E "warning|error"; (cd build && ctest -j8 --output-on-failure)`, then `cmake --build build-tsan -j && (cd build-tsan && ctest --output-on-failure -R tst_client)` and the same for `build-asan`.
Expected: no warnings; all PASS; no TSan/ASan reports.

- [ ] **Step 8: Commit**

```bash
git add include/yt/client.h src/engine.h src/client.cpp src/groups.cpp tests/tst_client.cpp
git commit -m "lib: every operation also returns a std::future" -m "The future form is fulfilled from the engine thread, bypassing ClientOptions::deliver,
so a GUI that blocks its delivery thread on get() cannot deadlock; a Client destroyed
first releases pending futures with ErrorCode::Canceled." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CcpAWezZwfUf6YSVnLXjYq"
```

---

### Task 7: umbrella header, examples and docs in the new style, full verification

**Files:**
- Create: `include/yt/yt.h`
- Modify: `examples/yt-cli/main.cpp` (blocking form), `README.md`, `AGENTS.md`, `docs/specs/2026-10-15-api-scenarios.md`, `docs/specs/2026-10-15-api-usability-research.md`
- Test: `tests/tst_types.cpp`

- [ ] **Step 1: Failing test** — in `tests/tst_types.cpp` replace `#include <yt/types.h>` with `#include <yt/yt.h>` and add:

```cpp
TEST_CASE("the umbrella header is enough") {
    std::optional<yt::Link> l = yt::Link::parse("dQw4w9WgXcQ");
    CHECK(l.has_value());
    yt::Page<yt::Video> p;
    CHECK_FALSE(p.next);
    yt::ClientOptions o;
    CHECK(o.ownThread);
}
```

Run: `cmake --build build -j 2>&1 | grep error | head -2` → `fatal error: yt/yt.h: No such file or directory`.

- [ ] **Step 2: `include/yt/yt.h`**

```cpp
// innertube-cpp: the whole public API in one include. C++17.
#ifndef YT_YT_H
#define YT_YT_H
#include <yt/auth.h>
#include <yt/catalog.h>
#include <yt/client.h>
#include <yt/link.h>
#include <yt/log.h>
#include <yt/player.h>
#include <yt/result.h>
#include <yt/session.h>
#include <yt/transport.h>
#include <yt/types.h>
#include <yt/version.h>
#endif
```

Run: `cmake --build build -j && (cd build && ctest --output-on-failure -R tst_types)` → PASS (at C++17).

- [ ] **Step 3: `examples/yt-cli/main.cpp`, the blocking form** — replace the file from `namespace {` to the end with:

```cpp
namespace {

struct SavedState { std::string hl = "en", gl = "US", visitorData, refreshToken, playerId; int signatureTimestamp = 0; };
const char *kStateFile = "yt-cli-state.json";

yt::SessionState load()
{
    yt::SessionState s;
    std::ifstream f(kStateFile);
    if (!f) return s;
    std::stringstream ss; ss << f.rdbuf();
    SavedState saved;
    if (!glz::read<glz::opts{.error_on_unknown_keys = false}>(saved, ss.str())) {
        s.hl = saved.hl; s.gl = saved.gl; s.visitorData = saved.visitorData; s.refreshToken = saved.refreshToken;
        s.playerId = saved.playerId; s.signatureTimestamp = saved.signatureTimestamp;
    }
    return s;
}

// The file holds the OAuth refresh token: owner-only, written to a temp file and
// renamed so a crash never leaves a half-written state behind.
void save(const yt::SessionState &s)
{
    SavedState out{s.hl, s.gl, s.visitorData, s.refreshToken, s.playerId, s.signatureTimestamp};
    const std::string json = glz::write_json(out).value_or("{}");
    const std::string tmp = std::string(kStateFile) + ".tmp";
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return;
    ::fchmod(fd, 0600);                              // also when the file already existed
    const bool ok = ::write(fd, json.data(), json.size()) == (ssize_t) json.size();
    ::close(fd);
    if (ok) std::rename(tmp.c_str(), kStateFile); else std::remove(tmp.c_str());
}

int fail(const yt::Error &e)
{
    std::fprintf(stderr, "error: %s\n", e.message.c_str());
    return 1;
}

int printVideos(const yt::Result<yt::Page<yt::Video>> &r)
{
    if (!r.ok()) return fail(*r.error);
    for (const yt::Video &v : r.value.items)
        std::printf("%-12s %-8s %s — %s\n", v.id.c_str(), v.duration.c_str(), v.title.c_str(), v.channelName.c_str());
    return 0;
}

int streams(yt::Client &yt, const std::string &link)
{
    const yt::Result<yt::Player> r = yt.videos().player(link).get();
    if (!r.ok()) return fail(*r.error);
    const yt::Player &p = r.value;
    std::printf("%s — %s [%s%s%s]\n", p.details.title.c_str(), p.details.author.c_str(), p.status.status.c_str(),
                p.status.reason.empty() ? "" : ": ", p.status.reason.c_str());
    for (const yt::Source &s : p.sources) {
        std::printf("source %s  hls=%d sabr=%d  UA %s\n", yt::defaultClientInfo(s.client).name.c_str(),
                    (int) !s.hlsUrl.empty(), (int) !s.sabrUrl.empty(), s.userAgent.c_str());
        for (const yt::Format &f : s.formats)
            std::printf("  %-4d %-3s %-8s %-28s %-8s %s\n", f.itag, f.hasVideo && f.hasAudio ? "av" : f.hasVideo ? "v" : "a",
                        f.qualityLabel.c_str(), f.codecs.c_str(), f.audio ? f.audio->id.c_str() : "",
                        f.url.empty() ? "(no url)" : f.url.c_str());
    }
    for (const yt::CaptionTrack &c : p.captions)
        std::printf("caption %s %s%s\n", c.language.c_str(), c.name.c_str(), c.autoGenerated ? " (asr)" : "");
    std::printf("storyboard levels: %zu\n", p.storyboard.size());
    return 0;
}

int captions(yt::Client &yt, const std::string &link, const std::string &lang)
{
    const yt::Result<yt::Player> r = yt.videos().player(link, 0).get();   // metadata only
    if (!r.ok()) return fail(*r.error);
    if (r.value.captions.empty()) { std::printf("no captions\n"); return 0; }
    yt::CaptionTrack pick = r.value.captions.front();
    for (const yt::CaptionTrack &c : r.value.captions) if (c.language == lang) { pick = c; break; }
    const yt::Result<std::vector<yt::Cue>> cues = yt.videos().captions(pick).get();
    if (!cues.ok()) return fail(*cues.error);
    for (size_t i = 0; i < cues.value.size() && i < 8; ++i)
        std::printf("%7.2f %s\n", cues.value[i].startMs / 1000.0, cues.value[i].text.c_str());
    return 0;
}

int signin(yt::Client &yt)
{
    std::promise<yt::Result<yt::Done>> done;
    yt.auth().signIn(
        [](const yt::DeviceCode &dc) {
            std::printf("Open %s and enter the code %s\n", dc.verificationUrl.c_str(), dc.userCode.c_str());
            if (!dc.qrPng.empty()) {
                std::ofstream("qr.png", std::ios::binary) << dc.qrPng;
                std::printf("or scan ./qr.png\n");
            }
            std::fflush(stdout);
        },
        [&](const yt::Result<yt::Done> &r) { done.set_value(r); });
    const yt::Result<yt::Done> r = done.get_future().get();
    if (!r.ok()) return fail(*r.error);
    std::printf("signed in; state saved to %s\n", kStateFile);
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    const std::string cmd = argc > 1 ? argv[1] : "";
    const std::string arg = argc > 2 ? argv[2] : "";
    if (cmd.empty() || (cmd != "signin" && arg.empty())) {
        std::fprintf(stderr, "usage: %s feed home|subscriptions|history|<topic id> | search <query> [pages] |\n"
                             "       streams <link> | captions <link> [lang] | channel <link> | comments <link> | signin\n",
                     argv[0]);
        return 2;
    }

    yt::ClientOptions o;
    o.initial = load();
    o.onStateChanged = save;                         // runs on the engine thread
    yt::Client yt(o);

    if (cmd == "feed") {
        auto page = arg == "home" ? yt.feeds().home()
                  : arg == "subscriptions" ? yt.feeds().subscriptions()
                  : arg == "history" ? yt.feeds().history()
                  : yt.feeds().topic(arg);
        return printVideos(page.get());
    }
    if (cmd == "search") {
        const int pages = argc > 3 ? std::atoi(argv[3]) : 1;
        yt::Result<yt::Page<yt::Video>> page = yt.search().videos(arg).get();
        for (int i = 1;; ++i) {
            if (const int rc = printVideos(page)) return rc;
            if (i >= pages || !page.value.next) return 0;
            std::printf("--- page %d\n", i + 1);
            page = yt.more(page.value.next).get();
        }
    }
    if (cmd == "streams") return streams(yt, arg);
    if (cmd == "captions") return captions(yt, arg, argc > 3 ? argv[3] : "");
    if (cmd == "channel") {
        auto header = yt.channels().get(arg);        // both requests in flight at once
        auto videos = yt.channels().videos(arg);
        const yt::Result<yt::Channel> c = header.get();
        if (!c.ok()) return fail(*c.error);
        std::printf("%s (%s) %s · %s\n", c.value.name.c_str(), c.value.id.c_str(), c.value.handle.c_str(),
                    c.value.subscriberCount.c_str());
        return printVideos(videos.get());
    }
    if (cmd == "comments") {
        const yt::Result<yt::Page<yt::Comment>> r = yt.comments().list(arg).get();
        if (!r.ok()) return fail(*r.error);
        if (r.value.disabled) { std::printf("comments are turned off\n"); return 0; }
        for (const yt::Comment &c : r.value.items)
            std::printf("[%s] %s\n", c.author.c_str(), c.body.substr(0, c.body.find('\n')).c_str());
        return 0;
    }
    if (cmd == "signin") return signin(yt);
    std::fprintf(stderr, "unknown command: %s\n", cmd.c_str());
    return 2;
}
```

Replace its header comment with:

```cpp
// yt-cli — the blocking form: every call is `.get()` on the std::future it returns; no
// event loop, no executor. The saved state (visitorData, refresh token, player build)
// is loaded on every run, so personal feeds work after `signin`.
//
//   yt-cli feed home|subscriptions|history|<topic id>
//   yt-cli search <query> [pages]
//   yt-cli streams <link>          e.g. https://youtu.be/dQw4w9WgXcQ
//   yt-cli captions <link> [lang]
//   yt-cli channel <link>          e.g. @NASA
//   yt-cli comments <link>
//   yt-cli signin                  device-code login; state saved to ./yt-cli-state.json
```

and the includes: drop `<condition_variable>`, `<deque>`, `<mutex>`; add `<cstdlib>` and `<future>` (keep `<yt/client.h>` → change to `<yt/yt.h>`).

- [ ] **Step 4: README** — replace the code block after "Your code only needs C++17 …" with the blocking quickstart, then the callback form:

````markdown
```cpp
#include <yt/yt.h>
#include <cstdio>

int main(int argc, char **argv)
{
    yt::Client yt;                                       // its own engine thread
    yt::Result<yt::Player> r = yt.videos().player(argv[1]).get();   // any link or id
    if (!r.ok()) { std::printf("error: %s\n", r.error->message.c_str()); return 1; }
    std::printf("%s: %zu sources\n", r.value.details.title.c_str(), r.value.sources.size());

    auto page = yt.search().videos("rick astley").get();  // every list is a yt::Page
    if (page.ok() && page.value.next) page = yt.more(page.value.next).get();   // the second page
}
```

In a GUI, use the callback form instead: it returns a `yt::Job` you can cancel, and the
callback runs where `ClientOptions::deliver` sends it:

```cpp
job = yt.videos().get(link, [this](const yt::Result<yt::WatchResult> &r) { show(r); });
```
````

In the examples table, change the `yt-cli` row to "the blocking form (`std::future`), sign-in, persisted state"; `yt-watch` → "callbacks with `runOnce()`, three requests in flight, comment paging"; `yt-channel` → "one link, two requests". In "Threading and cancellation" add a bullet after the first: "Every operation also has a form without the callback that returns a `std::future`. It is fulfilled straight from the engine thread (not through `deliver`), so blocking any thread on `get()` is safe; with `ownThread = false` someone must keep calling `runOnce()`. A Client destroyed first releases pending futures with `ErrorCode::Canceled`."

- [ ] **Step 5: AGENTS.md** — §3 layout: under `client.cpp` add `  groups.cpp       the resource groups of yt::Client (thin wrappers over core/chains)` and `  link.cpp         yt::Link — link and id recognition`. §7 recipe "New Client operation" becomes:

```markdown
**New Client operation**
1. Chain in `src/core/chains.cpp` (+ declaration with its contract in `chains.h`):
   build the body in `innertube/bodies.*`, post via `IHttp`, parse via `parsers/`,
   map `!r.ok` with `errorOf(r)`. A list delivers `Page<T>` through `pageOf` (its cursor
   names the endpoint and client); a link argument is turned into an id with
   `videoIdOf` / `playlistIdOf` / `fetchChannelId`, a wrong kind fails with `badLink`.
2. Both forms on its group in `include/yt/client.h` and `src/groups.cpp`: the callback
   form over `run<T>(m_e, chain, done)`, the future form as the `detail::futureOf`
   one-liner — the same shape as their neighbours.
3. Tests: chain behaviour in `tst_chains.cpp` (FakeHttp: sent body, client routing,
   parsed result, error mapping); add the call to "every group method reaches its chain"
   in `tst_client.cpp`.
```

- [ ] **Step 6: Spec statuses** — `docs/specs/2026-10-15-api-scenarios.md`: status line → `**Status:** agreed (2026-10-15); R2–R5 implemented by [../plans/2026-10-15-api-groups.md](../plans/2026-10-15-api-groups.md) — see its deviations table.` `docs/specs/2026-10-15-api-usability-research.md`: status line → `**Status:** research (2026-10-15). R1 done (scenarios); R2–R5 implemented (plan 2026-10-15-api-groups).`

- [ ] **Step 7: All four trees and the dead proxy**

```sh
for t in build build-asan build-tsan build-none; do
  echo "== $t"; cmake -S . -B $t >/dev/null; cmake --build $t -j 2>&1 | grep -E "warning|error"; (cd $t && ctest -j8 --output-on-failure) | tail -3
done
http_proxy=http://127.0.0.1:9 https_proxy=http://127.0.0.1:9 ./build/tests/tst_conformance_curl
http_proxy=http://127.0.0.1:9 ./build/tests/tst_client
```

Expected: no warnings; `100% tests passed` in every tree; both proxy runs pass.

- [ ] **Step 8: Live check** (network; not a ctest — run from a scratch directory so the state file stays out of the tree)

```sh
Y=$PWD/build/examples/yt-cli
$Y streams "https://youtu.be/dQw4w9WgXcQ?si=x"       # VISIONOS + ANDROID_VR sources (link with noise)
$Y search "rick astley" 2                             # two pages: search continuation works
$Y channel @NASA                                      # handle resolved; Videos tab params (kChannelVideosTab) return videos
$Y comments dQw4w9WgXcQ                               # first comments page
$Y feed UC-9-kyTW8ZkZNDHQJ6FgpwQ                      # a topic feed
$Y captions dQw4w9WgXcQ en
```

Expected: each prints results. If `channel @NASA` returns no videos, the Videos tab params no longer match — record it and fall back to `spec.params = ""` in `fetchChannelVideos` (the channel's Home tab, today's behaviour), with a comment naming the live result. Signed-in paths (`subscriptions`, `rate`, `save`/`unsave`, `comments().post`) are verified on FakeHttp only unless the owner signs in; report them as such (`ACTION_REMOVE_VIDEO_BY_VIDEO_ID` in particular is not live-verified).

- [ ] **Step 9: Commit**

```bash
git add include/yt/yt.h examples/yt-cli/main.cpp README.md AGENTS.md docs/specs/2026-10-15-api-scenarios.md docs/specs/2026-10-15-api-usability-research.md tests/tst_types.cpp
git commit -m "docs: groups API — umbrella header, blocking yt-cli, README quickstart, AGENTS recipe" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CcpAWezZwfUf6YSVnLXjYq"
```
