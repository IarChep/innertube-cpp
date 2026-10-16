# Feature parity with Troubadour and youtube_uwp — design

Status: draft for owner review (2026-10-16). Supersedes phases 2–5 of the 2026-10-14 roadmap
(phase 1, player sources and QR sign-in, is done). Each phase below gets its own plan in
`docs/plans/` before it is executed.

## 1. Goal

Every function the two feature-rich clients support — Troubadour (iOS) and youtube_uwp
(Windows 10 Mobile) — exists in libinnertube, completely, so that such a client is a thin UI
over the library. Wrong behaviour found on the way is fixed first.

Constraints that do not change: the public API may break before 1.0; the library returns
everything YouTube gives and the consumer chooses; public headers are C++17 with `std` types
only; no third-party dependency beyond libcurl/doctest; the threading, security and error
contracts of `AGENTS.md` §4 hold for every new operation.

Owner decisions (2026-10-16):

| Question | Decision |
|---|---|
| JS (n, signatures, PO token) | The library orchestrates; the consumer supplies only a JS evaluator (§4.8) |
| zemonkamin's fork | Port its own code onto `yt::json` and the current API, with attribution (§2) |
| Beyond the two clients | Also: playlist create/delete/rename/privacy, comment like/delete/sort, Data API fallback, media download helper |
| Order | P0 → P1 → … → P6 |

## 2. Sources and attribution

| Tag | Source | Revision | Licence | Author to credit |
|---|---|---|---|---|
| T | github.com/Computershik73/Troubadour | db2b0da | GPL-3.0 | Computershik73 |
| U | github.com/zemonkamin/youtube_uwp | eca3a12 | no licence file; the author permitted use with attribution (2026-10-16) | zemonkamin |
| Z | zemonkamin's fork of this library (`zemonkamin-moded/`) | 2026-10-14 tree | GPL-3.0; UWP-derived parts with the author's permission | zemonkamin |
| TR | github.com/Preloading/TubeReplacer (via T) | — | GPL-3.0 | Preloading |

Code and facts from all four may be taken (owner, 2026-10-16). Rules:

- Ported code carries a comment naming the author and the origin, in the voice already used
  for inherited code: `// Ported from zemonkamin's fork (src/core/sabr.cpp).`,
  `// zemonkamin's youtube_uwp, Innertube/Config.GuestHome.cs.`,
  `// Troubadour (Computershik73) src/net/YTApi.m +rate:as:params:`. A shipped resource (a JS
  file) keeps its own header and gains one naming its author, project and licence.
- Protocol facts cite where they were observed (`// T YTApi.m watchState: …`); Troubadour is the
  preferred citation when a fact appears in several sources.
- T's SABR base (`YTSabr.m`, `YTUmp.m`, `YTProto.m`) and its PO-token path come from
  TubeReplacer (T README): credit both Preloading and Computershik73 where that code is ported.
- The n solver: T's `Resources/nsig.js` (Computershik73) is preferred — it runs the whole
  player script on the device; Z's `src/core/nsolver.inc` (zemonkamin, from U `Assets/nsig.js`)
  is the alternative if it proves more robust.
- The BotGuard driver is T's `Resources/challenge_solver.html` (Preloading's TubeReplacer,
  GPL-3.0), shipped with that attribution.

## 3. Inventory

The full inventory with evidence is `2026-10-16-feature-gap-list.md`; this table assigns its
items to phases. W = wrong today (a defect), M = missing, X = beyond
the two clients (owner-requested). "verify" = known only from a reference, to be checked live.

