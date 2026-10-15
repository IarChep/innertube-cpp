# innertube-cpp

A Qt-free C++ client library for YouTube's InnerTube API (`youtubei/v1`), the
private API the YouTube apps and website use.

The library was extracted from [MeeTube](https://github.com/IarChep/MeeTube), a YouTube client
for the Nokia N9 (MeeGo Harmattan), and is built to run well on that phone: 1 GB of
RAM, an ARM CPU, no DOM. Responses are parsed in a single pass with
[Glaze](https://github.com/stephenberry/glaze), and nothing ties it to the N9. The
public API is plain C++17 with `std` types only, so it fits any desktop, mobile or
embedded C++ project.

## Features

- **Browse**: home and topic feeds, channels, playlists, history, subscriptions, Watch Later, Liked.
- **Links**: every method takes what the user pasted — a bare id, a watch / `youtu.be` /
  shorts / live / embed URL, `@handle`, `/channel/`, `/c/`, `/user/` or `?list=` URL
  (`yt::Link` parses them without the network).
- **Search**: videos (relevance, upload date, views or rating order), channels, playlists, and
  query suggestions. Every list is a `yt::Page<T>`; `yt.more(page.next)` fetches the next one.
- **Watch**: video details, related videos, comments with paging, posting comments.
- **Streams**: every source YouTube serves without player JavaScript — progressive, adaptive,
  HLS and SABR — each tagged with the client (`VISIONOS` first, then `ANDROID_VR`, signed-in
  `TVHTML5`, `IOS`) and the User-Agent to fetch it with; you choose what to play, or let
  `yt::choose()` pick within your codecs, height and audio language. Also a precise
  playability verdict (bot check, age gate, scheduled start…), multi-language audio tracks,
  captions as timed cues (`captionFor`, `cueAt`), storyboards (seek previews) and the
  videostats URLs.
- **Account**: OAuth 2.0 device-code sign-in ("enter this code on another device", with
  YouTube's own QR image), a revoked token signs the client out, proactive
  token refresh, one retry on HTTP 401, subscribe/unsubscribe, like/dislike, playlist edits,
  account info.
- **Votes**: like/dislike counts from [Return YouTube Dislike](https://returnyoutubedislike.com).
- **Transport policy**: identical requests are coalesced; feeds are cached per endpoint with a
  TTL; every request has a timeout and can be canceled; redirects and the body size are bounded;
  only `http`/`https` URLs are allowed.
- **Pluggable HTTP backend**: libcurl by default, or your own backend (WinHTTP, NSURLSession,
  Android, emscripten…) checked by the bundled conformance suite.

## Limitations

- InnerTube is an unofficial, undocumented API, so YouTube can change it at any time.
  Client names and versions can be overridden at runtime (`ClientOptions::clients`)
  without a new release of the library.
- There is no player-JS handling: no signature or `n`-parameter decipher and no PoToken.
  Sources come only from clients that serve ready-to-use URLs; ciphered formats are listed
  with `Format::sealed` and no URL. Caption machine translation needs a PoToken and is not
  offered.

## Building

You need CMake ≥ 3.20, a C++23 compiler for the library itself (recent GCC or Clang),
and libcurl ≥ 7.68 for the default backend. Glaze and doctest are git submodules.

```sh
git clone --recursive https://github.com/IarChep/innertube-cpp.git
cd innertube-cpp
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
./build/examples/yt-cli search "rick astley"
```

| CMake option | Default | Meaning |
|---|---|---|
| `INNERTUBE_HTTP_BACKEND` | `curl` | `curl`, `none` (you pass `ClientOptions::transport`), or the path of your own backend `.cpp` |
| `INNERTUBE_HTTP_BACKEND_LIBS` | — | extra link libraries for a custom backend |
| `INNERTUBE_BUILD_TESTS` | top-level only | the doctest suites |
| `INNERTUBE_BUILD_EXAMPLES` | top-level only | `yt-cli`, `yt-watch`, `yt-channel`, `yt-suggest` |
| `INNERTUBE_GLAZE_DIR` | `third_party/glaze` | use another Glaze checkout (a `glaze` target is reused if present) |
| `INNERTUBE_INSTALL` | top-level only | install rules: headers, `libinnertube.a`, the CMake package and `innertube.pc` |

## Using it in your project

```cmake
include(FetchContent)
FetchContent_Declare(innertube
    GIT_REPOSITORY https://github.com/IarChep/innertube-cpp.git
    GIT_TAG        main)
FetchContent_MakeAvailable(innertube)

target_link_libraries(myapp PRIVATE innertube::innertube)
```

A `git submodule` + `add_subdirectory(innertube-cpp)` setup works the same way. Your
code only needs C++17, because Glaze and libcurl stay private to the library.

Or install it and find it like any other package:

```sh
cmake --install build --prefix /opt/innertube
```

```cmake
find_package(innertube 0.1 REQUIRED)        # CMAKE_PREFIX_PATH=/opt/innertube
target_link_libraries(myapp PRIVATE innertube::innertube)
```

Without CMake (an SDK, a Makefile): `pkg-config --cflags --libs innertube` with
`PKG_CONFIG_PATH=/opt/innertube/lib/pkgconfig`. The `.pc` file finds its prefix from its own
location, so the installed tree can be moved, for example into a sysroot. Version 0.x: a minor
release may break the API, and the package version file only accepts the same minor version.

```cpp
#include <yt/yt.h>
#include <cstdio>

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    yt::Client yt;                                       // its own engine thread
    yt::Result<yt::Player> r = yt.videos().player(argv[1]).get();   // any link or id
    if (!r.ok()) { std::printf("error: %s\n", r.error->message.c_str()); return 1; }
    std::printf("%s\n", r.value.details.title.c_str());

    yt::Choice c = yt::choose(r.value);                  // the best way to play it
    if (c.video) std::printf("video %s\naudio %s\n", c.video->url.c_str(), c.audio->url.c_str());
    else if (!c.hls.empty()) std::printf("hls %s\n", c.hls.c_str());

    auto page = yt.search().videos("rick astley").get();  // every list is a yt::Page
    if (page.ok() && page.value.next) page = yt.more(page.value.next).get();   // the second page
}
```

In a GUI, use the callback form instead: it returns a `yt::Job` you can cancel, and the
callback runs where `ClientOptions::deliver` sends it:

```cpp
job = yt.videos().get(link, [this](const yt::Result<yt::Video> &r) { show(r); });
```

More complete programs are in [`examples/`](examples):

| Example | Shows |
|---|---|
| [`yt-cli`](examples/yt-cli/main.cpp) | the blocking form (`std::future`), sign-in, persisted state |
| [`yt-watch`](examples/watch/main.cpp) | callbacks with `runOnce()`, three requests in flight, comment paging |
| [`yt-channel`](examples/channel/main.cpp) | one link, two requests |
| [`yt-suggest`](examples/suggest/main.cpp) | canceling a request when a newer one is started |

## Threading and cancellation

- Every `Client`, group (`videos()`, `search()`, …) and `Auth` method may be called from any
  thread. It only posts work: network I/O, parsing and request logic run on the engine thread.
- Every operation also has a form without the callback that returns a `std::future`. It is
  fulfilled straight from the engine thread (not through `deliver`), so blocking any thread
  on `get()` is safe — except the engine thread itself, inside a callback; with
  `ownThread = false` someone must keep calling `runOnce()`. A Client destroyed first
  releases pending futures with `ErrorCode::Canceled`.
- By default (`ownThread = true`) the Client owns that thread. With `ownThread = false`
  you drive it yourself by calling `runOnce()`.
- Callbacks run where `ClientOptions::deliver` sends them. A GUI app passes a function
  that posts to its UI thread; without one, callbacks run on the engine thread.
- Each callback runs **exactly once or never**, and never from inside the call that
  started it.
- `Job::cancel()` (from any thread) guarantees no later callback when it is called on the
  delivery thread. `~Client` cancels everything, and no callback runs after it returns.

The full contract is in the header of [`include/yt/client.h`](include/yt/client.h).

## Raw requests (unstable)

For an InnerTube call the groups do not cover yet, `yt.raw().post(endpoint, client, json)`
sends it with everything the library adds — the context (client identity, locale,
visitorData), the bearer when signed in, timeouts, the 401 retry — and returns the response
JSON as text. Nothing is parsed for you, and YouTube changes these endpoints without notice:
prefer a group method, and ask for one when you need the same call twice.

```cpp
yt::Result<std::string> r = yt.raw().post("navigation/resolve_url", yt::ClientId::WEB,
                                          R"({"url":"https://www.youtube.com/@NASA"})").get();
```

## Sign-in and persistence

```cpp
o.initial = loadState();                                   // hl/gl, visitorData, refresh token
o.onStateChanged = [](const yt::SessionState &s) { saveState(s); };

client.auth().signIn(
    [](const yt::DeviceCode &dc) { show(dc.verificationUrl, dc.userCode, dc.qrPng); },   // qrPng may be empty
    [](const yt::Result<yt::Done> &r) { /* signed in, or r.error */ });
```

Only the refresh token is persisted, through `onStateChanged`. Access tokens are minted
on demand and refreshed before they expire. Store the state with owner-only permissions:
it is a credential.

## HTTP backends

All networking goes through one small interface, `yt::HttpTransport`. Policy (schemes,
redirects, timeouts, body cap, cancellation, thread marshalling) stays in the library,
so a backend cannot weaken it. A backend is supported when
`yt::conformance::runTransportConformance()` reports no failures. See
[docs/backends.md](docs/backends.md); [`examples/threaded-transport`](examples/threaded-transport)
is a complete reference backend.

## Repository layout

```
include/yt/     public API (C++17)
src/            implementation: client, auth, request chains, HTTP policy, JSON, parsers
backends/       the libcurl backend and the no-backend stub
conformance/    the backend conformance suite + a loopback HTTP server
tests/          doctest suites and fixtures
examples/       example programs
docs/           backend guide, design spec and plan
```

## Contributing

Conventions for code, tests and commits are in [AGENTS.md](AGENTS.md). It is written for
coding agents and applies equally to humans.

## License

GPL-3.0 — see [COPYING](COPYING).

innertube-cpp is not affiliated with, endorsed by, or connected to Google or YouTube.
