# API usability research: what makes a client library easy, and what it means for libinnertube

**Status:** research (2026-10-15). R1 done (scenarios); R2–R5 implemented (plan 2026-10-15-api-groups).

**Question.** libinnertube must become the simplest, clearest library a consumer can pick up:
a small, obvious API that is still complete. What do the libraries people *like* to use have in
common, and where does libinnertube stand against them?

**Method.** Read the READMEs, docs and source of ~30 projects and guides, in five groups:

| Group | Sources |
|---|---|
| YouTube libraries in other languages (closest analogues) | YoutubeExplode (C#), YouTube.js / youtubei.js (JS), pytubefix (Python), YouTubeKit (Swift), RustyPipe (Rust), NewPipe Extractor (Java), ytmusicapi (Python), Invidious API |
| C++ libraries known for being pleasant | cpr, cpp-httplib, nlohmann/json, spdlog, D++ (Discord), libmpv client API, TDLib |
| API design guides | Qt "Designing Qt-Style C++ APIs", Cwalina's scenario-driven design (.NET Framework Design Guidelines), Rust API Guidelines, C++ Core Guidelines (I, F), Muratori's reusable-components axes, stb howto |
| Large SDK rulebooks | Azure SDK general guidelines + API design, google-cloud-cpp (StatusOr ADR, StreamRange), go-github, AWS-style Outcome (by contrast) |
| Async, packaging, stability, docs | Asio completion tokens, Pitchfork layout, KDE binary-compatibility policy, Hyrum's law, Diátaxis |

Links are listed at the end.

---

## 1. Thirteen patterns the best libraries share

### P1. Design from the user's code, not from the object model
Cwalina: *"Frameworks must be designed starting from a set of usage scenarios and code samples
implementing these scenarios"* — write the code a user will write for the top 5–10 scenarios
first, then the API that makes it possible; *"Do not start API design with object model
diagrams."* Qt's QA advice is the same: write the use cases first, *"write the documentation
first; if you can't name something precisely, it may not need to exist"*, and let someone else
try the API with and without docs. Scenarios must be user-level ("read a file"), not steps
("open, read a line, close").

### P2. One entry object, grouped by resource
Every pleasant analogue has one client object whose surface is split by what the user thinks
about, not by endpoint:

```csharp
var youtube = new YoutubeClient();                          // YoutubeExplode
var video   = await youtube.Videos.GetAsync(url);
var manifest = await youtube.Videos.Streams.GetManifestAsync(url);
await foreach (var v in youtube.Playlists.GetVideosAsync(url)) { … }
```

go-github: `client.Repositories.Get(ctx, …)`, `client.Organizations.List(ctx, …)` — services
mirror the official docs. YouTube.js keeps reads as methods (`getInfo`, `search`, `getChannel`,
`getPlaylist`, `getComments`, `getHomeFeed`, `getHistory`, …) and moves actions into managers
(`account`, `playlist`, `interact`, plus `music`, `kids`, `studio` sub-clients). Azure:
*"keep the number of clients small"*, standardized verbs (`get_`, `list_`, `create_`, `delete_`,
`update_`) used identically everywhere.

### P3. Accept what the user has
YoutubeExplode accepts a URL or an ID anywhere a video is expected; its `VideoId` recognizes
watch, `youtu.be`, embed, shorts and live URLs, validates 11 chars of `[A-Za-z0-9_-]`, and has
`Parse` / `TryParse`. pytube starts from a URL (`YouTube(url)`), YouTubeKit from either
(`YouTube(url:)` / `YouTube(videoID:)`), YoutubeExplode channels from `/channel/`, `/user/`,
`/c/`, `/@handle` alike. Nobody asks the user for an internal identifier such as a browse id.

### P4. Progressive disclosure: easy first, power later, no cliff
spdlog: `spdlog::info("…")` works with no setup; then named loggers, then sinks, then async —
*"each step adds one concept and the earlier code still works."* Azure: *"predictable defaults
that implement best practices"* and *"progressive concept disclosure."* Qt: *"make common tasks
easy and rare tasks possible."* The top level stays tiny, and an **escape hatch** exists for the
rare case: YouTube.js exposes `call(endpoint, args)` and `actions` for raw InnerTube requests,
so advanced users never wait for a release.

### P5. Give everything, *and* a one-line way to choose
The analogues return full data but add selection helpers on top:

```csharp
manifest.GetAudioOnlyStreams().GetWithHighestBitrate();      // YoutubeExplode
```
```python
yt.streams.get_highest_resolution().download()               # pytubefix
```
```swift
try await video.streams.filterVideoAndAudio().filter { $0.isNativelyPlayable }.highestResolutionStream()  // YouTubeKit
```
RustyPipe: `player.select_video_audio_stream(&StreamFilter::default())` → an (optional video,
optional audio) pair. YouTube.js: `download(id, {type: 'audio', quality: 'best', format: 'mp4'})`.
Captions get the same treatment: `trackManifest.GetByLanguage("en")`, `track.GetByTime(t)`.

### P6. Pagination is iteration, not token juggling
YoutubeExplode offers three levels over the same list: `await foreach` (item by item),
`.CollectAsync(20)` (a bounded batch) and `Get…BatchesAsync` (one request per batch, for
control). google-cloud-cpp returns `StreamRange<T>` — a range of `StatusOr<T>` you `for` over
while pages load behind it. Azure: *"DO expose paginated collections using language-canonical
iterators over items within pages"*, and paged lists must accept a continuation token and a
maximum count. YouTube.js keeps the continuation on the result (`getContinuation()`,
`has_continuation`); NewPipe has `getInitialPage()` / `getPage(page)`; go-github loops until
`resp.NextPage == 0`. None of them makes the user store a raw server token *and* remember which
call it belongs to.

### P7. The caller picks the async style
Asio's completion tokens: *one* initiating function serves callbacks, `use_future`,
`use_awaitable` and stackful coroutines — *"All of these uses are supported by a single
implementation."* cpr ships `Get` (blocking), `GetAsync` (future) and `GetCallback`. TDLib has
`send`/`receive` (async, matched by `@extra`) plus `execute` (synchronous). D++ has callbacks
(`confirmation_callback_t` with `is_error()` / `get<T>()`) and `co_await` variants. A blocking
form matters most for first contact: scripts, tests, CLIs and REPL-style exploration.

### P8. Errors are values, and actionable
google-cloud-cpp chose `StatusOr<T>` over exceptions because *"our C++ libraries must work for
all of our users, regardless of their ability to use exceptions"* — one vocabulary for
everyone. cpp-httplib: `if (auto res = cli.Get("/hi")) … else res.error()`. Azure:
*"Error messages should be concise, correlated with the service, actionable, and human
readable"*, and give *"enough information … to take appropriate corrective action"*; go-github
exposes typed rate-limit errors with `RetryAfter`. "Nothing to play" is not an error but a
verdict — exactly how the analogues model age gates and offline streams.

### P9. Types carry meaning; names follow one scheme
Qt's boolean trap: `repaint(false)` says nothing — use two functions or an enum. Rust: *"Arguments
convey meaning through types, not bool or Option"*, *"Newtypes provide static distinctions"*.
C++ Core Guidelines I.4 *"Make interfaces precisely and strongly typed"*, I.23 *"Keep the
number of function arguments low"*, I.24 *"Avoid adjacent parameters that can be invoked by the
same arguments in either order with different meaning"*, F.21 *"prefer returning a struct"*.
Qt's "static polymorphism": similar classes get similar APIs, so knowing one teaches the rest.
Azure model naming: full entity `Video`, list entry `VideoItem`, per-call `…Result`.

### P10. Pluggable infrastructure with working defaults
NewPipe takes a `Downloader`; YouTube.js takes `fetch`, a cache and a JS `eval` shim; RustyPipe a
`CacheStorage`; cpr hides libcurl entirely. The default works out of the box; the seam is there
for platforms that need it.

### P11. Trivial to acquire and integrate
nlohmann/json: *"Trivial integration. No library, no subproject, no dependencies, no complex
build system"* — and yet `find_package`, `add_subdirectory`, FetchContent and a long list of
package managers all give the same `nlohmann_json::nlohmann_json` target. cpr: FetchContent →
`cpr::cpr`, *"no need to handle libcurl yourself."* Azure: *"easily acquired through the most
normal mechanisms."* Pitchfork: a predictable root (`include/`, `src/`, `tests/`, `examples/`,
`docs/`, `external/`, `extras/`) so newcomers find things without guessing.