| Id | Item | Source | Phase |
|---|---|---|---|
| W1 | `accounts_list` asks only `returnOwner`: brand/persona channels are never listed | T | P0 |
| W2 | save/unsave with "LL" goes to `edit_playlist` (LL changes only by like/removelike); a `VL` prefix is not stripped | T, Z | P0 |
| W3 | `playlists().get` of any non-WL/LL playlist is anonymous WEB: a signed-in user's private playlists fail | T | P0 |
| W4 | `comments().post` succeeds on any 2xx; YouTube refuses with 200 | T, Z | P0 |
| W5 | Shorts comments: WEB `/next` has no panel → reported as `disabled` | T | P0 |
| W6 | subscribe/unsubscribe omit `params` `CgIIAxgA` / `CgIIAxgB` (verify) | T, U | P0 |
| W7 | writes go TVHTML5 only; T falls back TVHTML5 → MWEB → WEB | T | P2 |
| W8 | signed-out `feeds().home()` is always empty (WEB `FEwhat_to_watch` answers 200 without videos) | T, U | P2 |
| W9 | own playlists use `FElibrary`; T and U use `FEplaylist_aggregation` (verify) | T, U | P0 |
| W10 | relevance search sends no params (mixed kinds); a Shorts-only page parses as empty | T | P0 |
| W11 | subscribe state read from the first `subscribeButtonRenderer`; view models carry both templates, the state is `subscriptionStateEntity` (verify with a fixture) | T | P0 |
| M1 | web session: cookies + SAPISIDHASH | T, Z | P1 |
| M2 | account channels: list, select, `onBehalfOfUser`, datasync fallback | T, Z | P1 |
| M3 | locale ladder (gl → US, then hl → en) | T | P1 |
| M4 | signed-in search (TV + bearer, or WEB + web session) | T | P1 |
| M5 | consent wall | roadmap | covered: the SOCS cookie is already sent; no work |
| M6 | live feed (`FEtopics_live` / `UC4R8DWoMoI7CAwX8_LjQHig`), two continuation kinds | T | P2 |
| M7 | titled sections: history days, live shelves | T | P2 |
| M8 | home chips (local list + Live params) | T, Z | P2 |
| M9 | item flags: live, upcoming, Short, members-only, watched % | Z, U | P2 |
| M10 | search kinds (Shorts) and filters | T, Z | P3 |
| M11 | watch state from TV `/next`: bell, comment count, comments token, owner avatar | T | P2 |
| M12 | watch queue: `/next` with a playlist (index, mixes) | T, Z | P3 |
| M13 | chapters from the description | T, U | P3 |
| M14 | `n` transform | T, Z, U | P5 |
| M15 | `signatureCipher` decipher (no client does it; stretch) | — | P5 |
| M16 | PO token (BotGuard) | T, Z | P5 |
| M17 | Shorts player chain ANDROID → WEB → WEB + PO token | T | P3 (P5 for the token) |
| M18 | `/player` reload with `reloadPlaybackContext` by the same client | T, Z | P6 |
| M19 | caption translation (`translationLanguages`, `tlang`) | U | P3 |
| M20 | TV player profile apart from TV browse (versions 7.20260715 / legacy 7.20220918) | T, Z | P6 |
| M21 | history reporting (videostats playback + watchtime) | T, U, Z | P2 |
| M22 | SABR session (UMP + protobuf) | T, Z | P6 |
| M23 | media download helper (ranged chunks) | T | P6 (X) |
| M24 | channel tabs: Shorts, Live, Posts, Home + the server's tab list | T, Z | P3 |
| M25 | bell: `notification/modify_channel_preference` | T | P2 |
| M26 | save dialog: `playlist/get_add_to_playlist` | T, U, Z | P2 |
| M27 | remove one playlist row by `setVideoId` | Z | P2 |
| M28 | comment replies, reply, edit | T | P3 |
| M29 | live chat | T, Z | P4 |
| M30 | Shorts feed (reel sequence) | T, U, Z | P3 |
| M31 | SponsorBlock | T, U | P3 |
| M32 | Data API v3 fallback for playlists | U, Z | P2 (X) |
| X1 | playlist create / delete / rename / privacy | — | P2 |
| X2 | comment like / delete / sort Top–Newest | — | P3 |

Out of scope (client concerns): UI, the downloads list and file storage, muxing (MP4, TS, HLS
proxy, DASH demuxer), Discord presence, Troubadour's WARP tunnel, app updates, image loading.
Z's `LiveTimeline` is a MediaStreamSource clock and stays in the client. Notifications: both
clients build them from the subscriptions feed (`get_notification_menu` answers 400 to the TV
token), which the library already serves.

## 4. Cross-cutting design

### 4.1 Identities and request signing

Three ways to be someone, each bound to the clients that accept it (T `YTApi.m +post:…`):

| Client | OAuth bearer | Web session (cookies + SAPISIDHASH) | `context.user.onBehalfOfUser` |
|---|---|---|---|
| TVHTML5 | when signed in | never | when signed in and a non-default channel is active |
| WEB, WEB_SAFARI, MWEB | never (400 INVALID_ARGUMENT) | when a web session exists | when signed with the web session and a channel is active |
| IOS, ANDROID, ANDROID_VR, VISIONOS | never | never (T: cookies on VR caused the bot wall) | never |

