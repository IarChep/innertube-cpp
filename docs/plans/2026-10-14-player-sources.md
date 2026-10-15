# Player sources Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `Client::player()` returns every source YouTube serves (progressive, adaptive, HLS, SABR — each tagged with the client and User-Agent that obtained it), a precise playability verdict, captions fetchable as cues, storyboards and videostats URLs; sign-in also delivers YouTube's QR image; a revoked refresh token signs the client out.

**Architecture:** One `/player` "walk" in `core/chains.cpp` asks clients in a fixed order (WEB seed → VISIONOS → ANDROID_VR → TVHTML5 → IOS) until every wanted kind is covered; each answer is parsed by one partial Glaze read in `parsers/playerparser.cpp`. Raw GETs (the `/tv` page, `base.js`, QR images, json3) go through a new `IHttp::fetch` that skips the JSON envelope; `signatureTimestamp`/`playerId` persist in `SessionState` like `visitorData`.

**Tech Stack:** C++23 implementation / C++17 public headers, Glaze (private), doctest, CMake + Ninja, libcurl backend.

**Spec:** [`docs/specs/2026-10-14-player-sources-design.md`](../specs/2026-10-14-player-sources-design.md) — read it first; this plan argues from it.

## Global Constraints

- Public headers (`include/yt/`) compile as C++17: no `std::expected`, `<format>`, ranges, designated initializers. `tst_types` builds at C++17 to prove it.
- No Qt anywhere (`innertube_no_qt` ctest). No new third-party dependency. Glaze never in a public header.
- Layering (never reverse): `parsers → json`, `core/chains → parsers, innertube, json`, `net → json, innertube`, `client/auth → core, net`.
- Every public method only posts work; a callback runs exactly once or never and never from inside the call that started it; multi-step chains check `live(job)` between steps and stop silently when canceled.
- URLs are byte-exact: never decode or re-encode a server-issued URL. Only `http`/`https`. Credentials never reach a log.
- Parsers never fail: malformed JSON degrades to "fields absent". Recursive scans are bounded by `gj::kScanMaxDepth`.
- No exceptions in the library except `Client`'s constructor. Asynchronous outcomes are `Result<T>`; keep `httpStatus`.
- Style: 4 spaces, `&`/`*` bind to the name, function-definition braces on their own line, `m_` members, `k` constants, C-style casts with a space (`(int) x`), `nullptr`, header guards `YT_<AREA>_<NAME>_H`, every file starts with a header comment, no `TODO`/`FIXME`/commented-out code, comments explain *why*.
- Done = warning-free build and all ctests green in `build`, `build-asan`, `build-tsan`, `build-none`; transport suites green with `http_proxy=http://127.0.0.1:9`; every fix has a test that fails without it.
- Commit only when the repository owner has said so for this plan (AGENTS.md §10). Commit steps below show the message to use; skip the `git commit` if commits were not authorized and leave the tree for review. Trailer: `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **Localized reasons**: with `hl=ru` the server says «Войдите, чтобы подтвердить, что вы не бот»; classification must not depend on `reason` text — a Russian `LOGIN_REQUIRED` is still `BotCheck` (test in Task 5).
2. **A broken QR answer**: a `data:` URL whose payload is not base64 or not a PNG must leave `qrPng` empty, not hand garbage to an image decoder (test in Task 9).
3. **A signed-in user whose needs VISIONOS already covers** must not pay for the TV request nor the 3 MB `base.js` download (test in Task 7).
4. **Caption URLs in odd shapes**: an existing `fmt=` value, `fmt` as the first parameter, a URL without `?` — `fmt=json3` replaced/appended, everything else byte-exact (test in Task 8).
5. **Two walks needing `sts` at once** share one `base.js` download (coalescing test on `HttpClient::fetch` in Task 4).

---

## File map

| File | Task | Responsibility |
|---|---|---|
| `src/json/ytjson.h` | 1 | `gj::base64Decode` |
| `include/yt/player.h` (new) | 2 | public player types, `storyboardFrame` |
| `src/player.cpp` (new) | 2 | `storyboardFrame` |
| `include/yt/catalog.h`, `src/innertube/clientconfig.cpp`, `src/innertube/contextbuilder.cpp`, `src/net/httpclient.h` | 3 | VISIONOS identity |
| `include/yt/session.h`, `src/core/session.h`, `src/core/ihttp.h`, `src/net/httpclient.*`, `src/client.cpp` (setup), `tests/fakehttp.h` | 4 | `fetch`, `resetVisitorData`, `setPlayerJs`, persistence |
| `src/parsers/playerparser.*`, `tests/fixtures/*` | 5 | `parsePlayerResponse`, `parseStoryboardSpec`, `parseJson3` |
| `src/innertube/playerjs.*` (new), `src/innertube/catalog.h` | 6 | `playerIdIn`, `signatureTimestampIn`, URLs |
| `src/innertube/bodies.*`, `src/core/chains.*`, `include/yt/client.h`, `include/yt/types.h`, `src/client.cpp`, `examples/yt-cli/main.cpp` | 7 | the walk; old player API removed |
| `src/core/chains.*`, `include/yt/client.h`, `src/client.cpp`, `examples/yt-cli/main.cpp` | 8 | `Client::captions` |
| `include/yt/auth.h`, `src/innertube/bodies.*`, `src/innertube/catalog.h`, `src/core/chains.*`, `src/auth.cpp`, `examples/yt-cli/main.cpp` | 9 | QR sign-in |
| `src/engine.h`, `src/auth.cpp`, `src/client.cpp` | 10 | `invalid_grant` |
| `AGENTS.md`, spec status | 11 | docs, full verification, live check |

Build/test commands used throughout (from the repo root):

```sh
cmake --build build -j 2>&1 | grep -E "warning|error" ; cmake --build build -j
(cd build && ctest -j8 --output-on-failure -R '<suite>')
```

---

### Task 1: base64 decoding in the JSON layer

**Files:**
- Modify: `src/json/ytjson.h` (append a section before `} // namespace gj`)
- Test: `tests/tst_parsers.cpp` (append)

**Interfaces:**
- Produces: `bool yt::gj::base64Decode(std::string_view in, std::string *out);` — accepts `+/` and `-_`, padding optional; on any other byte or a dangling sextet returns false with `*out` cleared.

- [ ] **Step 1: Write the failing test** — append to `tests/tst_parsers.cpp`:

```cpp
TEST_CASE("base64Decode") {
    std::string o;
    CHECK(gj::base64Decode("aGVsbG8=", &o));
    CHECK(o == "hello");
    CHECK(gj::base64Decode("aGVsbG8", &o));          // padding is optional (xtags omit it)
    CHECK(o == "hello");
    CHECK(gj::base64Decode("-_8", &o));              // base64url alphabet
    CHECK(o == "\xfb\xff");
    CHECK(gj::base64Decode("+/8=", &o));             // classic alphabet, same bytes
    CHECK(o == "\xfb\xff");
    CHECK(gj::base64Decode("", &o));
    CHECK(o.empty());
    CHECK_FALSE(gj::base64Decode("aGV*bG8=", &o));   // foreign byte
    CHECK(o.empty());
    CHECK_FALSE(gj::base64Decode("aGVsb", &o));      // one stray sextet carries no byte
    CHECK(o.empty());
}
```

`tst_parsers.cpp` already does `using namespace yt;`; check its top lines and add `#include "json/ytjson.h"` if it is not included yet.

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build build -j`
Expected: compile error `'base64Decode' is not a member of 'yt::gj'`.

- [ ] **Step 3: Implement** — append inside `namespace gj` in `src/json/ytjson.h`, after `contentOf`:

```cpp
// ---- base64 (xtags, data: URLs) ---------------------------------------------
// YouTube fields come in both alphabets and usually without padding (xtags,
// videoPlaybackUstreamerConfig are base64url; a QR data: URL is classic). Here, in
// the bottom layer, so the parsers and the chains share one decoder.
inline bool base64Decode(std::string_view in, std::string *out)
{
    out->clear();
    size_t n = in.size();
    while (n > 0 && in[n - 1] == '=') --n;
    if (n % 4 == 1) return false;                    // a lone sextet carries no byte
    out->reserve(n * 3 / 4);
    std::uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; ++i) {
        const char c = in[i];
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+' || c == '-') v = 62;
        else if (c == '/' || c == '_') v = 63;
        else { out->clear(); return false; }
        acc = (acc << 6) | (std::uint32_t) v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out->push_back((char) ((acc >> bits) & 0xff));
        }
    }
    return true;
}
```

Also extend the file's header comment: after "flexible numbers)" add ", base64".

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build -j && (cd build && ctest --output-on-failure -R tst_parsers)`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/json/ytjson.h tests/tst_parsers.cpp
git commit -m "lib: base64 decoding in the JSON layer" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: public player types and `storyboardFrame`

**Files:**
- Create: `include/yt/player.h`
- Create: `src/player.cpp`
- Modify: `CMakeLists.txt` (library source list: add `src/player.cpp` after `src/catalog.cpp`)
- Test: `tests/tst_types.cpp`

**Interfaces:**
- Produces (namespace `yt`, exactly as below): `ByteRange`, `AudioTrack`, `Format`, `Source`, `Playability`, `PlayabilityStatus`, `VideoDetails`, `CaptionTrack`, `StoryboardLevel`, `Player`, `Want` (`WantProgressive=1, WantAdaptive=2, WantHls=4, WantSabr=8, WantAll=15`), `PlayerRequest`, `Cue`, `StoryboardFrame`, `StoryboardFrame storyboardFrame(const StoryboardLevel &, int64_t ms)`.

- [ ] **Step 1: Write the failing test** — append to `tests/tst_types.cpp` (and add `#include <yt/player.h>` under the existing includes):

```cpp
TEST_CASE("Player defaults") {
    yt::Player p;
    CHECK(p.status.kind == yt::Playability::Unknown);
    CHECK(p.sources.empty());
    CHECK(yt::PlayerRequest{}.want == yt::WantAll);
    yt::Format f;
    CHECK_FALSE(f.initRange.has_value());
    CHECK_FALSE(f.audio.has_value());
}
TEST_CASE("storyboardFrame") {
    yt::StoryboardLevel l;
    l.urlTemplate = "https://i.ytimg.com/sb/v/storyboard3_L1/M$M.jpg?sqp=S&sigh=X";
    l.width = 80; l.height = 45; l.count = 108; l.cols = 10; l.rows = 10; l.intervalMs = 2000;
    yt::StoryboardFrame f = yt::storyboardFrame(l, 0);
    CHECK(f.url == "https://i.ytimg.com/sb/v/storyboard3_L1/M0.jpg?sqp=S&sigh=X");
    CHECK(f.x == 0);
    CHECK(f.y == 0);
    CHECK(f.width == 80);
    CHECK(f.height == 45);
    f = yt::storyboardFrame(l, 23 * 2000 + 1999);       // cell 23 on sheet 0
    CHECK(f.x == 240);
    CHECK(f.y == 90);
    f = yt::storyboardFrame(l, 100 * 2000);             // first cell of sheet 1
    CHECK(f.url == "https://i.ytimg.com/sb/v/storyboard3_L1/M1.jpg?sqp=S&sigh=X");
    CHECK(f.x == 0);
    CHECK(f.y == 0);
    f = yt::storyboardFrame(l, 10000000);               // past the end: the last frame
    CHECK(f.url.find("/M1.jpg") != std::string::npos);
    CHECK(f.x == 560);
    CHECK(f.y == 0);
    f = yt::storyboardFrame(l, -5);
    CHECK(f.x == 0);
    CHECK(f.url.find("/M0.jpg") != std::string::npos);
    CHECK(yt::storyboardFrame(yt::StoryboardLevel{}, 0).url.empty());   // an empty level
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build build -j`
Expected: `fatal error: yt/player.h: No such file or directory`.

- [ ] **Step 3: Create `include/yt/player.h`**

```cpp
// What /player yields: every source YouTube gave, tagged with the client that obtained
// it — the consumer chooses what to play. Plus the playability verdict, captions,
// storyboards and the videostats URLs from the same answers. C++17.
#ifndef YT_PLAYER_H
#define YT_PLAYER_H
#include <yt/catalog.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace yt {

struct ByteRange { int64_t first = 0, last = 0; };          // inclusive; maps to a Range header

// A dubbed / original / descriptive audio track (multi-language videos).
struct AudioTrack {
    std::string id, name, language;                          // "ru.3", "Russian", "ru"
    bool isDefault = false;                                  // the server's pick for this hl
    bool original = false;                                   // the language it was shot in
    bool autoDubbed = false;                                 // synthetic voice
    bool drc = false;                                        // dynamic range compressed
};

// One format of a Source. hasVideo && hasAudio = progressive (muxed).
struct Format {
    int itag = 0;
    std::string url;                       // byte-exact; empty = served only via SABR, or sealed
    bool sealed = false;                   // signatureCipher instead of url (needs decipher)
    std::string mimeType, codecs, qualityLabel;   // "video/mp4", "avc1.640028", "1080p"
    int width = 0, height = 0, fps = 0;
    int64_t bitrate = 0, averageBitrate = 0;      // bits/s
    int64_t contentLength = 0, durationMs = 0;    // 0 = not reported
    std::optional<ByteRange> initRange, indexRange;
    bool hasVideo = false, hasAudio = false;
    std::optional<AudioTrack> audio;
    int64_t lastModified = 0;              // SABR FormatId
    std::string xtags;                     // SABR FormatId (base64url, as given)
};

// One client's answer. Fetch its media with `userAgent`: the CDN checks the c= the URL
// was signed for.
struct Source {
    ClientId client = ClientId::WEB;
    std::string userAgent;
    std::vector<Format> formats;           // progressive + adaptive, server order
    std::string hlsUrl, dashUrl;
    std::string sabrUrl, sabrConfig;       // serverAbrStreamingUrl, videoPlaybackUstreamerConfig
    int64_t expiresAtSecs = 0;             // unix time the URLs stop working; 0 = unknown
};

enum class Playability {
    Ok,
    BotCheck,          // "confirm you're not a bot": a signed-in or other client may still pass
    LoginRequired,     // the account itself may not watch (private, members-only)
    AgeRestricted,
    Upcoming,          // scheduled stream or premiere; see scheduledStartSecs
    LiveOffline,       // a stream that is not live (no start time known)
    Unplayable,        // removed, private, region-blocked, …
    Unknown            // no playabilityStatus at all
};
struct PlayabilityStatus {
    Playability kind = Playability::Unknown;
    std::string status, reason;            // raw server words; reason is localized by hl
    int64_t scheduledStartSecs = 0;        // Upcoming: unix time
    std::string slateText;                 // the server's own waiting text, localized
};

struct VideoDetails {
    std::string id, title, author, channelId, description, thumbnailUrl;
    int64_t lengthSecs = 0, viewCount = 0;
    bool isLive = false, isLiveContent = false, isUpcoming = false;
};

struct CaptionTrack {
    std::string url, language, name;       // url byte-exact; fetch with Client::captions
    bool autoGenerated = false, translatable = false;
};

// One storyboard (seek preview) level: count frames on sheets of cols×rows.
struct StoryboardLevel {
    std::string urlTemplate;               // "$M" = sheet index (storyboardFrame fills it)
    int width = 0, height = 0, count = 0, cols = 0, rows = 0;
    int64_t intervalMs = 0;
};

struct Player {
    PlayabilityStatus status;              // the most informative answer of the walk
    VideoDetails details;
    std::vector<Source> sources;           // walk order; empty when nothing is playable
    std::vector<CaptionTrack> captions;
    std::vector<StoryboardLevel> storyboard;
    std::string watchtimeUrl, playbackUrl; // videostats (history reporting)
    std::string cpn;                       // the playback nonce of this view
};

// Which kinds of source the walk must look for; it stops once all are found.
// want == 0: metadata only — one answer (details, captions, storyboard), no sources hunt.
enum Want : unsigned { WantProgressive = 1, WantAdaptive = 2, WantHls = 4, WantSabr = 8, WantAll = 15 };
struct PlayerRequest { std::string videoId; unsigned want = WantAll; };

struct Cue { int64_t startMs = 0, endMs = 0; std::string text; };

struct StoryboardFrame { std::string url; int x = 0, y = 0, width = 0, height = 0; };
// Which sheet and which cell show the moment `ms` on `level`. Pure; empty url when the
// level is empty. Times before 0 show the first frame, past the end the last one.
StoryboardFrame storyboardFrame(const StoryboardLevel &level, int64_t ms);

}
#endif
```

- [ ] **Step 4: Create `src/player.cpp`**

```cpp
// Pure helpers of the public player types (no I/O).
#include <yt/player.h>
namespace yt {

StoryboardFrame storyboardFrame(const StoryboardLevel &l, int64_t ms)
{
    StoryboardFrame f;
    if (l.count <= 0 || l.cols <= 0 || l.rows <= 0) return f;
    int64_t idx = (l.intervalMs > 0 && ms > 0) ? ms / l.intervalMs : 0;
    if (idx >= l.count) idx = l.count - 1;
    const int64_t perSheet = (int64_t) l.cols * l.rows;
    const int64_t sheet = idx / perSheet, cell = idx % perSheet;
    f.width = l.width;
    f.height = l.height;
    f.x = (int) (cell % l.cols) * l.width;
    f.y = (int) (cell / l.cols) * l.height;
    f.url = l.urlTemplate;
    const size_t at = f.url.find("$M");
    if (at != std::string::npos) f.url.replace(at, 2, std::to_string(sheet));
    return f;
}

}
```

Add `src/player.cpp` to `add_library(innertube STATIC …)` in `CMakeLists.txt` right after `src/catalog.cpp`.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake -S . -B build -G Ninja >/dev/null && cmake --build build -j && (cd build && ctest --output-on-failure -R tst_types)`
Expected: PASS (the suite builds at C++17).

- [ ] **Step 6: Commit**

```bash
git add include/yt/player.h src/player.cpp CMakeLists.txt tests/tst_types.cpp
git commit -m "lib: public player types and storyboardFrame" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: the VISIONOS client identity

**Files:**
- Modify: `include/yt/catalog.h` (enum + count + comment)
- Modify: `src/innertube/clientconfig.cpp`
- Modify: `src/innertube/contextbuilder.cpp`
- Modify: `src/net/httpclient.h:68` (`kClientCount`)
- Test: `tests/tst_catalog.cpp`, `tests/tst_context.cpp`

**Interfaces:**
- Produces: `ClientId::VISIONOS` (value 6), `kClientIdCount == 7`; `defaultClientInfo(ClientId::VISIONOS) == {"VISIONOS", "1.02", <Safari macOS UA>, 101}`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/tst_catalog.cpp`:

```cpp
TEST_CASE("visionos identity") {
    const ClientInfo v = defaultClientInfo(ClientId::VISIONOS);
    CHECK(v.name == "VISIONOS");
    CHECK(v.version == "1.02");
    CHECK(v.id == 101);
    CHECK(v.userAgent.find("Version/26.0 Safari") != std::string::npos);
    CHECK(kClientIdCount == 7);
}
```

Append to `tests/tst_context.cpp`:

```cpp
// VISIONOS is the JS-less /player client whose sessions are not cut at ~60 s; it needs
// the session visitorData (else LOGIN_REQUIRED) and repeats its UA inside the context.
TEST_CASE("visionosContext") {
    Session s; s.visitorData = "VD";
    const std::string c = ContextBuilder::contextJson(ClientId::VISIONOS, s);
    CHECK(has(c, "\"clientName\":\"VISIONOS\""));
    CHECK(has(c, "\"clientVersion\":\"1.02\""));
    CHECK(has(c, "\"deviceMake\":\"Apple\""));
    CHECK(has(c, "\"deviceModel\":\"RealityDevice14,1\""));
    CHECK(has(c, "\"osName\":\"visionOS\""));
    CHECK(has(c, "\"osVersion\":\"1.0.2.21O209\""));
    CHECK(has(c, "\"visitorData\":\"VD\""));
    CHECK(has(c, "\"userAgent\":\"Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3)"));
    CHECK_FALSE(has(ContextBuilder::contextJson(ClientId::ANDROID_VR, s), "\"userAgent\""));
    const std::vector<Header> h = ContextBuilder::headers(ClientId::VISIONOS, s);
    REQUIRE(find(h, "X-YouTube-Client-Name") != nullptr);
    CHECK(find(h, "X-YouTube-Client-Name")->value == "101");
    REQUIRE(find(h, "X-Goog-Visitor-Id") != nullptr);
    CHECK(find(h, "Authorization") == nullptr);
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build build -j`
Expected: `'VISIONOS' is not a member of 'yt::ClientId'`.

- [ ] **Step 3: Implement**

`include/yt/catalog.h` — replace the enum block and its comment:

```cpp
// VISIONOS is the JS-less sweet spot for /player: ready adaptive URLs (no n, no
// PoToken), HLS and SABR, and its sessions are not cut at ~60 s like ANDROID_VR's and
// IOS's; it needs the session visitorData. ANDROID_VR adds the progressive itag 18.
enum class ClientId { WEB, WEB_SAFARI, IOS, ANDROID, ANDROID_VR, TVHTML5, VISIONOS };
constexpr int kClientIdCount = 7;
```

`src/innertube/clientconfig.cpp` — add after the `TV` entry, and the case:

```cpp
    // Apple Vision Pro (Troubadour, 2026-08; live-verified 2026-10-14): number 101.
    static const ClientInfo VISION = { "VISIONOS", "1.02",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 Safari/605.1.15", 101 };
```

```cpp
        case ClientId::VISIONOS:   return VISION;
```

`src/innertube/contextbuilder.cpp` — add the field to `cj::Client` after `visitorData`:

```cpp
    std::optional<std::string> userAgent;          // VISIONOS repeats its UA in the context
```

and the branch after the `ANDROID_VR` one in `contextJson`:

```cpp
    } else if (id == ClientId::VISIONOS) {
        ctx.client.deviceMake = "Apple"; ctx.client.deviceModel = "RealityDevice14,1";
        ctx.client.osName = "visionOS"; ctx.client.osVersion = "1.0.2.21O209";
        ctx.client.userAgent = ci.userAgent;
```

`src/net/httpclient.h` — replace `static constexpr int kClientCount = 6;                      // ClientId enum size` with:

```cpp
    static constexpr int kClientCount = kClientIdCount;
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build -j && (cd build && ctest --output-on-failure -R 'tst_catalog|tst_context|tst_http')`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/yt/catalog.h src/innertube/clientconfig.cpp src/innertube/contextbuilder.cpp src/net/httpclient.h tests/tst_catalog.cpp tests/tst_context.cpp
git commit -m "lib: VISIONOS client identity" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: raw fetch, visitorData reset, player-JS identity

**Files:**
- Modify: `src/core/session.h`, `include/yt/session.h`
- Modify: `src/core/ihttp.h`
- Modify: `src/net/httpclient.h`, `src/net/httpclient.cpp`
- Modify: `src/client.cpp` (`Engine::setup`)
- Modify: `tests/fakehttp.h`
- Test: `tests/tst_http.cpp`, `tests/tst_fakehttp.cpp`

**Interfaces:**
- Produces on `yt::core::IHttp`:
  - `virtual void fetch(std::string url, std::vector<Header> headers, const JobToken &, HttpFn) = 0;`
  - `virtual void resetVisitorData() = 0;`
  - `virtual void setPlayerJs(std::string playerId, int signatureTimestamp) = 0;`
- `Session::playerId` (`std::string`), `Session::signatureTimestamp` (`int`, 0 = unknown); same two fields on `SessionState`.
- `HttpClient::setPlayerJsSink(std::function<void(const std::string &, int)>)`.
- `FakeHttp`: `queue(key, body, status = 200, visitor = "")` (a non-empty `visitor` is adopted into `session().visitorData` on delivery when it is empty — as `HttpClient::complete` does); `sentClients` (`std::vector<int>`, every post in order); `fetched` (`std::vector<FakeHttp::Fetched>` with `url`, `headers`); `visitorResets`, `playerJsSets` (`int`). `fetch()` replies from `queue(url, …)`.

- [ ] **Step 1: Write the failing tests** — append to `tests/tst_http.cpp`:

```cpp
// The /tv page, base.js and QR images are not JSON: the envelope check must not refuse
// them, but an error status must still fail instead of reaching a byte scanner.
TEST_CASE("fetchDeliversRawBodies") {
    Fx f; f.ft.script("https://yt.test/tv", ok("<html>/s/player/abc/</html>"));
    Got g;
    f.h().fetch("https://yt.test/tv", {{"User-Agent", "TV-UA"}}, yt::core::newJob(), rec(g));
    f.pump();
    CHECK(g.calls == 1);
    CHECK(g.r.ok);
    CHECK(*g.r.body == "<html>/s/player/abc/</html>");
    REQUIRE(f.ft.started.size() == 1);
    CHECK(f.ft.started[0].method == yt::HttpRequest::Method::Get);
    REQUIRE(header(f.ft.started[0], "User-Agent") != nullptr);
    CHECK(header(f.ft.started[0], "User-Agent")->value == "TV-UA");
}
TEST_CASE("fetchNon2xxFails") {
    Fx f; f.ft.script("https://yt.test/api/timedtext", ok("<html>Sorry</html>", 429));
    Got g;
    f.h().fetch("https://yt.test/api/timedtext?v=1&fmt=json3", {}, yt::core::newJob(), rec(g));
    f.pump();
    CHECK_FALSE(g.r.ok);
    CHECK(g.r.code == ErrorCode::Api);
    CHECK(g.r.httpStatus == 429);
}
TEST_CASE("fetchCoalescesIdenticalUrls") {
    Fx f; f.ft.script("https://yt.test/s/player/p1/base.js", ok("signatureTimestamp:1"));
    Got a, b;
    f.h().fetch("https://yt.test/s/player/p1/base.js", {}, yt::core::newJob(), rec(a));
    f.h().fetch("https://yt.test/s/player/p1/base.js", {}, yt::core::newJob(), rec(b));
    f.pump();
    CHECK(f.ft.started.size() == 1);                 // two walks, one 3 MB download
    CHECK(a.r.ok);
    CHECK(b.r.ok);
}
TEST_CASE("resetVisitorDataRebuildsTheContext") {
    Fx f;
    f.ft.script(kBrowse, ok("{\"responseContext\":{\"visitorData\":\"OLD\"}}"));
    f.ft.script(kBrowse, ok("{}"));
    std::vector<std::string> sunk;
    f.h().setVisitorSink([&](const std::string &v) { sunk.push_back(v); });
    Got a, b;
    f.h().post("browse", ClientId::WEB, "{\"browseId\":\"A\"}", yt::core::newJob(), rec(a));
    f.pump();
    REQUIRE(f.h().session().visitorData == "OLD");
    f.h().resetVisitorData();
    CHECK(f.h().session().visitorData.empty());
    f.h().post("browse", ClientId::WEB, "{\"browseId\":\"B\"}", yt::core::newJob(), rec(b));
    f.pump();
    REQUIRE(f.ft.started.size() == 2);
    CHECK(f.ft.started[1].body.find("visitorData") == std::string::npos);
    CHECK(header(f.ft.started[1], "X-Goog-Visitor-Id") == nullptr);
    CHECK(sunk == std::vector<std::string>{"OLD", ""});
}
TEST_CASE("setPlayerJsReachesTheSink") {
    Fx f;
    std::string id; int sts = 0;
    f.h().setPlayerJsSink([&](const std::string &i, int s) { id = i; sts = s; });
    f.h().setPlayerJs("22cb88bc", 20737);
    CHECK(f.h().session().playerId == "22cb88bc");
    CHECK(f.h().session().signatureTimestamp == 20737);
    CHECK(id == "22cb88bc");
    CHECK(sts == 20737);
}
```

Append to `tests/tst_fakehttp.cpp` (check its includes/`using` lines first and match them):

```cpp
TEST_CASE("fake adopts a queued visitorData on delivery") {
    FakeHttp t;
    t.queue("player", "{}", 200, "VD1");
    t.queue("player", "{}", 200, "VD2");
    int calls = 0;
    t.post("player", yt::ClientId::WEB, "{}", yt::core::newJob(), [&](const yt::core::Reply &) { ++calls; });
    t.post("player", yt::ClientId::VISIONOS, "{}", yt::core::newJob(), [&](const yt::core::Reply &) { ++calls; });
    t.flush();
    CHECK(calls == 2);
    CHECK(t.session().visitorData == "VD1");          // only an empty identity is seeded
    CHECK(t.sentClients == std::vector<int>{(int) yt::ClientId::WEB, (int) yt::ClientId::VISIONOS});
}
TEST_CASE("fake fetch replies from the url queue") {
    FakeHttp t;
    t.queue("https://www.youtube.com/tv", "<html></html>");
    yt::core::Reply got;
    t.fetch("https://www.youtube.com/tv", {{"User-Agent", "UA"}}, yt::core::newJob(), [&](const yt::core::Reply &r) { got = r; });
    t.flush();
    CHECK(got.ok);
    REQUIRE(t.fetched.size() == 1);
    CHECK(t.fetched[0].headers[0].value == "UA");
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build build -j`
Expected: compile errors (`fetch`, `resetVisitorData`, `setPlayerJsSink` are not members; FakeHttp `queue` takes 3 arguments).

- [ ] **Step 3: Session fields** — `src/core/session.h`, inside `struct Session` after `bearer`:

```cpp
    std::string playerId;            // the base.js build the /tv page names
    int signatureTimestamp = 0;      // that build's sts (TV /player needs it); 0 = unknown
```

`include/yt/session.h`, inside `struct SessionState` after `refreshToken`:

```cpp
    std::string playerId;                 // YouTube's player build (rotates every few weeks)
    int signatureTimestamp = 0;           // read from that build; spares a 3 MB download per run
```

- [ ] **Step 4: The seam** — `src/core/ihttp.h`: add `#include "innertube/contextbuilder.h"` after `"core/session.h"` and `#include <vector>` to the std includes; inside `class IHttp`, after `get`:

```cpp
    // Raw GET for non-JSON resources (the /tv page, base.js, a QR image, json3): no
    // envelope check, but a non-2xx answer still fails. Identical (url, headers) requests
    // in flight are coalesced. `headers` must not carry credentials.
    virtual void fetch(std::string url, std::vector<Header> headers, const JobToken &, HttpFn) = 0;
    // Drops the visitorData a bot check flagged; the next youtubei reply seeds a fresh one.
    virtual void resetVisitorData() = 0;
    // Adopts the base.js identity (persisted by the engine like visitorData).
    virtual void setPlayerJs(std::string playerId, int signatureTimestamp) = 0;
```

- [ ] **Step 5: HttpClient** — `src/net/httpclient.h`: after `setVisitorSink` add

```cpp
    // Called on the engine thread when a new base.js identity is adopted.
    void setPlayerJsSink(std::function<void(const std::string &, int)> sink) { m_playerJsSink = std::move(sink); }
```

add the overrides after `get(...)`:

```cpp
    void fetch(std::string url, std::vector<Header> headers, const core::JobToken &, core::HttpFn) override;
    void resetVisitorData() override;
    void setPlayerJs(std::string playerId, int signatureTimestamp) override;
```

and the member after `m_visitorSink`:

```cpp
    std::function<void(const std::string &, int)> m_playerJsSink;
```

`src/net/httpclient.cpp`: in `struct HttpClient::Exchange` add after `wantVisitor`:

```cpp
    bool raw = false;                      // fetch(): no envelope check
```

After `HttpClient::get` add:

```cpp
void HttpClient::fetch(std::string url, std::vector<Header> headers, const core::JobToken &job, core::HttpFn done)
{
    if (schemeOf(url).empty()) { failAsync(job, std::move(done), "unsupported URL scheme"); return; }
    std::string key = "GET|" + url;
    for (const Header &h : headers) key.append(1, '\n').append(h.name).append(1, ':').append(h.value);
    // Coalesce: two walks needing base.js share one ~3 MB download.
    auto inflight = m_inflightByKey.find(key);
    if (inflight != m_inflightByKey.end()) {
        if (std::shared_ptr<Exchange> ex = inflight->second.lock()) {
            if (!ex->done) { ex->waiters.push_back({job, std::move(done), deadline()}); return; }
        }
    }
    HttpRequest req;
    req.url = std::move(url);                        // byte-exact: never re-encoded
    for (Header &h : headers) req.headers.push_back({std::move(h.name), std::move(h.value)});
    std::shared_ptr<Exchange> ex = newExchange(std::move(req), job, std::move(done));
    ex->raw = true;
    m_inflightByKey[key] = ex;
    ex->coalesceKey = std::move(key);
    launch(ex);
}

void HttpClient::resetVisitorData()
{
    m_session.visitorData.clear();
    for (int i = 0; i < kClientCount; ++i) { m_ctxValid[i] = false; m_hdrValid[i] = false; }
    if (m_visitorSink) m_visitorSink(m_session.visitorData);
}

void HttpClient::setPlayerJs(std::string playerId, int signatureTimestamp)
{
    m_session.playerId = std::move(playerId);
    m_session.signatureTimestamp = signatureTimestamp;
    if (m_playerJsSink) m_playerJsSink(m_session.playerId, m_session.signatureTimestamp);
}
```

In `HttpClient::onHopDone`, directly before `envelope(r, ex->wantVisitor);`:

```cpp
    if (ex->raw) {
        // HTML, JavaScript and images skip the JSON envelope; an error status still fails
        // rather than handing an error page to a byte scanner.
        r.ok = hop->status >= 200 && hop->status < 300;
        if (!r.ok) { r.code = ErrorCode::Api; r.error = "HTTP " + std::to_string(hop->status); }
        complete(ex, std::move(r));
        return;
    }
```

Also update the header comment of `httpclient.h`: after "visitorData capture," add " raw (non-JSON) fetches,".

- [ ] **Step 6: Engine wiring** — `src/client.cpp`, `Engine::setup`, after the `setVisitorSink` line:

```cpp
        hc->setPlayerJsSink([this](const std::string &id, int sts) {
            state.playerId = id;
            state.signatureTimestamp = sts;
            notifyState();
        });
```

and after `s.visitorData = state.visitorData;`:

```cpp
    s.playerId = state.playerId;
    s.signatureTimestamp = state.signatureTimestamp;
```

- [ ] **Step 7: FakeHttp** — in `tests/fakehttp.h`:

Replace `queue` with:

```cpp
    // `visitor`: the visitorData the reply carries; adopted on delivery when the session
    // has none (what HttpClient::complete does with responseContext.visitorData).
    void queue(const std::string &key, std::string body, long status = 200, std::string visitor = "")
    {
        m_q[key].push_back({std::move(body), status, std::move(visitor)});
    }
```

Add public members after `sentForm`:

```cpp
    std::vector<int> sentClients;                                    // ClientId of every post, in order
    struct Fetched { std::string url; std::vector<yt::Header> headers; };
    std::vector<Fetched> fetched;                                    // every fetch(), in order
    int visitorResets = 0, playerJsSets = 0;
```

In `post`, after `m_lastClient[...] = ...;` add `sentClients.push_back((int) client);`.

Add overrides after `get`:

```cpp
    void fetch(std::string url, std::vector<yt::Header> headers, const yt::core::JobToken &job,
               yt::core::HttpFn done) override
    {
        fetched.push_back({url, std::move(headers)});
        enqueue(url, job, std::move(done));
    }
    void resetVisitorData() override { m_session.visitorData.clear(); ++visitorResets; }
    void setPlayerJs(std::string id, int sts) override
    {
        m_session.playerId = std::move(id);
        m_session.signatureTimestamp = sts;
        ++playerJsSets;
    }
```

Replace the bodies of `flush` and `flushOne` so both go through one delivery helper:

```cpp
    void flush()
    {
        while (!m_pending.empty()) {
            std::vector<Waiter> batch;
            batch.swap(m_pending);
            for (Waiter &w : batch) deliver(w);
        }
    }
    // Delivers only the i-th pending reply: lets a test reorder replies in flight.
    void flushOne(size_t i)
    {
        Waiter w = std::move(m_pending.at(i));
        m_pending.erase(m_pending.begin() + (long) i);
        deliver(w);
    }
```

In the private section: extend `struct Queued { std::string body; long status; std::string visitor; };`, add

```cpp
    void deliver(Waiter &w)
    {
        if (!yt::core::live(w.job)) return;
        if (w.reply.ok && m_session.visitorData.empty() && !w.reply.visitorData.empty())
            m_session.visitorData = w.reply.visitorData;
        w.fn(w.reply);
    }
```

and in `enqueue`, after `r.httpStatus = q.status;` add `r.visitorData = q.visitor;`. Update the file's header comment: "Queued replies are raw JSON" → "Queued replies are raw bodies (JSON, or HTML/JS for fetch())".

- [ ] **Step 8: Run the tests to verify they pass**

Run: `cmake --build build -j && (cd build && ctest -j8 --output-on-failure)`
Expected: all suites PASS (existing FakeHttp users compile unchanged: `queue` keeps its first three parameters).

- [ ] **Step 9: Commit**

```bash
git add src/core/session.h include/yt/session.h src/core/ihttp.h src/net/httpclient.h src/net/httpclient.cpp src/client.cpp tests/fakehttp.h tests/tst_http.cpp tests/tst_fakehttp.cpp
git commit -m "lib: raw fetch, visitorData reset and player-JS identity on the IHttp seam" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: the full `/player` parser, storyboard spec and json3

**Files:**
- Modify: `src/parsers/playerparser.h`, `src/parsers/playerparser.cpp` (new code is **added**; the old entry points stay until Task 7)
- Create fixtures: `tests/fixtures/player_visionos_ok.json`, `player_visionos_dubbed.json`, `player_vr_ok.json`, `player_botcheck.json`, `player_removed.json`, `captions_manual.json3`, `captions_asr.json3`
- Test: `tests/tst_parsers.cpp`

**Interfaces:**
- Consumes: `gj::base64Decode` (Task 1), public types (Task 2).
- Produces in `playerparser.h`:

```cpp
struct PlayerPart {
    PlayabilityStatus status;
    bool final = false;                   // ERROR / LIVE_STREAM_OFFLINE: no client will have streams
    VideoDetails details;
    std::vector<Format> formats;          // formats[] (muxed) then adaptiveFormats[]
    std::string hlsUrl, dashUrl, sabrUrl, sabrConfig;
    int64_t expiresInSecs = 0;
    std::vector<CaptionTrack> captions;
    std::string storyboardSpec;
    std::string watchtimeUrl, playbackUrl;
};
PlayerPart parsePlayerResponse(const std::string &body);
std::vector<StoryboardLevel> parseStoryboardSpec(std::string_view spec, int64_t lengthSecs);
std::vector<Cue> parseJson3(const std::string &body);
```

The fixtures are real answers captured on 2026-10-14 (VISIONOS / ANDROID_VR, `hl=en gl=US`) and trimmed: stream/stat URLs replaced by `*.example` hosts, `ip=`, `ei=`, visitorData and tokens scrubbed, formats reduced, captions/storyboard tokens shortened.

- [ ] **Step 1: Create the fixtures** (each file is one line of JSON, exactly as given)

`tests/fixtures/player_visionos_ok.json`:

```json
{"responseContext": {"visitorData": "VISITOR_VISIONOS"}, "playabilityStatus": {"status": "OK", "playableInEmbed": true}, "streamingData": {"expiresInSeconds": "21540", "adaptiveFormats": [{"itag": 137, "mimeType": "video/mp4; codecs=\"avc1.640028\"", "bitrate": 4334157, "width": 1920, "height": 1080, "initRange": {"start": "0", "end": "741"}, "indexRange": {"start": "742", "end": "1229"}, "lastModified": "1766957926174250", "contentLength": "80911999", "fps": 25, "qualityLabel": "1080p", "averageBitrate": 3038377, "approxDurationMs": "213040", "url": "https://rr.example/videoplayback?itag=137&c=VISIONOS"}, {"itag": 136, "mimeType": "video/mp4; codecs=\"avc1.4D401F\"", "bitrate": 1058310, "width": 1280, "height": 720, "initRange": {"start": "0", "end": "739"}, "indexRange": {"start": "740", "end": "1227"}, "lastModified": "1766958204135500", "contentLength": "26455880", "fps": 25, "qualityLabel": "720p", "averageBitrate": 993461, "approxDurationMs": "213040", "url": "https://rr.example/videoplayback?itag=136&c=VISIONOS"}, {"itag": 140, "mimeType": "audio/mp4; codecs=\"mp4a.40.2\"", "bitrate": 130677, "initRange": {"start": "0", "end": "722"}, "indexRange": {"start": "723", "end": "1018"}, "lastModified": "1766955925572207", "contentLength": "3449447", "averageBitrate": 129502, "approxDurationMs": "213089", "audioQuality": "AUDIO_QUALITY_MEDIUM", "url": "https://rr.example/videoplayback?itag=140&c=VISIONOS"}, {"itag": 251, "mimeType": "audio/webm; codecs=\"opus\"", "bitrate": 136544, "initRange": {"start": "0", "end": "258"}, "indexRange": {"start": "259", "end": "629"}, "lastModified": "1766955883819090", "contentLength": "3433755", "averageBitrate": 128930, "approxDurationMs": "213061", "audioQuality": "AUDIO_QUALITY_MEDIUM", "url": "https://rr.example/videoplayback?itag=251&c=VISIONOS"}], "hlsManifestUrl": "https://manifest.example/hls_variant/VISIONOS/index.m3u8", "serverAbrStreamingUrl": "https://rr.example/videoplayback?sabr=1&c=VISIONOS"}, "playerConfig": {"mediaCommonConfig": {"mediaUstreamerRequestConfig": {"videoPlaybackUstreamerConfig": "CgUSA2FiYw"}}}, "captions": {"playerCaptionsTracklistRenderer": {"captionTracks": [{"baseUrl": "https://www.youtube.com/api/timedtext?v=dQw4w9WgXcQ&caps=asr&xoaf=5&lang=en", "name": {"runs": [{"text": "English"}]}, "languageCode": "en", "isTranslatable": true, "trackName": ""}, {"baseUrl": "https://www.youtube.com/api/timedtext?v=dQw4w9WgXcQ&caps=asr&xoaf=5&lang=en&kind=asr", "name": {"runs": [{"text": "English (auto-generated)"}]}, "languageCode": "en", "kind": "asr", "isTranslatable": true, "trackName": ""}]}}, "videoDetails": {"videoId": "dQw4w9WgXcQ", "title": "Rick Astley - Never Gonna Give You Up (Official Video) (4K Remaster)", "lengthSeconds": "213", "channelId": "UCuAXFkgsw1L7xaCfnd5JJOw", "author": "Rick Astley", "viewCount": "1826490705", "isLiveContent": false, "shortDescription": "The official video for “Never Gonna Give You Up” by Rick Ast", "thumbnail": {"thumbnails": [{"url": "https://i.ytimg.com/vi/dQw4w9WgXcQ/hq720.jpg", "width": 1280, "height": 720}]}}, "storyboards": {"playerStoryboardSpecRenderer": {"spec": "https://i.ytimg.com/sb/dQw4w9WgXcQ/storyboard3_L$L/$N.jpg?sqp=SQP|48#27#100#10#10#0#default#rs$SIGH0|80#45#108#10#10#2000#M$M#rs$SIGH1"}}, "playbackTracking": {"videostatsPlaybackUrl": {"baseUrl": "https://s.youtube.com/api/stats/playback?cl=1&docid=dQw4w9WgXcQ&ei=EI&c=VISIONOS"}, "videostatsWatchtimeUrl": {"baseUrl": "https://s.youtube.com/api/stats/watchtime?cl=1&docid=dQw4w9WgXcQ&ei=EI&c=VISIONOS"}}}
```

`tests/fixtures/player_visionos_dubbed.json`:

```json
{"responseContext": {"visitorData": "VISITOR_VISIONOS"}, "playabilityStatus": {"status": "OK", "playableInEmbed": true}, "streamingData": {"expiresInSeconds": "21540", "adaptiveFormats": [{"itag": 137, "mimeType": "video/mp4; codecs=\"avc1.640028\"", "bitrate": 4347102, "width": 1920, "height": 1080, "initRange": {"start": "0", "end": "740"}, "indexRange": {"start": "741", "end": "4228"}, "lastModified": "1789815210815874", "contentLength": "586751840", "fps": 24, "qualityLabel": "1080p", "averageBitrate": 3045676, "approxDurationMs": "1541206", "url": "https://rr.example/videoplayback?itag=137&c=VISIONOS"}, {"itag": 140, "mimeType": "audio/mp4; codecs=\"mp4a.40.2\"", "bitrate": 131621, "initRange": {"start": "0", "end": "722"}, "indexRange": {"start": "723", "end": "2614"}, "lastModified": "1737125636609420", "contentLength": "24945126", "averageBitrate": 129478, "approxDurationMs": "1541270", "audioTrack": {"displayName": "Russian", "id": "ru.3", "audioIsDefault": false}, "xtags": "Cg8KBWFjb250EgZkdWJiZWQKCgoEbGFuZxICcnU", "audioQuality": "AUDIO_QUALITY_MEDIUM", "url": "https://rr.example/videoplayback?itag=140&c=VISIONOS"}, {"itag": 140, "mimeType": "audio/mp4; codecs=\"mp4a.40.2\"", "bitrate": 131818, "initRange": {"start": "0", "end": "722"}, "indexRange": {"start": "723", "end": "2614"}, "lastModified": "1737164481356299", "contentLength": "24944676", "averageBitrate": 129475, "approxDurationMs": "1541270", "audioTrack": {"displayName": "English (US) original", "id": "en-US.4", "audioIsDefault": true}, "xtags": "ChEKBWFjb250EghvcmlnaW5hbAoNCgRsYW5nEgVlbi1VUw", "audioQuality": "AUDIO_QUALITY_MEDIUM", "url": "https://rr.example/videoplayback?itag=140&c=VISIONOS"}], "hlsManifestUrl": "https://manifest.example/hls_variant/VISIONOS/index.m3u8", "serverAbrStreamingUrl": "https://rr.example/videoplayback?sabr=1&c=VISIONOS"}}
```

`tests/fixtures/player_vr_ok.json`:

```json
{"responseContext": {"visitorData": "VISITOR_ANDROID_VR"}, "playabilityStatus": {"status": "OK", "playableInEmbed": true}, "streamingData": {"expiresInSeconds": "21540", "formats": [{"itag": 18, "mimeType": "video/mp4; codecs=\"avc1.42001E, mp4a.40.2\"", "bitrate": 444226, "width": 640, "height": 360, "lastModified": "1766960953317159", "fps": 25, "qualityLabel": "360p", "approxDurationMs": "213089", "audioQuality": "AUDIO_QUALITY_LOW", "url": "https://rr.example/videoplayback?itag=18&c=ANDROID_VR"}], "adaptiveFormats": [{"itag": 137, "mimeType": "video/mp4; codecs=\"avc1.640028\"", "bitrate": 4334157, "width": 1920, "height": 1080, "initRange": {"start": "0", "end": "741"}, "indexRange": {"start": "742", "end": "1229"}, "lastModified": "1766957926174250", "contentLength": "80911999", "fps": 25, "qualityLabel": "1080p", "averageBitrate": 3038377, "approxDurationMs": "213040", "url": "https://rr.example/videoplayback?itag=137&c=ANDROID_VR"}, {"itag": 140, "mimeType": "audio/mp4; codecs=\"mp4a.40.2\"", "bitrate": 130677, "initRange": {"start": "0", "end": "722"}, "indexRange": {"start": "723", "end": "1018"}, "lastModified": "1766955925572207", "contentLength": "3449447", "averageBitrate": 129502, "approxDurationMs": "213089", "audioQuality": "AUDIO_QUALITY_MEDIUM", "url": "https://rr.example/videoplayback?itag=140&c=ANDROID_VR"}], "serverAbrStreamingUrl": "https://rr.example/videoplayback?sabr=1&c=ANDROID_VR"}}
```

`tests/fixtures/player_botcheck.json`:

```json
{"responseContext": {"visitorData": "VISITOR_BOT"}, "playabilityStatus": {"status": "LOGIN_REQUIRED", "reason": "Sign in to confirm you’re not a bot"}}
```

`tests/fixtures/player_removed.json`:

```json
{"responseContext": {}, "playabilityStatus": {"status": "ERROR", "reason": "This video is unavailable"}}
```

`tests/fixtures/captions_manual.json3`:

```json
{"wireMagic": "pb3", "events": [{"tStartMs": 1360, "dDurationMs": 1680, "segs": [{"utf8": "[♪♪♪]"}]}, {"tStartMs": 18640, "dDurationMs": 3240, "segs": [{"utf8": "♪ We're no strangers to love ♪"}]}, {"tStartMs": 22640, "dDurationMs": 4320, "segs": [{"utf8": "♪ You know the rules\nand so do I ♪"}]}]}
```

`tests/fixtures/captions_asr.json3`:

```json
{"wireMagic": "pb3", "events": [{"tStartMs": 0, "dDurationMs": 211879, "id": 1, "wpWinPosId": 1, "wsWinStyleId": 1}, {"tStartMs": 320, "dDurationMs": 14260, "wWinId": 1, "segs": [{"utf8": "[Music]"}]}, {"tStartMs": 18790, "wWinId": 1, "aAppend": 1, "segs": [{"utf8": "\n"}]}, {"tStartMs": 18800, "dDurationMs": 7160, "wWinId": 1, "segs": [{"utf8": "We're", "acAsrConf": 0}, {"utf8": " no", "tOffsetMs": 239, "acAsrConf": 0}, {"utf8": " strangers", "tOffsetMs": 559, "acAsrConf": 0}, {"utf8": " to", "tOffsetMs": 1040, "acAsrConf": 0}]}]}
```

- [ ] **Step 2: Write the failing tests** — append to `tests/tst_parsers.cpp`:

```cpp
// ---- /player (parsePlayerResponse) ----
TEST_CASE("playerResponseVisionos") {
    const PlayerPart p = parsePlayerResponse(loadFixtureRaw("player_visionos_ok.json"));
    CHECK(p.status.kind == Playability::Ok);
    CHECK_FALSE(p.final);
    REQUIRE(p.formats.size() == 4);
    const Format &v = p.formats[0];
    CHECK(v.itag == 137);
    CHECK(v.url == "https://rr.example/videoplayback?itag=137&c=VISIONOS");
    CHECK(v.mimeType == "video/mp4");
    CHECK(v.codecs == "avc1.640028");
    CHECK(v.qualityLabel == "1080p");
    CHECK(v.width == 1920);
    CHECK(v.height == 1080);
    CHECK(v.fps == 25);
    CHECK(v.bitrate == 4334157);
    CHECK(v.averageBitrate == 3038377);
    CHECK(v.contentLength == 80911999);
    CHECK(v.durationMs == 213040);
    REQUIRE(v.initRange.has_value());
    CHECK(v.initRange->first == 0);
    CHECK(v.initRange->last == 741);
    REQUIRE(v.indexRange.has_value());
    CHECK(v.indexRange->first == 742);
    CHECK(v.lastModified == 1766957926174250LL);
    CHECK((v.hasVideo && !v.hasAudio));
    CHECK_FALSE(v.audio.has_value());
    CHECK_FALSE(v.sealed);
    const Format &a = p.formats[2];
    CHECK((a.hasAudio && !a.hasVideo));
    CHECK(a.codecs == "mp4a.40.2");
    CHECK(p.formats[3].codecs == "opus");
    CHECK(p.hlsUrl == "https://manifest.example/hls_variant/VISIONOS/index.m3u8");
    CHECK(p.sabrUrl == "https://rr.example/videoplayback?sabr=1&c=VISIONOS");
    CHECK(p.sabrConfig == "CgUSA2FiYw");
    CHECK(p.expiresInSecs == 21540);
    REQUIRE(p.captions.size() == 2);
    CHECK(p.captions[0].language == "en");
    CHECK(p.captions[0].name == "English");
    CHECK(p.captions[0].translatable);
    CHECK_FALSE(p.captions[0].autoGenerated);
    CHECK(p.captions[1].autoGenerated);
    CHECK(p.captions[1].url.find("kind=asr") != std::string::npos);
    CHECK(p.storyboardSpec.find("storyboard3_L$L") != std::string::npos);
    CHECK(p.watchtimeUrl == "https://s.youtube.com/api/stats/watchtime?cl=1&docid=dQw4w9WgXcQ&ei=EI&c=VISIONOS");
    CHECK(p.playbackUrl.find("/api/stats/playback") != std::string::npos);
    CHECK(p.details.id == "dQw4w9WgXcQ");
    CHECK(p.details.author == "Rick Astley");
    CHECK(p.details.channelId == "UCuAXFkgsw1L7xaCfnd5JJOw");
    CHECK(p.details.lengthSecs == 213);
    CHECK(p.details.viewCount == 1826490705LL);
    CHECK(p.details.thumbnailUrl == "https://i.ytimg.com/vi/dQw4w9WgXcQ/hq720.jpg");
}
TEST_CASE("playerResponseProgressiveFromFormats") {
    const PlayerPart p = parsePlayerResponse(loadFixtureRaw("player_vr_ok.json"));
    REQUIRE(p.formats.size() == 3);
    CHECK(p.formats[0].itag == 18);
    CHECK((p.formats[0].hasVideo && p.formats[0].hasAudio));   // formats[] are muxed
    CHECK(p.formats[0].codecs == "avc1.42001E, mp4a.40.2");
    CHECK(p.hlsUrl.empty());
}
TEST_CASE("playerResponseAudioTracks") {
    const PlayerPart p = parsePlayerResponse(loadFixtureRaw("player_visionos_dubbed.json"));
    REQUIRE(p.formats.size() == 3);
    REQUIRE(p.formats[1].audio.has_value());
    const AudioTrack &ru = *p.formats[1].audio;
    CHECK(ru.id == "ru.3");
    CHECK(ru.name == "Russian");
    CHECK(ru.language == "ru");
    CHECK_FALSE(ru.isDefault);
    CHECK_FALSE(ru.original);                         // xtags: acont=dubbed
    CHECK_FALSE(ru.autoDubbed);
    REQUIRE(p.formats[2].audio.has_value());
    const AudioTrack &en = *p.formats[2].audio;
    CHECK(en.language == "en-US");
    CHECK(en.isDefault);
    CHECK(en.original);                               // xtags: acont=original
    CHECK(p.formats[2].xtags == "ChEKBWFjb250EghvcmlnaW5hbAoNCgRsYW5nEgVlbi1VUw");
}
TEST_CASE("playerResponseVerdicts") {
    PlayerPart p = parsePlayerResponse(loadFixtureRaw("player_botcheck.json"));
    CHECK(p.status.kind == Playability::BotCheck);
    CHECK(p.status.status == "LOGIN_REQUIRED");
    CHECK(p.status.reason.find("not a bot") != std::string::npos);
    CHECK_FALSE(p.final);
    p = parsePlayerResponse(loadFixtureRaw("player_removed.json"));
    CHECK(p.status.kind == Playability::Unplayable);
    CHECK(p.status.reason == "This video is unavailable");
    CHECK(p.final);
    // Review focus: the reason is localized by hl — the verdict must not read it.
    p = parsePlayerResponse(std::string("{\"playabilityStatus\":{\"status\":\"LOGIN_REQUIRED\","
        "\"reason\":\"Войдите, чтобы подтвердить, что вы не бот\"}}"));
    CHECK(p.status.kind == Playability::BotCheck);
    p = parsePlayerResponse(std::string("{\"playabilityStatus\":{\"status\":\"LOGIN_REQUIRED\","
        "\"reason\":\"Sign in to confirm your age\",\"desktopLegacyAgeGateReason\":1}}"));
    CHECK(p.status.kind == Playability::AgeRestricted);
    p = parsePlayerResponse(std::string("{\"playabilityStatus\":{\"status\":\"AGE_CHECK_REQUIRED\"}}"));
    CHECK(p.status.kind == Playability::AgeRestricted);
    p = parsePlayerResponse(std::string("not json"));
    CHECK(p.status.kind == Playability::Unknown);
    CHECK(p.formats.empty());
}
TEST_CASE("playerResponseUpcoming") {
    // Shape per Troubadour (YTApi.m scheduledStartIn): no scheduled stream was at hand to capture.
    PlayerPart p = parsePlayerResponse(std::string(
        "{\"playabilityStatus\":{\"status\":\"LIVE_STREAM_OFFLINE\",\"reason\":\"This live event will begin in 3 days.\","
        "\"liveStreamability\":{\"liveStreamabilityRenderer\":{\"offlineSlate\":{\"liveStreamOfflineSlateRenderer\":"
        "{\"scheduledStartTime\":\"1792000000\",\"mainText\":{\"runs\":[{\"text\":\"Live in 3 days\"}]},"
        "\"subtitleText\":{\"simpleText\":\"October 13, 8:00 PM\"}}}}}}}"));
    CHECK(p.status.kind == Playability::Upcoming);
    CHECK(p.status.scheduledStartSecs == 1792000000LL);
    CHECK(p.status.slateText == "Live in 3 days\nOctober 13, 8:00 PM");
    CHECK(p.final);
    // No slate: the microformat's ISO time (with its zone) is the fallback.
    p = parsePlayerResponse(std::string(
        "{\"playabilityStatus\":{\"status\":\"LIVE_STREAM_OFFLINE\"},\"microformat\":{\"playerMicroformatRenderer\":"
        "{\"liveBroadcastDetails\":{\"startTimestamp\":\"2026-09-09T20:00:00+03:00\"}}}}"));
    CHECK(p.status.kind == Playability::Upcoming);
    CHECK(p.status.scheduledStartSecs == 1788973200LL);
    p = parsePlayerResponse(std::string("{\"playabilityStatus\":{\"status\":\"LIVE_STREAM_OFFLINE\"}}"));
    CHECK(p.status.kind == Playability::LiveOffline);
    CHECK(p.final);
}
TEST_CASE("playerResponseSealedFormat") {
    const PlayerPart p = parsePlayerResponse(std::string(
        "{\"playabilityStatus\":{\"status\":\"OK\"},\"streamingData\":{\"formats\":[{\"itag\":18,"
        "\"mimeType\":\"video/mp4\",\"signatureCipher\":\"s=AB&sp=sig&url=https%3A%2F%2Fx\"}]}}"));
    REQUIRE(p.formats.size() == 1);
    CHECK(p.formats[0].sealed);
    CHECK(p.formats[0].url.empty());
}
// The slate search walks the whole answer: it must stay bounded like every scan.
TEST_CASE("playerResponseSlateScanDepthBounded") {
    const int n = 200000;
    std::string deep = "{\"playabilityStatus\":{\"status\":\"LIVE_STREAM_OFFLINE\"},\"x\":";
    for (int i = 0; i < n; ++i) deep += "{\"a\":";
    deep += "{}";
    for (int i = 0; i < n; ++i) deep += "}";
    deep += "}";
    const PlayerPart p = parsePlayerResponse(deep);   // must return, not overflow the stack
    CHECK(p.status.kind == Playability::LiveOffline);
}
TEST_CASE("storyboardSpec") {
    const PlayerPart p = parsePlayerResponse(loadFixtureRaw("player_visionos_ok.json"));
    const std::vector<StoryboardLevel> l = parseStoryboardSpec(p.storyboardSpec, 213);
    REQUIRE(l.size() == 2);
    CHECK(l[0].urlTemplate == "https://i.ytimg.com/sb/dQw4w9WgXcQ/storyboard3_L0/default.jpg?sqp=SQP&sigh=rs$SIGH0");
    CHECK(l[0].width == 48);
    CHECK(l[0].height == 27);
    CHECK(l[0].count == 100);
    CHECK(l[0].intervalMs == 2130);                   // 0 in the spec: length / count
    CHECK(l[1].urlTemplate == "https://i.ytimg.com/sb/dQw4w9WgXcQ/storyboard3_L1/M$M.jpg?sqp=SQP&sigh=rs$SIGH1");
    CHECK(l[1].count == 108);
    CHECK(l[1].cols == 10);
    CHECK(l[1].rows == 10);
    CHECK(l[1].intervalMs == 2000);
    CHECK(parseStoryboardSpec("", 10).empty());
    CHECK(parseStoryboardSpec("https://x/$L/$N.jpg|1#2", 10).empty());   // a short level is skipped
    CHECK(parseStoryboardSpec("https://x/$L/$N.jpg|a#b#c#d#e#f#g#h", 10).empty());
}
TEST_CASE("json3Cues") {
    std::vector<Cue> c = parseJson3(loadFixtureRaw("captions_manual.json3"));
    REQUIRE(c.size() == 3);
    CHECK(c[0].startMs == 1360);
    CHECK(c[0].endMs == 3040);
    CHECK(c[0].text == "[♪♪♪]");
    CHECK(c[2].text == "♪ You know the rules\nand so do I ♪");
    c = parseJson3(loadFixtureRaw("captions_asr.json3"));
    REQUIRE(c.size() == 2);                           // the window event and the "\n" append are not cues
    CHECK(c[0].text == "[Music]");
    CHECK(c[1].text == "We're no strangers to");
    CHECK(c[1].startMs == 18800);
    CHECK(c[1].endMs == 25960);
    CHECK(parseJson3("").empty());
    CHECK(parseJson3("<html>Sorry</html>").empty());
}
```

- [ ] **Step 3: Run them to verify they fail**

Run: `cmake --build build -j`
Expected: `'PlayerPart' was not declared in this scope`.

- [ ] **Step 4: Declare** — in `src/parsers/playerparser.h`, add `#include <yt/player.h>` after `#include <yt/types.h>` and, after `parsePlayer(const std::string &)`, add:

```cpp
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
```

- [ ] **Step 5: Implement** — in `src/parsers/playerparser.cpp` add `#include "json/jsonscan.h"` after `"json/ytjson.h"` and `#include <charconv>` before `<string>`; then append before the final `}` of `namespace yt`:

```cpp
// ---------------------------------------------------------------------------
// parsePlayerResponse — the client walk's view (sub-project 1 of the modernisation)
// ---------------------------------------------------------------------------

namespace pr {

struct Range {
    std::optional<FlexInt> start;
    std::optional<FlexInt> end;
};
struct AudioTrackJ {
    std::optional<std::string> displayName;
    std::optional<std::string> id;
    std::optional<bool> audioIsDefault;
};
struct FormatJ {
    std::optional<FlexInt> itag;
    std::optional<std::string> url;
    std::optional<std::string> signatureCipher;
    std::optional<std::string> cipher;
    std::optional<std::string> mimeType;
    std::optional<std::string> qualityLabel;
    std::optional<FlexInt> width;
    std::optional<FlexInt> height;
    std::optional<FlexInt> fps;
    std::optional<FlexInt> bitrate;
    std::optional<FlexInt> averageBitrate;
    std::optional<FlexInt> contentLength;
    std::optional<FlexInt> approxDurationMs;
    std::optional<FlexInt> lastModified;
    std::optional<Range> initRange;
    std::optional<Range> indexRange;
    std::optional<AudioTrackJ> audioTrack;
    std::optional<std::string> xtags;
    std::optional<bool> isDrc;
};
struct StreamingJ {
    std::optional<FlexInt> expiresInSeconds;
    std::optional<std::vector<FormatJ>> formats;
    std::optional<std::vector<FormatJ>> adaptiveFormats;
    std::optional<std::string> hlsManifestUrl;
    std::optional<std::string> dashManifestUrl;
    std::optional<std::string> serverAbrStreamingUrl;
};
struct UstreamerJ {
    std::optional<std::string> videoPlaybackUstreamerConfig;
};
struct MediaCommonJ {
    std::optional<UstreamerJ> mediaUstreamerRequestConfig;
};
struct PlayerConfigJ {
    std::optional<MediaCommonJ> mediaCommonConfig;
};
struct StatusJ {
    std::optional<std::string> status;
    std::optional<std::string> reason;
    std::optional<FlexInt> desktopLegacyAgeGateReason;
};
struct DetailsJ {
    std::optional<std::string> videoId;
    std::optional<std::string> title;
    std::optional<std::string> author;
    std::optional<std::string> channelId;
    std::optional<std::string> shortDescription;
    std::optional<FlexInt> lengthSeconds;
    std::optional<FlexInt> viewCount;
    std::optional<bool> isLive;
    std::optional<bool> isLiveContent;
    std::optional<bool> isUpcoming;
    std::optional<ThumbSet> thumbnail;
};
struct TrackJ {
    std::optional<std::string> baseUrl;
    std::optional<std::string> languageCode;
    std::optional<Text> name;
    std::optional<std::string> kind;
    std::optional<bool> isTranslatable;
};
struct TracklistJ {
    std::optional<std::vector<TrackJ>> captionTracks;
};
struct CaptionsJ {
    std::optional<TracklistJ> playerCaptionsTracklistRenderer;
};
struct SpecJ {
    std::optional<std::string> spec;
};
struct StoryboardsJ {
    std::optional<SpecJ> playerStoryboardSpecRenderer;
};
struct UrlJ {
    std::optional<std::string> baseUrl;
};
struct TrackingJ {
    std::optional<UrlJ> videostatsPlaybackUrl;
    std::optional<UrlJ> videostatsWatchtimeUrl;
};
struct BroadcastJ {
    std::optional<std::string> startTimestamp;
};
struct MicroRendererJ {
    std::optional<BroadcastJ> liveBroadcastDetails;
};
struct MicroformatJ {
    std::optional<MicroRendererJ> playerMicroformatRenderer;
};
struct Root {
    std::optional<StatusJ> playabilityStatus;
    std::optional<StreamingJ> streamingData;
    std::optional<PlayerConfigJ> playerConfig;
    std::optional<DetailsJ> videoDetails;
    std::optional<CaptionsJ> captions;
    std::optional<StoryboardsJ> storyboards;
    std::optional<TrackingJ> playbackTracking;
    std::optional<MicroformatJ> microformat;
};
struct SlateJ {
    std::optional<FlexInt> scheduledStartTime;
    std::optional<Text> mainText;
    std::optional<Text> subtitleText;
};
struct Json3Seg {
    std::optional<std::string> utf8;
};
struct Json3Event {
    std::optional<FlexInt> tStartMs;
    std::optional<FlexInt> dDurationMs;
    std::optional<std::vector<Json3Seg>> segs;
};
struct Json3Root {
    std::optional<std::vector<Json3Event>> events;
};

} // namespace pr

namespace {

// The verdict from the status word alone: the reason is localized by hl.
Playability kindOf(const std::string &status, bool ageMarker)
{
    if (status == "OK") return Playability::Ok;
    if (status.empty()) return Playability::Unknown;
    if (ageMarker || status.rfind("AGE_", 0) == 0 || status == "CONTENT_CHECK_REQUIRED")
        return Playability::AgeRestricted;
    // The walk renames it LoginRequired when the bearer's TV client got it.
    if (status == "LOGIN_REQUIRED") return Playability::BotCheck;
    // Becomes Upcoming once a start time is known.
    if (status == "LIVE_STREAM_OFFLINE") return Playability::LiveOffline;
    return Playability::Unplayable;              // ERROR, UNPLAYABLE, …
}

// "2026-09-09T20:00:00+03:00" / "…Z" / "….123Z" → unix seconds; 0 when malformed.
int64_t unixFromIso8601(std::string_view s)
{
    auto num = [s](size_t at, size_t n, int &out) {
        if (at + n > s.size()) return false;
        const char *b = s.data() + at, *e = b + n;
        const std::from_chars_result r = std::from_chars(b, e, out);
        return r.ec == std::errc() && r.ptr == e;
    };
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    if (s.size() < 19 || !num(0, 4, y) || s[4] != '-' || !num(5, 2, mo) || s[7] != '-' || !num(8, 2, d)
        || (s[10] != 'T' && s[10] != ' ') || !num(11, 2, h) || s[13] != ':' || !num(14, 2, mi)
        || s[16] != ':' || !num(17, 2, se) || mo < 1 || mo > 12)
        return 0;
    // days_from_civil (Howard Hinnant's public-domain algorithm).
    y -= mo <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned) (y - era * 400);
    const unsigned doy = (unsigned) ((153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t t = ((int64_t) era * 146097 + (int64_t) doe - 719468) * 86400 + h * 3600 + mi * 60 + se;
    size_t z = 19;
    while (z < s.size() && (s[z] == '.' || (s[z] >= '0' && s[z] <= '9'))) ++z;   // fractional seconds
    if (z < s.size() && (s[z] == '+' || s[z] == '-')) {
        int oh = 0, om = 0;
        if (!num(z + 1, 2, oh) || z + 3 >= s.size() || s[z + 3] != ':' || !num(z + 4, 2, om)) return 0;
        const int64_t off = oh * 3600 + om * 60;
        t += s[z] == '+' ? -off : off;
    }
    return t;
}

// The offline slate lies under playabilityStatus on WEB-family answers and elsewhere on
// others: find the renderer anywhere, bounded by kScanMaxDepth like every scan.
struct SlateFinder {
    std::string_view found;
    void enter(int) {}
    void leave(int) {}
    scan::Action what(std::string_view k, int)
    {
        if (!found.empty()) return scan::Action::Skip;
        return k == "liveStreamOfflineSlateRenderer" ? scan::Action::Capture : scan::Action::Descend;
    }
    void capture(std::string_view, std::string_view v, int) { found = v; }
};

Format formatOf(const pr::FormatJ &f, bool muxed)
{
    Format o;
    o.itag = (int) toInt64(f.itag);
    o.url = str(f.url);
    o.sealed = o.url.empty() && (f.signatureCipher.has_value() || f.cipher.has_value());
    const std::string mime = str(f.mimeType);         // video/mp4; codecs="avc1.640028"
    o.mimeType = mime.substr(0, mime.find(';'));
    const size_t q = mime.find("codecs=\"");
    if (q != std::string::npos) {
        const size_t b = q + 8, e = mime.find('"', b);
        o.codecs = mime.substr(b, e == std::string::npos ? std::string::npos : e - b);
    }
    o.qualityLabel = str(f.qualityLabel);
    o.width = (int) toInt64(f.width);
    o.height = (int) toInt64(f.height);
    o.fps = (int) toInt64(f.fps);
    o.bitrate = toInt64(f.bitrate);
    o.averageBitrate = toInt64(f.averageBitrate);
    o.contentLength = toInt64(f.contentLength);
    o.durationMs = toInt64(f.approxDurationMs);
    o.lastModified = toInt64(f.lastModified);
    if (f.initRange) o.initRange = ByteRange{toInt64(f.initRange->start), toInt64(f.initRange->end)};
    if (f.indexRange) o.indexRange = ByteRange{toInt64(f.indexRange->start), toInt64(f.indexRange->end)};
    o.xtags = str(f.xtags);
    o.hasVideo = o.mimeType.rfind("video/", 0) == 0;
    o.hasAudio = muxed || o.mimeType.rfind("audio/", 0) == 0;
    const bool drcFlag = f.isDrc.value_or(false);
    if (f.audioTrack || drcFlag) {
        // xtags is a small protobuf whose pairs ("acont" → "original" / "dubbed" /
        // "dubbed-auto", "drc" → "1") are in clear text; displayName is localized and
        // useless as a marker.
        std::string marks;
        base64Decode(o.xtags, &marks);
        AudioTrack a;
        if (f.audioTrack) {
            a.id = str(f.audioTrack->id);
            a.name = str(f.audioTrack->displayName);
            a.isDefault = f.audioTrack->audioIsDefault.value_or(false);
            a.language = a.id.substr(0, a.id.find('.'));
        }
        a.original = marks.find("original") != std::string::npos;
        a.autoDubbed = marks.find("dubbed-auto") != std::string::npos;
        a.drc = drcFlag || marks.find("drc") != std::string::npos;
        o.audio = a;
    }
    return o;
}

void splitInto(std::string_view s, char sep, std::vector<std::string_view> &out)
{
    out.clear();
    size_t b = 0;
    while (true) {
        const size_t e = s.find(sep, b);
        out.push_back(s.substr(b, e == std::string_view::npos ? std::string_view::npos : e - b));
        if (e == std::string_view::npos) return;
        b = e + 1;
    }
}

bool numberOf(std::string_view s, int64_t &out)
{
    const std::from_chars_result r = std::from_chars(s.data(), s.data() + s.size(), out);
    return !s.empty() && r.ec == std::errc() && r.ptr == s.data() + s.size();
}

void replaceAll(std::string &s, std::string_view what, std::string_view with)
{
    for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + with.size()))
        s.replace(at, what.size(), with);
}

} // namespace

PlayerPart parsePlayerResponse(const std::string &body)
{
    pr::Root root{};
    readJsonDoc(root, body);
    PlayerPart p;
    if (root.playabilityStatus) {
        const pr::StatusJ &st = *root.playabilityStatus;
        p.status.status = str(st.status);
        p.status.reason = str(st.reason);
        p.status.kind = kindOf(p.status.status, st.desktopLegacyAgeGateReason.has_value());
    }
    if (p.status.status == "LIVE_STREAM_OFFLINE") {
        SlateFinder sf;
        scan::document(body, sf);
        if (!sf.found.empty()) {
            pr::SlateJ sl{};
            readJson(sl, sf.found);
            p.status.scheduledStartSecs = toInt64(sl.scheduledStartTime);
            const std::string main = textOf(sl.mainText), sub = textOf(sl.subtitleText);
            p.status.slateText = (!main.empty() && !sub.empty()) ? main + "\n" + sub : main + sub;
        }
        if (p.status.scheduledStartSecs <= 0 && root.microformat && root.microformat->playerMicroformatRenderer
            && root.microformat->playerMicroformatRenderer->liveBroadcastDetails)
            p.status.scheduledStartSecs =
                unixFromIso8601(str(root.microformat->playerMicroformatRenderer->liveBroadcastDetails->startTimestamp));
        if (p.status.scheduledStartSecs > 0) p.status.kind = Playability::Upcoming;
    }
    p.final = p.status.status == "ERROR" || p.status.kind == Playability::Upcoming
           || p.status.kind == Playability::LiveOffline;
    if (root.streamingData) {
        const pr::StreamingJ &sd = *root.streamingData;
        if (sd.formats) for (const pr::FormatJ &f : *sd.formats) p.formats.push_back(formatOf(f, true));
        if (sd.adaptiveFormats) for (const pr::FormatJ &f : *sd.adaptiveFormats) p.formats.push_back(formatOf(f, false));
        p.hlsUrl = str(sd.hlsManifestUrl);
        p.dashUrl = str(sd.dashManifestUrl);
        p.sabrUrl = str(sd.serverAbrStreamingUrl);
        p.expiresInSecs = toInt64(sd.expiresInSeconds);
    }
    if (root.playerConfig && root.playerConfig->mediaCommonConfig
        && root.playerConfig->mediaCommonConfig->mediaUstreamerRequestConfig)
        p.sabrConfig = str(root.playerConfig->mediaCommonConfig->mediaUstreamerRequestConfig->videoPlaybackUstreamerConfig);
    if (root.videoDetails) {
        const pr::DetailsJ &d = *root.videoDetails;
        p.details.id = str(d.videoId);
        p.details.title = str(d.title);
        p.details.author = str(d.author);
        p.details.channelId = str(d.channelId);
        p.details.description = str(d.shortDescription);
        p.details.lengthSecs = toInt64(d.lengthSeconds);
        p.details.viewCount = toInt64(d.viewCount);
        p.details.isLive = d.isLive.value_or(false);
        p.details.isLiveContent = d.isLiveContent.value_or(false);
        p.details.isUpcoming = d.isUpcoming.value_or(false);
        p.details.thumbnailUrl = lastThumbUrl(d.thumbnail);
        if (p.details.thumbnailUrl.empty() && !p.details.id.empty())
            p.details.thumbnailUrl = "https://i.ytimg.com/vi/" + p.details.id + "/hqdefault.jpg";
    }
    if (root.captions && root.captions->playerCaptionsTracklistRenderer
        && root.captions->playerCaptionsTracklistRenderer->captionTracks)
        for (const pr::TrackJ &t : *root.captions->playerCaptionsTracklistRenderer->captionTracks) {
            CaptionTrack c;
            c.url = str(t.baseUrl);
            c.language = str(t.languageCode);
            c.name = textOf(t.name);
            c.autoGenerated = str(t.kind) == "asr";
            c.translatable = t.isTranslatable.value_or(false);
            if (!c.url.empty()) p.captions.push_back(std::move(c));
        }
    if (root.storyboards && root.storyboards->playerStoryboardSpecRenderer)
        p.storyboardSpec = str(root.storyboards->playerStoryboardSpecRenderer->spec);
    if (root.playbackTracking) {
        const pr::TrackingJ &tr = *root.playbackTracking;
        if (tr.videostatsWatchtimeUrl) p.watchtimeUrl = str(tr.videostatsWatchtimeUrl->baseUrl);
        if (tr.videostatsPlaybackUrl) p.playbackUrl = str(tr.videostatsPlaybackUrl->baseUrl);
    }
    return p;
}

std::vector<StoryboardLevel> parseStoryboardSpec(std::string_view spec, int64_t lengthSecs)
{
    std::vector<StoryboardLevel> out;
    std::vector<std::string_view> parts, f;
    splitInto(spec, '|', parts);
    if (parts.size() < 2) return out;
    for (size_t i = 1; i < parts.size(); ++i) {
        splitInto(parts[i], '#', f);
        if (f.size() < 8) continue;
        int64_t v[6];
        bool numbers = true;
        for (int k = 0; k < 6; ++k) numbers = numbers && numberOf(f[k], v[k]);
        if (!numbers || v[2] <= 0 || v[3] <= 0 || v[4] <= 0) continue;
        StoryboardLevel l;
        l.width = (int) v[0];
        l.height = (int) v[1];
        l.count = (int) v[2];
        l.cols = (int) v[3];
        l.rows = (int) v[4];
        l.intervalMs = v[5];
        if (l.intervalMs <= 0 && lengthSecs > 0) l.intervalMs = lengthSecs * 1000 / l.count;
        std::string url(parts[0]);
        replaceAll(url, "$L", std::to_string(i - 1));
        replaceAll(url, "$N", f[6]);                 // a name like "M$M" keeps $M for the sheet
        url += url.find('?') == std::string::npos ? "?sigh=" : "&sigh=";
        url += f[7];
        l.urlTemplate = std::move(url);
        out.push_back(std::move(l));
    }
    return out;
}

std::vector<Cue> parseJson3(const std::string &body)
{
    pr::Json3Root root{};
    readJsonDoc(root, body);
    std::vector<Cue> out;
    if (!root.events) return out;
    for (const pr::Json3Event &e : *root.events) {
        if (!e.segs) continue;                        // window/pen definitions
        std::string text;
        for (const pr::Json3Seg &s : *e.segs) if (s.utf8) text += *s.utf8;
        if (text.find_first_not_of(" \n") == std::string::npos) continue;   // ASR line-append
        Cue c;
        c.startMs = toInt64(e.tStartMs);
        c.endMs = c.startMs + toInt64(e.dDurationMs);
        c.text = std::move(text);
        out.push_back(std::move(c));
    }
    return out;
}
```

`scan::document`'s signature is used by `net/httpclient.cpp` as `gj::scan::document(sv, visitor)`; check `json/jsonscan.h` for the exact name before compiling (inside `namespace gj` with `using namespace gj;` at the top of this file, `scan::document` resolves).

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cmake --build build -j && (cd build && ctest --output-on-failure -R tst_parsers)`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/parsers/playerparser.h src/parsers/playerparser.cpp tests/tst_parsers.cpp tests/fixtures/player_visionos_ok.json tests/fixtures/player_visionos_dubbed.json tests/fixtures/player_vr_ok.json tests/fixtures/player_botcheck.json tests/fixtures/player_removed.json tests/fixtures/captions_manual.json3 tests/fixtures/captions_asr.json3
git commit -m "lib: full /player parser, storyboard spec and json3 captions" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: player-JS scans

**Files:**
- Create: `src/innertube/playerjs.h`, `src/innertube/playerjs.cpp`
- Modify: `src/innertube/catalog.h` (URLs), `CMakeLists.txt` (add `src/innertube/playerjs.cpp` after `src/innertube/contextbuilder.cpp`)
- Test: `tests/tst_parsers.cpp`

**Interfaces:**
- Produces (namespace `yt`): `std::string playerIdIn(std::string_view html);` (`""` if absent), `int signatureTimestampIn(std::string_view js);` (`0` if absent), `std::string playerScriptUrl(std::string_view playerId);`, `Catalog::kTvPageUrl`.

- [ ] **Step 1: Write the failing test** — append to `tests/tst_parsers.cpp` (add `#include "innertube/playerjs.h"` to the includes):

```cpp
// The /tv page (TV UA, 2026-10-14) names the build as ".../s/player/22cb88bc/tv-player-ias…";
// base.js carries "signatureTimestamp:20737".
TEST_CASE("playerJsScans") {
    CHECK(playerIdIn("<script src=\"/s/player/22cb88bc/tv-player-ias.vflset/tv-player-ias.js\">") == "22cb88bc");
    CHECK(playerIdIn("\"jsUrl\":\"\\/s\\/player\\/22cb88bc\\/tv-player-ias.vflset\\/x.js\"") == "22cb88bc");
    CHECK(playerIdIn("<html>no player here</html>").empty());
    CHECK(playerIdIn("/s/player/bad\"id/x.js").empty());
    CHECK(playerScriptUrl("22cb88bc") == "https://www.youtube.com/s/player/22cb88bc/player_ias.vflset/en_US/base.js");
    CHECK(signatureTimestampIn("var a={signatureTimestamp:20737,b:1}") == 20737);
    CHECK(signatureTimestampIn("\"signatureTimestamp\": 20737") == 20737);
    CHECK(signatureTimestampIn("x.signatureTimestamp=y; z={signatureTimestamp:19999}") == 19999);
    CHECK(signatureTimestampIn("nothing") == 0);
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build build -j`
Expected: `fatal error: innertube/playerjs.h: No such file or directory`.

- [ ] **Step 3: Implement**

`src/innertube/catalog.h`, inside `namespace Catalog` after `kTokenUrl`:

```cpp
    // The TV app page names the live player build (/s/player/<id>/…); its player_ias
    // base.js carries the signatureTimestamp the TV /player needs. Fetched with the TV UA.
    static const char *const kTvPageUrl = "https://www.youtube.com/tv";
```

`src/innertube/playerjs.h`:

```cpp
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
```

`src/innertube/playerjs.cpp`:

```cpp
#include "innertube/playerjs.h"
namespace yt {

namespace {
bool idChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}
// The id after `marker` up to the next '/' (or the "\/" of escaped JSON).
std::string idAfter(std::string_view html, std::string_view marker)
{
    const size_t at = html.find(marker);
    if (at == std::string_view::npos) return std::string();
    size_t b = at + marker.size(), e = b;
    while (e < html.size() && idChar(html[e]) && e - b < 32) ++e;
    if (e == b || e >= html.size() || (html[e] != '/' && html[e] != '\\')) return std::string();
    return std::string(html.substr(b, e - b));
}
}

std::string playerIdIn(std::string_view html)
{
    std::string id = idAfter(html, "/s/player/");
    if (id.empty()) id = idAfter(html, "\\/s\\/player\\/");
    return id;
}

int signatureTimestampIn(std::string_view js)
{
    static const std::string_view kName = "signatureTimestamp";
    for (size_t at = js.find(kName); at != std::string_view::npos; at = js.find(kName, at + 1)) {
        size_t i = at + kName.size();
        while (i < js.size() && (js[i] == ':' || js[i] == '=' || js[i] == ' ' || js[i] == '"' || js[i] == '\'')) ++i;
        int value = 0, digits = 0;
        while (i < js.size() && js[i] >= '0' && js[i] <= '9' && digits < 9) { value = value * 10 + (js[i] - '0'); ++i; ++digits; }
        if (digits > 0) return value;
    }
    return 0;
}

std::string playerScriptUrl(std::string_view playerId)
{
    return "https://www.youtube.com/s/player/" + std::string(playerId) + "/player_ias.vflset/en_US/base.js";
}

}
```

Add `src/innertube/playerjs.cpp` to the `add_library` list after `src/innertube/contextbuilder.cpp`.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake -S . -B build -G Ninja >/dev/null && cmake --build build -j && (cd build && ctest --output-on-failure -R tst_parsers)`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/innertube/playerjs.h src/innertube/playerjs.cpp src/innertube/catalog.h CMakeLists.txt tests/tst_parsers.cpp
git commit -m "lib: read the player build and signatureTimestamp without running JS" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: the client walk replaces the old player chain

**Files:**
- Modify: `src/innertube/bodies.h`, `src/innertube/bodies.cpp` (`player` signature)
- Modify: `src/core/chains.h`, `src/core/chains.cpp` (remove `PlayerAccum`/`playerTry`/old `fetchPlayer`; add the walk)
- Modify: `include/yt/client.h`, `src/client.cpp` (`player` signature; drop `httpStatusOf(const PlayerInfo &)`)
- Modify: `include/yt/types.h` (drop `Stream`, `Subtitle`, `PlayerInfo`)
- Modify: `src/parsers/playerparser.h`, `.cpp` (drop `parseStreams`, `parseCaptions`, `isPlayable`, `parseVideoDetails`, `parsePlayer`, `PlayerResult` and the `pj` namespace once unused)
- Modify: `examples/yt-cli/main.cpp` (`streams`, state file)
- Test: `tests/tst_bodies.cpp`, `tests/tst_chains.cpp`, `tests/tst_client.cpp`, `tests/tst_parsers.cpp`

**Interfaces:**
- Consumes: Tasks 2–6.
- Produces:
  - `struct bodies::PlayerBody { bool tv = false; std::string cpn; int signatureTimestamp = 0; };` and `std::string bodies::player(std::string_view videoId, const PlayerBody &b = {});`
  - `void core::fetchPlayer(IHttp &, PlayerRequest, const JobToken &, Callback<Player> done);`
  - `Job Client::player(PlayerRequest request, Callback<Player> done);`

- [ ] **Step 1: Write the failing tests**

`tests/tst_bodies.cpp` — replace the two `bodies::player` checks in "next / player / resolve" with:

```cpp
    CHECK(bodies::player("vid") == "{\"videoId\":\"vid\",\"contentCheckOk\":true,\"racyCheckOk\":true}");
    CHECK(bodies::player("vid", {true, "CPN", 0}) ==
          "{\"videoId\":\"vid\",\"contentCheckOk\":true,\"racyCheckOk\":true,\"cpn\":\"CPN\","
          "\"playbackContext\":{\"contentPlaybackContext\":{\"html5Preference\":\"HTML5_PREF_WANTS\"}}}");
    CHECK(bodies::player("vid", {true, "CPN", 20737}) ==
          "{\"videoId\":\"vid\",\"contentCheckOk\":true,\"racyCheckOk\":true,\"cpn\":\"CPN\","
          "\"playbackContext\":{\"contentPlaybackContext\":{\"html5Preference\":\"HTML5_PREF_WANTS\","
          "\"signatureTimestamp\":20737}}}");
```

`tests/tst_chains.cpp` — delete every test from `// ---- fetchPlayer: streams side ----` through the last one that uses `PlayerCap` / `PlayerInfo` (`grep -n "PlayerCap\|PlayerInfo\|fetchPlayer" tests/tst_chains.cpp` lists them), delete `struct PlayerCap`, and add after the anonymous namespace:

```cpp
// ---- fetchPlayer: the client walk ----
namespace {
const char *kOkEmpty = "{\"playabilityStatus\":{\"status\":\"OK\"},\"streamingData\":{\"adaptiveFormats\":[]}}";
const char *kTvSabr = "{\"playabilityStatus\":{\"status\":\"OK\"},\"streamingData\":{\"adaptiveFormats\":"
    "[{\"itag\":137,\"mimeType\":\"video/mp4\"}],\"serverAbrStreamingUrl\":\"https://rr.example/sabr?c=TVHTML5\"},"
    "\"playbackTracking\":{\"videostatsWatchtimeUrl\":{\"baseUrl\":\"https://s.youtube.com/api/stats/watchtime?c=TVHTML5\"}}}";
const char *kUnplayable = "{\"playabilityStatus\":{\"status\":\"UNPLAYABLE\",\"reason\":\"The page needs to be reloaded.\"}}";
const char *kSeed = "{\"responseContext\":{\"visitorData\":\"FRESH\"},\"playabilityStatus\":{\"status\":\"UNPLAYABLE\"},"
    "\"captions\":{\"playerCaptionsTracklistRenderer\":{\"captionTracks\":[{\"baseUrl\":\"https://www.youtube.com/api/timedtext?web=1\",\"languageCode\":\"en\"}]}}}";
std::vector<int> ids(std::initializer_list<ClientId> c)
{
    std::vector<int> v;
    for (ClientId x : c) v.push_back((int) x);
    return v;
}
}

TEST_CASE("the walk stops once every wanted kind is covered") {
    FakeHttp t; t.session().visitorData = "VD";
    t.queue("player", loadFixtureRaw("player_visionos_ok.json"));
    Cap<Player> c;
    fetchPlayer(t, {"dQw4w9WgXcQ", WantAdaptive | WantHls | WantSabr}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.calls == 1);
    REQUIRE(c.r.ok());
    CHECK(t.sentClients == ids({ClientId::VISIONOS}));
    REQUIRE(c.r.value.sources.size() == 1);
    const Source &s = c.r.value.sources[0];
    CHECK(s.client == ClientId::VISIONOS);
    CHECK(s.userAgent == defaultClientInfo(ClientId::VISIONOS).userAgent);
    CHECK(s.formats.size() == 4);
    CHECK(s.hlsUrl.find("manifest.example") != std::string::npos);
    CHECK(s.sabrConfig == "CgUSA2FiYw");
    CHECK(s.expiresAtSecs > 1700000000);
    CHECK(c.r.value.status.kind == Playability::Ok);
    CHECK(c.r.value.details.title.find("Never Gonna") != std::string::npos);
    CHECK(c.r.value.storyboard.size() == 2);
    CHECK(c.r.value.captions.size() == 2);
    CHECK(c.r.value.cpn.size() == 16);
    CHECK(has(t.sent[0], "\"videoId\":\"dQw4w9WgXcQ\""));
    CHECK_FALSE(has(t.sent[0], "playbackContext"));   // only TV needs it
}
TEST_CASE("progressive comes from ANDROID_VR and every source keeps its own UA") {
    FakeHttp t; t.session().visitorData = "VD";
    t.queue("player", loadFixtureRaw("player_visionos_ok.json"));
    t.queue("player", loadFixtureRaw("player_vr_ok.json"));
    Cap<Player> c;
    fetchPlayer(t, {"v"}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(t.sentClients == ids({ClientId::VISIONOS, ClientId::ANDROID_VR}));   // HLS already covered: no IOS
    REQUIRE(c.r.value.sources.size() == 2);
    CHECK(c.r.value.sources[1].client == ClientId::ANDROID_VR);
    CHECK(c.r.value.sources[1].userAgent == defaultClientInfo(ClientId::ANDROID_VR).userAgent);
    CHECK(c.r.value.sources[1].formats[0].itag == 18);
}
TEST_CASE("without visitorData a WEB answer seeds it first") {
    FakeHttp t;
    t.queue("player", kSeed, 200, "FRESH");
    t.queue("player", loadFixtureRaw("player_visionos_ok.json"));
    Cap<Player> c;
    fetchPlayer(t, {"v", WantAdaptive}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(t.sentClients == ids({ClientId::WEB, ClientId::VISIONOS}));
    CHECK(t.session().visitorData == "FRESH");
    REQUIRE(c.r.value.sources.size() == 1);          // the seed is never a source
    CHECK(c.r.value.sources[0].client == ClientId::VISIONOS);
    REQUIRE_FALSE(c.r.value.captions.empty());       // WEB caption URLs need pot: not taken
    CHECK(c.r.value.captions[0].url.find("web=1") == std::string::npos);
}
TEST_CASE("a bot check reseeds visitorData once and repeats the same client") {
    FakeHttp t; t.session().visitorData = "FLAGGED";
    t.queue("player", loadFixtureRaw("player_botcheck.json"));
    t.queue("player", kSeed, 200, "FRESH");
    t.queue("player", loadFixtureRaw("player_visionos_ok.json"));
    Cap<Player> c;
    fetchPlayer(t, {"v", WantAdaptive}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(t.sentClients == ids({ClientId::VISIONOS, ClientId::WEB, ClientId::VISIONOS}));
    CHECK(t.visitorResets == 1);
    CHECK(c.r.value.status.kind == Playability::Ok);
    CHECK(c.r.value.sources.size() == 1);
}
TEST_CASE("a second bot check is not retried and the verdict is reported") {
    FakeHttp t; t.session().visitorData = "FLAGGED";
    t.queue("player", loadFixtureRaw("player_botcheck.json"));
    t.queue("player", kSeed, 200, "FRESH");
    t.queue("player", loadFixtureRaw("player_botcheck.json"));
    t.queue("player", loadFixtureRaw("player_botcheck.json"));
    Cap<Player> c;
    fetchPlayer(t, {"v", WantAdaptive}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());                                // an answer arrived: not a failure
    CHECK(t.sentClients == ids({ClientId::VISIONOS, ClientId::WEB, ClientId::VISIONOS, ClientId::ANDROID_VR}));
    CHECK(t.visitorResets == 1);
    CHECK(c.r.value.status.kind == Playability::BotCheck);
    CHECK(c.r.value.sources.empty());
}
TEST_CASE("a later bot check does not hide an earlier OK") {
    FakeHttp t; t.session().visitorData = "VD";
    t.queue("player", loadFixtureRaw("player_visionos_ok.json"));
    t.queue("player", loadFixtureRaw("player_botcheck.json"));
    t.queue("player", kSeed, 200, "FRESH");
    t.queue("player", loadFixtureRaw("player_botcheck.json"));
    Cap<Player> c;
    fetchPlayer(t, {"v", WantProgressive}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(c.r.value.status.kind == Playability::Ok);
    CHECK(c.r.value.sources.empty());                 // nothing progressive anywhere
}
TEST_CASE("a removed video stops the walk") {
    FakeHttp t; t.session().visitorData = "VD";
    t.queue("player", loadFixtureRaw("player_removed.json"));
    Cap<Player> c;
    fetchPlayer(t, {"v"}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(t.sent.size() == 1);
    CHECK(c.r.value.status.kind == Playability::Unplayable);
    CHECK(c.r.value.status.reason == "This video is unavailable");
}
TEST_CASE("a scheduled stream stops the walk and keeps its start time") {
    FakeHttp t; t.session().visitorData = "VD";
    t.queue("player", "{\"playabilityStatus\":{\"status\":\"LIVE_STREAM_OFFLINE\",\"liveStreamability\":"
        "{\"liveStreamabilityRenderer\":{\"offlineSlate\":{\"liveStreamOfflineSlateRenderer\":"
        "{\"scheduledStartTime\":\"1792000000\"}}}}}}");
    Cap<Player> c;
    fetchPlayer(t, {"v"}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(t.sent.size() == 1);
    CHECK(c.r.value.status.kind == Playability::Upcoming);
    CHECK(c.r.value.status.scheduledStartSecs == 1792000000LL);
}
TEST_CASE("signed in: TV is asked for SABR with sts, cpn and playbackContext") {
    FakeHttp t; t.session().visitorData = "VD"; t.session().bearer = "tok";
    t.session().playerId = "p1"; t.session().signatureTimestamp = 20737;
    t.queue("player", kOkEmpty);
    t.queue("player", kOkEmpty);
    t.queue("player", kTvSabr);
    Cap<Player> c;
    fetchPlayer(t, {"v", WantSabr}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(t.sentClients == ids({ClientId::VISIONOS, ClientId::ANDROID_VR, ClientId::TVHTML5}));
    CHECK(t.fetched.empty());                         // sts already known
    REQUIRE(t.sent.size() == 3);
    CHECK(has(t.sent[2], "\"signatureTimestamp\":20737"));
    CHECK(has(t.sent[2], ("\"cpn\":\"" + c.r.value.cpn + "\"").c_str()));
    CHECK(has(t.sent[2], "contentPlaybackContext"));
    REQUIRE(c.r.value.sources.size() == 1);
    CHECK(c.r.value.sources[0].client == ClientId::TVHTML5);
    CHECK(c.r.value.watchtimeUrl.find("c=TVHTML5") != std::string::npos);   // the account's view
}
// Review focus: a signed-in user whose needs VISIONOS covers pays neither for the TV
// request nor for the 3 MB base.js download.
TEST_CASE("signed in but covered: no TV request and no base.js") {
    FakeHttp t; t.session().visitorData = "VD"; t.session().bearer = "tok";
    t.queue("player", loadFixtureRaw("player_visionos_ok.json"));
    Cap<Player> c;
    fetchPlayer(t, {"v", WantAdaptive | WantSabr}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(t.sentClients == ids({ClientId::VISIONOS}));
    CHECK(t.fetched.empty());
}
TEST_CASE("sts is read from base.js once and persisted") {
    FakeHttp t; t.session().visitorData = "VD"; t.session().bearer = "tok";
    t.queue(Catalog::kTvPageUrl, "<script src=\"/s/player/22cb88bc/tv-player-ias.vflset/tv-player-ias.js\">");
    t.queue("https://www.youtube.com/s/player/22cb88bc/player_ias.vflset/en_US/base.js", "x={signatureTimestamp:20737}");
    t.queue("player", kOkEmpty);
    t.queue("player", kOkEmpty);
    t.queue("player", kTvSabr);
    Cap<Player> c;
    fetchPlayer(t, {"v", WantSabr}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    REQUIRE(t.fetched.size() == 2);
    CHECK(t.fetched[0].url == Catalog::kTvPageUrl);
    REQUIRE_FALSE(t.fetched[0].headers.empty());
    CHECK(t.fetched[0].headers[0].value == defaultClientInfo(ClientId::TVHTML5).userAgent);
    CHECK(t.playerJsSets == 1);
    CHECK(t.session().signatureTimestamp == 20737);
    CHECK(has(t.sent.back(), "\"signatureTimestamp\":20737"));
    // A second walk does not download anything again.
    t.queue("player", kOkEmpty);
    t.queue("player", kOkEmpty);
    t.queue("player", kTvSabr);
    Cap<Player> d;
    fetchPlayer(t, {"v", WantSabr}, newJob(), d.fn());
    t.flush();
    CHECK(t.fetched.size() == 2);
}
TEST_CASE("a TV UNPLAYABLE re-reads the player build once") {
    FakeHttp t; t.session().visitorData = "VD"; t.session().bearer = "tok";
    t.session().playerId = "old"; t.session().signatureTimestamp = 1;
    t.queue("player", kOkEmpty);
    t.queue("player", kOkEmpty);
    t.queue("player", kUnplayable);
    t.queue(Catalog::kTvPageUrl, "/s/player/new1/tv-player-ias.vflset/x.js");
    t.queue("https://www.youtube.com/s/player/new1/player_ias.vflset/en_US/base.js", "signatureTimestamp:2");
    t.queue("player", kTvSabr);
    Cap<Player> c;
    fetchPlayer(t, {"v", WantSabr}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(t.sentClients == ids({ClientId::VISIONOS, ClientId::ANDROID_VR, ClientId::TVHTML5, ClientId::TVHTML5}));
    CHECK(has(t.sent.back(), "\"signatureTimestamp\":2"));
    CHECK(t.session().playerId == "new1");
    REQUIRE(c.r.value.sources.size() == 1);
    CHECK(c.r.value.status.kind == Playability::Ok);
}
TEST_CASE("empty sources after a TV 401 report 401 so the bearer is refreshed") {
    FakeHttp t; t.session().visitorData = "VD"; t.session().bearer = "stale";
    t.session().playerId = "p1"; t.session().signatureTimestamp = 1;
    t.queue("player", kOkEmpty);
    t.queue("player", kOkEmpty);
    t.queue("player", "{}", 401);
    Cap<Player> c;
    fetchPlayer(t, {"v", WantSabr}, newJob(), c.fn());
    t.flush();
    REQUIRE_FALSE(c.r.ok());
    CHECK(c.r.error->httpStatus == 401);
}
TEST_CASE("no answer at all fails with the transport error") {
    FakeHttp t; t.session().visitorData = "VD";
    t.queue("player", "", 0);
    t.queue("player", "", 0);
    t.queue("player", "", 0);
    Cap<Player> c;
    fetchPlayer(t, {"v"}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.calls == 1);
    REQUIRE_FALSE(c.r.ok());
    CHECK(c.r.error->code == ErrorCode::Transport);
}
TEST_CASE("want 0 asks one client for metadata only") {
    FakeHttp t; t.session().visitorData = "VD";
    t.queue("player", loadFixtureRaw("player_visionos_ok.json"));
    Cap<Player> c;
    fetchPlayer(t, {"v", 0}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(t.sent.size() == 1);
    CHECK(c.r.value.captions.size() == 2);
}
TEST_CASE("a walk canceled between steps delivers nothing") {
    FakeHttp t; t.session().visitorData = "VD";
    t.queue("player", kOkEmpty);
    t.queue("player", kOkEmpty);
    JobToken job = newJob();
    Cap<Player> c;
    fetchPlayer(t, {"v"}, job, c.fn());
    t.flushOne(0);                                    // VISIONOS answers; ANDROID_VR goes out
    job->canceled = true;
    t.flush();
    CHECK(c.calls == 0);
    CHECK(t.sent.size() <= 2);
}
```

`tests/tst_client.cpp` — in "every endpoint reaches its chain" replace `c->player("v", [](const yt::PlayerInfo &) {});` with `c->player({"v"}, nop);`.

`tests/tst_parsers.cpp` — delete the old `TEST_CASE("streams")`, `"videoDetails"`, `"captions"` (the one on `player_ios.json`) and `"isPlayableStatus"`.

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build build -j`
Expected: compile errors (`bodies::player` has no `PlayerBody` overload; `fetchPlayer` takes `std::string`).

- [ ] **Step 3: Bodies** — `src/innertube/bodies.h`: replace the `player` declaration and its comment with

```cpp
// tv=true adds playbackContext.contentPlaybackContext (html5Preference, and the
// signatureTimestamp when known) and the playback nonce: TVHTML5 needs them — without
// them an authed TV /player answers "UNPLAYABLE: The page needs to be reloaded". The
// mobile/vision clients stay minimal.
struct PlayerBody { bool tv = false; std::string cpn; int signatureTimestamp = 0; };
std::string player(std::string_view videoId, const PlayerBody &b = {});
```

`src/innertube/bodies.cpp`: in `bj::ContentPlaybackContext` replace the two comment lines above `signatureTimestamp` with `// From base.js (innertube/playerjs.*); omitted while unknown.`; in `bj::Player` insert `std::optional<std::string> cpn;` between `racyCheckOk` and `playbackContext`; replace the `player` builder with:

```cpp
std::string player(std::string_view videoId, const PlayerBody &pb)
{
    bj::Player b;
    b.videoId = std::string(videoId);
    if (pb.tv) {
        b.cpn = pb.cpn;
        b.playbackContext = bj::PlaybackContext{};
        if (pb.signatureTimestamp > 0) b.playbackContext->contentPlaybackContext.signatureTimestamp = pb.signatureTimestamp;
    }
    return dump(b);
}
```

- [ ] **Step 4: The walk** — `src/core/chains.h`: add `#include <yt/player.h>` after `<yt/types.h>`; replace the `fetchPlayer` declaration with

```cpp
// The /player client walk (spec §2): WEB seeds visitorData when missing, then VISIONOS
// → ANDROID_VR → TVHTML5 (signed in) → IOS, each asked only while a kind in
// request.want is still missing. Every answer that carried a wanted kind becomes a
// Source. Fails only when no client produced a parseable answer (or, with empty
// sources, when TV got a 401 — the Client then refreshes the bearer and retries).
void fetchPlayer(IHttp &, PlayerRequest request, const JobToken &, Callback<Player> done);
```

`src/core/chains.cpp`: add includes `"innertube/playerjs.h"` (after `"innertube/clientconfig.h"`) and `<chrono>`, `<random>` (with `<memory>`); delete `struct PlayerAccum`, `playerTry` and the old `fetchPlayer`; in their place add:

```cpp
namespace {

// One /player walk (engine thread only).
struct PlayerWalk {
    PlayerRequest req;
    unsigned covered = 0;                // kinds already found
    std::vector<ClientId> steps;         // clients still to ask, front first
    bool seeded = false;                 // asked WEB for a visitorData in this walk
    bool botRetried = false;             // one visitorData reseed per walk
    bool jsTried = false;                // one lazy base.js read per walk
    bool jsReloaded = false;             // one re-read after a TV UNPLAYABLE
    bool anyParsed = false;
    bool sawUnauthorized = false;
    bool metadataOnly = false;           // want == 0: stop after the first answer
    Error lastError{ErrorCode::Transport, "no player response", 0};
    int bestRank = -1;
    Player out;
    Callback<Player> done;
};

std::string newCpn()
{
    static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    // Per engine thread: several Clients may run walks at once.
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::string s(16, 'A');
    for (char &c : s) c = kAlphabet[rng() % 64];
    return s;
}

int rankOf(Playability k)
{
    switch (k) {
    case Playability::Ok: return 5;
    case Playability::Upcoming: case Playability::LiveOffline: return 4;
    case Playability::Unplayable: case Playability::AgeRestricted: case Playability::LoginRequired: return 3;
    case Playability::BotCheck: return 2;
    case Playability::Unknown: return 1;
    }
    return 0;
}

// The kinds a parsed answer covers — decided on URLs, never on the status: an OK answer
// may carry formats without a single URL (SABR-only).
unsigned kindsOf(const PlayerPart &p)
{
    unsigned k = 0;
    bool video = false, audio = false;
    for (const Format &f : p.formats) {
        if (f.url.empty()) continue;
        if (f.hasVideo && f.hasAudio) k |= WantProgressive;
        else if (f.hasVideo) video = true;
        else if (f.hasAudio) audio = true;
    }
    if (video && audio) k |= WantAdaptive;
    if (!p.hlsUrl.empty()) k |= WantHls;
    if (!p.sabrUrl.empty()) k |= WantSabr;
    return k;
}

// Whether asking `c` can still pay off for the `missing` kinds.
bool worth(ClientId c, unsigned missing)
{
    if (c == ClientId::TVHTML5) return (missing & (WantSabr | WantAdaptive)) != 0;
    if (c == ClientId::IOS) return (missing & WantHls) != 0;
    return missing != 0;
}

bool needsVisitor(ClientId c) { return c == ClientId::VISIONOS || c == ClientId::ANDROID_VR; }

void merge(PlayerWalk &w, IHttp &http, ClientId c, PlayerPart &p)
{
    const unsigned kinds = kindsOf(p) & w.req.want;
    if (kinds) {
        Source s;
        s.client = c;
        s.userAgent = http.session().clients[(int) c].userAgent;
        s.formats = std::move(p.formats);
        s.hlsUrl = p.hlsUrl;
        s.dashUrl = p.dashUrl;
        s.sabrUrl = p.sabrUrl;
        s.sabrConfig = p.sabrConfig;
        if (p.expiresInSecs > 0) {
            const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            s.expiresAtSecs = now + p.expiresInSecs;
        }
        w.out.sources.push_back(std::move(s));
        w.covered |= kinds;
    }
    const int rank = rankOf(p.status.kind);
    if (rank > w.bestRank) { w.bestRank = rank; w.out.status = p.status; }
    if (w.out.details.id.empty() && !p.details.id.empty()) w.out.details = p.details;
    // WEB caption URLs need a PO token (they answer empty without one).
    if (w.out.captions.empty() && c != ClientId::WEB && !p.captions.empty()) w.out.captions = p.captions;
    if (w.out.storyboard.empty() && !p.storyboardSpec.empty())
        w.out.storyboard = parseStoryboardSpec(p.storyboardSpec, p.details.lengthSecs);
    // Prefer the TV (signed-in) videostats URLs: the view is attributed to the account.
    if (!p.watchtimeUrl.empty() && (w.out.watchtimeUrl.empty() || c == ClientId::TVHTML5)) {
        w.out.watchtimeUrl = p.watchtimeUrl;
        w.out.playbackUrl = p.playbackUrl;
    }
}

void finishWalk(const std::shared_ptr<PlayerWalk> &w)
{
    Result<Player> r;
    if (!w->anyParsed) {
        r.error = w->lastError;
        if (w->sawUnauthorized) r.error->httpStatus = 401;
    } else if (w->sawUnauthorized && w->out.sources.empty()) {
        r.error = Error{ErrorCode::AuthRequired, "player: bearer rejected", 401};
    } else {
        r.value = std::move(w->out);
    }
    w->done(r);
}

// Reads the live player build and its signatureTimestamp (/tv page, then base.js) unless
// they are known and `force` is false; runs `then` afterwards whatever happened — the TV
// request goes out without sts rather than not at all.
void ensurePlayerJs(IHttp &http, const JobToken &job, bool force, std::function<void()> then)
{
    if (!force && http.session().signatureTimestamp > 0) { then(); return; }
    IHttp *h = &http;
    std::vector<Header> ua = {{"User-Agent", http.session().clients[(int) ClientId::TVHTML5].userAgent}};
    http.fetch(Catalog::kTvPageUrl, std::move(ua), job, [h, job, then](const Reply &r) {
        if (!live(job)) return;
        const std::string id = r.ok ? playerIdIn(*r.body) : std::string();
        if (id.empty() || (id == h->session().playerId && h->session().signatureTimestamp > 0)) { then(); return; }
        h->fetch(playerScriptUrl(id), {}, job, [h, job, id, then](const Reply &js) {
            if (!live(job)) return;
            const int sts = js.ok ? signatureTimestampIn(*js.body) : 0;
            if (sts > 0) h->setPlayerJs(id, sts);
            then();
        });
    });
}

void playerStep(IHttp &http, std::shared_ptr<PlayerWalk> w, JobToken job);

// Step 0: one anonymous WEB /player, only for the visitorData its responseContext
// carries (HttpClient adopts it); its answer is never a source.
void seedVisitor(IHttp &http, std::shared_ptr<PlayerWalk> w, JobToken job)
{
    w->seeded = true;
    IHttp *h = &http;
    http.post("player", ClientId::WEB, bodies::player(w->req.videoId), job, [h, w, job](const Reply &r) {
        if (r.ok) {
            w->anyParsed = true;
            PlayerPart p = parsePlayerResponse(*r.body);
            // WEB formats are n-protected or SABR-only, and WEB answers UNPLAYABLE for
            // ordinary videos (live-verified): neither a source nor a verdict.
            p.formats.clear(); p.hlsUrl.clear(); p.sabrUrl.clear(); p.dashUrl.clear();
            p.status = PlayabilityStatus{};
            merge(*w, *h, ClientId::WEB, p);
        }
        if (!live(job)) return;
        playerStep(*h, w, job);
    });
}

void playerStep(IHttp &http, std::shared_ptr<PlayerWalk> w, JobToken job)
{
    const unsigned missing = w->req.want & ~w->covered;
    while (!w->steps.empty() && !worth(w->steps.front(), missing)) w->steps.erase(w->steps.begin());
    if (w->steps.empty()) { finishWalk(w); return; }
    const ClientId c = w->steps.front();
    if (needsVisitor(c) && http.session().visitorData.empty() && !w->seeded) { seedVisitor(http, w, job); return; }
    if (c == ClientId::TVHTML5 && !w->jsTried) {
        w->jsTried = true;
        IHttp *h = &http;
        ensurePlayerJs(http, job, false, [h, w, job]() { playerStep(*h, w, job); });
        return;
    }
    w->steps.erase(w->steps.begin());
    bodies::PlayerBody body;
    if (c == ClientId::TVHTML5) {
        body.tv = true;
        body.cpn = w->out.cpn;
        body.signatureTimestamp = http.session().signatureTimestamp;
    }
    IHttp *h = &http;
    http.post("player", c, bodies::player(w->req.videoId, body), job, [h, w, job, c](const Reply &r) {
        if (!r.ok) {
            w->lastError = errorOf(r);
            if (r.httpStatus == 401) w->sawUnauthorized = true;
            YTLOG(h->log(), "player") << "player" << h->session().clients[(int) c].name << "failed:" << r.error;
            if (live(job)) playerStep(*h, w, job);
            return;
        }
        w->anyParsed = true;
        PlayerPart p = parsePlayerResponse(*r.body);
        YTLOG(h->log(), "player") << "player" << h->session().clients[(int) c].name << p.status.status
            << "formats=" << p.formats.size() << "kinds=" << kindsOf(p) << "hls=" << !p.hlsUrl.empty()
            << "sabr=" << !p.sabrUrl.empty();
        // TV carries the bearer: LOGIN_REQUIRED there is about the account, not a bot wall.
        if (c == ClientId::TVHTML5 && p.status.kind == Playability::BotCheck) p.status.kind = Playability::LoginRequired;
        if (needsVisitor(c) && p.status.kind == Playability::BotCheck && !w->botRetried) {
            // A flagged visitorData: drop it, reseed, and ask the same client again.
            w->botRetried = true;
            w->seeded = false;
            h->resetVisitorData();
            w->steps.insert(w->steps.begin(), c);
            merge(*w, *h, c, p);
            if (live(job)) playerStep(*h, w, job);
            return;
        }
        if (c == ClientId::TVHTML5 && p.status.kind == Playability::Unplayable && !w->jsReloaded
            && !h->session().playerId.empty()) {
            // "The page needs to be reloaded" (localized): YouTube may have rotated the player
            // build under us — re-read it and, if it changed, ask TV once more.
            w->jsReloaded = true;
            const std::string before = h->session().playerId;
            ensurePlayerJs(*h, job, true, [h, w, job, before]() {
                if (h->session().playerId != before) w->steps.insert(w->steps.begin(), ClientId::TVHTML5);
                playerStep(*h, w, job);
            });
            return;
        }
        merge(*w, *h, c, p);
        if (p.final || w->metadataOnly) w->steps.clear();
        if (live(job)) playerStep(*h, w, job);
    });
}

} // namespace

void fetchPlayer(IHttp &http, PlayerRequest request, const JobToken &job, Callback<Player> done)
{
    auto w = std::make_shared<PlayerWalk>();
    w->req = std::move(request);
    w->done = std::move(done);
    w->out.cpn = newCpn();
    if (w->req.want == 0) { w->metadataOnly = true; w->req.want = WantAll; }
    w->steps = {ClientId::VISIONOS, ClientId::ANDROID_VR};
    if (!http.session().bearer.empty()) w->steps.push_back(ClientId::TVHTML5);
    w->steps.push_back(ClientId::IOS);
    playerStep(http, w, job);
}
```

`playerStep` is called before the anonymous namespace closes and `fetchPlayer` is outside it — keep `fetchPlayer` after `} // namespace`. `errorOf`, `str` and `unavailable` live in the earlier anonymous namespace of the same file and are visible here.

- [ ] **Step 5: Client and public types**

`include/yt/client.h`: add `#include <yt/player.h>` after `#include <yt/log.h>`; replace the `player` declaration with

```cpp
    // Every source YouTube serves for the video (see yt/player.h); request.want limits the
    // walk to the kinds the consumer can play.
    Job player(PlayerRequest request, Callback<Player> done);
```

`src/client.cpp`: delete `long httpStatusOf(const PlayerInfo &p) {…}`; replace `Client::player` with

```cpp
Job Client::player(PlayerRequest req, Callback<Player> done)
{
    return m_engine->start<Result<Player>>([req](core::IHttp &h, const core::JobToken &t, Callback<Player> d) {
        core::fetchPlayer(h, req, t, std::move(d));
    }, std::move(done));
}
```

`include/yt/types.h`: delete the `Stream` struct with its comment, the `Subtitle` struct, the `PlayerInfo` struct with its comment.

`src/parsers/playerparser.h/.cpp`: delete `parseStreams`, `parseVideoDetails`, `parseCaptions`, `isPlayable`, `PlayerResult`, `parsePlayer` (both overloads), the static helpers only they used (`playableOf`, `streamOf`, `streamsOf`, `detailsOf`, `captionsOf`, `parsePlayerRoot`) and `namespace pj`. Update the header comment of `playerparser.h` to describe `parsePlayerResponse` / `parseStoryboardSpec` / `parseJson3`. Then:

Run: `grep -rnE "PlayerInfo|Subtitle\b|yt::Stream|parseStreams|parseCaptions|isPlayable|parseVideoDetails|parsePlayer\(|PlayerResult" src include tests examples`
Expected: no output. Also `grep -rn "player_ios.json\|player_streams.json\|kPlayableOk\|kPlayableLogin" tests` — delete the fixtures / payload constants that no longer have a user.

- [ ] **Step 6: yt-cli** — `examples/yt-cli/main.cpp`: extend `SavedState` and its load/save to carry the player build:

```cpp
struct SavedState { std::string hl = "en", gl = "US", visitorData, refreshToken, playerId; int signatureTimestamp = 0; };
```

In `load()` add `s.playerId = saved.playerId; s.signatureTimestamp = saved.signatureTimestamp;`; in `save()` build `SavedState out{s.hl, s.gl, s.visitorData, s.refreshToken, s.playerId, s.signatureTimestamp};`. Replace the `streams` branch with:

```cpp
    } else if (cmd == "streams") {
        client.player({arg}, [&](const yt::Result<yt::Player> &r) {
            if (!r.ok()) { rc = fail(*r.error); mq.finish(); return; }
            const yt::Player &p = r.value;
            std::printf("%s — %s [%s%s%s]\n", p.details.title.c_str(), p.details.author.c_str(), p.status.status.c_str(),
                        p.status.reason.empty() ? "" : ": ", p.status.reason.c_str());
            for (const yt::Source &s : p.sources) {
                std::printf("source %s  hls=%d sabr=%d  UA %s\n", yt::defaultClientInfo(s.client).name.c_str(),
                            (int) !s.hlsUrl.empty(), (int) !s.sabrUrl.empty(), s.userAgent.c_str());
                for (const yt::Format &f : s.formats)
                    std::printf("  %-4d %-3s %-8s %-28s %-8s %s\n", f.itag,
                                f.hasVideo && f.hasAudio ? "av" : f.hasVideo ? "v" : "a",
                                f.qualityLabel.c_str(), f.codecs.c_str(),
                                f.audio ? f.audio->id.c_str() : "", f.url.empty() ? "(no url)" : f.url.c_str());
            }
            for (const yt::CaptionTrack &c : p.captions)
                std::printf("caption %s %s%s\n", c.language.c_str(), c.name.c_str(), c.autoGenerated ? " (asr)" : "");
            std::printf("storyboard levels: %zu\n", p.storyboard.size());
            mq.finish();
        });
```

- [ ] **Step 7: Run all tests**

Run: `cmake --build build -j 2>&1 | grep -E "warning|error"; (cd build && ctest -j8 --output-on-failure)`
Expected: no warnings; all suites PASS.

- [ ] **Step 8: Commit**

```bash
git add -A src include tests examples
git commit -m "lib: /player client walk — every source YouTube gives, tagged by client" -m "VISIONOS first (ready adaptive URLs, HLS, SABR; not cut at ~60 s), ANDROID_VR for
progressive, TVHTML5 with signatureTimestamp and cpn for SABR when signed in, IOS for HLS.
A flagged visitorData is reseeded once; a rotated player build is re-read once. Player
replaces PlayerInfo/Stream/Subtitle; nothing-to-play is a verdict, not an error." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: `Client::captions`

**Files:**
- Modify: `src/core/chains.h`, `src/core/chains.cpp`
- Modify: `include/yt/client.h`, `src/client.cpp`
- Modify: `examples/yt-cli/main.cpp`
- Test: `tests/tst_chains.cpp`, `tests/tst_client.cpp`

**Interfaces:**
- Produces: `std::string core::json3Url(std::string url);` and `void core::fetchCaptions(IHttp &, CaptionTrack, const JobToken &, Callback<std::vector<Cue>> done);`; `Job Client::captions(CaptionTrack track, Callback<std::vector<Cue>> done);`

- [ ] **Step 1: Write the failing tests** — append to `tests/tst_chains.cpp`:

```cpp
// Review focus: fmt=json3 is set whatever shape the server's URL has; the rest stays byte-exact.
TEST_CASE("json3Url") {
    CHECK(json3Url("https://y/api/timedtext?v=a&lang=en") == "https://y/api/timedtext?v=a&lang=en&fmt=json3");
    CHECK(json3Url("https://y/api/timedtext?v=a&fmt=srv3&lang=en") == "https://y/api/timedtext?v=a&fmt=json3&lang=en");
    CHECK(json3Url("https://y/api/timedtext?fmt=vtt") == "https://y/api/timedtext?fmt=json3");
    CHECK(json3Url("https://y/api/timedtext") == "https://y/api/timedtext?fmt=json3");
    CHECK(json3Url("https://y/t?v=a&xfmt=1&sig=%2F") == "https://y/t?v=a&xfmt=1&sig=%2F&fmt=json3");
}
TEST_CASE("captions fetch json3 cues") {
    FakeHttp t;
    const std::string url = "https://www.youtube.com/api/timedtext?v=dQw4w9WgXcQ&lang=en";
    t.queue(url + "&fmt=json3", loadFixtureRaw("captions_manual.json3"));
    CaptionTrack track; track.url = url; track.language = "en";
    Cap<std::vector<Cue>> c;
    fetchCaptions(t, track, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(c.r.value.size() == 3);
    REQUIRE(t.fetched.size() == 1);
    CHECK(t.fetched[0].url == url + "&fmt=json3");
}
TEST_CASE("captions: an empty 200 is no cues, a 429 is an error") {
    FakeHttp t;
    t.queue("https://y/t?fmt=json3", "");
    t.queue("https://y/u?fmt=json3", "<html>Sorry</html>", 429);
    CaptionTrack a; a.url = "https://y/t";
    CaptionTrack b; b.url = "https://y/u";
    Cap<std::vector<Cue>> ca, cb;
    fetchCaptions(t, a, newJob(), ca.fn());
    fetchCaptions(t, b, newJob(), cb.fn());
    t.flush();
    REQUIRE(ca.r.ok());
    CHECK(ca.r.value.empty());
    REQUIRE_FALSE(cb.r.ok());
    CHECK(cb.r.error->httpStatus == 429);
}
```

`tests/tst_client.cpp` — in "every endpoint reaches its chain" add `c->captions(yt::CaptionTrack{"https://y/t", "en", "English"}, nop);` next to the `player` call, and after the existing loop add `CHECK_FALSE(f->fetched.empty());`.

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build build -j`
Expected: `'json3Url' was not declared in this scope`.

- [ ] **Step 3: Implement** — `src/core/chains.h`, after `fetchPlayer`:

```cpp
// The caption URL with fmt=json3 set (an existing fmt value replaced, else appended);
// every other byte stays as the server issued it.
std::string json3Url(std::string url);
// One caption track as cues: a raw GET of json3Url(track.url). An empty 200 is no cues.
void fetchCaptions(IHttp &, CaptionTrack track, const JobToken &, Callback<std::vector<Cue>> done);
```

`src/core/chains.cpp`, after `fetchPlayer`:

```cpp
std::string json3Url(std::string url)
{
    const size_t q = url.find('?');
    if (q == std::string::npos) return url + "?fmt=json3";
    for (size_t p = q; p != std::string::npos; p = url.find('&', p + 1)) {
        if (url.compare(p + 1, 4, "fmt=") != 0) continue;
        const size_t v = p + 5, end = url.find('&', v);
        url.replace(v, (end == std::string::npos ? url.size() : end) - v, "json3");
        return url;
    }
    return url + "&fmt=json3";
}

void fetchCaptions(IHttp &http, CaptionTrack track, const JobToken &job, Callback<std::vector<Cue>> done)
{
    http.fetch(json3Url(std::move(track.url)), {}, job, [done](const Reply &r) {
        Result<std::vector<Cue>> out;
        if (!r.ok) { out.error = errorOf(r); done(out); return; }
        out.value = parseJson3(*r.body);
        done(out);
    });
}
```

`include/yt/client.h`, after `player`:

```cpp
    // One caption track (from Player::captions) as timed cues.
    Job captions(CaptionTrack track, Callback<std::vector<Cue>> done);
```

`src/client.cpp`, after `Client::player`:

```cpp
Job Client::captions(CaptionTrack track, Callback<std::vector<Cue>> done)
{
    return m_engine->start<Result<std::vector<Cue>>>(
        [track](core::IHttp &h, const core::JobToken &t, Callback<std::vector<Cue>> d) {
            core::fetchCaptions(h, track, t, std::move(d));
        }, std::move(done));
}
```

`examples/yt-cli/main.cpp`: add `//   yt-cli captions <videoId> [lang]` to the usage comment and the usage string, and a branch before `signin`:

```cpp
    } else if (cmd == "captions") {
        const std::string lang = argc > 3 ? argv[3] : "";
        client.player({arg, 0}, [&](const yt::Result<yt::Player> &r) {
            if (!r.ok() || r.value.captions.empty()) {
                if (r.ok()) std::printf("no captions\n"); else rc = fail(*r.error);
                mq.finish();
                return;
            }
            yt::CaptionTrack pick = r.value.captions.front();
            for (const yt::CaptionTrack &c : r.value.captions) if (c.language == lang) { pick = c; break; }
            client.captions(pick, [&](const yt::Result<std::vector<yt::Cue>> &cues) {
                if (!cues.ok()) rc = fail(*cues.error);
                else for (size_t i = 0; i < cues.value.size() && i < 8; ++i)
                    std::printf("%7.2f %s\n", cues.value[i].startMs / 1000.0, cues.value[i].text.c_str());
                mq.finish();
            });
        });
```

(`want = 0` is the walk's metadata-only mode from Task 7: one answer, its captions.)

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build -j && (cd build && ctest -j8 --output-on-failure -R 'tst_chains|tst_client')`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/chains.h src/core/chains.cpp include/yt/client.h src/client.cpp examples/yt-cli/main.cpp tests/tst_chains.cpp tests/tst_client.cpp
git commit -m "lib: captions as timed cues (json3)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: QR sign-in

**Files:**
- Modify: `include/yt/auth.h` (`DeviceCode`)
- Modify: `src/innertube/catalog.h` (QR preset), `src/innertube/bodies.h/.cpp` (`mdxHandoff`)
- Modify: `src/core/chains.h/.cpp` (`fetchSignInQr`)
- Modify: `src/auth.cpp` (`startSignIn`)
- Modify: `examples/yt-cli/main.cpp` (`signin` writes `qr.png`)
- Test: `tests/tst_bodies.cpp`, `tests/tst_chains.cpp`, `tests/tst_auth.cpp`; fixture `tests/fixtures/mdx_handoff.json`

**Interfaces:**
- Produces: `DeviceCode{verificationUrl, userCode, intervalSecs = 5, expiresInSecs = 0, qrPng}`; `std::string bodies::mdxHandoff(std::string_view userCode);`; `void core::fetchSignInQr(IHttp &, std::string userCode, const JobToken &, Callback<std::string> done);` (PNG bytes).

- [ ] **Step 1: Fixture** — `tests/fixtures/mdx_handoff.json` (live answer 2026-10-14, PNG replaced by a 1×1 PNG, tokens scrubbed):

```json
{"responseContext": {}, "rapidQrRenderer": {"qrCodeRenderer": {"qrCodeImage": {"thumbnails": [{"url": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAAAAAA6fptVAAAACklEQVR4nGNgAAAAAgABSK+kcQAAAABJRU5ErkJggg=="}]}, "style": "QR_CODE_RENDERER_STYLE_TWO_COLUMN", "trackingParams": "TP"}}}
```

- [ ] **Step 2: Write the failing tests**

`tests/tst_bodies.cpp`, new case:

```cpp
TEST_CASE("mdx handoff (QR)") {
    CHECK(bodies::mdxHandoff("ABC-DEF-GHI") ==
          "{\"handoffQrParams\":{\"rapidQrParams\":{\"qrPresetStyle\":"
          "\"HANDOFF_QR_LIMITED_PRESET_STYLE_MODERN_BIG_DOTS_INVERT_WITH_YT_LOGO\","
          "\"userCode\":\"ABC-DEF-GHI\",\"rapidQrFeature\":\"RAPID_QR_FEATURE_DEFAULT\"}}}");
}
```

`tests/tst_chains.cpp`:

```cpp
TEST_CASE("sign-in QR from a data URL") {
    FakeHttp t; t.queue("mdx/handoff", loadFixtureRaw("mdx_handoff.json"));
    Cap<std::string> c;
    fetchSignInQr(t, "U-1", newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(c.r.value.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0);
    CHECK(t.lastClientFor("mdx/handoff") == (int) ClientId::TVHTML5);
    CHECK(has(t.sent[0], "\"userCode\":\"U-1\""));
}
TEST_CASE("sign-in QR from an image URL") {
    FakeHttp t;
    t.queue("mdx/handoff", "{\"rapidQrRenderer\":{\"qrCodeRenderer\":{\"qrCodeImage\":{\"thumbnails\":"
                           "[{\"url\":\"https://i.ytimg.com/qr.png\"}]}}}}");
    t.queue("https://i.ytimg.com/qr.png", std::string("\x89PNG\r\n\x1a\nrest", 12));
    Cap<std::string> c;
    fetchSignInQr(t, "U", newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(c.r.value.size() == 12);
}
// Review focus: a broken answer must never hand garbage to an image decoder.
TEST_CASE("sign-in QR: garbage is refused") {
    FakeHttp t;
    t.queue("mdx/handoff", "{\"rapidQrRenderer\":{\"qrCodeRenderer\":{\"qrCodeImage\":{\"thumbnails\":"
                           "[{\"url\":\"data:image/png;base64,not*base64\"}]}}}}");
    t.queue("mdx/handoff", "{\"rapidQrRenderer\":{\"qrCodeRenderer\":{\"qrCodeImage\":{\"thumbnails\":"
                           "[{\"url\":\"data:image/png;base64,aGVsbG8=\"}]}}}}");   // valid base64, not a PNG
    t.queue("mdx/handoff", "{}");
    Cap<std::string> a, b, c;
    fetchSignInQr(t, "U", newJob(), a.fn());
    fetchSignInQr(t, "U", newJob(), b.fn());
    fetchSignInQr(t, "U", newJob(), c.fn());
    t.flush();
    CHECK_FALSE(a.r.ok());
    CHECK(a.r.value.empty());
    CHECK_FALSE(b.r.ok());
    CHECK(b.r.value.empty());
    CHECK_FALSE(c.r.ok());
}
```

`tests/tst_auth.cpp` (add `#include "testutil.h"` to the includes):

```cpp
TEST_CASE("device flow delivers YouTube's QR image with the code") {
    Rig r;
    r.f->queue(yt::Catalog::kDeviceCodeUrl,
               "{\"device_code\":\"D\",\"user_code\":\"U\",\"verification_url\":\"V\",\"interval\":1,\"expires_in\":1800}");
    r.f->queue("mdx/handoff", loadFixtureRaw("mdx_handoff.json"));
    r.f->queue(yt::Catalog::kTokenUrl, "{\"access_token\":\"A\",\"refresh_token\":\"R\",\"expires_in\":3600}");
    yt::DeviceCode code; int codes = 0; bool done = false;
    r.c->auth().signIn([&](const yt::DeviceCode &dc) { code = dc; ++codes; },
                       [&](const yt::Result<yt::Done> &) { done = true; });
    r.pumpFor(5000, [&] { return done; });
    REQUIRE(done);
    CHECK(codes == 1);
    CHECK(code.userCode == "U");
    CHECK(code.expiresInSecs == 1800);
    CHECK(code.qrPng.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0);
}
TEST_CASE("a failed QR request does not stop the sign-in") {
    Rig r;
    r.f->queue(yt::Catalog::kDeviceCodeUrl, "{\"device_code\":\"D\",\"user_code\":\"U\",\"verification_url\":\"V\",\"interval\":1}");
    r.f->queue("mdx/handoff", "{}", 500);
    r.f->queue(yt::Catalog::kTokenUrl, "{\"access_token\":\"A\",\"refresh_token\":\"R\"}");
    yt::DeviceCode code; int codes = 0; bool done = false; yt::Result<yt::Done> res;
    r.c->auth().signIn([&](const yt::DeviceCode &dc) { code = dc; ++codes; },
                       [&](const yt::Result<yt::Done> &x) { res = x; done = true; });
    r.pumpFor(5000, [&] { return done; });
    REQUIRE(done);
    CHECK(res.ok());
    CHECK(codes == 1);
    CHECK(code.qrPng.empty());
}
```

- [ ] **Step 3: Run them to verify they fail**

Run: `cmake --build build -j`
Expected: `'mdxHandoff' is not a member of 'yt::bodies'`.

- [ ] **Step 4: Implement**

`include/yt/auth.h` — replace `DeviceCode`:

```cpp
struct DeviceCode {
    std::string verificationUrl, userCode;
    int intervalSecs = 5;
    int expiresInSecs = 0;                 // the code's lifetime (0 = not reported)
    std::string qrPng;                     // YouTube's own QR (PNG bytes); empty if it could not be had
};
```

and in the `signIn` comment replace "(show it, and/or a QR of the URL)" with "(show the code and/or the QR image; reported once, after the QR request)".

`src/innertube/catalog.h`, after `kTvPageUrl`:

```cpp
    // The QR the TV app shows next to the code (mdx/handoff, youtube_uwp TvSignInClient).
    static const char *const kQrPresetStyle = "HANDOFF_QR_LIMITED_PRESET_STYLE_MODERN_BIG_DOTS_INVERT_WITH_YT_LOGO";
```

`src/innertube/bodies.h`, after `editPlaylist`:

```cpp
// {handoffQrParams:{rapidQrParams:{qrPresetStyle, userCode, rapidQrFeature}}} — the TV
// sign-in QR for a device code (mdx/handoff).
std::string mdxHandoff(std::string_view userCode);
```

`src/innertube/bodies.cpp`: add `#include "innertube/catalog.h"` after `"bodies.h"`; in `namespace bj`:

```cpp
struct RapidQr {
    std::string qrPresetStyle = Catalog::kQrPresetStyle;
    std::string userCode;
    std::string rapidQrFeature = "RAPID_QR_FEATURE_DEFAULT";
};
struct HandoffQr {
    RapidQr rapidQrParams;
};
struct Mdx {
    HandoffQr handoffQrParams;
};
```

and the builder:

```cpp
std::string mdxHandoff(std::string_view userCode)
{
    bj::Mdx b;
    b.handoffQrParams.rapidQrParams.userCode = std::string(userCode);
    return dump(b);
}
```

`src/core/chains.h`, after `oauthRefresh`:

```cpp
// YouTube's QR image for a device code (mdx/handoff as TVHTML5): the PNG bytes from its
// data: URL, or from one raw GET of a plain https URL. Anything that is not a PNG fails.
void fetchSignInQr(IHttp &, std::string userCode, const JobToken &, Callback<std::string> done);
```

`src/core/chains.cpp`: in `namespace oj` add

```cpp
struct QrThumb { std::optional<std::string> url; };
struct QrImage { std::optional<std::vector<QrThumb>> thumbnails; };
struct QrCode { std::optional<QrImage> qrCodeImage; };
struct RapidQr { std::optional<QrCode> qrCodeRenderer; };
struct Handoff { std::optional<RapidQr> rapidQrRenderer; };
```

in the first anonymous namespace:

```cpp
bool isPng(const std::string &b) { return b.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0; }
```

and after `oauthRefresh`:

```cpp
void fetchSignInQr(IHttp &http, std::string userCode, const JobToken &job, Callback<std::string> done)
{
    IHttp *h = &http;
    http.post("mdx/handoff", ClientId::TVHTML5, bodies::mdxHandoff(userCode), job, [h, job, done](const Reply &r) {
        Result<std::string> out;
        if (!r.ok) { out.error = errorOf(r); done(out); return; }
        oj::Handoff hf{};
        gj::readJsonDoc(hf, *r.body);
        std::string url;
        if (hf.rapidQrRenderer && hf.rapidQrRenderer->qrCodeRenderer && hf.rapidQrRenderer->qrCodeRenderer->qrCodeImage
            && hf.rapidQrRenderer->qrCodeRenderer->qrCodeImage->thumbnails
            && !hf.rapidQrRenderer->qrCodeRenderer->qrCodeImage->thumbnails->empty())
            url = str(hf.rapidQrRenderer->qrCodeRenderer->qrCodeImage->thumbnails->front().url);
        static const std::string_view kData = "data:image/png;base64,";
        if (url.compare(0, kData.size(), kData) == 0) {
            if (!gj::base64Decode(std::string_view(url).substr(kData.size()), &out.value) || !isPng(out.value)) {
                out.value.clear();
                out.error = unavailable("sign-in QR is not a PNG");
            }
            done(out);
            return;
        }
        if (url.compare(0, 8, "https://") == 0) {
            if (!live(job)) return;
            h->fetch(url, {}, job, [done](const Reply &img) {
                Result<std::string> o;
                if (!img.ok) o.error = errorOf(img);
                else if (!isPng(*img.body)) o.error = unavailable("sign-in QR is not a PNG");
                else o.value = *img.body;
                done(o);
            });
            return;
        }
        out.error = unavailable("no sign-in QR in the answer");
        done(out);
    });
}
```

`src/auth.cpp`, in `startSignIn`, replace the two lines from `DeviceCode dc{...}` through the `pollTimer = loop.after(...)` line with:

```cpp
        DeviceCode dc;
        dc.verificationUrl = r.value.verificationUrl;
        dc.userCode = r.value.userCode;
        dc.intervalSecs = pollIntervalSecs;
        dc.expiresInSecs = r.value.expiresInSecs;
        // Polling starts now: the QR request must not eat into the code's lifetime.
        pollTimer = loop.after(pollIntervalSecs * 1000, [this, h, onDone]() { pollTimer = 0; pollToken(h, onDone); });
        core::fetchSignInQr(*http, dc.userCode, h->token, [this, h, onCode, dc](const Result<std::string> &qr) {
            if (signInJob != h) return;              // finished or superseded meanwhile
            DeviceCode withQr = dc;
            if (qr.ok()) withQr.qrPng = qr.value;
            deliver(h, [onCode, withQr]() { onCode(withQr); }, /*final*/ false);
        });