### P12. Documentation in four separate kinds, examples first
Diátaxis: tutorials (learning), how-to guides (a task), reference (facts), explanation (why) —
kept apart. spdlog's README: install → features → one short sample per topic → link to the wiki.
pytubefix's quickstart is a handful of runnable snippets, one per common task. Rust: *"All items
have an example."*

### P13. Stability is a feature
Hyrum's law: *"all observable behaviors of your system will be depended on by somebody"* — so
don't expose what you can't keep (raw protocol strings are observable behavior). KDE: adding a
data member to a public class or struct breaks ABI; use a d-pointer (pimpl) for classes that must
grow. libmpv versions its client API and keeps a `client-api-changes` log. Azure: *"breaking
changes are more harmful to a user's experience than most new features are beneficial."*

---

## 2. Where libinnertube stands

Good already: one `Client` entry point (P2), `Result<T>` with `ErrorCode` and `httpStatus` and
no exceptions (P8), the `HttpTransport` seam with a curl default (P10), `Player` returning every
source with a precise verdict (P5's "give everything"), a clear threading contract, a pimpl'd
`Client` (P13), FetchContent with an `innertube::innertube` alias (P11).

Gaps, measured against the patterns:

| # | Gap | Where | Pattern |
|---|---|---|---|
| G1 | InnerTube internals leak into the API: browse ids (`"FEsubscriptions"`), protobuf `params`, `createCommentParams`, `resourceId`, raw continuation strings | `BrowseRequest`, `playlists(resourceId, page, params)`, `postComment(createParams, …)`, `channelList(browseId, …)` | P3, P13 |
| G2 | Boolean trap and overloaded meaning: `editPlaylist(playlistId, bool add, std::string id)` — `id` is a videoId when adding and a setVideoId when removing | `client.h` | P9 |
| G3 | Flat list of 20 methods with no naming scheme: `channel`, `channelByUrl`, `channelSubscribed`, `channelList`, `searchChannels`, `playlists`, `searchPlaylists`, `votes`, `action` | `client.h` | P2, P9 |
| G4 | Pagination is "keep this string and pass it back to the right method": `page` / `next` tokens of different meaning per call | all list calls | P6 |
| G5 | Asynchronous only; even a ten-line program needs an executor and a wait loop (`yt-cli` has a `MainQueue`) | `client.h` contract | P7 |
| G6 | IDs only — no URL acceptance, no `VideoId` parsing/validation; channel by URL is a separate method | `watch`, `player`, `channel` | P3 |
| G7 | No selection helpers: the consumer must walk `Player::sources` × `formats` to find "best ≤720p with audio" | `player.h` | P5 |
| G8 | One `Video` struct serves list items and full pages; magic values (`likeStatus` 0/1/2, `duration`/`date` as display strings, `streamUrl` left over) | `types.h` | P9 |
| G9 | No raw escape hatch for an endpoint the library doesn't wrap yet | — | P4 |
| G10 | Not installable: no `install(EXPORT)`, no `innertubeConfig.cmake`, so `find_package(innertube)` and distro/vcpkg/conan packaging are impossible; no umbrella header | `CMakeLists.txt`, `include/yt/` | P11 |
| G11 | Docs are a README plus internal specs; no tutorial, no per-task guides, examples are tools rather than scenario samples | `docs/`, `examples/` | P12 |
| G12 | Public value structs will grow (they did in phase 1) — fine in 0.x, an ABI break per field after 1.0 | `types.h`, `player.h` | P13 |

---

## 3. Recommendations (prioritized)

### R1. Write the scenarios first (process, before any header change)
A short `docs/specs/<date>-api-scenarios.md` with the C++ a consumer writes for the top ~10
scenarios, reviewed before the redesign: play a video from a pasted link; search with "load
more"; home / trending feed; channel page and its tabs; a playlist with paging; comments and
replies; captions shown at playback time; sign in with the QR; subscriptions / history / likes;
like / subscribe / save-to-playlist; resume a session across runs. Then a usability check: hand
only the headers to someone (or a fresh agent) and see whether they can implement three scenarios
without reading the source.

### R2. Group the client by resource, with one verb scheme
Sketch (C++17, names to be settled by R1):

```cpp
yt::Client yt;                                    // curl backend, own thread, sane defaults

yt.videos().get(link, cb);                        // details + related (today: watch)
yt.videos().player(link, cb);                     // every source (today: player)
yt.videos().captions(track, cb);
yt.search().videos("rick astley", cb);            // also .channels(), .playlists(), .suggestions()
yt.channels().get(link, cb);                      // id, /@handle, /c/, /user/ URL alike
yt.channels().videos(channel, cb);                // a tab
yt.playlists().get(link, cb);
yt.feeds().home(cb);                              // .subscriptions(), .history(), .trending()
yt.comments().list(video, cb);                    // .replies(thread, cb), .post(target, text, cb)
yt.account().like(video, cb);                     // .subscribe(channel), .addToPlaylist(pl, video)
yt.auth().signIn(onCode, cb);
```

Accessors return lightweight references into the same engine (no extra state). Verbs:
`get` (one entity), `list`/plural noun (a collection), `add`/`remove`, `like`/`subscribe`.
This removes G1–G3 at once: browse ids, `params` and `createCommentParams` become internal.

### R3. Accept links everywhere; add `yt::Link`
One parser, pure and testable: `yt::Link::parse(std::string_view) -> std::optional<Link>` with
`kind` (Video, Short, Live, Playlist, Channel, Handle, …) and `id`, covering watch / `youtu.be` /
embed / shorts / live / playlist / `@handle` / `/channel/` / `/c/` / `/user/` URLs and bare ids
(YoutubeExplode's patterns are a checklist). Every method that takes a video, channel or
playlist accepts a string that may be an id or a URL; an unrecognizable one fails with
`ErrorCode::InvalidArgument` (asynchronously, per the contract).

### R4. Opaque handles instead of server strings
Lists return `yt::Page<T>{ std::vector<T> items; Cursor next; }` where `Cursor` is opaque, knows
which request it continues and `bool(cursor)` tells whether more exists; one call serves every
list: `yt.more(page.next, cb)` (Qt's static polymorphism: learn it once). The same for action
handles: a `CommentThread` carries what is needed to reply/edit, a `PlaylistEntry` what is needed
to remove it. A convenience `collect(n)` that follows cursors until `n` items (YoutubeExplode's
`CollectAsync`) covers the common "first 50" case.

### R5. Let the caller choose blocking or callback
Every method gets a second overload without the callback that returns
`std::future<Result<T>>` (cpr's `GetAsync` precedent; C++17, no new concepts):

```cpp
yt::Client yt;
auto r = yt.videos().player("https://youtu.be/dQw4w9WgXcQ").get();   // blocks this thread only
```

The callback form stays the primary API for GUIs; the threading contract is unchanged (the
future is fulfilled from the delivery path). Coroutine adapters can come later as an `extras/`
header without touching the core (Asio's lesson: one implementation, many completion styles).

### R6. Selection helpers next to the full data
Pure, optional functions in `player.h` — the library still returns everything (the owner's
rule: the consumer chooses); helpers only answer the common question:

```cpp
yt::Preference want;
want.maxHeight = 720;
want.codecs = {"avc1", "mp4a"};
yt::Choice c = yt::choose(player, want);
// c.progressive | (c.video + c.audio) | c.hls, each with the Source (client + User-Agent) to fetch it with
const yt::CaptionTrack *en = yt::captionFor(player, "en");
const yt::Cue *now = yt::cueAt(cues, positionMs);
```

### R7. Types that say what they mean
Split list entries from full entities (`VideoItem` vs `Video`, Azure naming); enums instead of
magic ints (`LikeState::Liked`); durations and timestamps as numbers with the display text beside
them; drop leftovers (`Video::streamUrl`). Keep plain aggregates — they are the friendliest C++
value type — but plan for P13 (R11).

### R8. An escape hatch
`yt.raw().post(endpoint, client, jsonBody, cb)` returning the vetted JSON body (transport policy,
auth and context applied), documented as "unstable, for endpoints not wrapped yet." It turns
"wait for a release" into "use raw now, ask for a wrapper later" (YouTube.js `call`).

### R9. Make it installable
`install(TARGETS … EXPORT)`, `innertubeConfig.cmake` + version file, `find_package(innertube)`
giving the same `innertube::innertube` target as FetchContent; a pkg-config file for plain
Makefiles (the N9 SDK); an umbrella `#include <yt/yt.h>`. Later: vcpkg / conan recipes.

### R10. Documentation in the Diátaxis split
README: what it is → a 10-line quickstart (blocking form) → install → links. `docs/tutorial.md`
("a terminal player in 15 minutes"); `docs/howto/*.md` one per R1 scenario (examples/ becomes
these scenarios, compiled in CI); reference stays in header comments (they already carry the
contracts); `docs/explanation/` for threading, client identities and sources. Every public
function gets one example in its header comment.

### R11. Stability policy for 1.0
Until 1.0 the API is clay (the owner allows breaking it). At 1.0: a `CHANGELOG.md` / API-changes log like
libmpv's, semantic versioning, `YT_VERSION` checks; decide ABI policy for value structs (either
"ABI changes with every minor in 1.x, API is stable" or reserved/`Details` sub-structs). Hyrum's
law argues for doing R1–R4 *before* 1.0: every exposed InnerTube string becomes a promise.

### Suggested order
R1 (scenarios) → R2 + R3 + R4 together (one coherent breaking change) → R5 → R6 + R7 → R8 →
R9 → R10 → R11 at 1.0. R9 is independent and can land any time.

---

## Sources

- YoutubeExplode — https://github.com/Tyrrrz/YoutubeExplode ; `VideoId` parsing —
  https://github.com/Tyrrrz/YoutubeExplode/blob/master/YoutubeExplode/Videos/VideoId.cs
- YouTube.js — https://github.com/LuanRT/YouTube.js , https://ytjs.dev/guide/getting-started ,
  https://ytjs.dev/api/classes/Innertube , download example
  https://github.com/LuanRT/YouTube.js/blob/main/examples/download/index.ts
- pytubefix — https://github.com/JuanBindez/pytubefix
- YouTubeKit — https://github.com/alexeichhorn/YouTubeKit
- RustyPipe — https://codeberg.org/ThetaDev/rustypipe
- NewPipe Extractor — https://github.com/TeamNewPipe/NewPipeExtractor ,
  https://teamnewpipe.github.io/documentation/01_Concept_of_the_extractor/
- ytmusicapi — https://github.com/sigma67/ytmusicapi
- Invidious API — https://docs.invidious.io/api/
- cpr — https://github.com/libcpr/cpr , https://docs.libcpr.dev/advanced-usage.html
- cpp-httplib — https://github.com/yhirose/cpp-httplib
- nlohmann/json — https://github.com/nlohmann/json
- spdlog — https://github.com/gabime/spdlog
- D++ — https://github.com/brainboxdotcc/DPP , https://dpp.dev/callback-functions.html
- libmpv client API — https://github.com/mpv-player/mpv/blob/master/include/mpv/client.h
- TDLib — https://github.com/tdlib/td , https://core.telegram.org/tdlib/getting-started
- Qt, Designing Qt-Style C++ APIs — https://doc.qt.io/archives/qq/qq13-apis.html
- Cwalina, The Principle of Scenario-Driven Design —
  https://learn.microsoft.com/en-us/archive/blogs/kcwalina/design-guidelines-book-the-principle-of-scenario-driven-design ;
  Framework Design Guidelines — https://learn.microsoft.com/en-us/dotnet/standard/design-guidelines/
- Rust API Guidelines checklist — https://rust-lang.github.io/api-guidelines/checklist.html
- C++ Core Guidelines — https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines
- Muratori's reusable-components axes (notes) — https://chrishecker.com/API_Design
- stb howto — https://github.com/nothings/stb/blob/master/docs/stb_howto.txt
- Azure SDK — https://azure.github.io/azure-sdk/general_introduction.html ,
  https://azure.github.io/azure-sdk/general_design.html
- google-cloud-cpp — StatusOr ADR
  https://github.com/googleapis/google-cloud-cpp/blob/main/doc/adr/2019-01-04-error-reporting-with-statusor.md ,
  error handling https://docs.cloud.google.com/cpp/docs/reference/common/latest/common-error-handling ,
  StreamRange https://googleapis.dev/cpp/google-cloud-common/2.7.0/stream__range_8h.html ,
  listing https://docs.cloud.google.com/storage/docs/listing-buckets
- go-github — https://github.com/google/go-github
- Asio completion tokens —
  https://www.boost.org/doc/libs/1_86_0/doc/html/boost_asio/overview/model/completion_tokens.html
- Pitchfork layout — https://github.com/vector-of-bool/pitchfork/blob/develop/data/spec.bs
- KDE binary compatibility — https://community.kde.org/Policies/Binary_Compatibility_Issues_With_C%2B%2B
- Hyrum's law — https://www.hyrumslaw.com/
- Diátaxis — https://diataxis.fr/ (summary via
  https://ubuntu.com/blog/diataxis-a-new-foundation-for-canonical-documentation)
