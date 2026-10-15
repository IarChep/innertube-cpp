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
- **Search**: videos (relevance, date, views or rating order), channels, playlists, and query suggestions.
- **Watch**: video details, related videos, comments with paging, posting comments.
- **Streams**: every source YouTube serves without player JavaScript — progressive, adaptive,
  HLS and SABR — each tagged with the client (`VISIONOS` first, then `ANDROID_VR`, signed-in
  `TVHTML5`, `IOS`) and the User-Agent to fetch it with; you choose what to play. Also a
  precise playability verdict (bot check, age gate, scheduled start…), multi-language audio
  tracks, captions as timed cues, storyboards (seek previews) and the videostats URLs.
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

```cpp
#include <yt/client.h>
#include <cstdio>

int main()
{
    yt::ClientOptions o;
    o.ownThread = false;               // no extra thread: callbacks run inside runOnce()
    yt::Client client(o);

    bool done = false;
    client.search({"rick astley", "relevance"}, [&](const yt::Result<yt::VideoPage> &r) {
        if (r.ok())
            for (const yt::Video &v : r.value.items)
                std::printf("%s  %s\n", v.id.c_str(), v.title.c_str());
        else
            std::fprintf(stderr, "error: %s\n", r.error->message.c_str());
        done = true;
    });
    while (!done) client.runOnce(100);
}
```

More complete programs are in [`examples/`](examples):

| Example | Shows |
|---|---|
| [`yt-cli`](examples/yt-cli/main.cpp) | own engine thread, a delivery queue, sign-in, persisted state |
| [`yt-watch`](examples/watch/main.cpp) | several requests in flight, comment paging |
| [`yt-channel`](examples/channel/main.cpp) | chained requests |
| [`yt-suggest`](examples/suggest/main.cpp) | canceling a request when a newer one is started |

## Threading and cancellation

- Every `Client` method may be called from any thread. It only posts work: network I/O,
  parsing and request logic run on the engine thread.
- By default (`ownThread = true`) the Client owns that thread. With `ownThread = false`
  you drive it yourself by calling `runOnce()`.
- Callbacks run where `ClientOptions::deliver` sends them. A GUI app passes a function
  that posts to its UI thread; without one, callbacks run on the engine thread.
- Each callback runs **exactly once or never**, and never from inside the call that
  started it.
- `Job::cancel()` (from any thread) guarantees no later callback when it is called on the
  delivery thread. `~Client` cancels everything, and no callback runs after it returns.

The full contract is in the header of [`include/yt/client.h`](include/yt/client.h).

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
