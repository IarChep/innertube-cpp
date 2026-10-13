# innertube-cpp separation — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract `meetube-core` into **innertube-cpp** — a standalone, Qt-free C++ YouTube InnerTube client library (`innertube::innertube`, optional `innertube::media`) — and re-host MeeTube on it through a thin Qt 4.7 bridge with zero `.qml` edits.

**Architecture:** Strangler migration, bottom-up. A new in-tree `libinnertube/` (own CMake project, C++17 public headers, C++23 impl, Glaze PRIVATE, libcurl) absorbs, phase by phase, the value types → parsers → session/context → engine loop + **pluggable HTTP transport** (`yt::HttpTransport`; curl is the default backend, others are a `.cpp` or a runtime factory away) → chains → public `yt::Client`. The old `meetube-core` shrinks into `src/bridge/` (QObject facades, list models, `yt→CT` converters, GUI executor, Qt NAM for QML thumbnails). The player state machine stays in the app; Qt-free media building blocks become `innertube::media`.

**Tech Stack:** C++17 (public API) / C++23 (impl), libcurl ≥ 7.68 for the default backend (host: system 8.20/OpenSSL; device: static 8.21 + wolfSSL 5.9.2), Glaze v7.8.4, doctest 2.4.11, CMake ≥ 3.18. Bridge/app: Qt 4.7.4, QtTest.

**Spec:** [../specs/2026-10-14-innertube-cpp-separation-design.md](../specs/2026-10-14-innertube-cpp-separation-design.md) — read §1 (analysis), §2 (decisions D1–D11), §3.2 (threading contract) before any task; §3.5 (network backends) before Phase 3.

## Global Constraints

- **No Qt anywhere under `libinnertube/`**: no `#include <Q…>`, no `Q_OBJECT`, no `qint64`/`qreal`, no Qt in the target's link interface. Enforced by the `innertube_no_qt` ctest (Task 0.2) from Phase 0 on.
- Public headers (`libinnertube/include/yt/*.h`) compile at **`-std=c++17`**; they include no Glaze, no curl, no private header. Implementation files compile at **C++23**.
- libcurl minimum **7.68** (`curl_multi_poll` + `curl_multi_wakeup`) — and **only inside `backends/curl/`**: nothing else in the library includes `<curl/curl.h>`; with `INNERTUBE_HTTP_BACKEND=none` the library builds and its non-conformance suites pass without libcurl.
- **Backends implement no policy**: schemes, redirects, body cap, timeouts, cancellation gating and thread marshalling live in `net::HttpClient` (spec §3.5 table).
- All strings in the library are **UTF-8 `std::string`**; server-issued URLs pass through **byte-exact** (never decoded/re-encoded).
- **QML contract frozen**: no `.qml` edits; every `Q_INVOKABLE`/`Q_PROPERTY`/signal/role name unchanged; `Status` ints Null=0, Loading=1, Canceled=2, Ready=3, Failed=4.
- **Every commit**: `./configure simulator && make -C build-sim -j"$(nproc)"`, `./configure n9 && make -C build-n9 -j"$(nproc)"`, `source simulator_env.sh && (cd build-sim && ctest --output-on-failure)` — all green, no new warnings.
- Qt 4.7.4 gotchas still apply to **bridge/app** code (CLAUDE.md): no `foreach`, string `SIGNAL/SLOT`, no Glaze or raw string literals in moc'ed TUs, `QUrl::fromEncoded` for server URLs.
- N9 link stays: static curl+wolfSSL+nghttp2+zlib with `-Wl,--exclude-libs,ALL`; `CURLOPT_IPRESOLVE_V4` behaviour preserved for MeeTube (via `ClientOptions::ipResolve`).
- Library never reads `MEETUBE_*` env vars, never writes files, never hard-codes paths.
- Commit per task; message prefix `lib:` (library), `bridge:` (Qt bridge), `build:`; end with the `Co-Authored-By` trailer.

## Review Focus

1. **Access token expires mid-session (>1 h)** → the next authed call must transparently refresh first; an HTTP 401 on an authed call → one refresh + one retry, never a loop. *(Task 5.3 tests `refreshesExpiredBearerBeforeCall`, `retries401Once`.)*
2. **`Client` destroyed with transfers in flight and deliveries queued** → no callback after `~Client()` returns, no curl handle leaked, no use-after-free. *(Task 5.2 `destroyWithInflight` under ASan; Task 8.3 TSan job.)*
3. **Races across threads** — `Job::cancel()` racing completion, or a self-driven backend calling its sink from foreign threads / after cancel / after destruction → callback at most once, never after `cancel()` on the delivery thread, never after `~Client()`. *(Task 5.2 `cancelOnDeliveryThreadSuppresses`, `cancelFromOtherThreadAtMostOnce`; Task 3.3 `foreignThreadCallbacksLandOnLoop`, `lateCallbacksAfterDestroyIgnored`; Task 3.4 conformance `serializedCallbacks`, `noCallbackAfterDestroy`.)*
4. **Non-ASCII user input & signed URLs** → Cyrillic/emoji search queries and OAuth form fields are RFC 3986 percent-encoded exactly like `QUrl::toPercentEncoding`; googlevideo/timedtext/RYD URLs carrying `%2C`/`%3D` go to libcurl unchanged. *(Task 3.3 `pctEncodeMatchesQt`, `getPreservesPercentEscapes`, `resolveLocation`; Task 3.4 `urlByteExact`.)*
5. **Hostile/broken transport, any backend** → truncated body is an error and is **not cached**; a redirect on a request carrying `Authorization`/`Cookie` is not followed; `file://` (directly or as a redirect target) is refused; >cap body fails; a backend that itself follows redirects fails conformance. *(Task 3.3 `partialBodyIsErrorAndUncached`, `credentialedRedirectNotFollowed`, `redirectToFileRefused`, `schemeRefused`, `bodyCap`; Task 3.4 `noRedirectFollow`.)*

---

## Type mapping (used by Phases 1–4; read once)

| Qt (today) | Library |
|---|---|
| `QString` | `std::string` (UTF-8) — params as `std::string_view` where only read |
| `QList<T>` / `QVector<T>` | `std::vector<T>` |
| `QStringList` | `std::vector<std::string>` |
| `QMap<QString,QString>` (form fields) | `std::vector<std::pair<std::string,std::string>>` (ordered as given) |
| `QByteArray` | `std::string` (bytes) |
| `qint64` / `quint32` / `uchar` | `int64_t` / `uint32_t` / `uint8_t` |
| `QString *nextToken` out-param | `std::string *nextToken` |
| `CT::User` | `yt::Channel` |
| `-1` "unknown" counts | `std::optional<int64_t>` |
| `core::Outcome<T>{ok, QString error, T value}` | `yt::Result<T>{optional<Error> error, T value}` |
| `x.isEmpty()` / `startsWith` / `contains` | `empty()` / `starts_with` / `find != npos` (C++23 `contains` OK in .cpp) |
| `QString::fromUtf8(p,n)` / `qstr()` | delete — the bytes already are the string |
| `QUrl::toPercentEncoding` | `yt::core::pctEncode` (Task 3.3) |
| `QDateTime::currentMSecsSinceEpoch` | `std::chrono::steady_clock` (monotonic — never wall clock for TTL/expiry) |
| `QCryptographicHash::Md5` key | the full key string `endpoint|client|payload` in `std::unordered_map` |

---

## Target file map (end state)

```
libinnertube/
  CMakeLists.txt                     project(innertube), options, targets, install/export
  cmake/innertubeConfig.cmake.in
  include/yt/
    types.h        Video Playlist Channel Comment Stream Subtitle Account + pages, WatchResult, PlayerInfo, Votes
    result.h       ErrorCode Error Result<T> Done Callback<T>
    session.h      SessionState
    client.h       ClientOptions Executor Logger LogLevel Job Client
    transport.h    HttpTransport HttpResponseSink LoopDriver HttpRequest TransportConfig TransportFactory IpResolve makeDefaultTransport
    auth.h         DeviceCode Auth
    catalog.h      ClientId ClientInfo FeedEntry SearchType topicFeeds() searchTypes() feedSections()
    testing.h      makeClientForTesting() + IHttp re-export (unsupported, for consumers' tests)
  src/
    core/job.h  core/log.{h,cpp}  core/session.h
    core/chains.{h,cpp}
    innertube/clientconfig.cpp  innertube/contextbuilder.{h,cpp}  innertube/bodies.{h,cpp}
    parsers/  (all, moved)
    core/ihttp.h  core/pct.{h,cpp}
    net/loop.{h,cpp}  net/httpclient.{h,cpp}  net/url.{h,cpp}
    client.cpp  auth.cpp  catalog.cpp
  backends/curl/curltransport.{h,cpp} default.cpp     the default backend (only place with <curl/curl.h>)
  backends/none/default.cpp                         INNERTUBE_HTTP_BACKEND=none
  conformance/transport_conformance.{h,cpp} loopback.h   innertube_conformance: what every backend must pass
  examples/threaded-transport/                      self-driven reference backend
  docs/backends.md                                  backend author guide
  media/  (Phase 7)  fmp4demux bufferplanner hlsmaster captions rangedsource
  tests/  doctest.cpp  tst_*.cpp  fixtures/  parserpayloads.h  faketransport.h  fakehttp.h  no_qt.cmake
  examples/yt-cli/main.cpp
  third_party/doctest (submodule)
src/bridge/      (was src/core: Qt facades, models, convert, GuiExecutor, net/curl* Qt NAM, status.h, debuglog)
src/app/         (unchanged role; SettingsStore moves here; media/ player keeps living here)
```

---

# Phase 0 — Incubator scaffold

### Task 0.1: `libinnertube/` standalone CMake project + doctest

**Files:**
- Create: `libinnertube/CMakeLists.txt`, `libinnertube/tests/CMakeLists.txt`, `libinnertube/tests/doctest.cpp`, `libinnertube/tests/tst_smoke.cpp`, `libinnertube/src/version.cpp`, `libinnertube/include/yt/version.h`
- Create: submodule `libinnertube/third_party/doctest` (tag `v2.4.11`)
- Modify: `CMakeLists.txt` (root) — `add_subdirectory(libinnertube)` between `deps` and `src`

**Interfaces:**
- Produces: CMake target `innertube` (alias `innertube::innertube`), STATIC, PIC; options `INNERTUBE_BUILD_TESTS`, `INNERTUBE_GLAZE_DIR`; function `it_test(name)` registering a doctest suite.

- [ ] **Step 1: Add doctest submodule**

```bash
git submodule add https://github.com/doctest/doctest libinnertube/third_party/doctest
git -C libinnertube/third_party/doctest checkout v2.4.11
```

- [ ] **Step 2: Write the failing smoke test** — `libinnertube/tests/tst_smoke.cpp`

```cpp
#include <doctest/doctest.h>
#include <yt/version.h>
TEST_CASE("version string is semver") {
    CHECK(std::string(yt::versionString()) == "0.1.0");
}
```

`libinnertube/tests/doctest.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
```

- [ ] **Step 3: Write the CMake project** — `libinnertube/CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.18)
project(innertube VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(_it_top ON)
else()
    set(_it_top OFF)
endif()
option(INNERTUBE_BUILD_TESTS "Build the innertube test suites" ${_it_top})
set(INNERTUBE_GLAZE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../deps/glaze" CACHE PATH
    "Glaze source root (contains include/glaze)")

add_library(innertube STATIC src/version.cpp)
add_library(innertube::innertube ALIAS innertube)
set_target_properties(innertube PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(innertube PUBLIC cxx_std_17)
# The impl needs C++23 (Glaze v7); consumers only see the C++17 floor above.
target_compile_options(innertube PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=c++23>)
target_include_directories(innertube
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Glaze: PRIVATE, header-only. Reuse a consumer's `glaze` target if present.
if(TARGET glaze)
    target_link_libraries(innertube PRIVATE glaze)
else()
    target_include_directories(innertube SYSTEM PRIVATE ${INNERTUBE_GLAZE_DIR}/include)
endif()

# libcurl: a consumer may pre-define CURL::libcurl (MeeTube's N9 build points it at
# the static curl+wolfSSL archives); otherwise find the system one.
if(NOT TARGET CURL::libcurl)
    find_package(CURL 7.68 REQUIRED)
endif()
target_link_libraries(innertube PUBLIC CURL::libcurl)

if(INNERTUBE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

> MSVC note for Phase 8: replace the raw `-std=c++23` with a generator expression on
> `CXX_COMPILER_ID` (`/std:c++latest`). Not needed until the CI matrix exists.

`libinnertube/tests/CMakeLists.txt`:

```cmake
add_library(innertube_doctest STATIC doctest.cpp)
target_include_directories(innertube_doctest PUBLIC ${PROJECT_SOURCE_DIR}/third_party/doctest)