`onBehalfOfUser` on an unsigned request makes every answer 401 (T), so it rides only requests
the library signs. `ContextBuilder::headers`/`contextJson` own this table; no chain decides it.

Routing per operation (the chain picks the client; the table above decides credentials):

| Operation | Signed out | OAuth only | Web session |
|---|---|---|---|
| home, subscriptions, history, live feed, own playlists | WEB anonymous / guest (§6 P2) | TVHTML5 | TVHTML5; history WEB (day sections) |
| search | WEB | TVHTML5 (personalised) | WEB signed |
| watch page | WEB | TVHTML5 | WEB signed |
| playlist browse | WEB | TVHTML5 (private playlists need the bearer) | WEB signed |
| channel browse | WEB | WEB, plus a TVHTML5 browse for the subscribe state | WEB signed |
| comments list | WEB | WEB (TV has no panel); token from TV `/next` for Shorts | WEB signed |
| writes | — | TVHTML5 → MWEB → WEB (§4.3) | WEB signed first |

### 4.2 Session state

`SessionState` gains:

```cpp
struct ChannelIdentity {                    // the channel requests speak as
    std::string pageId;                     // brand channel page id; empty = the account's default
    std::string datasyncId;                 // "profile||owner": the second form, used after a 401
};
struct SessionState {
    ...                                     // as today
    std::string webCookies;                 // web session Cookie value; credentials: never logged
    ChannelIdentity channel;                // the active channel (account().select)
};
```

Every change of `refreshToken`, `webCookies` or `channel` is an identity change: caches are
cleared, generation counters bumped, queued deliveries of the old identity are suppressed (the
existing `authGen` fencing extended). Rotated cookies arriving in `Set-Cookie` on a signed
youtube.com reply are merged into `webCookies` and reported through `onStateChanged`.

### 4.3 Writes

A write is tried as TVHTML5, then MWEB, then WEB (T: rate, edit_playlist, get_add_to_playlist).
The next client is tried **only on a definite rejection** (HTTP 400/403/404 or an error
envelope), never after a timeout, a 5xx or a transport error, because the first attempt may
have been applied (Z's no-duplicate rule). A successful write clears the cached
browse/next answers. With a web session, WEB signed is tried first.

### 4.4 Locale ladder

On HTTP 400 from `browse`/`next`/`search`, or a 200 search answer without `contents`, the
request is repeated once with `gl=US`, then once with `hl=en` as well (T `relaxLocale`: a user
in KG got 400 for `hl=ru gl=KG`; region first, so the language survives). The relaxed locale is
remembered **per endpoint** for the session and dropped by `setLocale()`; it is never
persisted. Failed replies (401, transport errors) never trigger it (T's own bug: a 401 storm
moved the whole app to `en/US`).

### 4.5 Pages with sections

```cpp
struct Section { std::string title; size_t begin = 0, end = 0; };  // items[begin, end)
template <class T> struct Page { std::vector<T> items; Cursor<T> next; bool disabled = false;
                                 std::vector<Section> sections; };  // empty = one untitled list
```

Used for history days (WEB only; TV answers flat), live shelves, a channel Home tab. Items
stay one flat vector so existing consumers keep working.

### 4.6 Item flags

```cpp
struct VideoItem {
    ...
    bool live = false, upcoming = false, isShort = false, membersOnly = false;
    int watchedPercent = -1;               // resume bar; -1 = unknown
    std::string setVideoId;                // playlist rows only: this row (Account::remove)
};
```

### 4.7 In-library primitives

All in `src/core/`, each with standard test vectors, no dependency:

- `hash.{h,cpp}` — SHA-1 (P1, SAPISIDHASH) and SHA-256 (P3, SponsorBlock prefix), FIPS 180-4.
- `proto.h` — protobuf writer (varint, length-delimited, fixed32) for bell params (P2), search
  filters (P3) and SABR requests (P6); reader added in P6.
- `base64` encode next to the existing `gj::base64Decode`.

### 4.8 The JS seam (P5)

The library never embeds a JS engine. The consumer may pass one evaluator; everything else —
downloading the player script, the solver code, caching, token lifetimes, HTTP — is the
library's (owner decision).

```cpp
// yt/js.h — C++17.
class JsHost {
public:
    virtual ~JsHost() = default;
    // Evaluates `source` in `realm` (created on first use, kept until drop) and reports the
    // completion value as a string (JSON for objects). Any thread; done runs exactly once.
    virtual void eval(std::string realm, std::string source,
                      std::function<void(Result<std::string>)> done) = 0;
    virtual void drop(const std::string &realm) = 0;
    // window/document exist (a web view): BotGuard needs it; the n solver does not.
    virtual bool browserLike() const = 0;
};
struct ClientOptions { ...; std::shared_ptr<JsHost> js; };   // empty: today's JS-less walk
```

Asynchronous JS work (BotGuard) stores its result in a global and the library polls it with
short `eval` calls on the engine loop's timers, so even a synchronous evaluator (UIWebView's
`stringByEvaluatingJavaScriptFromString`, QJSEngine) suffices. Like `HttpTransport`, calls may
complete on any thread; late and duplicate completions are dropped.