```

`examples/yt-cli/main.cpp` — in the `signin` branch, inside the `onCode` lambda after the `printf`:

```cpp
                if (!dc.qrPng.empty()) {
                    std::ofstream("qr.png", std::ios::binary) << dc.qrPng;
                    std::printf("or scan ./qr.png\n");
                }
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build -j && (cd build && ctest -j8 --output-on-failure -R 'tst_bodies|tst_chains|tst_auth')`
Expected: PASS (the older sign-in tests still pass: their `mdx/handoff` post fails for lack of a fixture and `onCode` gets an empty `qrPng`).

- [ ] **Step 6: Commit**

```bash
git add include/yt/auth.h src/innertube/catalog.h src/innertube/bodies.h src/innertube/bodies.cpp src/core/chains.h src/core/chains.cpp src/auth.cpp examples/yt-cli/main.cpp tests/tst_bodies.cpp tests/tst_chains.cpp tests/tst_auth.cpp tests/fixtures/mdx_handoff.json
git commit -m "lib: sign-in QR from YouTube (mdx/handoff)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: a revoked refresh token signs the client out

**Files:**
- Modify: `src/engine.h` (`revokedGen`)
- Modify: `src/auth.cpp` (`refreshBearer`)
- Modify: `src/client.cpp` (`Engine::start`)
- Test: `tests/tst_auth.cpp`