function(it_test name)
    add_executable(${name} ${name}.cpp ${ARGN})
    target_link_libraries(${name} PRIVATE innertube innertube_doctest)
    target_compile_options(${name} PRIVATE -std=c++23)
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
    if(TARGET glaze)
        target_link_libraries(${name} PRIVATE glaze)
    else()
        target_include_directories(${name} SYSTEM PRIVATE ${INNERTUBE_GLAZE_DIR}/include)
    endif()
    target_compile_definitions(${name} PRIVATE IT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

it_test(tst_smoke)
```

`libinnertube/include/yt/version.h`:

```cpp
#ifndef YT_VERSION_H
#define YT_VERSION_H
namespace yt { const char *versionString(); }
#endif
```

`libinnertube/src/version.cpp`:

```cpp
#include <yt/version.h>
namespace yt { const char *versionString() { return "0.1.0"; } }
```

- [ ] **Step 4: Wire into MeeTube root** — in `CMakeLists.txt` after `add_subdirectory(deps)`:

```cmake
# innertube-cpp (incubating). On the device, hand it the static curl stack.
if(BUILD_N9)
    file(MAKE_DIRECTORY ${CURL_INCLUDE})
    add_library(CURL::libcurl STATIC IMPORTED GLOBAL)
    set_target_properties(CURL::libcurl PROPERTIES
        IMPORTED_LOCATION ${CURL_A}
        INTERFACE_INCLUDE_DIRECTORIES ${CURL_INCLUDE}
        INTERFACE_LINK_LIBRARIES "${WOLFSSL_A};${NGHTTP2_A};${ZLIB_A};m")
endif()
if(NOT BUILD_N9)
    set(INNERTUBE_BUILD_TESTS ON CACHE BOOL "" FORCE)
else()
    set(INNERTUBE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
endif()
add_subdirectory(libinnertube)
if(BUILD_N9)
    add_dependencies(innertube libcurl_build)
endif()
```

- [ ] **Step 5: Build both ways and run**

Run: `cmake -S libinnertube -B /tmp/it-standalone && cmake --build /tmp/it-standalone -j && ctest --test-dir /tmp/it-standalone --output-on-failure`
Expected: `tst_smoke` PASS. Then the three Global-Constraints commands — PASS (MeeTube's 9 suites + `tst_smoke`).

- [ ] **Step 6: Commit** — `build: libinnertube incubator — standalone CMake project + doctest`

### Task 0.2: Qt-ban guard + move the already-Qt-free files

**Files:**
- Create: `libinnertube/tests/no_qt.cmake`
- Move (git mv): `src/core/core/job.h` → `libinnertube/src/core/job.h`; `src/core/innertube/clientconfig.{h,cpp}` → `libinnertube/src/innertube/`; `src/core/innertube/catalog.h` → `libinnertube/src/innertube/catalog.h`
- Modify: `src/core/CMakeLists.txt` (drop moved sources; `target_link_libraries(meetube-core PUBLIC innertube)` and `target_include_directories(meetube-core PUBLIC ${PROJECT_SOURCE_DIR}/libinnertube/src)` **temporarily** — removed in Task 6.4), `libinnertube/CMakeLists.txt` (add `src/innertube/clientconfig.cpp`)

**Interfaces:**
- Produces: `yt::core::JobState/JobToken/newJob/live` (unchanged), `yt::ClientId`, `yt::clientInfo()` (unchanged), ctest `innertube_no_qt`.

- [ ] **Step 1: Write the guard** — `libinnertube/tests/no_qt.cmake`

```cmake
# Fails if any library source mentions Qt. Run as: cmake -DROOT=<libinnertube> -P no_qt.cmake
file(GLOB_RECURSE _srcs ${ROOT}/include/*.h ${ROOT}/src/*.h ${ROOT}/src/*.cpp ${ROOT}/media/*.h ${ROOT}/media/*.cpp)
set(_bad "")
foreach(f ${_srcs})
    file(STRINGS ${f} _hits REGEX "#include <Q[A-Za-z]|Q_OBJECT|\\bqint64\\b|\\bqreal\\b|\\bQString\\b")
    if(_hits)
        list(APPEND _bad "${f}: ${_hits}")
    endif()
endforeach()
if(_bad)
    message(FATAL_ERROR "Qt found in libinnertube:\n${_bad}")
endif()
```

Register in `libinnertube/tests/CMakeLists.txt`:

```cmake
add_test(NAME innertube_no_qt
         COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/no_qt.cmake)
```

- [ ] **Step 2: Prove the guard bites** — temporarily add `// QString` … no: add a scratch file `libinnertube/src/_probe.h` containing `#include <QString>`; run `ctest -R innertube_no_qt` → FAIL naming `_probe.h`; delete the file → PASS.

- [ ] **Step 3: git mv the three files**, fix includes (`"innertube/clientconfig.h"`, `"core/job.h"` resolve through the temporary include dir). Build both targets.

- [ ] **Step 4: Run all tests** — PASS.

- [ ] **Step 5: Commit** — `lib: Qt-ban guard; adopt job.h, clientconfig, catalog`

---

# Phase 1 — Value types, parsers, bodies

### Task 1.1: `yt/types.h` + `yt/result.h`

**Files:**
- Create: `libinnertube/include/yt/types.h`, `libinnertube/include/yt/result.h`, `libinnertube/tests/tst_types.cpp`

**Interfaces:**
- Produces (exact):

```cpp
// include/yt/types.h
#ifndef YT_TYPES_H
#define YT_TYPES_H
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace yt {
struct Video {
    std::string id, title, description, thumbnailUrl, largeThumbnailUrl, date, duration, url,
                streamUrl, channelId, channelName, avatarUrl, likeText, viewText;
    int64_t viewCount = 0;
    int likeStatus = 0;                      // 0 Indifferent, 1 Liked, 2 Disliked
    std::optional<int64_t> likeCount;        // authed /next only
    std::optional<int64_t> dislikeCount;     // RYD
    bool subscribed = false;
    bool downloadable = false;
};
struct Playlist { std::string id, title, description, thumbnailUrl, date, channelId, channelName; int videoCount = 0; };
struct Channel  { std::string id, name, description, thumbnailUrl, subscriberCount, bannerUrl, handle, videoCount; bool subscribed = false; };
struct Comment  { std::string id, body, date, channelId, author, thumbnailUrl, videoId; };
// id = itag ("18","140",…) or "hls"/"audio". Muxed: hasAudio && width>0; video-only:
// width>0 && !hasAudio; audio-only: width==0 && hasAudio. bitrate bits/s (0 unknown).
struct Stream   { std::string id, url, description, mimeType; int width = 0, height = 0, bitrate = 0; bool hasAudio = false; };
struct Subtitle { std::string id, url, title, language; };
struct Account  { std::string id, name, thumbnailUrl, handle, channelId; };

struct VideoPage    { std::vector<Video> items; std::string next; };
struct PlaylistPage { std::vector<Playlist> items; std::string next; };
struct ChannelPage  { std::vector<Channel> items; std::string next; };
struct CommentPage  { std::vector<Comment> items; std::string next; std::string createCommentParams; bool disabled = false; };
struct WatchResult  { Video primary; std::vector<Video> related; };
struct Votes        { std::optional<int64_t> likes, dislikes; };
}
#endif
```

Field renames vs `CT::` (bridge converter maps them back): `userId→channelId`, `username→channelName` (Video/Playlist), `User::username→Channel::name`, `Comment::userId→channelId`, `Comment::username→author`, `Account::username→name`.

```cpp
// include/yt/result.h
#ifndef YT_RESULT_H
#define YT_RESULT_H
#include <functional>
#include <optional>
#include <string>
namespace yt {
enum class ErrorCode {
    Transport,        // DNS/connect/TLS/reset/partial body/cap exceeded
    Timeout,          // request deadline
    InvalidResponse,  // not JSON (HTML consent page, empty)
    Api,              // InnerTube/OAuth error envelope
    Unavailable,      // channel/account/resolve/streams not available
    Unsupported,      // e.g. streams need signature decipher
    AuthRequired,     // authed call without a session
    Canceled
};
struct Error { ErrorCode code = ErrorCode::Transport; std::string message; long httpStatus = 0; };
template <class T> struct Result {
    std::optional<Error> error;   // nullopt = ok
    T value{};
    bool ok() const { return !error.has_value(); }
};
struct Done {};
template <class T> using Callback = std::function<void(const Result<T> &)>;
}
#endif
```

- [ ] **Step 1: Failing test** — `tst_types.cpp`: compiles `types.h`/`result.h` **at C++17** (add `set_source_files_properties(tst_types.cpp PROPERTIES COMPILE_OPTIONS -std=c++17)` and do NOT apply the it_test C++23 flag to it — give `it_test` an optional `STD17` argument), checks `Result<int>{}.ok()` is true, `Result<int>{Error{ErrorCode::Api,"x"}}.ok()` false, `Video{}.likeCount` empty.
- [ ] **Step 2: Run** — FAIL (headers missing). **Step 3:** write headers. **Step 4:** PASS.
- [ ] **Step 5: Commit** — `lib: public value types and Result/Error`

### Task 1.2: Parsers → std, ported test suite

**Files:**
- Move: `src/core/parsers/*` → `libinnertube/src/parsers/`
- Move: `tests/fixtures/*`, `tests/parserpayloads.h` → `libinnertube/tests/` (MeeTube tests that still need fixtures use `MT_TEST_DATA_DIR` pointing at the new dir — update `tests/CMakeLists.txt`)
- Create: `libinnertube/src/parsers/textmatch.h` (+ cpp) ; `libinnertube/tests/tst_parsers.cpp` (port of `tests/tst_meetube_parsers.cpp`)
- Create: `src/core/bridge/convert.{h,cpp}` (Qt side, temporary home inside meetube-core)
- Modify: `src/core/core/chains.cpp` (call std parsers, convert), `libinnertube/CMakeLists.txt` (add parser sources)
- Delete: `tests/tst_meetube_parsers.cpp` (after the doctest port is green and asserts the same values)

**Interfaces:**
- Produces (signatures — all in `namespace yt`, header `parsers/rendererparser.h`, `parsers/playerparser.h`, `parsers/continuation.h`, `parsers/suggestparser.h`):

```cpp
std::string parseText(std::string_view field);
Video parseVideoRenderer(std::string_view r);
Video parseLockupViewModel(std::string_view lm);
Video parseTileRenderer(std::string_view t);
std::vector<Video> parseVideoList(std::string_view response, std::string *nextToken);
std::vector<Comment> parseComments(std::string_view response, std::string *nextToken, std::string *createCommentParams = nullptr);
Playlist parsePlaylistRenderer(std::string_view r);
std::vector<Playlist> parsePlaylistList(std::string_view response, std::string *nextToken);
Channel parseChannel(std::string_view response);
Channel parseChannel(const std::string &response);
bool parseChannelSubscribed(std::string_view response);
Channel parseUserRenderer(std::string_view r);
std::vector<Channel> parseUserList(std::string_view response, std::string *nextToken);
Account parseAccountsList(std::string_view response);
void parseWatchPage(std::string_view response, Video *primary, std::vector<Video> *related);
std::string parseResolvedBrowseId(std::string_view response);
std::string findContinuationTokenUnder(std::string_view response, std::string_view key);
std::vector<std::string> parseSuggestions(std::string_view body);
struct PlayerResult { /* existing fields, std types: */ bool playable; std::string reason;
    std::vector<Stream> streams; std::vector<Subtitle> captions;
    int formatsSeen, adaptiveSeen; bool cipheredOnly, sabr; };
PlayerResult parsePlayer(std::string_view body);
```

- Produces (`parsers/textmatch.h`, replaces the three `QRegExp`s in `rendererinternal.h`):

```cpp
namespace yt::gj {
int64_t digitsOf(std::string_view s);   // "1,234,567 views" -> 1234567; no digits -> 0
bool isClockText(std::string_view s);   // ^(\d+:)?\d?\d:\d\d$  ("3:07","12:03","1:02:03")
bool hasYear(std::string_view s);       // contains \b\d{4}\b   ("Mar 3, 2021" yes; "12345" no)
}
```

- Produces (`src/core/bridge/convert.h`, Qt, inside meetube-core for now):

```cpp
namespace bridge {
CT::Video    toQt(const yt::Video &);     CT::Playlist toQt(const yt::Playlist &);
CT::User     toQt(const yt::Channel &);   CT::Comment  toQt(const yt::Comment &);
CT::Stream   toQt(const yt::Stream &);    CT::Subtitle toQt(const yt::Subtitle &);
CT::Account  toQt(const yt::Account &);
template <class T> QList<decltype(toQt(std::declval<T>()))> toQt(const std::vector<T> &v);
QString      qs(const std::string &s);    // QString::fromUtf8(s.data(), int(s.size()))
std::string  ss(const QString &s);        // s.toUtf8() bytes
}
```
`toQt(Video)` maps `likeCount`/`dislikeCount` `nullopt → -1` (the CT sentinel) and the renamed fields back.

- [ ] **Step 1: Write failing textmatch tests** — in `tst_parsers.cpp`:

```cpp
TEST_CASE("textmatch") {
    using namespace yt::gj;
    CHECK(digitsOf("1,234,567 views") == 1234567);
    CHECK(digitsOf("no views") == 0);
    CHECK(isClockText("3:07"));  CHECK(isClockText("12:03"));  CHECK(isClockText("1:02:03"));
    CHECK_FALSE(isClockText("LIVE")); CHECK_FALSE(isClockText("123:4")); CHECK_FALSE(isClockText(""));
    CHECK(hasYear("Streamed Mar 3, 2021")); CHECK_FALSE(hasYear("12345 views")); CHECK_FALSE(hasYear("3 years ago"));
}
```

- [ ] **Step 2: Run** — FAIL (undefined). **Step 3: Implement** `textmatch.cpp`:

```cpp
#include "parsers/textmatch.h"
#include <cctype>
namespace yt::gj {
static bool dig(char c) { return c >= '0' && c <= '9'; }
int64_t digitsOf(std::string_view s) {
    int64_t v = 0;
    for (char c : s) if (dig(c)) v = v * 10 + (c - '0');
    return v;
}
bool isClockText(std::string_view s) {
    // Groups separated by ':': 2 or 3 groups; last two groups exactly 2 digits
    // ("\d\d"), first group 1-2 digits when 2 groups ("\d?\d"), any >=1 digits
    // for the hour group when 3 groups ("\d+:").
    std::vector<std::string_view> g;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i)
        if (i == s.size() || s[i] == ':') { g.push_back(s.substr(start, i - start)); start = i + 1; }
    auto allDig = [](std::string_view x) { if (x.empty()) return false; for (char c : x) if (!dig(c)) return false; return true; };
    for (auto x : g) if (!allDig(x)) return false;
    if (g.size() == 2) return g[0].size() <= 2 && g[1].size() == 2;
    if (g.size() == 3) return g[1].size() <= 2 && g[2].size() == 2;
    return false;
}
bool hasYear(std::string_view s) {
    // \b\d{4}\b: a run of exactly 4 digits bounded by non-word chars / ends.
    auto word = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };
    for (size_t i = 0; i + 4 <= s.size(); ++i) {
        if (!(dig(s[i]) && dig(s[i+1]) && dig(s[i+2]) && dig(s[i+3]))) continue;
        const bool lb = (i == 0) || !word(s[i-1]);
        const bool rb = (i + 4 == s.size()) || !word(s[i+4]);
        if (lb && rb) return true;
    }
    return false;
}
}
```

> `QRegExp("(\\d+:)?\\d?\\d:\\d\\d").exactMatch` on 3 groups constrains the middle group
> to `\d?\d` (1–2 digits) and the hour group to `\d+` — the code above matches exactly that.
> `"123:4"` fails (last group 1 digit). `\b` in QRegExp treats `_` and letters/digits as word chars.

- [ ] **Step 4: Run** — PASS.

- [ ] **Step 5: Port the parsers mechanically** per the Type mapping table: every `QString` return/field becomes `std::string`; delete `qstr()` (assign the `std::string` directly); `digitsOf(QString)` → `gj::digitsOf`; the `QRegExp` duration check → `gj::isClockText(t)`; the year check → `gj::hasYear(text)`; `text.contains(QLatin1String("view"))` → `text.find("view") != std::string::npos`; `url.startsWith("//")` → `url.starts_with("//")`, `url.prepend("https:")` → `url.insert(0, "https:")`; `QList::isEmpty/<<` → `empty()/push_back`. Worked example (rendererinternal.h around line 549):

```cpp
// before
QString url = qstr(lastThumbUrl(*t.header->tileHeaderRenderer->thumbnail));
if (url.startsWith(QLatin1String("//"))) url.prepend(QLatin1String("https:"));
// after
std::string url = lastThumbUrl(*t.header->tileHeaderRenderer->thumbnail);
if (url.starts_with("//")) url.insert(0, "https:");
```

- [ ] **Step 6: Port `tst_meetube_parsers.cpp` → `tst_parsers.cpp`** one `QCOMPARE` → one `CHECK` (same fixture, same expected literal, field renamed per Task 1.1). `QVERIFY(x.isEmpty())` → `CHECK(x.empty())`. Add `src/parsers/textmatch.cpp` (and every moved parser `.cpp`) to the `innertube` sources; register `it_test(tst_parsers)`.

- [ ] **Step 7: Bridge the chains** — in `src/core/core/chains.cpp` each parser call becomes e.g.:

```cpp
std::string token; const std::vector<yt::Video> v = parseVideoList(*r.body, &token);
out.ok = true; out.value.items = bridge::toQt(v); out.value.next = bridge::qs(token);
```

Write `convert.cpp` field-by-field (14 Video fields + counts sentinel, etc.).

- [ ] **Step 8: Run everything** — `tst_parsers` PASS with every ported assertion; MeeTube `tst_meetube_chains`, `_model`, `_account` PASS unchanged (they observe CT:: values through the converter); `innertube_no_qt` PASS. Delete `tests/tst_meetube_parsers.cpp` + its `mt_test` line.

- [ ] **Step 9: Commit** — `lib: parsers on std types (Qt-free); bridge converters`

### Task 1.3: POST bodies → std

**Files:**
- Move: `src/core/requests/bodies.{h,cpp}` → `libinnertube/src/innertube/bodies.{h,cpp}`
- Modify: params `const QString &` → `std::string_view`; `src/core/core/chains.cpp` call sites pass `bridge::ss(x)`
- Test: extend `libinnertube/tests/tst_context.cpp` (new) with the body assertions from `tests/tst_meetube_context.cpp` that cover `bodies::*`

**Interfaces:** `namespace yt::bodies { std::string browse(std::string_view browseId, std::string_view params, std::string_view continuation); std::string search(std::string_view query, std::string_view params); std::string nextVideo(std::string_view); std::string nextContinuation(std::string_view); std::string player(std::string_view videoId, bool withPlaybackContext = false); std::string resolveUrl(std::string_view); std::string accountsList(); std::string subscribeChannels(std::string_view); std::string likeTarget(std::string_view); std::string createComment(std::string_view params, std::string_view text); std::string editPlaylist(std::string_view playlistId, bool add, std::string_view id); }`

- [ ] **Step 1:** port the body tests (exact JSON string equality, e.g. `CHECK(bodies::browse("FEx","","") == "{\"browseId\":\"FEx\"}")` — copy the literal expectations from `tst_meetube_context.cpp`), plus a UTF-8 case: `bodies::search("кот","")` contains the raw UTF-8 bytes `"\"query\":\"кот\""` (Glaze must not \u-escape). **Step 2:** FAIL. **Step 3:** port. **Step 4:** PASS + MeeTube suites PASS. **Step 5: Commit** — `lib: POST body builders on std strings`

---

# Phase 2 — Session, context, logging

### Task 2.1: `Session` + `ContextBuilder` on std; `SessionState` public type

**Files:**
- Create: `libinnertube/src/core/session.h`, `libinnertube/include/yt/session.h`
- Move: `src/core/innertube/contextbuilder.{h,cpp}` → `libinnertube/src/innertube/`
- Delete: `src/core/innertube/session.h`
- Modify: `src/core/core/http.{h,cpp}` (Qt Http now holds `yt::Session` std; headers loop over `std::vector<Header>`), `src/core/innertube/innertube.cpp` (session writes via `bridge::ss`)
- Test: `libinnertube/tests/tst_context.cpp` (port the context/header cases of `tst_meetube_context.cpp`; then delete that QtTest file)

**Interfaces:**

```cpp
// src/core/session.h (private)
namespace yt {
struct Session { std::string visitorData, hl = "en", gl = "US", bearer; };
}
// src/innertube/contextbuilder.h (private)
namespace yt {
struct Header { std::string name, value; };
struct ContextBuilder {
    static std::string contextJson(ClientId id, const Session &s);
    static std::vector<Header> headers(ClientId id, const Session &s);
};
}
// include/yt/session.h (public)
namespace yt {
struct SessionState {
    std::string hl = "en", gl = "US";     // InnerTube locale
    std::string visitorData;             // persisted anonymous identity
    std::string refreshToken;            // OAuth; empty = signed out
    std::optional<Account> account;      // identity of the signed-in user
};
}
```

- [ ] **Step 1:** port tests — must keep: IOS omits visitorData in context **and** `X-Goog-Visitor-Id`; ANDROID_VR carries both; `Authorization: Bearer …` only on TVHTML5; consent cookie `SOCS=CAISAiAD` on every client; ANDROID_VR context has `deviceMake":"Oculus"`. **Step 2:** FAIL. **Step 3:** port (`toStdString()` calls disappear; header list → vector). **Step 4:** all PASS. **Step 5: Commit** — `lib: Session/ContextBuilder on std; public SessionState`

