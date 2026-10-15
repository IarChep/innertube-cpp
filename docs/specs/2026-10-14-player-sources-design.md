# Player sources: every stream YouTube gives, without JavaScript (+ QR sign-in)

**Status:** implemented (plan [../plans/2026-10-14-player-sources.md](../plans/2026-10-14-player-sources.md)).

**Goal:** `Client::player()` returns *everything* YouTube serves for a video — progressive,
adaptive, HLS and SABR endpoints, each tagged with the client that obtained it — together with
a precise playability verdict, captions (fetchable as cues), storyboards
and the videostats URLs. The consumer chooses what to play. Sign-in additionally delivers the
YouTube-rendered QR code. Nothing here executes JavaScript.

---

## 0. Context

### 0.1 The programme this belongs to

The modernisation ports what two feature-rich clients learned about YouTube into
libinnertube: **Troubadour** (iOS 5+, Objective-C, GPL-3.0 —
`github.com/Computershik73/Troubadour`, notably `src/net/YTApi.m`, `src/player/YTSabr.m`,
`PORTING.md`) and **youtube_uwp** (Windows 10 Mobile, C#, `github.com/zemonkamin/youtube_uwp`,
notably `YouTube/Innertube/`). Knowledge is ported, not code: everything is rewritten in this
tree's style (AGENTS.md). youtube_uwp ships no licence file, so only protocol facts are taken
from it.

The work is split into five sub-projects, each with its own spec → plan → implementation:

| # | Sub-project | JS needed |
|---|---|---|
| **1** | **Player sources without JS + QR sign-in** (this spec) | no |
| 2 | Account & personal surfaces: all channels of an account (`accountReadMask`) and the active one (`onBehalfOfUser`), home/subscriptions/history (day groups)/own playlists/live, videostats history reporting, notification bell, save-to-playlist states, TV watch state | no |
| 3 | Content surfaces: Shorts (`reel/*`), channel header + server-listed tabs, playlists vs mixes + watch queue, comment threads/replies/edit, live chat, SponsorBlock, per-endpoint reversible locale fallback, SOCS consent retry | no |
| 4 | JS bridge: `n` transform and PO token (BotGuard), with a consumer-provided script evaluator behind a seam like `HttpTransport` | yes |
| 5 | SABR streaming session (protobuf, UMP, request state, reloads, redirects) | for PO token |

### 0.2 Understanding (as agreed)

- The library returns **all** kinds of sources; which one plays is the consumer's choice.
- The public API is not frozen (0.x): types and signatures change freely where that serves the
  new model.
- Out of the library: HLS proxies, MPEG-TS/MP4 muxing, players, downloads UI, WARP.

### 0.3 Facts this design rests on

Live-verified on 2026-10-14 from this machine (anonymous, `hl=en gl=US`, three videos):

- **VISIONOS** (client number 101, version `1.02`) with the session's `visitorData` answers
  `OK` with adaptive formats up to 2160p **with ready URLs** (`c=VISIONOS`, no `n`, no `pot`),
  plus `hlsManifestUrl` (manifest → 200) and `serverAbrStreamingUrl`. It has **no**
  progressive `formats`. Ranged GETs with its UA at the start, middle and last 200 KB of a
  213 s itag-137 track all → 206.
- VISIONOS **without** visitorData: `LOGIN_REQUIRED` on two of three videos. A WEB `/player`
  answer (itself `UNPLAYABLE`) carries a usable `responseContext.visitorData`.
- ANDROID_VR with visitorData: `OK`, progressive itag 18 + adaptive.
- `https://www.youtube.com/tv` with the TV UA (244 KB) names `/s/player/<id>/…`;
  `/s/player/<id>/player_ias.vflset/en_US/base.js` (2.9 MB) contains `signatureTimestamp:<n>`.
- Caption `baseUrl` + `&fmt=json3` → 200 (`events[].{tStartMs,dDurationMs,segs[].utf8}`);
  **with `&tlang=<lang>` → 429** ("Sorry" page) for every language tried: machine translation
  needs the PO token and moves to sub-project 4.
- Storyboard spec: `…/storyboard3_L$L/$N.jpg?sqp=…|48#27#100#10#10#0#default#rs$…|80#45#108#10#10#2000#M$M#rs$…`.

From Troubadour's device logs (not re-verified here):