**Interfaces:**
- Consumes: every `Engine::start<Arg>` caller passes `Arg = Result<T>` (true since Task 7 removed `PlayerInfo`).

- [ ] **Step 1: Write the failing test** — append to `tests/tst_auth.cpp`:

```cpp
// A revoked refresh token (invalid_grant) stayed installed: every authed call refreshed
// again, failed the same way, and the client never reported itself signed out.
TEST_CASE("invalid_grant on refresh signs out and fails the waiting call") {
    Rig r("R");
    r.f->queue(yt::Catalog::kTokenUrl, "{\"error\":\"invalid_grant\",\"error_description\":\"Token has been expired or revoked.\"}", 400);
    bool called = false; yt::Result<yt::VideoPage> got;
    r.c->browse({"FEsubscriptions", "", ""}, [&](const yt::Result<yt::VideoPage> &x) { got = x; called = true; });
    r.pump();
    REQUIRE(called);
    REQUIRE_FALSE(got.ok());
    CHECK(got.error->code == yt::ErrorCode::AuthRequired);
    CHECK(r.f->sent.empty());                         // the browse never went out
    CHECK_FALSE(r.c->auth().signedIn());
    REQUIRE_FALSE(r.states.empty());
    CHECK(r.states.back().refreshToken.empty());
    // The next call runs signed out instead of refreshing a dead token again.
    r.f->queue("browse", "{}");
    r.c->browse({"FEwhat_to_watch", "", ""}, [](const yt::Result<yt::VideoPage> &) {});
    r.pump();
    CHECK(r.f->sentForm.size() == 1);
    CHECK(r.f->sent.size() == 1);
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build build -j && (cd build && ctest --output-on-failure -R tst_auth)`
Expected: FAIL — `got.error->code` is `Transport` ("no fixture queued for browse"), `signedIn()` is still true.

