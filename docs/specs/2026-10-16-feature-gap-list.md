# Feature gap list — libinnertube vs Troubadour, youtube_uwp and zemonkamin's fork

Research of 2026-10-16 (owner asked: every function the Troubadour / youtube_uwp clients support
must exist in the lib, fully). Compared: lib @ c1f54c0 (dev/extraction) vs Troubadour db2b0da
(`src/net/YTApi.m`), youtube_uwp eca3a12 (`YouTube/Innertube/Config*.cs`), and the fork in
`zemonkamin-moded/` (based on the pre-R2 lib; its spec/plan `docs/*/2026-10-14-uwp-parity*`).
Sources: T = Troubadour, U = youtube_uwp, Z = fork (attribution rules: the design §2).
WRONG = present but incorrect (fix first), MISS = absent, PART = partial. "verify" = evidence only from a reference, not live-checked.

The phases that implement these items are in `2026-10-16-feature-parity-design.md`; when an
item lands, note its commit next to it here.

## WRONG (defects in the current lib)
- W1 accounts_list sends only `returnOwner` → brand/persona channels never listed (T accountReadMask).
- W2 Account::save/unsave to "LL" goes to browse/edit_playlist; LL is changed by like/removelike (T, Z).
  Also a "VL" prefix is not stripped from the playlist id for edits (T savePlaylistIdFrom).
- W3 playlists().get of a non-WL/LL playlist always goes anonymous WEB → a signed-in user's
  private/unlisted playlists fail; signed in must browse with TV+bearer (T).
- W4 comments().post reports success on any 2xx; YouTube answers 200 with a refusal. Success =
  actionResult STATUS_SUCCEEDED or a returned comment; else the refusal text (T commentAccepted/
  refusalTextIn, Z writeComment).
- W5 comments of a Short: WEB /next has no comments panel → lib says `disabled`; TV /next carries
  the token (T watchState). verify anonymously.
- W6 subscribe/unsubscribe omit `params` CgIIAxgA / CgIIAxgB (T, U DefaultSubscribeParams). verify.
- W7 rate omits server-issued like/dislike/removeLike `params` from TV /next likeEndpoint, and
  writes have no fallback TVHTML5 → MWEB → WEB (T rate, setVideo, playlistSaveStates).
- W8 signed-out feeds().home(): WEB FEwhat_to_watch answers 200 with no videos (T); U shows a guest
  feed = playlist PL-p0-Yh03xpi2AsCiyuafMeQrMF6czMoL (TubeReplacer mapping), T a search prompt.
- W9 own playlists use FElibrary; T and U use FEplaylist_aggregation on TV. verify.
- W10 relevance search sends no params → mixed result kinds; T sends EgIQAQ== (videos). A page of
  only Shorts yields an empty list (T falls back to the shorts in it).
- W11 subscribe state: `subscribeButtonViewModel` carries BOTH templates; the real state is
  frameworkUpdates…subscriptionStateEntity (T applySubscriptionEntitiesTo). Lib reads the first
  subscribeButtonRenderer.subscribed — check against a modern fixture.

## MISSING — identity, transport
- M1 web session: host-supplied cookies, SAPISIDHASH = SHA1("ts SAPISID origin"), X-Goog-AuthUser,
  Origin/X-Origin; cookies only on requests we sign, never IOS/VR/VISIONOS/TV (T YTWebAuth, Z
  BrowserSession). Needs an in-lib SHA-1.
- M2 accounts: list all channels, active channel = context.user.onBehalfOfUser ONLY on signed
  requests (401 on anon otherwise); on 401 switch pageId → datasync "profile||owner" once; persist;
  identity change clears caches (T, Z). X-Goog-PageId for non-primary history pings.
- M3 locale ladder: 400 (browse) or empty search `contents` → gl=US, then hl=en; never on a
  failed reply (T relaxLocale). Make it per-request/reversible, not session-wide.
- M4 signed-in search/personalised chips: search as TV+bearer when no web session (T).
- M5 consent wall (consentBumpV2Renderer) → SOCS retry once (roadmap; T deliberately sends no SOCS).
## MISSING — feeds and lists
- M6 live feed: signed FEtopics_live (TV) / anon UC4R8DWoMoI7CAwX8_LjQHig (WEB); two continuation
  kinds — take the token of the named node (T liveContinuationIn).