- ANDROID_VR, ANDROID and IOS sessions stop delivering between 60 and 70 s; VISIONOS does not
  (matches Opaline's measurement of 59 904 / 69 888 ms).
- IOS must be sent **without** visitorData or it loses `hlsManifestUrl` (also device-verified
  in this tree, `ContextBuilder`).
- The TV client requires `signatureTimestamp`, else "The page needs to be reloaded". Bearer
  tokens are minted for TVHTML5; IOS rejects them with 400 (verified in this tree).
- `LIVE_STREAM_OFFLINE` (scheduled stream): no client has streams; walking the chain only
  wastes requests (Troubadour log 100: ten requests, four seconds).
- WEB `/player` serves SABR-only adaptive formats and `n`-protected URLs; useless as a source
  before sub-project 4.

---

## 1. Public types — `include/yt/player.h` (new, C++17)

Replaces `Stream`, `Subtitle`, `PlayerInfo` in `types.h`.

```cpp
struct ByteRange { int64_t first = 0, last = 0; };          // inclusive; maps to a Range header
struct AudioTrack {
    std::string id, name, language;                          // "ru.4", "Русский", "ru"
    bool isDefault = false, original = false, autoDubbed = false, drc = false;
};
struct Format {
    int itag = 0;
    std::string url;                       // byte-exact; empty = SABR-only or sealed
    bool sealed = false;                   // signatureCipher instead of url (sub-project 4)
    std::string mimeType, codecs, qualityLabel;
    int width = 0, height = 0, fps = 0;
    int64_t bitrate = 0, averageBitrate = 0, contentLength = 0, durationMs = 0;
    std::optional<ByteRange> initRange, indexRange;
    bool hasVideo = false, hasAudio = false;      // both = progressive (muxed)
    std::optional<AudioTrack> audio;
    int64_t lastModified = 0; std::string xtags;  // SABR FormatId (sub-project 5)
};
struct Source {                            // one client's answer
    ClientId client;
    std::string userAgent;                 // fetch media with it: the CDN checks c=
    std::vector<Format> formats;           // progressive + adaptive, server order
    std::string hlsUrl, dashUrl;
    std::string sabrUrl, sabrConfig;       // serverAbrStreamingUrl, videoPlaybackUstreamerConfig
    int64_t expiresAtSecs = 0;             // unix time
};
enum class Playability { Ok, BotCheck, LoginRequired, AgeRestricted, Upcoming, LiveOffline,
                         Unplayable, Unknown };
struct PlayabilityStatus {
    Playability kind = Playability::Unknown;
    std::string status, reason;            // raw server words (localized by hl)
    int64_t scheduledStartSecs = 0;        // Upcoming: unix time
    std::string slateText;                 // the server's own waiting text
};
struct VideoDetails {
    std::string id, title, author, channelId, description, thumbnailUrl;
    int64_t lengthSecs = 0, viewCount = 0;
    bool isLive = false, isLiveContent = false, isUpcoming = false;
};
struct CaptionTrack { std::string url, language, name; bool autoGenerated = false, translatable = false; };
struct StoryboardLevel { std::string urlTemplate; int width = 0, height = 0, count = 0, cols = 0, rows = 0;
                         int64_t intervalMs = 0; };
struct Player {
    PlayabilityStatus status;
    VideoDetails details;
    std::vector<Source> sources;           // chain order; empty when nothing is playable
    std::vector<CaptionTrack> captions;
    std::vector<StoryboardLevel> storyboard;
    std::string watchtimeUrl, playbackUrl; // videostats (sub-project 2)
    std::string cpn;                       // one playback nonce per view
};
enum Want : unsigned { WantProgressive = 1, WantAdaptive = 2, WantHls = 4, WantSabr = 8, WantAll = 15 };
struct PlayerRequest { std::string videoId; unsigned want = WantAll; };
struct Cue { int64_t startMs = 0, endMs = 0; std::string text; };
struct StoryboardFrame { std::string url; int x = 0, y = 0, width = 0, height = 0; };
StoryboardFrame storyboardFrame(const StoryboardLevel &level, int64_t ms);   // pure
```

`Client`:

```cpp
Job player(PlayerRequest request, Callback<Player> done);
Job captions(CaptionTrack track, Callback<std::vector<Cue>> done);
```

**Error model.** `Result<Player>` fails only when no client produced a parseable `/player`
response (transport, timeout, error envelope). "Nothing to play" — bot check, scheduled
stream, removed video — is a success with `status.kind` set and empty `sources`, so the
scheduled start, the server's text, captions and details still reach the UI.

## 2. The client walk

`ClientId::VISIONOS`: number 101, version `1.02`, UA `Mozilla/5.0 (Macintosh; Intel Mac OS X
15_7_3) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 Safari/605.1.15`; context
`deviceMake=Apple, deviceModel=RealityDevice14,1, osName=visionOS, osVersion=1.0.2.21O209`,
`userAgent` in the context, visitorData sent. Overridable via `ClientOptions::clients`.

Clients are asked **sequentially** (fewer requests; visitorData is seeded before the clients
that need it):

| Step | Client | Asked when | Usually yields |
|---|---|---|---|
| 0 | WEB `/player` | the session has no visitorData | visitorData only (never a `Source`); persisted, so once per identity |
| 1 | VISIONOS | always | adaptive ≤2160p with URLs, HLS, SABR |
| 2 | ANDROID_VR | something wanted is still missing | progressive itag 18 + adaptive (cut at ~60 s) |
| 3 | TVHTML5 + bearer | signed in and SABR or adaptive still missing | SABR, sometimes adaptive; body carries `signatureTimestamp`, `cpn`, `html5Preference` |
| 4 | IOS (no visitorData) | HLS still missing | `hlsManifestUrl` |

**Bot-check retry**: the first VISIONOS or ANDROID_VR answer with `LOGIN_REQUIRED` (not an
age gate) drops the visitorData, reseeds it (step 0) and repeats that same client — once per
walk.

A kind is **covered** when: Progressive — a format with `url` and both video and audio;
Adaptive — a video-only and an audio-only format, both with `url`; HLS — `hlsUrl`; SABR —
`sabrUrl`. The walk **stops** when every wanted kind is covered, when the clients run out, or
on a final status (`Upcoming`, `LiveOffline`, `ERROR`). Every client that yielded something
wanted becomes a `Source` (all its formats, its UA). Typical anonymous cost with `WantAll`
and a persisted visitorData: VISIONOS → ANDROID_VR (two requests; ANDROID_VR only for
progressive); without progressive: VISIONOS alone.

**Status.** The most informative wins: `Ok` > `Upcoming`/`LiveOffline` >
`Unplayable`/`AgeRestricted`/`LoginRequired` > `BotCheck` > `Unknown`. `LOGIN_REQUIRED` maps to
`AgeRestricted` when the response carries age markers (`AGE_*` status/subreason,
`desktopLegacyAgeGateReason`), else `BotCheck` — never by the localized `reason` text. The
scheduled start comes from `liveStreamOfflineSlateRenderer.scheduledStartTime` (found by a
bounded scanner visitor — its path differs per client) or, failing that,
`microformat.playerMicroformatRenderer.liveBroadcastDetails.startTimestamp` (ISO 8601).
Details, captions, storyboard: first response that has them non-empty (captions: non-WEB
only). Videostats URLs: prefer the TV response (attributed to the account).

**`playerId` / `signatureTimestamp`.** Fetched lazily before the first TV `/player`: GET
`https://www.youtube.com/tv`, find `/s/player/<id>/`; GET
`/s/player/<id>/player_ias.vflset/en_US/base.js` (~3 MB) and scan bytes for
`signatureTimestamp`. Both persist in `SessionState` (they rotate every few weeks; `playerId`
also feeds sub-project 4). On failure the TV request goes without `sts`. On "The page needs to
be reloaded" from TV: forget both, refetch, retry TV once. Concurrent walks share the base.js
download: `IHttp::fetch` coalesces identical URLs.

**`cpn`**: 16 chars of `[A-Za-z0-9-_]`, one per `player()` call; sent in the TV body and
returned in `Player::cpn`.

**Bodies**: `contentCheckOk` and `racyCheckOk` for every client (already the case); `playbackContext` only for TV.

**Cancellation / auth**: `live(job)` between steps. Empty `sources` after a TV 401 →
`httpStatus = 401`, so the Client refreshes the bearer and retries (existing behaviour, kept).

**Deliberately not ported**: authed IOS (rejects the TV bearer here), the legacy TV version
(VISIONOS covers ready URLs), WEB with a browser session (no web session in the library yet).

## 3. QR sign-in and revoked sign-in

After the device code, `POST youtubei/v1/mdx/handoff` as TVHTML5 with
`handoffQrParams.rapidQrParams = {qrPresetStyle, userCode, rapidQrFeature:
"RAPID_QR_FEATURE_DEFAULT"}`; read
`rapidQrRenderer.qrCodeRenderer.qrCodeImage.thumbnails[0].url`. A `data:image/png;base64,…`
URL is decoded to bytes; a plain `https` URL is fetched with one raw GET. Anything that does not
start with the PNG signature is dropped (`qrPng` stays empty). The preset
(`HANDOFF_QR_LIMITED_PRESET_STYLE_MODERN_BIG_DOTS_INVERT_WITH_YT_LOGO`) is a constant in
`innertube/catalog.h`. Source: youtube_uwp `Innertube/TvSignInClient.cs`.

```cpp
struct DeviceCode {
    std::string verificationUrl, userCode;
    int intervalSecs = 5, expiresInSecs = 0;
    std::string qrPng;                     // PNG bytes from YouTube; empty if that failed
};
```

`onCode` still runs once, after the QR attempt; a failed QR leaves `qrPng` empty and sign-in
continues. Polling starts with the device code and does not wait for the QR. No QR encoder in
the library — YouTube renders it; a consumer can still encode `verificationUrl` itself.

**`invalid_grant`** on refresh (for the current `authGen`) runs the `signOut()` path: tokens
and `account` dropped, caches cleared, `onStateChanged` reports the empty refresh token; calls
started for the revoked identity and waiting on that refresh fail with `ErrorCode::AuthRequired`
("session revoked"); calls started afterwards run signed out. Today such a
token is refreshed again on every authed call.

## 4. Captions and storyboards

**Tracks** from `captions.playerCaptionsTracklistRenderer`: `captionTracks[]` → `url`
(`baseUrl`, byte-exact), `language` (`languageCode`), `name` (`gj::textOf`), `autoGenerated`
(`kind == "asr"`), `translatable` (`isTranslatable`). Machine translation (`tlang`, the
`translationLanguages` list) is **not** offered here: it answers 429 without a PO token
(§0.3) and lands with sub-project 4.

**`Client::captions(track)`**: raw GET (`IHttp::fetch`, so an empty 200 body is not an envelope error) of `track.url` with `fmt=json3` (appended, or the existing
`fmt` value replaced — the rest of the URL untouched). `json3`: `events[] → {tStartMs, dDurationMs, segs[].utf8}` → `Cue`;
events without `segs` and whitespace-only events are skipped; segment texts are joined; an
event without `dDurationMs` ends where it starts. Non-2xx /
transport → failed `Result`; a 200 empty body → empty list.

**Storyboard** from `storyboards.playerStoryboardSpecRenderer.spec`:
`<template>|w#h#count#cols#rows#intervalMs#name#sigh|…`. Level *i* (0-based): `$L → i`,
`$N → name`, `&sigh=` appended; `$M` (sheet index) is left for `storyboardFrame`. A level with
`intervalMs == 0` gets `lengthSecs·1000 / count`. `storyboardFrame`: `idx = min(ms/interval,
count−1)`, `sheet = idx/(cols·rows)`, `cell = idx%(cols·rows)`, `x = (cell%cols)·w`,
`y = (cell/cols)·h`, `$M → sheet`. Live storyboards (`playerLiveStoryboardSpecRenderer`) are
out of scope.

## 5. Changes by file

| Where | Change |
|---|---|
| `include/yt/player.h` (new) | §1 types, `storyboardFrame` |
| `include/yt/types.h` | drop `Stream`, `Subtitle`, `PlayerInfo` |
| `include/yt/client.h` | `player(PlayerRequest, …)`, `captions(…)` |
| `include/yt/catalog.h` | `ClientId::VISIONOS`, `kClientIdCount = 7` |
| `include/yt/session.h` | `SessionState::playerId`, `signatureTimestamp` |
| `include/yt/auth.h` | `DeviceCode::expiresInSecs`, `qrPng` |
| `innertube/clientconfig.cpp`, `contextbuilder.cpp` | VISIONOS entry; optional `cj::Client::userAgent` |
| `innertube/bodies.*` | `player(videoId, PlayerBody{tv, cpn, sts})` (`contentCheckOk`/`racyCheckOk` are already sent); `mdxHandoff(userCode)` |
| `innertube/catalog.h` | QR preset; base.js / tv page URL patterns |
| `innertube/playerjs.{h,cpp}` (new) | `playerIdIn(html)`, `signatureTimestampIn(js)`: byte scans (grows with sub-project 4) |
| `parsers/playerparser.*` | rewritten: one partial Glaze read collects status, details, formats, HLS/DASH/SABR, expiry, captions, storyboard spec, videostats; slate via a bounded scanner visitor; also `parseStoryboardSpec` and the `json3` reader |
| `src/player.cpp` (new, beside `catalog.cpp`) | `storyboardFrame` |
| `json/ytjson.h` | `gj::base64Decode` (both alphabets, optional padding): the bottom layer, so the parsers (`xtags`) and the chains (QR data URL) may both use it |
| `core/chains.*` | `fetchPlayer` rewritten around one `PlayerWalk` state (wanted/covered kinds, collected parts, step, visitor retry, saw-401); `ensurePlayerJs`, `fetchCaptions`, `fetchSignInQr` |
| `core/ihttp.h`, `net/httpclient.*` | `IHttp::fetch(url, headers, job, fn)`: raw GET (no JSON envelope check — the `/tv` HTML, `base.js`, a QR image), non-2xx still fails, identical URLs coalesced. `IHttp::resetVisitorData()`: drop it and rebuild the memoized context (bot-check retry). `IHttp::setPlayerJs(id, sts)`. Both identity changes reach `SessionState` + `onStateChanged` through sinks, like visitorData today. `FakeHttp` gains per-call client recording, `fetch` fixtures and visitorData adoption |
| `net/httpclient.h` | `kClientCount = 6` → `kClientIdCount` |
| `auth.cpp` | QR step before `onCode`; `invalid_grant` |
| `client.cpp` | thin `m_engine->start<…>` wrappers |
| `examples/yt-cli` | `streams <id>` prints sources by client and kind; `captions <id>` prints the first cues; `signin` writes `qr.png`; the state file persists `playerId` / `signatureTimestamp` |
| `AGENTS.md` | layout section (new files) |

The old `parseStreams` / `parseCaptions` / `isPlayable` / `parseVideoDetails` entry points go
once every caller is migrated (grep before removal).

## 6. Tests and verification

**Fixtures** (real, trimmed; `ip=`, `visitorData`, `cpn` and other session marks scrubbed):
`player_visionos_ok.json`, `player_ios_hls.json`, `player_tv_sabr.json`,
`player_vr_botcheck.json`, `player_agegate.json`, `player_upcoming.json`,
`player_removed.json`, `captions_manual.json3`, `captions_asr.json3`, `tv_page.html`,
`base_js_snippet.js`, `mdx_handoff.json`.

- **`tst_parsers`**: every fixture → expected fields; scheduled start by both paths;
  malformed input → absent fields; storyboard spec; json3; `playerjs` scans.
- **`tst_chains`** (FakeHttp): order per `want` and visitorData state (IOS/WEB seed); VISIONOS
  covering all → no further request; VR bot check → reseed + exactly one retry; `Upcoming` /
  `ERROR` stop the walk; status priority; per-`Source` client and UA; TV body has `sts`,
  `cpn`, `playbackContext`; `sts` fetched once and persisted; "page needs to be reloaded" →
  refetch once; 401 with empty sources → `httpStatus = 401`; cancel between steps → no
  delivery; captions never from WEB; captions URL `fmt` appended or replaced, rest byte-exact; `mdx/handoff` body.
- **`tst_auth`**: `onCode` carries the PNG; QR failure is non-fatal; polling not delayed by
  the QR; `invalid_grant` (regression test, seen failing without the fix).
- **`tst_client`**: `player`, `captions` in "every endpoint reaches its chain".
- **`tst_types`**: `player.h` at C++17. **base64**: missing padding, url alphabet, garbage.

**Done** (AGENTS.md §2): warning-free and green in `build`, `build-asan`, `build-tsan`,
`build-none`; transport suites with a dead proxy; every fix has a test that fails without it;
header comments and docs updated.

**Live check** (`yt-cli streams` / `captions`, by hand) on an ordinary, an age-restricted, a
scheduled, a live, a Shorts and a removed video: VISIONOS adaptive URL → 206 with the VISIONOS
UA, including a range past the 60 s mark; HLS manifest → 200; captions json3; a storyboard
sheet → 200; `sts` found; `qr.png` is a valid PNG. The signed-in path (TV, SABR URL,
`invalid_grant`) needs the owner to scan a QR in `yt-cli login`; without that it is verified
on FakeHttp only and reported as such.

## 7. Risks

- **Client identities rot.** VISIONOS may be throttled or cut like the others. Mitigation: the
  walk falls through to ANDROID_VR/TV/IOS; versions are overridable at runtime.
- **base.js size** on a 1 GB device: ~3 MB transient, once per player rotation; the 32 MB body
  cap holds it.
- **Two-stage responses**: a client may answer `OK` with formats but no URLs (SABR-only).
  Coverage is decided per kind on URLs, never on `status`, so the walk continues.