- [ ] **Step 3: Implement**

`src/engine.h`, after `uint64_t bearerGen = 0; …`:

```cpp
    uint64_t revokedGen = 0;                         // authGen right after an invalid_grant sign-out
```

`src/auth.cpp`, in `refreshBearer`'s callback, after `refreshing = false;`:

```cpp
        if (gen == authGen && g.error == "invalid_grant") {
            // Revoked refresh token (password change, "remove access"): drop the session,
            // or every authed call would refresh again and fail the same way.
            signOut();
            revokedGen = authGen;
        }
```

`src/client.cpp`, replace the body of `Engine::start`'s posted lambda:

```cpp
    loop.post([e, h, chain, cb]() {
        const bool wasSignedIn = e->signedIn();
        const uint64_t gen = e->authGen;
        e->ensureBearer([e, h, chain, cb, wasSignedIn, gen]() {
            // Started for an identity that was revoked while this call waited for its bearer.
            if (wasSignedIn && e->revokedGen > gen) {
                Arg out;
                out.error = Error{ErrorCode::AuthRequired, "session revoked", 0};
                e->deliver(h, [cb, out]() { cb(out); });
                return;
            }
            runAttempt<Arg>(e, h, chain, cb, false);
        });
    });
```

- [ ] **Step 4: Run the test to verify it passes; then revert the fix and watch it fail**