### Task 2.2: Library logging (`Logger`), no env reads

**Files:**
- Create: `libinnertube/src/core/log.{h,cpp}`, `libinnertube/include/yt/client.h` (first slice: `LogLevel`, `Logger` only)
- Test: `libinnertube/tests/tst_log.cpp`

**Interfaces:**

```cpp
// include/yt/client.h (slice)
namespace yt {
enum class LogLevel { Debug, Info, Warn };
using Logger = std::function<void(LogLevel, std::string_view category, std::string_view message)>;
}
// src/core/log.h (private)
namespace yt::core {
class LogLine {                      // streams items space-separated, emits in dtor
public:
    LogLine(const Logger &log, LogLevel lvl, std::string_view cat) : m_log(log), m_lvl(lvl), m_cat(cat) {}
    ~LogLine() { m_log(m_lvl, m_cat, m_s.str()); }
    template <class T> LogLine &operator<<(const T &v) { if (m_any) m_s << ' '; m_s << v; m_any = true; return *this; }
private:
    const Logger &m_log; LogLevel m_lvl; std::string_view m_cat; std::ostringstream m_s; bool m_any = false;
};
}
// Zero-cost when no logger: args are not evaluated.
#define YTLOG(logger, cat) if (!(logger)) {} else ::yt::core::LogLine((logger), ::yt::LogLevel::Debug, cat)
```

- [ ] **Step 1: failing test**:

```cpp
TEST_CASE("YTLOG formats and is lazy") {
    std::vector<std::string> got;
    yt::Logger log = [&](yt::LogLevel, std::string_view c, std::string_view m) { got.push_back(std::string(c) + "|" + std::string(m)); };
    YTLOG(log, "net") << "DONE" << 200 << "ms=" << 12;
    REQUIRE(got.size() == 1); CHECK(got[0] == "net|DONE 200 ms= 12");
    yt::Logger none; int evaluated = 0;
    YTLOG(none, "net") << ++evaluated;
    CHECK(evaluated == 0);
}
```

**Step 2:** FAIL. **Step 3:** implement. **Step 4:** PASS. **Step 5: Commit** — `lib: injectable Logger + YTLOG`

---

# Phase 3 — Engine loop, pluggable transport, curl backend

Spec §3.5 is normative for this phase: backends move one HTTP exchange; **schemes, redirects,
body cap, timeouts, cancellation gating and thread marshalling are library policy** (Task 3.3).

### Task 3.1: `net::Loop` — backend-agnostic engine loop

**Files:**
- Create: `libinnertube/src/net/loop.{h,cpp}`, `libinnertube/tests/tst_loop.cpp`
- Create (public, first slice): `libinnertube/include/yt/transport.h` with **only** `LoopDriver` (the rest lands in Task 3.2)

**Interfaces:**
- Consumes: `yt::LoopDriver` (spec §3.5): `virtual void wait(int maxWaitMs) = 0; virtual void wake() = 0;`
- Produces:

```cpp
// src/net/loop.h
#ifndef YT_NET_LOOP_H
#define YT_NET_LOOP_H
#include <yt/transport.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
namespace yt::net {
// Task queue + one-shot timers, run by whichever thread calls runOnce()/run() (the
// "engine thread"). Idle waits go to the installed LoopDriver (a loop-driven backend
// does its I/O there) or, without one, to a condition variable.
// post()/quit() are thread-safe; everything else is engine-thread only.
class Loop {
public:
    using Fn = std::function<void()>;
    using Clock = std::chrono::steady_clock;
    void setDriver(LoopDriver *d);                     // engine thread; nullptr = cv wait
    void post(Fn fn);                                  // any thread; wakes the idle wait
    uint64_t after(int ms, Fn fn);
    void cancelTimer(uint64_t id);
    void runOnce(int maxWaitMs);                       // posted tasks, due timers, then idle wait
    void run();                                        // runOnce(1000) until quit()
    void quit();                                       // any thread
    bool onLoopThread() const { return m_owner.load() == std::this_thread::get_id(); }
private:
    LoopDriver *m_driver = nullptr;
    std::mutex m_mx;
    std::condition_variable m_cv;
    std::vector<Fn> m_posted;                          // guarded by m_mx
    std::multimap<Clock::time_point, std::pair<uint64_t, Fn>> m_timers;
    uint64_t m_nextTimer = 1;
    std::atomic<bool> m_quit{false};
    std::atomic<std::thread::id> m_owner{};
};
}
#endif
```

- [ ] **Step 1: failing tests** — `tst_loop.cpp`; every case runs twice via `SUBCASE("cv")` / `SUBCASE("driver")`, the latter with this fake driver installed:

```cpp
struct FakeDriver : yt::LoopDriver {
    std::mutex mx; std::condition_variable cv; bool woken = false; int waits = 0; int lastWait = -2;
    void wait(int ms) override {
        std::unique_lock<std::mutex> l(mx); ++waits; lastWait = ms;
        cv.wait_for(l, std::chrono::milliseconds(ms), [&] { return woken; }); woken = false;
    }
    void wake() override { { std::lock_guard<std::mutex> l(mx); woken = true; } cv.notify_one(); }
};
static void drain(yt::net::Loop &l, std::function<bool()> done, int ms = 2000) {
    auto end = yt::net::Loop::Clock::now() + std::chrono::milliseconds(ms);
    while (!done() && yt::net::Loop::Clock::now() < end) l.runOnce(10);
}
TEST_CASE("post runs on the loop, in order, never inline") {
    FakeDriver drv; yt::net::Loop l;
    SUBCASE("cv") {}
    SUBCASE("driver") { l.setDriver(&drv); }
    std::vector<int> v;
    l.post([&] { v.push_back(1); l.post([&] { v.push_back(3); }); });
    l.post([&] { v.push_back(2); });
    CHECK(v.empty());
    drain(l, [&] { return v.size() == 3; });
    CHECK(v == std::vector<int>{1, 2, 3});
}
TEST_CASE("post from another thread wakes a blocked idle wait") {
    FakeDriver drv; yt::net::Loop l;
    SUBCASE("cv") {}
    SUBCASE("driver") { l.setDriver(&drv); }
    std::atomic<bool> ran{false};
    std::thread t([&] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); l.post([&] { ran = true; }); });
    auto t0 = yt::net::Loop::Clock::now();
    l.runOnce(5000);
    if (!ran) l.runOnce(0);
    t.join();
    CHECK(ran);
    CHECK(yt::net::Loop::Clock::now() - t0 < std::chrono::milliseconds(2000));
}
TEST_CASE("timers fire in deadline order, cancel works, wait is bounded by the next timer") {
    FakeDriver drv; yt::net::Loop l; l.setDriver(&drv);
    std::vector<int> v;
    l.after(30, [&] { v.push_back(2); });
    uint64_t gone = l.after(10, [&] { v.push_back(99); });
    l.after(10, [&] { v.push_back(1); });
    l.cancelTimer(gone);
    l.runOnce(1000);
    CHECK(drv.lastWait <= 10);                          // not 1000: a timer is due sooner
    drain(l, [&] { return v.size() == 2; });
    CHECK(v == std::vector<int>{1, 2});
}
TEST_CASE("onLoopThread is true only inside runOnce's thread") {
    yt::net::Loop l; bool inside = false;
    l.post([&] { inside = l.onLoopThread(); });
    l.runOnce(0);
    CHECK(inside);
    bool other = true;
    std::thread([&] { other = l.onLoopThread(); }).join();
    CHECK_FALSE(other);
}
TEST_CASE("run() exits on quit()") {
    yt::net::Loop l;
    std::thread t([&] { l.run(); });
    l.post([&] { l.quit(); });
    t.join();
    CHECK(true);
}
```