- M7 sections: history day groups (WEB+web session only; TV is flat), live shelves; Page has no
  section titles (T historyPage/shelvesIn).
- M8 home chips: local list of English queries + "Live" = FEwhat_to_watch params EgIIBBoETGl2ZUgC
  via WEB (T homeCategories); Z dynamic chipCloud filters (fetchFilters).
- M9 item flags: live / upcoming / short / members-only badges, watched progress % (Z, U).
- M10 search kinds/filters: shorts (no params; reelItemRenderer/shortsLockupViewModel), server
  filter params (upload date, duration, features) (T, Z SearchRequest.params).
## MISSING — watch page
- M11 watch state from TV /next: bell state, comment count (commentsEntryPointRenderer), like
  params, comments token, owner avatar/subscriber text (T watchState).
- M12 watch queue: /next with playlistId (+index) → playlistPanelVideoRenderer, currentIndex; mixes
  RD… (T videoDetails:playlist:, Z WatchRequest).
- M13 chapters from description timestamps (T, U UpdateDescriptionChaptersFromDescription).
## MISSING — player
- M14 n transform via a consumer JS evaluator (T Resources/nsig.js; Z nsolver.inc, from U
  Assets/nsig.js).
- M15 signatureCipher decipher (Format::sealed exists, no decipher) — same JS seam.
- M16 PO token (BotGuard: jnn/v1/Create + GenerateIT, key O43z0dpjhgX20SCx4KAo; host runs the VM in
  a browser-like view) (T YTPoToken, Z ClientOptions::poToken). Player vs media bindings differ.
- M17 Shorts player chain ANDROID → WEB → WEB+token (VR bot-walls Shorts) (T shortsPlayerResponse).
- M18 reload: /player with reloadPlaybackContext token by THE SAME client (T, Z reloadToken).
- M19 caption translation: translationLanguages list + tlang URL (U Subtitles.cs; T: 429 w/o pot).
- M20 TV player profile separate from TV browse (T YTTvPlayerVersion 7.20260715, legacy
  7.20220918; Z TVHTML5_PLAYER/LEGACY).
- M21 history reporting: videostats playback + watchtime segments, one cpn per view, bearer +
  X-YouTube-DataSync-Id (+ X-Goog-PageId); 204 = ok (T reportWatched, U, Z reportWatchHistory).
- M22 SABR session (UMP + protobuf, parts 20/21/22/35/42/43/44/46/58/31/69, live field 29)
  (T YTSabr/YTUmp/YTProto, Z sabr.cpp). LiveTimeline (Z) is a MediaStreamSource clock → client.
- M23 media fetch helper: ranged chunks (open Range is throttled; T 8 MiB chunks) — optional.
## MISSING — channels, playlists, comments, live, shorts, extras
- M24 channel tabs shorts/streams/posts/featured (T params) + server-issued tab list (Z).
- M25 bell: notification/modify_channel_preference, hand-built proto (T notificationParams).
- M26 save dialog: playlist/get_add_to_playlist → containsSelectedVideos ALL/SOME/bool (T, U, Z).
- M27 remove a specific row by setVideoId (Z editPlaylist).
- M28 comments: replies continuation, reply (create_comment_reply), edit own (update_comment),
  createReplyParams/updateCommentParams; posting = TV token + WEB params (T).
- M29 live chat: token from /next, get_live_chat polling, text/paid/membership, deletions, banners,
  timeoutMs; Top/All filters only from /live_chat?is_popout=1 page (T, Z).
- M30 Shorts feed: reel_item_watch (params CA8%3D, SEEDLESS) + reel_watch_sequence; same client for
  all pages; filter ads (T, U, Z).
- M31 SponsorBlock: GET sponsor.ajay.app/api/skipSegments/<sha256(id)[:4]>, keep actionType skip
  (T, U). Needs an in-lib SHA-256.
- M32 Data API v3 fallback for empty playlist items / own playlists (U Config.Playlists, Z) — optional.
## Beyond the references (owner decided 2026-10-16: included — X1, X2 in the design)
create/delete/rename playlist, comment like/delete, comment sort Top/Newest, notifications menu
(both clients use the subscriptions feed: TV token → 400 on get_notification_menu).
## Out of scope (client concerns)
UI, downloads manager/muxing (Mp4 muxer, TS muxer, HLS proxy, DashDemuxer), Discord RPC, VPN (T warp),
app updates, image loading, settings.