Run: `cmake --build build -j && (cd build && ctest --output-on-failure -R tst_auth)` → PASS.
Then `git stash push src/auth.cpp src/client.cpp src/engine.h`, rebuild, run `tst_auth` → the new case FAILS; `git stash pop`, rebuild, PASS again.

- [ ] **Step 5: Commit**

```bash
git add src/engine.h src/auth.cpp src/client.cpp tests/tst_auth.cpp
git commit -m "fix(lib): a revoked refresh token signs the client out" -m "- An invalid_grant refresh left the dead refresh token installed: every authed call
  refreshed again and failed the same way, and onStateChanged never reported the
  sign-out. It now runs the signOut() path; calls started for the revoked identity
  fail with AuthRequired, later ones run signed out." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11: docs, full verification, live check

**Files:**
- Modify: `AGENTS.md` (§3 layout), `README.md` (feature list, if it lists player capabilities), `docs/specs/2026-10-14-player-sources-design.md` (status line)

- [ ] **Step 1: Docs**

`AGENTS.md` §3: in the `src/` tree add `player.cpp        pure helpers of the public player types` under `client.cpp`, and in the `innertube/` line append `, player-JS scans (playerjs)`. Check `README.md` for a feature list (`grep -n -i "stream\|player\|sign" README.md`) and add: VISIONOS-first `/player` walk returning every source by client, captions as cues, storyboards, YouTube's sign-in QR. Spec status: `**Status:** implemented (plan [../plans/2026-10-14-player-sources.md](../plans/2026-10-14-player-sources.md)).`

- [ ] **Step 2: All four trees**

Run:

```sh
for t in build build-asan build-tsan build-none; do
  echo "== $t"; cmake --build $t -j 2>&1 | grep -E "warning|error"; (cd $t && ctest -j8 --output-on-failure) | tail -3