- [ ] **Step 2: Run** — FAIL (no loop.cpp).

- [ ] **Step 3: Implement** — `src/net/loop.cpp`:

```cpp
#include "net/loop.h"
#include <algorithm>
namespace yt::net {
void Loop::setDriver(LoopDriver *d) { m_driver = d; }
void Loop::post(Fn fn) {
    { std::lock_guard<std::mutex> l(m_mx); m_posted.push_back(std::move(fn)); }
    if (m_driver) m_driver->wake(); else m_cv.notify_one();
}
uint64_t Loop::after(int ms, Fn fn) {
    const uint64_t id = m_nextTimer++;
    m_timers.emplace(Clock::now() + std::chrono::milliseconds(ms), std::make_pair(id, std::move(fn)));
    return id;
}
void Loop::cancelTimer(uint64_t id) {
    for (auto it = m_timers.begin(); it != m_timers.end(); ++it)
        if (it->second.first == id) { m_timers.erase(it); return; }
}
void Loop::runOnce(int maxWaitMs) {
    m_owner = std::this_thread::get_id();
    std::vector<Fn> batch;
    { std::lock_guard<std::mutex> l(m_mx); batch.swap(m_posted); }
    for (Fn &f : batch) f();
    while (!m_timers.empty() && m_timers.begin()->first <= Clock::now()) {
        Fn f = std::move(m_timers.begin()->second.second);
        m_timers.erase(m_timers.begin());
        f();
    }
    if (m_quit) return;
    int wait = maxWaitMs;
    if (!m_timers.empty()) {
        const auto d = std::chrono::duration_cast<std::chrono::milliseconds>(
                           m_timers.begin()->first - Clock::now()).count();
        wait = (int) std::max<long long>(0, std::min<long long>(wait, d));
    }
    {
        std::unique_lock<std::mutex> l(m_mx);
        if (!m_posted.empty()) wait = 0;
        if (!m_driver) {
            m_cv.wait_for(l, std::chrono::milliseconds(wait), [&] { return !m_posted.empty() || m_quit.load(); });
            return;
        }
    }
    m_driver->wait(wait);    // loop-driven backend: its I/O + dispatch happen in here
}
void Loop::run() { while (!m_quit) runOnce(1000); }
void Loop::quit() {
    m_quit = true;
    if (m_driver) m_driver->wake(); else { std::lock_guard<std::mutex> l(m_mx); m_cv.notify_one(); }
}
}
```

A driver's `wake()` arriving before its `wait()` must make that `wait()` return immediately
(the fake's `woken` flag; curl's `curl_multi_wakeup` already behaves so) — state this in the
`LoopDriver` doc comment.

- [ ] **Step 4: Run** — PASS; TSan build of `tst_loop` clean.
- [ ] **Step 5: Commit** — `lib: net::Loop — task queue + timers with a pluggable idle wait`

### Task 3.2: Public `HttpTransport` interface + `FakeTransport`

**Files:**
- Modify: `libinnertube/include/yt/transport.h` (full interface, spec §3.5 verbatim: `HttpHeader`, `HttpRequest`, `TransportError`, `TransportStatus`, `HttpResponseSink`, `LoopDriver`, `HttpTransport`, `IpResolve`, `TransportConfig`, `TransportFactory`, `makeDefaultTransport`)
- Create: `libinnertube/tests/faketransport.h`, `libinnertube/tests/tst_faketransport.cpp`

**Interfaces:**
- Produces `FakeTransport : yt::HttpTransport` — a scripted, socket-free backend used by every policy test:

```cpp
// tests/faketransport.h
struct FakeExchange {                     // one scripted response
    int status = 200;
    std::vector<yt::HttpHeader> headers;
    std::vector<std::string> chunks;      // delivered as separate onBody calls
    yt::TransportStatus end;              // default: None
    bool hang = false;                    // never complete (timeouts, cancel)
};
class FakeTransport : public yt::HttpTransport {
public:
    enum class Mode { Inline, Threaded }; // Threaded: every sink call from a separate std::thread
    explicit FakeTransport(Mode m = Mode::Inline) : m_mode(m) {}
    ~FakeTransport() override;            // joins its threads; no sink call afterwards
    void script(const std::string &urlPrefix, FakeExchange x);   // FIFO per prefix; longest prefix wins
    uint64_t start(yt::HttpRequest r, std::shared_ptr<yt::HttpResponseSink> s) override;
    void cancel(uint64_t id) override;    // records; a hanging exchange then gets onDone(Canceled)
    void complete();                      // Inline mode: deliver everything started so far
    std::vector<yt::HttpRequest> started; // every request, in start order
    std::vector<uint64_t> canceled;
};
```

Unscripted URLs answer `404` with an empty body. Inline mode delivers only on `complete()`
(so "never re-entrant" stays observable); Threaded mode delivers immediately from a new thread
per request, joined in the destructor.

- [ ] **Step 1:** `tst_faketransport.cpp`: inline — a scripted 2-chunk response produces `onHead(200)`, `onBody` ×2, `onDone(None)` in order, only after `complete()`; threaded — the same sequence arrives from a thread ≠ the test thread; cancel on a `hang` exchange → `onDone(Canceled)`.
- [ ] **Step 2:** FAIL. **Step 3:** implement. **Step 4:** PASS.
- [ ] **Step 5: Commit** — `lib: public HttpTransport interface; FakeTransport`

### Task 3.3: `net::HttpClient` — the policy layer on any transport

**Files:**
- Create: `libinnertube/src/core/ihttp.h`, `libinnertube/src/core/pct.{h,cpp}`, `libinnertube/src/net/httpclient.{h,cpp}`, `libinnertube/src/net/url.{h,cpp}`, `libinnertube/tests/tst_url.cpp`, `libinnertube/tests/tst_http.cpp`

**Interfaces:**
- Consumes: `yt::HttpTransport`, `net::Loop` (Tasks 3.1–3.2), `ContextBuilder`, `Session` (Phase 2), gj scanner (Phase 1).
- Produces (`src/core/ihttp.h`) — the seam chains + `FakeHttp` program against:

```cpp
namespace yt::core {
struct Reply {
    bool ok = false;
    std::shared_ptr<const std::string> body = std::make_shared<std::string>();  // never null; kept on !ok
    ErrorCode code = ErrorCode::Transport;   // meaningful when !ok
    std::string error;
    long httpStatus = 0;
    std::string visitorData;                 // pre-extracted by the envelope scan
};
using HttpFn = std::function<void(const Reply &)>;
using Form = std::vector<std::pair<std::string, std::string>>;
class IHttp {
public:
    virtual ~IHttp() = default;
    virtual void post(std::string_view endpoint, ClientId, std::string bodyJson, const JobToken &, HttpFn) = 0;
    virtual void postForm(std::string url, Form fields, const JobToken &, HttpFn) = 0;
    virtual void get(std::string url, const JobToken &, HttpFn) = 0;
    virtual void abort(const JobToken &) = 0;
    virtual Session &session() = 0;
    virtual void clearCache() = 0;                    // response cache + context/header memo
    virtual void setBearer(std::string bearer) = 0;   // header memo only (same-identity refresh)
    virtual const Logger &log() const = 0;
};
}
```

- Produces (`src/net/httpclient.h`):

```cpp
namespace yt::net {
struct HttpOptions {
    int timeoutMs = 20000;
    int64_t maxBodyBytes = 32LL * 1024 * 1024;
    int maxRedirects = 5;
    std::string baseUrl = "https://www.youtube.com/youtubei/v1/";
    Logger log;
};
class HttpClient final : public core::IHttp {
public:
    HttpClient(Loop &loop, HttpTransport &transport, HttpOptions opts);   // engine thread
    ~HttpClient() override;    // cancels every in-flight exchange on the transport; no callback after
    void setVisitorSink(std::function<void(const std::string &)> sink);
    // IHttp …
};
}
```

- Produces (`src/net/url.h`):

```cpp
namespace yt::net {
// "http"/"https" (lower-cased) or "" when the URL has another/no scheme.
std::string schemeOf(std::string_view url);
// RFC 3986 §5.2 reference resolution, byte-exact (no decoding / re-encoding):
// absolute "https://h/p", network-path "//h/p", absolute-path "/p", query-only "?q", relative "p".
std::string resolveLocation(std::string_view base, std::string_view location);
}
```