## 5. Public API changes (sketch)

Shapes follow the current groups: both forms (callback and future) for every method.

| Group | Added or changed |
|---|---|
| `Auth` | `useWebSession(std::string cookies)`, `webSignedIn()`; `signOut()` drops both identities |
| `Account` | `channels()` → `std::vector<AccountChannel>`; `select(const AccountChannel &)`; `notifications(channelLink, Bell)`; `saveStates(videoLink)` → `std::vector<SaveState>`; `remove(playlistLink, const VideoItem &row)`; `createPlaylist(title, Privacy, videoLinks)` → `Playlist`; `deletePlaylist`, `renamePlaylist`, `setPrivacy`; `report(const View &, Watched)` |
| `Feeds` | `live()`; `home(const HomeChip &)`; `shorts()`; history pages carry sections |
| `Videos` | `get(link)` understands `&list=`/`&index=` (Video::queue); `segments(link, categories)` (SponsorBlock); `captions(track, translateTo)`; `view(const Player &)` → `View` |
| `Search` | `shorts(query)`; `SearchOptions` gains upload date, duration, type features |
| `Channels` | `shorts`, `streams`, `posts`, `tab(link, const ChannelTab &)`; `Channel::tabs`, `Channel::bell` |
| `Comments` | `list(link, CommentSort)`; `post` → `Comment`; `reply(const Comment &, text)`; `edit`; `remove`; `like(const Comment &, bool)`; replies via `Client::more(Cursor<Comment>)` |
| `Live` (new) | `chat(videoLink, ChatMode)` → `ChatPage`; `poll(const ChatCursor &)` |
| `Media` (new) | `download(const Source &, const Format &, sink, progress)`; SABR session (P6) |
| free functions | `chapters(const Video &)`, `homeChips()` |

New value types: `AccountChannel{id, name, handle, avatarUrl, channelId, kind, selected}`,
`Bell{All, Personalized, None, Unknown}`, `SaveState{Playlist playlist; bool contains}`,
`Privacy{Public, Unlisted, Private}`, `HomeChip{label, query, params}`, `ChannelTab{title,
kind}` (opaque params inside), `Post`, `Chapter{title, startMs}`, `Segment{startMs, endMs,
category, action}`, `ChatItem`, `ChatPage`, `Queue{playlistId, title, currentIndex,
Page<VideoItem> items}`.

Opaque handles, like `Cursor<T>`: `Comment::ref` (reply/edit/action params),
`View` (cpn, videostats URLs, client, identity generation; `save()`/`load()` to a string so a
client can finish a pending report after a restart — T `keepPendingWatch`), `ChatCursor`,
`ChannelTab`'s params. Server tokens never appear as public `std::string` fields (R-series rule).

`yt::Link` gains `playlistId` and `index` on video links (`watch?v=…&list=…&index=…`).

## 6. Phases