done
http_proxy=http://127.0.0.1:9 https_proxy=http://127.0.0.1:9 ./build/tests/tst_conformance_curl
http_proxy=http://127.0.0.1:9 ./build/tests/tst_client
```

Expected: no warnings; `100% tests passed` in each tree; both proxy runs pass. Paste the summaries into the report.

- [ ] **Step 3: Live check** (network; not a ctest)

```sh
./build/examples/yt-cli streams dQw4w9WgXcQ        # VISIONOS source (adaptive, hls=1, sabr=1) + ANDROID_VR (itag 18)
./build/examples/yt-cli streams aaaaaaaaaaa        # [ERROR: This video is unavailable], no sources
./build/examples/yt-cli captions dQw4w9WgXcQ en    # first cues
```

Then, for the VISIONOS itag-137 URL from the `streams` output (last column), with its UA: `curl -s -o /dev/null -w "%{http_code}\n" -A "<VISIONOS UA>" -r 80711999-80811998 "<url>"` → `206` (a range past the 60 s mark). Also an age-restricted, a live and a Shorts video id — note the printed verdicts. `./build/examples/yt-cli signin` → `qr.png` exists and `file qr.png` says PNG; cancel with Ctrl-C (scanning needs the owner). Record what was and was not verified (signed-in TV/SABR path and `invalid_grant` only on FakeHttp unless the owner scans the QR).

- [ ] **Step 4: Commit**

```bash
git add AGENTS.md README.md docs/specs/2026-10-14-player-sources-design.md
git commit -m "docs: player sources — layout, README, spec status" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