**Policy, all in `HttpClient` (backends implement none of it):**
1. **Schemes:** `schemeOf(url)` must be `http`/`https` before `start()` and on every redirect target; else `Reply{Transport, "unsupported URL scheme"}` delivered via `Loop::post`, transport untouched.
2. **Redirects:** on `onHead` status 301/302/303/307/308 with `Location`: if the request carries `Authorization` or `Cookie` → **no follow**, deliver as is (empty body → `InvalidResponse`, `httpStatus` = 3xx). Else follow up to `maxRedirects` (`"too many redirects"` → `Transport`), resolving `Location` with `resolveLocation`; 303, and 301/302 after a POST, become a body-less GET; 307/308 keep method + body. The caller's waiters stay attached across hops.
3. **Body cap:** the sink appends into the transfer's `std::string`; past `maxBodyBytes` `onBody` returns `false` → `Reply{Transport, "response too large"}`.
4. **Timeout:** `Loop::after(timeoutMs)` per exchange → `transport.cancel(id)` + mark `timedOut` → `Reply{Timeout, "request timed out"}`; the timer is canceled on completion.
5. **Marshalling + late calls:** the library sink forwards each call to the engine thread (`loop.onLoopThread()` ? direct : `loop.post`); each exchange has a `done` flag on the engine thread — calls after `onDone`, after cancel/timeout, or after `~HttpClient` began are dropped. The sink holds a `std::weak_ptr` to the exchange state, so a backend outliving it is harmless.
6. **Errors:** `TransportStatus` → `Reply::code`: `None` with a full body → envelope ladder (below); `Canceled` caused by the cap/timeout/abort → as above; every other error (incl. `Reset` mid-body = truncated) → `Transport` with the backend's message. **Truncated/failed replies are never cached.**
7. **Ported verbatim from `src/core/core/http.cpp`:** `cacheTtlSecs` table; `makeReply` envelope ladder (non-`{`/`[` → `InvalidResponse`; top-level `"error"` → `Api` with message, body kept; visitorData capture only while the session lacks one); context splice `{"context":…,<body members>}`; coalescing on `endpoint|client|payload` for all posts; TTL cache (64 entries, FIFO, success only, never gets/forms); cache hits delivered through `Loop::post` (never re-entrant); `abort(job)` drops that job's waiters and cancels exchanges left without waiters; visitorData adoption invalidates the ctx/header memo and calls the sink. Request headers come from `ContextBuilder::headers`; `postForm` sends `core::formEncode(fields)` with `Content-Type: application/x-www-form-urlencoded`.
8. **Percent-encoding** (`src/core/pct.{h,cpp}`, created in this task, used again by the chains in Phase 4): `std::string pctEncode(std::string_view)` — unreserved `A–Z a–z 0–9 - . _ ~` pass, every other byte → `%XX` uppercase (exactly `QUrl::toPercentEncoding`'s defaults); `std::string formEncode(const Form &)` = `k=v&k=v`, both sides encoded, in vector order.

- [ ] **Step 1: failing URL tests** — `tst_url.cpp`:

```cpp
using yt::net::resolveLocation; using yt::net::schemeOf;
TEST_CASE("schemeOf") {
    CHECK(schemeOf("HTTPS://x/") == "https"); CHECK(schemeOf("http://x") == "http");
    CHECK(schemeOf("file:///etc/passwd") == ""); CHECK(schemeOf("/relative") == "");
}
TEST_CASE("resolveLocation") {
    const std::string b = "https://r.googlevideo.com/videoplayback?sig=a%2Cb";
    CHECK(resolveLocation(b, "https://x.googlevideo.com/vp?id=1%3D") == "https://x.googlevideo.com/vp?id=1%3D");
    CHECK(resolveLocation(b, "//cdn.example/a") == "https://cdn.example/a");
    CHECK(resolveLocation(b, "/other?q=1") == "https://r.googlevideo.com/other?q=1");
    CHECK(resolveLocation(b, "?n=2") == "https://r.googlevideo.com/videoplayback?n=2");
    CHECK(resolveLocation("https://h/a/b/c", "d") == "https://h/a/b/d");
    CHECK(resolveLocation("https://h:8443/a", "/z") == "https://h:8443/z");
}
```

  Same file, the encoder (Review Focus 4):

```cpp
TEST_CASE("pctEncodeMatchesQt") {
    using yt::core::pctEncode;
    CHECK(pctEncode("abc-._~XYZ09") == "abc-._~XYZ09");
    CHECK(pctEncode("a b&c=d/e?") == "a%20b%26c%3Dd%2Fe%3F");
    CHECK(pctEncode("кот") == "%D0%BA%D0%BE%D1%82");
    CHECK(pctEncode("\xF0\x9F\x98\x80") == "%F0%9F%98%80");
    CHECK(yt::core::formEncode({{"grant_type", "refresh_token"}, {"x y", "1+1"}}) == "grant_type=refresh_token&x%20y=1%2B1");
}
```

- [ ] **Step 2: failing policy tests** — `tst_http.cpp`, fixture `Loop loop; FakeTransport ft; HttpOptions o; o.baseUrl = "https://yt.test/youtubei/v1/"; HttpClient http(loop, ft, o);` + `pump()` = `loop.runOnce(0); ft.complete(); loop.runOnce(0);` repeated until idle. Every case below is a full `TEST_CASE`:

| Case | Script → action | Required assertions |
|---|---|---|
| `postSplicesContext` | browse → `{"contents":{}}`; post browse WEB `{"browseId":"FEx"}` | `ok`; `ft.started[0].body` starts `{"context":{"client":{"clientName":"WEB"` and contains `"browseId":"FEx"`; method Post; header `X-YouTube-Client-Name: 1` present |
| `neverReentrant` | any | callback not called before the first `pump()` |
| `coalesces` | browse `hang`… then complete once | two identical posts → `ft.started.size()==1`, both callbacks, same `body.get()` |
| `ttlCache` | browse ok | 2nd post after completion → `started.size()==1`, delivered only after `pump()`; player twice → 2 starts |
| `apiEnvelope` | `{"error":{"code":400,"message":"bad"}}` | `!ok`, `code==Api`, `error=="bad"`, body non-empty |
| `htmlInvalid` | `<html>` | `code==InvalidResponse` |
| `timeout` | `hang` ; `o.timeoutMs=50` | after ~60 ms of `runOnce(10)`: `code==Timeout`, `ft.canceled.size()==1` |
| `partialBodyIsErrorAndUncached` | chunks `{"contents"` + end `{Reset,"partial"}` | `code==Transport`; a 2nd identical post starts again (`started.size()==2`) |
| `visitorDataAdopted` | 1st `{"responseContext":{"visitorData":"V1"}}` | sink got `"V1"`; 2nd request body contains `"visitorData":"V1"`, header `X-Goog-Visitor-Id: V1` |
| `credentialedRedirectNotFollowed` | `session().bearer="T"`; browse → 302 `Location: https://evil.test/` | `started.size()==1`; `!ok`, `httpStatus==302` |
| `redirectFollowedRelative` | get `https://a.test/x` → 302 `Location: /y`; `https://a.test/y` → 200 `ok` | `started[1].url=="https://a.test/y"`; ok |
| `redirect303BecomesGet` | postForm → 303 `Location: /done` | `started[1].method==Get`, empty body |
| `redirectToFileRefused` | get → 302 `Location: file:///etc/passwd` | `!ok`, `Transport`; `started.size()==1` |
| `tooManyRedirects` | `/r` → 302 `Location: /r` | `!ok`, error `"too many redirects"`, `started.size()==6` |
| `schemeRefused` | get `file:///etc/hostname` | `!ok`, `started.empty()` |
| `bodyCap` | `o.maxBodyBytes=1024`; one 4096-byte chunk | `!ok`, error `"response too large"` |
| `getPreservesPercentEscapes` | get `https://v.test/v?sig=a%2Cb%3D` | `started[0].url` byte-identical |
| `foreignThreadCallbacksLandOnLoop` | `FakeTransport(Threaded)`; get ok | callback ran with `loop.onLoopThread()==true` |
| `abortDropsCallbacks` | `hang`; cancel token + `http.abort(job)` | callback never; `ft.canceled.size()==1` |
| `lateCallbacksAfterDestroyIgnored` | Threaded, `hang`; destroy `HttpClient`, then let the fake finish | callback never; ASan clean |

- [ ] **Step 3: Run** — FAIL. **Step 4: Implement** `url.cpp` and `httpclient.cpp` per the policy list. **Step 5: Run** — PASS (ASan+UBSan and TSan builds too).
- [ ] **Step 6: Commit** — `lib: net::HttpClient — transport-agnostic policy layer (coalesce, cache, redirects, caps, timeouts)`

### Task 3.4: curl backend + transport conformance suite + build-time selection

**Files:**
- Create: `libinnertube/backends/curl/curltransport.{h,cpp}`, `libinnertube/backends/curl/default.cpp`, `libinnertube/backends/none/default.cpp`
- Create: `libinnertube/conformance/loopback.h`, `libinnertube/conformance/transport_conformance.{h,cpp}`, `libinnertube/tests/tst_conformance_curl.cpp`
- Modify: `libinnertube/CMakeLists.txt` — replace Task 0.1's unconditional libcurl block with the backend selection below; add target `innertube_conformance`

**Interfaces:**
- Produces:

```cpp
// backends/curl/curltransport.h (private to the backend)
namespace yt::backends {
// libcurl multi on the engine thread. Its LoopDriver::wait() = curl_multi_poll +
// curl_multi_perform + info_read dispatch; wake() = curl_multi_wakeup. All sink calls
// therefore happen on the engine thread (no marshalling hop).
class CurlTransport final : public HttpTransport, public LoopDriver {
public:
    explicit CurlTransport(const TransportConfig &cfg);   // std::call_once(curl_global_init)
    ~CurlTransport() override;                             // removes + frees every easy, no sink calls
    uint64_t start(HttpRequest r, std::shared_ptr<HttpResponseSink> s) override;
    void cancel(uint64_t id) override;                     // remove from multi → sink->onDone(Canceled)
    LoopDriver *driver() override { return this; }
    void wait(int maxWaitMs) override;
    void wake() override;
};
}
// conformance/transport_conformance.h (public to backend authors; no test framework)
namespace yt::conformance {
// Runs every check against a fresh transport from `make`; returns "<check>: <why>" per failure.
// Drives driver()->wait() on the calling thread when the backend has one; otherwise waits.
std::vector<std::string> runTransportConformance(const TransportFactory &make);
}
```

Per easy handle (ported from `CurlNetworkReply`, minus redirects): `ACCEPT_ENCODING ""`, `NOSIGNAL 1`, `PIPEWAIT 1`, `CAINFO` when set, `IPRESOLVE` from `cfg.ipResolve`, `PROXY` when set, `PROTOCOLS_STR`/`REDIR_PROTOCOLS_STR "http,https"` (int form under `LIBCURL_VERSION_NUM < 0x075500`), **`FOLLOWLOCATION 0` always** (policy is the library's), `UNRESTRICTED_AUTH 0`, headers verbatim + `"Expect:"` on POST, `POSTFIELDS`/`POSTFIELDSIZE` from the owned body. `HEADERFUNCTION` collects lines; on the blank line of a **final** response (skip `1xx`) → `onHead(status, headers)`. `WRITEFUNCTION` → `onBody`; `false` → return 0 (→ `CURLE_WRITE_ERROR` → `onDone(Canceled)`). `CURLcode` → `TransportError`: `COULDNT_RESOLVE_HOST/PROXY` → `Resolve`, `COULDNT_CONNECT` → `Connect`, `SSL_*`/`PEER_FAILED_VERIFICATION` → `Tls`, `RECV_ERROR`/`SEND_ERROR`/`PARTIAL_FILE`/`GOT_NOTHING` → `Reset`, `WRITE_ERROR`/`ABORTED_BY_CALLBACK` → `Canceled`, else `Other` (message = `curl_easy_strerror`). Verbose tracing under `cfg.log` as in Task 3.3's predecessor (`YTLOG(log,"net")`, TEXT/HEADER_IN/HEADER_OUT only).

`backends/curl/default.cpp`: `std::unique_ptr<HttpTransport> yt::makeDefaultTransport(const TransportConfig &c) { return std::make_unique<backends::CurlTransport>(c); }` — `backends/none/default.cpp` returns `nullptr`.

CMake (replaces the libcurl block of Task 0.1):

```cmake
set(INNERTUBE_HTTP_BACKEND "curl" CACHE STRING
    "HTTP backend: curl, none, or the path of a .cpp defining yt::makeDefaultTransport")
set(INNERTUBE_HTTP_BACKEND_LIBS "" CACHE STRING "Extra link libraries for a custom backend")
if(INNERTUBE_HTTP_BACKEND STREQUAL "curl")
    if(NOT TARGET CURL::libcurl)
        find_package(CURL 7.68 REQUIRED)
    endif()
    target_sources(innertube PRIVATE backends/curl/curltransport.cpp backends/curl/default.cpp)
    target_link_libraries(innertube PRIVATE CURL::libcurl)
elseif(INNERTUBE_HTTP_BACKEND STREQUAL "none")
    target_sources(innertube PRIVATE backends/none/default.cpp)
else()
    get_filename_component(_it_backend "${INNERTUBE_HTTP_BACKEND}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    if(NOT EXISTS "${_it_backend}")
        message(FATAL_ERROR "INNERTUBE_HTTP_BACKEND: no such file: ${INNERTUBE_HTTP_BACKEND}")
    endif()
    target_sources(innertube PRIVATE "${_it_backend}")
    target_link_libraries(innertube PRIVATE ${INNERTUBE_HTTP_BACKEND_LIBS})
endif()
message(STATUS "innertube HTTP backend: ${INNERTUBE_HTTP_BACKEND}")

add_library(innertube_conformance STATIC conformance/transport_conformance.cpp)
target_link_libraries(innertube_conformance PUBLIC innertube)
target_compile_options(innertube_conformance PRIVATE -std=c++23)
```

(`PRIVATE` is enough for a STATIC lib: CMake still propagates the archive as `$<LINK_ONLY:…>`.
MeeTube's N9 `CURL::libcurl` imported target from Task 0.1 keeps working unchanged.)

`conformance/loopback.h` (POSIX; Winsock variant in Task 8.2) — scripted HTTP/1.1 server:

```cpp
// Scripted HTTP/1.1 server on 127.0.0.1:<ephemeral> for transport tests.
#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
struct Canned {
    int status = 200;
    std::string body;
    std::vector<std::string> headers;      // extra "Name: value" lines
    int delayMs = 0;
    long declaredLength = -1;              // > body.size() => truncated transfer
};
class Loopback {
public:
    Loopback() {
        m_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1; ::setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(m_fd, (sockaddr *)&a, sizeof a); ::listen(m_fd, 16);
        socklen_t len = sizeof a; ::getsockname(m_fd, (sockaddr *)&a, &len); m_port = ntohs(a.sin_port);
        m_acceptor = std::thread([this] { serve(); });
    }
    ~Loopback() {
        m_stop = true; ::shutdown(m_fd, SHUT_RDWR); ::close(m_fd); m_acceptor.join();
        for (auto &t : m_workers) t.join();
    }
    std::string url(const std::string &path) const { return "http://127.0.0.1:" + std::to_string(m_port) + path; }
    void route(const std::string &path, Canned c) { std::lock_guard<std::mutex> l(m_mx); m_routes[path] = c; }
    int hits(const std::string &path) { std::lock_guard<std::mutex> l(m_mx); return m_hits[path]; }
    std::string lastRequest(const std::string &path) { std::lock_guard<std::mutex> l(m_mx); return m_last[path]; }
private:
    void serve() {
        while (!m_stop) {
            int c = ::accept(m_fd, nullptr, nullptr);
            if (c < 0) break;
            std::lock_guard<std::mutex> l(m_mx);
            m_workers.emplace_back([this, c] { handle(c); });
        }
    }
    void handle(int c) {
        std::string req; char buf[4096];
        size_t hdrEnd = std::string::npos; long need = -1;
        while (true) {
            ssize_t n = ::recv(c, buf, sizeof buf, 0);
            if (n <= 0) break;
            req.append(buf, (size_t)n);
            if (hdrEnd == std::string::npos && (hdrEnd = req.find("\r\n\r\n")) != std::string::npos) {
                auto p = req.find("Content-Length: ");
                need = (p != std::string::npos && p < hdrEnd) ? std::stol(req.substr(p + 16)) : 0;
            }
            if (hdrEnd != std::string::npos && (long)(req.size() - hdrEnd - 4) >= need) break;
        }
        const auto sp1 = req.find(' '), sp2 = req.find(' ', sp1 + 1);
        std::string path = req.substr(sp1 + 1, sp2 - sp1 - 1);
        path = path.substr(0, path.find('?'));
        Canned k;
        { std::lock_guard<std::mutex> l(m_mx); ++m_hits[path]; m_last[path] = req; k = m_routes.count(path) ? m_routes[path] : Canned{404, "", {}, 0, -1}; }
        if (k.delayMs) std::this_thread::sleep_for(std::chrono::milliseconds(k.delayMs));
        std::string out = "HTTP/1.1 " + std::to_string(k.status) + " X\r\nConnection: close\r\nContent-Length: "
            + std::to_string(k.declaredLength >= 0 ? k.declaredLength : (long)k.body.size()) + "\r\n";
        for (auto &h : k.headers) out += h + "\r\n";
        out += "\r\n" + k.body;
        ::send(c, out.data(), out.size(), MSG_NOSIGNAL);
        ::shutdown(c, SHUT_RDWR); ::close(c);
    }
    int m_fd = -1; int m_port = 0; std::atomic<bool> m_stop{false};
    std::thread m_acceptor; std::vector<std::thread> m_workers; std::mutex m_mx;
    std::map<std::string, Canned> m_routes; std::map<std::string, int> m_hits; std::map<std::string, std::string> m_last;
};
```

**Conformance checks** (each a named function in `transport_conformance.cpp`; a failure
appends `"name: reason"`; every check has a 5 s deadline):

| Check | Server script → request | Pass condition |
|---|---|---|
| `get200Body` | `/a` → 200 `hello` | `onHead(200)`, body `hello`, `onDone(None)` |
| `postBodyAndHeaders` | `/p` → 200; POST `{"x":1}` + `Content-Type: application/json` + `X-Test: 1` | server request has the body, both headers |
| `responseHeaders` | `/h` → header `X-Reply: v` | delivered headers contain `x-reply` (case-insensitive) = `v` |
| `noRedirectFollow` | `/r` → 302 `Location: /t` | `onHead(302)`; `hits("/t")==0` |
| `urlByteExact` | GET `/v?sig=a%2Cb%3D` | server request line contains `/v?sig=a%2Cb%3D` |
| `largeBodyInOrder` | `/big` → 1 MiB pattern `i % 251` | concatenated `onBody` bytes equal the pattern |
| `onBodyFalseAborts` | `/big`; sink returns `false` on the first chunk | no further `onBody`; `onDone(Canceled)` |
| `cancelMidTransfer` | `/slow` delayMs 500; `cancel()` after 50 ms | at most one `onDone`; no `onBody` after it |
| `connectRefused` | port of a closed listener | `onDone(Connect)` |
| `unresolvable` | `http://nonexistent.invalid/` | `onDone(Resolve)` |
| `truncatedBody` | declaredLength 100, body 10 bytes | `onDone` error ≠ `None` |
| `noExpectHeader` | POST 4 KiB | server request lacks `Expect:` |
| `concurrent32` | 32 GETs at once | all 32 complete with their own body |
| `serializedCallbacks` | 32 GETs | no sink call overlaps another for the same request (atomic in-callback flag); order head→body*→done |
| `noCallbackAfterDestroy` | `/slow`; destroy the transport mid-transfer | no sink call once the destructor returned (flag set after dtor) |

- [ ] **Step 1:** `tst_conformance_curl.cpp`:

```cpp
#include <doctest/doctest.h>
#include "conformance/transport_conformance.h"
TEST_CASE("curl backend passes the transport conformance suite") {
    const auto failures = yt::conformance::runTransportConformance(
        [](const yt::TransportConfig &c) { return yt::makeDefaultTransport(c); });
    for (const auto &f : failures) MESSAGE(f);
    CHECK(failures.empty());
}
```

Register with `it_test(tst_conformance_curl)` + `target_link_libraries(tst_conformance_curl PRIVATE innertube_conformance)`, only when `INNERTUBE_HTTP_BACKEND STREQUAL "curl"`.
- [ ] **Step 2:** FAIL (no backend). **Step 3:** implement `CurlTransport`, the suite, CMake. **Step 4:** PASS; also configure once with `-DINNERTUBE_HTTP_BACKEND=none` — builds, `tst_conformance_curl` not registered, every other suite PASS (they use `FakeTransport`).
- [ ] **Step 5: Commit** — `lib: curl HTTP backend, transport conformance suite, INNERTUBE_HTTP_BACKEND`

### Task 3.5: Reference self-driven backend + backend author guide

**Files:**
- Create: `libinnertube/examples/threaded-transport/threadedtransport.{h,cpp}`, `libinnertube/examples/threaded-transport/tst_conformance_threaded.cpp`, `libinnertube/docs/backends.md`

**Interfaces:**
- Produces `examples::ThreadedTransport : yt::HttpTransport` — `driver()` returns `nullptr`; `start()` spawns one `std::thread` running a blocking `curl_easy_perform` (same easy options as `CurlTransport`), calling the sink **from that thread**; `cancel()` sets a per-request `std::atomic<bool>` checked in `CURLOPT_XFERINFOFUNCTION` (return 1 → `ABORTED_BY_CALLBACK` → `onDone(Canceled)`); the destructor cancels all and joins.

Purpose: (1) proves the foreign-thread path of the library against a real network stack; (2) is the copy-me template for wrapping a blocking or callback-based native API (WinHTTP sync, `NSURLSession` completion handlers, JNI).

`docs/backends.md` (backend author guide): when to write one; the §3.5 contract (copy the interface + the policy/backends table); the two integration styles (loop-driven `LoopDriver` vs self-driven) with the curl and threaded backends as references; selecting it (`-DINNERTUBE_HTTP_BACKEND=path/to/x.cpp` + `INNERTUBE_HTTP_BACKEND_LIBS`, or `ClientOptions::transport`); "supported = `runTransportConformance` returns empty"; checklist (no redirect following, decoded bodies, no `Expect: 100-continue`, honour `onBody → false`, nothing after the destructor); the sketch of a future `sockets.h`-based builtin backend (spec §3.5 "Later").

- [ ] **Step 1:** `tst_conformance_threaded.cpp` = the Task 3.4 test with `std::make_unique<examples::ThreadedTransport>(c)`; built when `INNERTUBE_BUILD_EXAMPLES` and curl are available. **Step 2:** FAIL. **Step 3:** implement. **Step 4:** PASS (TSan clean). **Step 5: Commit** — `lib: self-driven reference backend + backend author guide`

---

# Phase 4 — Chains on std

### Task 4.1: std `FakeHttp`

**Files:**
- Create: `libinnertube/tests/fakehttp.h`, `libinnertube/tests/tst_fakehttp.cpp`

**Interfaces:**
- Consumes: `core::IHttp`, `Reply`, `Form` (Task 3.3).
- Produces: `FakeHttp : core::IHttp` — std port of `tests/testutil.h`: `queue(std::string endpoint, std::string body, long status = 200)` (status ≥ 400 → `Reply{ok=false, code=Api, httpStatus=status}` with the body kept), `std::vector<std::string> sent`, `std::vector<std::pair<std::string, core::Form>> sentForm`, `int lastClientFor(std::string_view endpoint) const`, `setGetBody`, `lastGetUrl`, `flush()` draining to completion and skipping dead tokens; `setBearer` writes `session().bearer`; `setVisitorSink` + `deliverVisitor(std::string)` to exercise capture; `log()` returns an empty Logger. (Chains are tested one level above `HttpClient`; `HttpClient` itself is tested on `FakeTransport` in Task 3.3.)

- [ ] **Step 1:** `tst_fakehttp.cpp`: a queued reply is delivered only by `flush()`; a dead token is skipped; a chain-style callback that posts again is drained by the same `flush()`; status 401 → `!ok`, `httpStatus==401`. **Step 2:** FAIL. **Step 3:** implement. **Step 4:** PASS. **Step 5: Commit** — `lib: std FakeHttp`

### Task 4.2: Move chains into the library

**Files:**
- Move: `src/core/core/chains.{h,cpp}` → `libinnertube/src/core/chains.{h,cpp}`
- Create: `libinnertube/tests/tst_chains.cpp` (port of `tests/tst_meetube_chains.cpp`)
- Modify: `src/core/core/http.{h,cpp}` — Qt `core::Http` now **implements the std `yt::core::IHttp`** (converts `std::string` ↔ `QString`/`QByteArray` at its edges; `setBearer` = set + invalidate header memo; `log()` returns a `Logger` built by the app); delete the old `IHttp`/`Reply` from `http.h`
- Create: `src/core/bridge/qtresults.h` — the old Qt shapes (`Outcome<T>`, `VideoPage`, `WatchResult`, `CommentPage`, `PlaylistPage`, `UserPage`, `PlayerOutcome`, `RydVotes`, `DeviceCode`, `TokenGrant`) moved verbatim out of `chains.h`, plus `toQt()` overloads from `yt::Result<…>` / lib structs
- Modify: every facade delivery (`models/*.cpp`, `innertube/videodetails.cpp`, `channeldetails.cpp`, `accountdetails.cpp`, `streamset.cpp`, `searchsuggest.cpp`, `accountmanager.cpp`): convert **inside the worker-side callback**, before `invokeGui`:

```cpp
core::fetchVideoList(*api.http, spec, job,
    [api, job, self](const yt::Result<yt::VideoPage> &r) {
        const bridge::Outcome<bridge::VideoPage> q = bridge::toQt(r);   // worker thread
        api.host->invokeGui([job, self, q]() {
            if (!core::live(job)) return;   // MUST be first
            self->applyList(q);
        });
    });
```

- Delete: `tests/tst_meetube_chains.cpp`, `tests/testutil.h`'s Qt `FakeHttp` is replaced by a thin Qt adapter over the std one only if `tst_meetube_model`/`_account` still need QString helpers — otherwise they include `libinnertube/tests/fakehttp.h` directly.

**Interfaces:**
- Produces (lib, `src/core/chains.h`, std-only; same names/semantics as today, error strings preserved, codes added):

```cpp
namespace yt::core {
struct VideoListSpec { enum Kind { Browse, Search } kind = Browse;
    std::string browseId, params, page, query, order; };
enum ActionKind { Subscribe, Unsubscribe, Like, Dislike, RemoveLike };
struct PlayerInfo { Result<std::vector<Stream>> streams; Result<std::vector<Subtitle>> captions; };
struct DeviceCodeGrant { std::string deviceCode, userCode, verificationUrl; int intervalSecs = 5; };
struct TokenGrant { std::string accessToken, refreshToken, error; int expiresInSecs = 0;
                    bool transportOk = false; std::string transportError; };
void fetchVideoList(IHttp &, const VideoListSpec &, const JobToken &, Callback<VideoPage>);
void fetchWatch(IHttp &, std::string videoId, const JobToken &, Callback<WatchResult>);
void fetchComments(IHttp &, std::string videoId, std::string page, const JobToken &, Callback<CommentPage>);
void fetchPlayer(IHttp &, std::string videoId, const JobToken &, std::function<void(const PlayerInfo &)>);
void fetchChannelById(IHttp &, std::string channelId, const JobToken &, Callback<Channel>);
void fetchChannelByUrl(IHttp &, std::string url, const JobToken &, Callback<Channel>);
void fetchChannelSubscribed(IHttp &, std::string channelId, const JobToken &, Callback<bool>);
void fetchUserSearch(IHttp &, std::string query, const JobToken &, Callback<ChannelPage>);
void fetchChannelList(IHttp &, std::string browseId, std::string page, const JobToken &, Callback<ChannelPage>);
void fetchPlaylists(IHttp &, std::string resourceId, std::string page, std::string params, const JobToken &, Callback<PlaylistPage>);
void fetchPlaylistSearch(IHttp &, std::string query, const JobToken &, Callback<PlaylistPage>);
void fetchAccount(IHttp &, const JobToken &, Callback<Account>);
void submitAction(IHttp &, ActionKind, std::string targetId, const JobToken &, Callback<Done>);
void postComment(IHttp &, std::string createParams, std::string text, const JobToken &, Callback<Done>);
void editPlaylist(IHttp &, std::string playlistId, bool add, std::string id, const JobToken &, Callback<Done>);
void fetchDislikes(IHttp &, std::string videoId, const JobToken &, Callback<Votes>);
void fetchSearchSuggestions(IHttp &, std::string query, const JobToken &, Callback<std::vector<std::string>>);
void oauthDeviceCode(IHttp &, const JobToken &, Callback<DeviceCodeGrant>);
void oauthPollToken(IHttp &, std::string deviceCode, const JobToken &, std::function<void(const TokenGrant &)>);
void oauthRefresh(IHttp &, std::string refreshToken, const JobToken &, std::function<void(const TokenGrant &)>);
}
```

Error code assignment (new): transport/timeout/api/invalid → copied from `Reply::code`; `"channel unavailable"`, `"could not resolve channel"`, `"account unavailable"`, `"no playable streams"`, a non-playable `reason` → `Unavailable`; `"streams require signature decipher (unsupported)"` → `Unsupported`. `oj::Token` gains `std::optional<gj::FlexInt> expires_in` → `TokenGrant::expiresInSecs`. Logging: `PLOG()` → `YTLOG(http.log(), "player")`; drop `qPrintable`.

- [ ] **Step 1:** port every `tst_meetube_chains` case to doctest over the std `FakeHttp` (same queued fixtures, same `sent` substring asserts, same routing asserts via `lastClientFor`), add `CHECK(r.error->code == yt::ErrorCode::Unavailable)` on the "channel unavailable" case and `Unsupported` on the ciphered case, and `CHECK(g.expiresInSecs == 3599)` on a token fixture `{"access_token":"A","expires_in":3599}`.
- [ ] **Step 2:** FAIL. **Step 3:** port chains (mapping table), then the facades + qtresults. **Step 4:** `tst_chains` PASS; MeeTube `tst_meetube_model`, `_account`, `_client`, `_threading`, `_curlnam`, `_media` PASS; app runs in the Simulator: home feed, search, VideoPage (comments, related), channel page (memory: *smoke covers home only* — open the detail pages by hand).
- [ ] **Step 5: Commit** — `lib: chains on std IHttp; Qt Http implements it; bridge Qt results`

---

# Phase 5 — Public `yt::Client`

### Task 5.1: `ClientOptions`, `ClientInfo` overrides, catalog

**Files:**
- Modify: `libinnertube/include/yt/client.h` (full), create `libinnertube/include/yt/catalog.h`, `libinnertube/src/catalog.cpp`; `libinnertube/src/innertube/clientconfig.{h,cpp}` (table becomes overridable)
- Test: `libinnertube/tests/tst_catalog.cpp`

**Interfaces:**

```cpp
// include/yt/catalog.h
namespace yt {
enum class ClientId { WEB, WEB_SAFARI, IOS, ANDROID, ANDROID_VR, TVHTML5 };
struct ClientInfo { std::string name, version, userAgent; int id = 0; };
struct FeedEntry { std::string label, id; bool requiresAuth = false; };
struct SearchOrder { std::string label, value; };
struct SearchType { std::string label, kind; std::vector<SearchOrder> orders; };
std::vector<FeedEntry> topicFeeds();      // the six anonymous topic feeds (Innertube::navEntries)
std::vector<FeedEntry> feedSections();    // {"Home","FEwhat_to_watch",false}
std::vector<SearchType> searchTypes();    // Videos/Channels/Playlists with orders
ClientInfo defaultClientInfo(ClientId);
}
```

`ClientInfo` moves from `const char*` fields to `std::string`; the private `clientInfo(id)` reads a per-`HttpClient` table seeded from defaults + `ClientOptions::clients` overrides (ContextBuilder takes the table, not a global).

- [ ] **Step 1:** tests: `topicFeeds().size()==6`, first id `"FEnews_destination"`; `searchTypes()[0].orders.size()==4`; an override `{TVHTML5, {"TVHTML5","9.0","UA",7}}` makes `ContextBuilder::contextJson` emit `"clientVersion":"9.0"`. **Step 2:** FAIL. **Step 3:** implement (data copied from `innertube.cpp` `navEntries`/`searchTypes`/`feedSections` and `clientconfig.cpp`). **Step 4:** PASS. **Step 5: Commit** — `lib: catalog + runtime client-version overrides`

### Task 5.2: `Client` + `Job` (engine thread, executor delivery, cancellation contract)

**Files:**
- Create: `libinnertube/src/client.cpp`, `libinnertube/src/engine.h`, `libinnertube/include/yt/testing.h`, `libinnertube/tests/tst_client.cpp`

**Interfaces:**
- Produces: the `Client`/`Job`/`ClientOptions` API exactly as spec §3.1, plus:

```cpp
// include/yt/testing.h — unsupported, for consumers' unit tests
namespace yt::core { class IHttp; }
namespace yt::testing {
// A Client whose transport is `http` (e.g. a FakeHttp). Forces ownThread=false.
std::unique_ptr<Client> makeClientForTesting(ClientOptions, std::shared_ptr<core::IHttp> http);
}
```

Internals (`src/engine.h`): `struct Engine { net::Loop loop; std::unique_ptr<HttpTransport> transport; std::shared_ptr<core::IHttp> http; ClientOptions opts; std::thread thread; std::chrono::steady_clock::time_point bearerExpiry; bool refreshing = false; std::vector<std::function<void()>> afterRefresh; };` held by `std::shared_ptr<Engine>`; `Job` holds `JobToken` + `std::weak_ptr<Engine>`.

The one generic dispatcher every public method uses (in `client.cpp`):

```cpp
template <class T, class Chain>
Job Client::start(Chain chain, Callback<T> cb, bool authed) {
    Job j(core::newJob(), m_engine);
    const core::JobToken token = j.token();
    std::shared_ptr<Engine> eng = m_engine;
    eng->loop.post([eng, token, chain, cb, authed]() {
        if (!core::live(token)) return;
        auto run = [eng, token, chain, cb]() {
            chain(*eng->http, token, [eng, token, cb](const Result<T> &r) {
                auto deliver = [token, cb, r]() { if (core::live(token)) cb(r); };   // gate on the delivery thread
                if (eng->opts.deliver) eng->opts.deliver(deliver); else deliver();
            });
        };
        if (authed) ensureBearer(*eng, token, run); else run();
    });
    return j;
}
```

`Job::cancel()`: `token->canceled = true;` then, if the engine is alive, `loop.post([http, token]{ http->abort(token); })`.

`Client::Client`: on the **engine thread** (first posted task when `ownThread`, the ctor waits on a `std::promise`; inline otherwise): `transport = opts.transport ? opts.transport(opts.net) : makeDefaultTransport(opts.net)`; `nullptr` → the ctor throws `std::invalid_argument("innertube: no HTTP transport (built with INNERTUBE_HTTP_BACKEND=none?)")`; `loop.setDriver(transport->driver())`; build `HttpClient(loop, *transport, {timeoutMs, log=opts.log})`; seed `Session` from `opts.initial` (hl, gl, visitorData); visitor sink → updates the engine's `SessionState` copy and calls `onStateChanged` through `deliver`; `ownThread` → `thread = std::thread([eng]{ eng->loop.run(); })`.

`Client::~Client`: mark all jobs dead (a per-engine `std::atomic<bool> closing` checked inside `deliver` lambdas in addition to the token), `loop.post([eng]{ eng->http.reset(); eng->loop.setDriver(nullptr); eng->transport.reset(); eng->loop.quit(); })` (HttpClient before the transport it cancels on; the transport is destroyed on the engine thread), join (or, manual mode, run `runOnce(0)` until the post ran). After return no callback may run: deliveries already queued in a consumer executor check `closing` first.

`setLocale(hl, gl)`: post → `session().hl/gl = …; http->clearCache();` + `onStateChanged`.

- [ ] **Step 1: failing tests** — `tst_client.cpp` (manual mode + `FakeHttp`):

```cpp
static std::unique_ptr<yt::Client> mk(std::shared_ptr<FakeHttp> f, yt::ClientOptions o = {}) {
    return yt::testing::makeClientForTesting(std::move(o), f);
}
TEST_CASE("browse delivers a parsed page") {
    auto f = std::make_shared<FakeHttp>(); f->queue("browse", fixture("browse_feed.json"));
    auto c = mk(f); yt::Result<yt::VideoPage> got; bool called = false;
    c->browse({"FEwhat_to_watch"}, [&](const yt::Result<yt::VideoPage> &r) { got = r; called = true; });
    CHECK(!called);                 // never inline
    c->runOnce(0); f->flush();
    CHECK(called); CHECK(got.ok()); CHECK(!got.value.items.empty());
}
TEST_CASE("cancelOnDeliveryThreadSuppresses") {
    // deliver = queue into a std::vector<std::function<void()>>; start browse; runOnce; flush;
    // the queued delivery exists; job.cancel(); run the queued delivery -> callback NOT called
}
TEST_CASE("cancelFromOtherThreadAtMostOnce") {
    // real thread mode against conformance/loopback.h: 200 iterations of start + cancel() from a second
    // std::thread at random delays; per job count callback invocations <= 1 (TSan clean)
}
TEST_CASE("destroyWithInflight") {
    // ownThread=true against loopback with delayMs=300; start 5 requests; destroy Client
    // immediately; sleep 500ms; CHECK(no callback ran); ASan: no leak
}
TEST_CASE("a runtime transport factory overrides the build-time backend") {
    // opts.transport = [&](const yt::TransportConfig &c){ made = true; CHECK(c.ipResolve == yt::IpResolve::V4); return std::make_unique<FakeTransport>(); };
    // opts.net.ipResolve = V4; a real Client (testing baseUrl overload not needed): browse -> FakeTransport.started[0].url
    // == "https://www.youtube.com/youtubei/v1/browse?prettyPrint=false"; made == true
}
TEST_CASE("no transport -> constructor throws") {
    // opts.transport = [](const yt::TransportConfig &){ return std::unique_ptr<yt::HttpTransport>(); };
    // CHECK_THROWS_AS(yt::Client(opts), std::invalid_argument)
}
TEST_CASE("setLocale reaches the context and clears the cache") {
    // setLocale("ru","RU"); browse -> f->sent.back() contains "\"hl\":\"ru\"" and "\"gl\":\"RU\""
}
TEST_CASE("visitorData capture surfaces via onStateChanged") {
    // queue browse reply carrying responseContext.visitorData "VX" through a FakeHttp that
    // invokes its visitor sink; onStateChanged received state.visitorData == "VX"
}
```

`BrowseRequest`/`SearchRequest` (in `client.h`): `struct BrowseRequest { std::string browseId, params, page; }; struct SearchRequest { std::string query, order; };`.

Write all cases fully. `cancelFromOtherThreadAtMostOnce` and `destroyWithInflight` need the real transport against the loopback, without adding a public `ClientOptions` field: add the overload `yt::testing::makeClientForTesting(ClientOptions, std::string baseUrl)` — it builds a real `HttpClient` over the default transport with that base URL and keeps `ownThread` as given.

- [ ] **Step 2:** FAIL. **Step 3:** implement `Client` methods — each one line over `start<T>`, e.g.

```cpp
Job Client::watch(std::string id, Callback<WatchResult> cb) {
    return start<WatchResult>([id](core::IHttp &h, const core::JobToken &t, Callback<WatchResult> d) {
        core::fetchWatch(h, id, t, std::move(d)); }, std::move(cb), /*authed*/ signedInHint());
}
```

`authed` = "a refresh token is held" (bearer only matters then; anonymous calls skip `ensureBearer`). `player()` uses a `start`-like variant taking `std::function<void(const PlayerInfo&)>`.

- [ ] **Step 4:** PASS, including ASan and TSan builds of `tst_client`.
- [ ] **Step 5: Commit** — `lib: yt::Client — engine thread, executor delivery, cancellation contract`

### Task 5.3: `Auth` — device flow, proactive refresh, 401 retry

**Files:**
- Create: `libinnertube/include/yt/auth.h`, `libinnertube/src/auth.cpp`, `libinnertube/tests/tst_auth.cpp` (port of `tests/tst_meetube_account.cpp` flow cases)

**Interfaces:** spec §3.1 `Auth` / `DeviceCode`. Internal: `void ensureBearer(Engine &, const core::JobToken &, std::function<void()> run);`

Behaviour:
- `signIn(onCode, onDone)`: `oauthDeviceCode` → `onCode` (via deliver) → `loop.after(interval*1000, poll)`; poll → `oauthPollToken`: access token → `http->setBearer(access)`, `bearerExpiry = now + expiresIn` (default 3600 when 0), `state.refreshToken = refresh`, `state.account = Account{"default","YouTube"}`, `http->clearCache()`, `onStateChanged`, `onDone(ok)`; `authorization_pending`/`slow_down` → re-arm (slow_down adds 5 s, RFC 8628); else `onDone(Error{Api, err})`. The returned `Job` cancels the flow (timer canceled, no further callbacks).
- `signOut()`: post → bearer "", refreshToken "", account reset, `clearCache()`, `onStateChanged`.
- `ensureBearer`: if `refreshToken` empty → `run()` (chain will get anonymous behaviour). Else if bearer non-empty and `now < bearerExpiry - 60s` → `run()`. Else queue `run` in `afterRefresh`; if not `refreshing`: `oauthRefresh` → on success `setBearer` + expiry, run all queued; on failure run them anyway (they fail with the server's 401 → surfaced).
- 401 retry (in `start<T>` for `authed` calls): if `r.error && r.error->httpStatus == 401 && !retried` → force expiry, `ensureBearer`, re-run chain once with `retried=true`.
- Startup with a stored refresh token: bearer empty → first authed call refreshes (replaces `AccountManager::restore()` and the `prefetchLaunchFeed` wait-for-bearer dance).

The engine clock is injectable for tests: `std::function<std::chrono::steady_clock::time_point()> now` in `Engine`, settable via `yt::testing::setClock(Client&, fn)`.

- [ ] **Step 1: failing tests**:

```cpp
TEST_CASE("device flow: pending -> granted persists refresh token") {
    // FakeHttp: device code {"device_code":"D","user_code":"U","verification_url":"V","interval":1}
    // token polls: {"error":"authorization_pending"} then {"access_token":"A","refresh_token":"R","expires_in":3600}
    // drive runOnce with a fake clock advancing 1s; CHECK onCode got ("V","U"); onDone ok;
    // onStateChanged.refreshToken=="R"; f->session().bearer=="A"; auth().signedIn()
}
TEST_CASE("refreshesExpiredBearerBeforeCall") {
    // initial.refreshToken="R"; first browse FEsubscriptions -> f->sentForm.back() is the token URL with
    // grant_type=refresh_token BEFORE the browse post; advance clock 3601s; next authed call refreshes again;
    // within expiry no extra refresh
}
TEST_CASE("retries401Once") {
    // authed browse replies 401 twice in a row -> exactly one refresh + one retry, then the
    // callback gets !ok with httpStatus 401 (no loop)
}
TEST_CASE("concurrent authed calls share one refresh") {
    // 3 authed calls while expired -> 1 refresh form post, 3 browse posts
}
TEST_CASE("signOut clears state") { /* onStateChanged.refreshToken empty; session().bearer empty */ }
TEST_CASE("cancel signIn stops polling") { /* job.cancel(); advance clock 10s; no further token posts; onDone not called */ }
```

FakeHttp needs `httpStatus` on queued replies: `queue(endpoint, body, long status = 200)`; status 401 → `Reply{ok=false, code=Api, httpStatus=401}`.

- [ ] **Step 2:** FAIL. **Step 3:** implement. **Step 4:** PASS. **Step 5: Commit** — `lib: Auth device flow with proactive refresh and single 401 retry`

### Task 5.4: `examples/yt-cli` — the framework-free smoke

**Files:** Create `libinnertube/examples/yt-cli/main.cpp`, `libinnertube/examples/CMakeLists.txt`; option `INNERTUBE_BUILD_EXAMPLES` (default = top-level).

Commands: `yt-cli feed <browseId>`, `yt-cli search <query>`, `yt-cli streams <videoId>`, `yt-cli signin` (prints code, waits, writes `SessionState` to `./yt-cli-state.json` via Glaze — the example may use Glaze, it links it PRIVATE itself). Uses `ownThread=true`, `deliver` = a `std::mutex`+`std::condition_variable` queue drained by `main`.

- [ ] **Step 1:** build; **Step 2:** `yt-cli search "кот"` prints ≥ 1 title (live network — manual check, not ctest); `yt-cli streams dQw4w9WgXcQ` prints itags. **Step 3: Commit** — `lib: yt-cli example`

---

# Phase 6 — MeeTube on `yt::Client` (the switch)

### Task 6.1: Bridge seam + GUI executor

**Files:**
- Create: `src/core/bridge/guiexecutor.{h,cpp}` (the GUI half of `WorkerHost`: `CallEvent` + `Dispatcher` on the GUI thread; `yt::Executor executor()` returning `[d](std::function<void()> fn){ QCoreApplication::postEvent(d, new CallEvent(fn)); }`; inline when `setInline(true)` for tests)
- Create: `src/core/bridge/bridgeapi.h`: `struct BridgeApi { yt::Client *client = nullptr; bridge::GuiExecutor *gui = nullptr; };`
- Test: `tests/tst_meetube_threading.cpp` — replace the `WorkerHost` cases with `GuiExecutor` cases (posted closure runs on the GUI thread from a `std::thread`; inline mode runs synchronously)

**Interfaces:** facades' test seam changes from `virtual yt::ApiRef apiRef() const` to `virtual bridge::BridgeApi api() const` (same override pattern).

- [ ] Steps: test → FAIL → implement → PASS → **Commit** `bridge: GuiExecutor + BridgeApi seam`

### Task 6.2: `Innertube` + `AccountManager` bridge on `yt::Client`

**Files:**
- Modify: `src/core/innertube/innertube.{h,cpp}`, `src/core/innertube/accountmanager.{h,cpp}`
- Move: `src/core/innertube/settingsstore.{h,cpp}` → `src/app/settingsstore.{h,cpp}` (unchanged file format; gains `yt::SessionState state() const` + `void setState(const yt::SessionState &)` — accounts[active].refreshToken/identity, visitorData, region)

`Innertube` ctor builds:

```cpp
yt::ClientOptions o;
o.net.caBundlePath = MEETUBE_CA_BUNDLE;             // compile def stays app-side
o.net.ipResolve = yt::IpResolve::V4;                // N9 networks (memory: n9-ipv4-only-curl)
o.net.log = appLogger();                            // curl backend's [net] trace
// o.transport left empty: the build-time curl backend (static curl+wolfSSL on the N9)
o.log = appLogger();                                // MEETUBE_DEBUG parsing moves to src/app
o.initial = m_store.state();
o.onStateChanged = [this](const yt::SessionState &s) { m_store.setState(s); };   // runs on GUI
o.deliver = {};                                     // engine thread: bridge converts, then hops
m_client.reset(new yt::Client(o));
```

`onStateChanged` must run on the GUI thread (it writes the store): wrap — `o.onStateChanged = [gui](const SessionState &s){ gui->post([s]{ store.setState(s); }); }` — the lib calls it on the engine thread when `deliver` is empty.

`AccountManager` keeps its QML surface (`signedIn`, `signIn/cancel/signOut/restore`, signals `userCodeReady/authenticated/authFailed/bearerChanged/signedInChanged`): `signIn()` → `client.auth().signIn(onCode→userCodeReady, onDone→authenticated|authFailed + signedInChanged + bearerChanged)`; `restore()` becomes a no-op (lazy refresh in the lib); `currentBearer()` is deleted (only `Innertube::applyBearer` used it). `prefetchLaunchFeed()` → always `prefetchNow()` (the lib refreshes before the authed Home browse). `applySettings(region, language)` → `client.setLocale(language, region)` (empty keeps the current value — read it from the store first).

- [ ] Steps: port `tests/tst_meetube_account.cpp` onto `yt::testing::makeClientForTesting` + std `FakeHttp` + `GuiExecutor` inline (assert the same signals with `QSignalSpy`) → FAIL → implement → PASS → Simulator: sign-in flow with the real TV code page (manual) → **Commit** `bridge: Innertube/AccountManager on yt::Client`

### Task 6.3: Facades and models on `BridgeApi`

**Files:**
- Modify: `src/core/models/{videomodel,commentmodel,playlistmodel,channelmodel}.cpp`, `src/core/innertube/{videodetails,channeldetails,accountdetails,streamset,searchsuggest,videoapi,channelapi,playlistapi}.cpp`

Pattern (every delivery):

```cpp
const bridge::BridgeApi a = api();
if (!a.client) { applyList(bridge::notSupported<bridge::VideoPage>()); return; }
VideoModel *self = this; const core::JobToken job = m_job;
m_call = a.client->browse({resourceId.toUtf8().constData(), params.toUtf8().constData(), {}},
    [a, job, self](const yt::Result<yt::VideoPage> &r) {      // engine thread
        const bridge::Outcome<bridge::VideoPage> q = bridge::toQt(r);
        a.gui->post([job, self, q]() { if (!core::live(job)) return; self->applyList(q); });
    });
```

`cancelJob()` → `m_job->canceled = true; m_call.cancel();` (`yt::Job m_call` replaces the manual `abort` post). `signedIn()` overrides → `Innertube::instance()->client().auth().signedIn()`.

- [ ] Steps: port `tst_meetube_model` onto the new seam (same assertions: row counts, roles, paging, cancel-suppresses, optimistic like revert on failure, Watch Later session memory) → FAIL → implement → PASS → Simulator: home, search (videos/channels/playlists), VideoPage (like/dislike/save/subscribe/comments/related), ChannelPage, AccountPage feeds, Settings region change → **Commit** `bridge: facades and models on yt::Client`

### Task 6.4: Delete the Qt backend; rename to `meetube-bridge`

**Files:**
- Delete: `src/core/core/http.{h,cpp}`, `src/core/threading/workerhost.{h,cpp}`, `src/core/innertube/apiref.h`, `src/core/innertube/contextbuilder.*` leftovers, `tests/tst_meetube_client.cpp` (covered by `tst_http`), the temporary `libinnertube/src` include dir from Task 0.2
- Move: `src/core/` → `src/bridge/` (target `meetube-bridge`); `src/core/net/curl*` → `src/bridge/net/` (QML thumbnails + the player NAM until Phase 7); `core/debuglog.*` → `src/app/`; `core/status.h` → `src/bridge/status.h`
- Modify: `src/CMakeLists.txt`, `src/app/CMakeLists.txt`, `tests/CMakeLists.txt`, **CLAUDE.md** (Architecture: libinnertube + bridge; test count)

- [ ] Steps: build both targets; `ctest` all green (lib suites + bridge suites); `grep -rn 'Innertube::instance' src/bridge | wc -l` ≤ the facade `api()`/`signedIn()` defaults only → **Commit** `bridge: drop the Qt transport and WorkerHost; meetube-core -> meetube-bridge`

### Task 6.5: Device verification (gate for Phase 7)

Per `memory/n9-device-deploy.md` (`dpkg -i` with `AEGIS_FIXED_ORIGIN`, never `cp` over the binary): cold launch time with `MEETUBE_DEBUG=startup` (compare to the 2026-09 profile), home feed, search Cyrillic, VideoPage (all buttons), channel, sign-in via code, **leave the app signed in > 65 min, then open Subscriptions** (Review Focus 1), region switch, 360p/480p/720p playback still on the old player path, RYD dislike count (credential-free GET through the library's own redirect policy). Record results in the spec's Results section. **Commit** `docs: Phase 6 device verification`.

---

# Phase 7 — `innertube::media` (own detailed plan at phase start)

Scope check: this phase touches device-verified playback (memory: *dual-stream-playback*, *n9-video-overlay*); write `docs/superpowers/plans/<date>-innertube-media.md` with full TDD steps when Phase 6 is verified. Fixed scope and interfaces:

| Task | Library deliverable (Qt-free, `libinnertube/media/`, target `innertube_media`) | App change | Acceptance |
|---|---|---|---|
| 7.1 | `Fmp4Demuxer` on `std::vector<uint8_t>`/`std::span`-like `(const uint8_t*, size_t)` API; `feed()`, `takeSamples()`, `seekOffsetForNs()`, `segmentStartsNs()` unchanged semantics | `MediaPump` uses it (QByteArray ↔ bytes at the edge) | ported `tst_meetube_media` demux cases + libFuzzer target from `tests/fuzz` green; device 480p/720p |
| 7.2 | `BufferPlanner` std (only `qint64` today); `prebufferFrames(fps, std::optional<int> override)` — env `MEETUBE_PREBUFFER_FRAMES` read app-side | `ProgressiveSource` passes the override | planner tests ported |
| 7.3 | `parseCaptionsJson3(std::string_view) -> std::vector<Cue{int startMs,endMs; std::string text}>` (Glaze) + `captionsUrl(baseUrl)` forcing `fmt=json3` byte-exact | `SubtitleTrack` drops `QXmlStreamReader` | fixture-based tests incl. `%2C` preservation |
| 7.4 | `parseHlsMaster(std::string_view, std::string_view baseUrl) -> {variants, audio}` | `HlsSource` uses it | ported HLS cases |
| 7.5 | `HttpClient::stream(url, headers, onHead, onBody, onDone)` (policy-wrapped streaming GET: redirects incl. googlevideo `redirector` 302s, timeouts, cap off) + `RangedSource` on it: Range windows, read-ahead, EWMA via `BufferPlanner`, pull API `open/request/seek/close` with callbacks — so media works on **any** backend | `ProgressiveSource` becomes a QObject shell over it; the player's `CurlNetworkAccessManager` goes away | loopback tests (206 ranges, short tail EOF, mid-window failure); device: start, seek, minimize/restore, captions |

Stays in MeeTube permanently: `StreamPlayer`, `MediaPump` (QObject/QThread shell), `IPipeline`/`GstAppPipeline`, `IPolicy`/`PolicyGuard`, `playbackmode.h` colour key, `EglVideoItem`.

---

# Phase 8 — Productization (own detailed plan at phase start)

| Task | Deliverable | Acceptance |
|---|---|---|
| 8.1 Install/export | `install(TARGETS innertube EXPORT …)`, `include/yt` install, `innertubeConfig.cmake` + version file, `innertube::innertube`/`innertube::media`; `find_dependency(CURL 7.68)` | a throwaway consumer `cmake -S consumer` with `find_package(innertube 0.1 REQUIRED)` builds and runs `yt-cli`-like main at `-std=c++17` |
| 8.2 Compiler portability | MSVC flags (`/std:c++latest`, `/utf-8`), Clang ≥ 17, AppleClang; Winsock in `conformance/loopback.h` (`#ifdef _WIN32`) | CI green on all |
| 8.3 CI matrix | GitHub Actions: ubuntu GCC 14 + Clang (Release, ASan+UBSan, **TSan**), macOS, windows-msvc, android-ndk r27 arm64 build-only; `innertube_no_qt` + both conformance suites (curl, threaded) on every job; one job with `INNERTUBE_HTTP_BACKEND=none` (no libcurl installed) and one with a path backend (`examples/threaded-transport/threadedtransport.cpp`) | all jobs green |
| 8.4 Fuzzing | libFuzzer targets: `parseVideoList`, `parseWatchPage`, `parsePlayer`, `parseComments`, `Fmp4Demuxer::feed` with the fixtures as seed corpus | 10 min each, no crash |
| 8.5 Docs | `README.md` (quick start), `docs/API.md` (threading & cancellation contract §3.2 verbatim, error codes, auth flow), `CHANGELOG.md`, versioning policy (SemVer; static-first; no ABI promise before 1.0) | reviewed |
| 8.6 Repo split | `git subtree split --prefix=libinnertube -b innertube-cpp` → new repo; vendored `third_party/glaze` submodule; MeeTube: delete `libinnertube/`, add `deps/innertube-cpp` submodule, `add_subdirectory(deps/innertube-cpp)` with `INNERTUBE_GLAZE_DIR=${CMAKE_SOURCE_DIR}/deps/glaze` | both MeeTube targets + device build green from the submodule (check `git merge-base` before any force-push — memory: *repo-remote-diverged*) |
| 8.7 License | apply the D2 decision (header notices + `LICENSE`) | owner sign-off |
| 8.8 Release | tag `v0.1.0` | — |
| 8.9 Native backends *(on demand, own plan each)* | `backends/winhttp`, `backends/nsurlsession` (ObjC++), `backends/android` (JNI → `HttpURLConnection`), `backends/emscripten` (`emscripten_fetch`) | each passes `runTransportConformance` on its platform's CI runner |
| 8.10 Builtin socket backend *(on demand, own plan; baileys-cpp model)* | `backends/builtin`: `net/sockets.h` free-function socket layer (`posix`/`win32`/your `.cpp` via `INNERTUBE_SOCKET_BACKEND`), minimal HTTP/1.1 client (Content-Length + chunked, keep-alive, optional zlib inflate), TLS via wolfSSL/mbedTLS + a CA bundle; trigger: a target with neither libcurl nor a native HTTP API | passes `runTransportConformance` + its own socket conformance test |

---

## Self-review (done)

- **Spec coverage:** §1 layers → Phases 1–7; §1.3 defects → Task 5.3 (token expiry), Task 2.1 (no public mutable session), Task 7.3 (json3 captions); D1–D11 → Tasks 0.1 (D4/D9/D10), 1.1 (D7), 2.2 (no env), 5.1 (D11), 5.2 (D5/D6/D8), 6.2 (D8 persistence app-side), 7 (D3), 8.6 (D10), 8.7 (D2).
- **Backends (D12, §3.5):** interface Task 3.2; policy Task 3.3; curl + conformance + build selection Task 3.4; self-driven reference + guide Task 3.5; runtime factory Task 5.2; media on any backend Task 7.5; CI/none/path builds Task 8.3; native + builtin backends 8.9/8.10.
- **Type consistency:** `yt::Channel` (not `User`) in lib, `CT::User` only in the bridge; `Result<T>`/`ErrorCode` names identical across Tasks 1.1, 3.2, 4.2, 5.x; `Reply::code`/`httpStatus` defined in 3.3 and used in 4.2/5.3; `setBearer` defined in 3.3 IHttp, used in 5.3; `HttpTransport`/`TransportConfig`/`TransportFactory` defined in 3.2, used in 3.3–3.5/5.2/6.2; `pctEncode`/`formEncode` defined in 3.3, used in 4.2; `BridgeApi`/`GuiExecutor::post` defined in 6.1, used in 6.2/6.3.
- **Review Focus:** each of the five lines has a named test in its owning task.
- **Deferred detail:** Phases 7–8 deliberately get their own TDD plans (device-coupled / CI-coupled); their scope, interfaces and acceptance are fixed above.