Each phase: plan → owner review → TDD execution → one commit per logical change. Every
defect fix is its own `fix(lib)` commit with a regression test that fails without it.
"Live" lists what must be checked against the service (S = needs the owner's sign-in).

### P0 — correctness

W1, W2, W3, W4, W5, W6, W9, W10, W11.

- W1: full `accountReadMask` {returnOwner, returnBrandAccounts, returnPersonaAccounts,
  returnFamilyChildAccounts: true; returnFamilyMembersAccounts: false} (T `+accountReadMask`).
  `info()` keeps reporting the selected/owner channel.
- W2: LL → `like/like` / `like/removelike`; strip `VL` before every edit (T `+setVideo:saved:`).
- W3: signed in, every playlist browses as TVHTML5; the parser learns the TV header/tiles
  (fixture needed).
- W4: success = `actionResult.status == STATUS_SUCCEEDED` or a returned comment; otherwise
  `ErrorCode::Api` with the server's refusal text (T `commentAccepted`, `refusalTextIn`).
- W5: no panel on a Short → signed in: token from TVHTML5 `/next` (`engagementPanelSectionList`
  with `comment-item-section` / `engagement-panel-comments-section`); signed out:
  `Unavailable`, not `disabled`, until P3's reel answer supplies it.
- W6: subscribe `params` (live-check S).
- W9: `FEplaylist_aggregation` if the live check (S) confirms `FElibrary` is wrong.
- W10: relevance search sends `EgIQAQ==`; when a page has no regular videos but Shorts, they
  are returned (T `search:…kind:`).
- W11: fixture of a modern signed-in channel/watch answer; read
  `frameworkUpdates.entityBatchUpdate.mutations[].payload.subscriptionStateEntity` first.

Live: W3, W6, W9, W11 (S); W10 anonymous.

### P1 — identities

M1, M2, M3, M4.

- `src/core/hash`: SHA-1. SAPISIDHASH = `SAPISIDHASH <ts>_<hex(sha1("<ts> <SAPISID> <origin>"))>`
  with `X-Goog-AuthUser: 0`, `Origin`/`X-Origin: https://www.youtube.com`; SAPISID falls back
  to `__Secure-3PAPISID` (T `YTWebAuth`, Z `BrowserSession`). Cookies only on requests the
  library signs (§4.1); never in a log (existing redaction extended).
- Accounts: `accountsIn` walks the list for `pageId` / `personaId` / `obfuscatedGaiaId` and the
  `datasyncIdToken` (T `+accountsIn:`, `+pageIdIn:`, `+datasyncIdIn:`); `select()` sets
  `SessionState::channel`; on 401 with a channel active, switch once to the datasync form
  (T `switchIdentityForm`) and remember it.
- Locale ladder §4.4 in `HttpClient` (it owns retries), per endpoint.
- Search routing per §4.1.

Live: web-session requests signed (S), brand channel switch (S, needs an account with a brand
channel), locale ladder with `gl=KG` (anonymous).

### P2 — personal surfaces and feeds

W7, W8, M6, M7, M8, M9, M11, M21, M25, M26, M27, M32, X1.

- `ClientId::MWEB` (T: client 2, `2.20251222.01.00`); write ladder §4.3.
- Watch state from TV `/next` (T `+watchState:`): `likeButtonRenderer.likeStatus` /
  `likeStatusEntity`, subscribe + bell (`subscriptionNotificationToggleButtonRenderer`
  current state, else `subscriptionNotificationStateEntity`), `commentsEntryPointRenderer`
  count, comments panel token, owner avatar.
- Bell: params = base64 of `0A <len> <channelId> 12 02 08 <code> 18 00 20 04`, code 1
  personalized / 2 all / 3 none (T `+notificationParams:channel:`), via `proto.h`.
- Save dialog: `playlist/get_add_to_playlist` {videoIds, excludeWatchLater:false};
  `containsSelectedVideos` is "ALL"/"SOME"/"NONE" or a bool (T `+playlistSaveStates:`).
- Remove row: `ACTION_REMOVE_VIDEO` with `setVideoId` (Z); rows carry `setVideoId`.
- X1: `playlist/create` {title, privacyStatus, videoIds}; `playlist/delete` {playlistId};
  `browse/edit_playlist` `ACTION_SET_PLAYLIST_NAME` / `ACTION_SET_PLAYLIST_PRIVACY`. No client
  uses them: live-verify every one (S) before release.
- History reporting (T `+reportWatched:`, U `ReportWatchHistoryAsync`, Z
  `reportWatchHistory`): GET `videostatsPlaybackUrl` once, then `videostatsWatchtimeUrl`
  segments with `&cpn&ver=2&c=TVHTML5&cver&cplayer=UNIPLAYER&cmodel=SmartTV&cos=Tizen
  &cosver=5.0&cplatform=TV&hl&cr&cmt&st&et&rt&len[&final=1]`, bearer +
  `X-YouTube-DataSync-Id` (+ `X-Goog-PageId` for a non-default channel). One cpn per view;
  the caller reports watched intervals, never seek distance (Z). 204 is success. Only trusted
  `https://*.youtube.com` stats hosts receive the bearer; no redirects with credentials.
- Feeds: live (T `+liveFeed:`, named-node continuation `+liveContinuationIn:`); history
  sections (WEB + web session; TV flat — T `+historyPage:`); chips (T `+homeCategories`
  local list; Live = `FEwhat_to_watch` with params `EgIIBBoETGl2ZUgC` as WEB); flags from
  badges/overlays (thumbnailOverlayTimeStatusRenderer style LIVE/UPCOMING/SHORTS,
  resume-playback overlay percent).
- W8: signed out, `home()` serves the guest feed: browse `VLPL-p0-Yh03xpi2AsCiyuafMeQrMF6czMoL`
  and its continuation (U `Config.GuestHome.cs`, mapping inherited from TubeReplacer).
- M32: when a signed-in user's playlist comes back empty from InnerTube, GET
  `https://www.googleapis.com/youtube/v3/playlistItems?part=snippet&playlistId=…&maxResults=50`
  with the bearer, page tokens verbatim (U `Config.Playlists.cs`, Z). The bearer goes to that
  exact origin only.

Live: every write (S), history reporting appears in the account (S), guest home and live
feed (anonymous).

### P3 — content

W5 (anonymous half), M10, M12, M13, M17 (without the token), M19, M24, M28, M30, M31, X2.

- Shorts feed: `reel/reel_item_watch` {inputType REEL_WATCH_INPUT_TYPE_SEEDLESS, params
  `CA8%3D`, disablePlayerResponse:true}, then `reel/reel_watch_sequence` {sequenceParams};
  the client that served page 1 serves all; reel TV header client number 7; web clients need
  `X-Goog-Visitor-Id`; drop ads (`adSlotRenderer`, `isAd`). Player chain for Shorts:
  ANDROID → WEB (T `+shortsPlayerResponse:gate:`).
- Channel tabs: params videos `EgZ2aWRlb3PyBgQKAjoA`, shorts `EgZzaG9ydHPyBgUKA5oBAA%3D%3D`,
  playlists `EglwbGF5bGlzdHPyBgQKAkIA`, featured `EghmZWF0dXJlZPIGBAoCMgA%3D`, streams
  `EgdzdHJlYW1z8gYECgJ6AA%3D%3D`, posts `EgVwb3N0c_IGBAoCSgA%3D` (T `+channel:tab:`); the
  server's own tab list from `twoColumnBrowseResultsRenderer.tabs` (Z) for anything else.
- Watch queue: `/next` {videoId, playlistId[, playlistIndex]} → `playlist.playlist`
  (`playlistPanelVideoRenderer`, `currentIndex`); mixes `RD…` (T `+videoDetails:playlist:`).
- Search: Shorts kind (no params; `reelItemRenderer` / `shortsLockupViewModel`); filters
  built with `proto.h` (field 1 sort; field 2 {1 upload date, 2 type, 3 duration, features})
  — field numbers from public knowledge, live-verify each.
- Comments: replies continuation from `commentThreadRenderer.replies` joined by
  `toolbarStateKey`; list continuation = the last item of
  `appendContinuationItemsAction`/`reloadContinuationItemsCommand`; reply
  `comment/create_comment_reply` {createReplyParams, commentText}; edit
  `comment/update_comment` {updateCommentParams, commentText}; posting = TV token + params from
  WEB (T `+postComment…`, `+replyComment…`, `+editComment…`). X2: sort from the header's
  `sortFilterSubMenuRenderer` tokens; like/delete via `comment/perform_comment_action` with
  the comment's action params (live-verify, S).
- Chapters: `M:SS`/`H:MM:SS` lines of the description, first at 0:00, ascending (T, U).
- Captions: `CaptionTrack::translations` from `translationLanguages`; `tlang=` appended
  byte-exact (U `Subtitles.cs`); may answer 429 without a PO token (T) — P5 adds `pot`.
- SponsorBlock: GET `https://sponsor.ajay.app/api/skipSegments/<hex(sha256(videoId))[:4]>
  ?categories=[…]`, keep the entry whose `videoID` matches, `actionType == "skip"` unless the
  caller asks for more (T `YTSponsorBlock.m`, U `SponsorBlock.cs`).

Live: all anonymous except comment writes (S).

### P4 — live chat

M29. Token from `/next` (`liveChatRenderer…reloadContinuationData`); poll
`live_chat/get_live_chat` {continuation}; items `addChatItemAction`
(`liveChatTextMessageRenderer`, paid message/sticker, membership), removals
(`markChatItemAsDeletedAction`, by author), banners (`addBannerToLiveChatCommand`,
`removeBannerForLiveChatCommand`); next token and `timeoutMs` from
invalidation/timed/reload continuation data, clamped to ≥ 2 s (Z). Top/All tokens only from
the popout page `/live_chat?v=…&is_popout=1` (`ytInitialData`, a bounded extent read with
`yt::json`; T `+liveChatFiltersForVideo:`): `/next`'s are truncated and answer 400. Replays
(`live_chat_replay`) are not in either client and stay out.

Live: a running stream (anonymous).

### P5 — JS seam

M14, M15 (stretch), M16, M17's token, M19's `pot`.

- `n`: realm per player build; source = the build's `player_ias.vflset/…/base.js` + the
  solver (§2); results cached per (build, n), at most 64. Only the `n` value is replaced in a URL,
  every other byte stays (Z's rule). With a host, WEB/WEB_SAFARI/TV formats become sources.
- PO token: `https://www.youtube.com/api/jnn/v1/Create` and `GenerateIT` (request key
  `O43z0dpjhgX20SCx4KAo`; T `YTPoToken.m`), the program runs in a `browserLike()` realm
  driven by TubeReplacer's `challenge_solver.html` (Preloading, via T; shipped as a resource
  string with its attribution); the
  integrity token is reused until its refresh time (~12 h); mint per binding — visitorData
  (or datasyncId when signed in) for `/player`'s `serviceIntegrityDimensions.poToken`, videoId
  for media and caption URLs (`pot=`). Android clients reject it (T).
- `signatureCipher`: only if the solver approach extends to the signature function; else
  formats stay `sealed` (today's behaviour).

Live: n on a WEB format (206 with the transformed URL), PO token minted and accepted (needs a
real web view; the owner's MeeTube or a desktop host).

### P6 — SABR, reload, media

M18, M20, M22, M23.

- `IHttp::stream`: a binary POST whose body arrives in pieces to a sink (no whole-body
  buffer; per-part cap 16 MiB as in Z); same security policy (HTTPS googlevideo only, no
  credentials, no redirects).
- UMP reader (length prefix is UTF-8-like, not a protobuf varint) and protobuf reader;
  `VideoPlaybackAbrRequest` (fields 1, 5, 16/17, 19, 29 for live); parts 20/21/22, 35, 42, 43,
  44, 46, 58, 31, 69 (T `YTSabr.m`, `YTUmp.m`, `YTProto.m`, based on TubeReplacer; code ported
  from Z `src/core/sabr.cpp`).
- Reload: `/player` with `playbackContext.reloadPlaybackContext.reloadPlaybackParams` by the
  client that issued the session (T `+tvPlayerResponse:reloadToken:`).
- TV player profile: `ClientId::TV_PLAYER` only if live checks show the browse identity
  failing for `/player` (T keeps them apart: 7.20260715 vs 7.20250209).
- Media download: ranged GETs of at most 8 MiB per request (an open `Range` is throttled —
  T `YTDownloads.m`), resumable from a byte offset, the Source's User-Agent; URL-less formats
  via the SABR session.

Live: a SABR VOD and a live stream fetched to the end of a segment run; a 100 MB download.

## 7. Testing

- Unit: FakeHttp chains (sent client, headers, body; routing table §4.1 as one table-driven
  test), parsers on trimmed real fixtures in `tests/fixtures/` (well-formed JSON), hash and
  proto against published vectors, UMP/protobuf against Z's and T's captured bytes.
- Every W item: a regression test that fails before its fix.
- The four build trees of AGENTS.md §2 green; the transport suites with a dead proxy.
- Live checks listed per phase; the S ones are run by the owner (the agent has no account)
  and recorded in the phase plan with the date.

## 8. Not changing

Transport seam (`HttpTransport`) and backends; `yt::json`; `Result`/`ErrorCode` (new codes
only if a phase needs one); `choose()` and the player walk order of phase 1 except where P3/P5
add clients.

## 9. Open questions

1. Signed-in live verification needs the owner's account (and one with a brand channel for P1).
