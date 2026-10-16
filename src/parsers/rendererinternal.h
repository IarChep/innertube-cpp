#ifndef YT_RENDERERINTERNAL_H
#define YT_RENDERERINTERNAL_H
// Internal (non-public) shared layer for the per-domain renderer parsers: the converters
// from one renderer object (a json::Value) to a yt:: value type, shared by the
// videolist/watch/playlist/channel/comment/account TUs. Paths are written out where they
// are read — a renderer's shape lives in exactly one converter. Everything is `inline`
// so the TUs share one definition (COMDAT-folded by the linker).
//
#include "rendererparser.h"
#include "json/json.h"
#include "json/ytjson.h"
#include "textmatch.h"
#include <string>
#include <string_view>
#include <vector>

namespace yt {

using gj::digitsOf;
using gj::firstSourceUrl;
using gj::lastSourceUrl;
using gj::lastThumbUrl;
using gj::textOf;
using json::Value;
using json::Visit;

// The leaf renderer keys of the list collectors: a matched renderer is converted and NOT
// looked into, and the rest of the object holding it is skipped (Visit::SkipRest) — the
// old walkers' early `return`.
inline bool isVideoKind(std::string_view key)
{
    return key == "videoRenderer" || key == "compactVideoRenderer"
        || key == "gridVideoRenderer" || key == "playlistVideoRenderer";
}

inline bool isPlaylistKind(std::string_view key)
{
    return key == "playlistRenderer" || key == "gridPlaylistRenderer"
        || key == "compactPlaylistRenderer";
}

// videoRenderer / compactVideoRenderer / gridVideoRenderer / playlistVideoRenderer.
inline VideoItem fromVideoRenderer(Value r)
{
    VideoItem v;
    v.id = r["videoId"].string();
    v.title = textOf(r["title"]);
    // Presence-based fallback (not value-based): mirrors the old ternary on contains().
    if (r["ownerText"]) v.channel.name = textOf(r["ownerText"]);
    else if (r["longBylineText"]) v.channel.name = textOf(r["longBylineText"]);
    v.durationText = textOf(r["lengthText"]);
    v.thumbnailUrl = lastThumbUrl(r["thumbnail"]);   // native (WebP); decoded by the qwebp plugin
    if (v.thumbnailUrl.empty() && !v.id.empty())
        v.thumbnailUrl = "https://i.ytimg.com/vi/" + v.id + "/hqdefault.jpg";
    v.channel.avatarUrl = lastThumbUrl(
        r.path("channelThumbnailSupportedRenderers", "channelThumbnailWithLinkRenderer", "thumbnail"));
    if (const Value vc = r["viewCountText"]) {       // the exact count ("1,234 views"), unlike lockups
        v.viewText = textOf(vc);
        v.viewCount = digitsOf(v.viewText);
    }
    v.publishedText = textOf(r["publishedTimeText"]);   // "2 days ago" — shown on the delegate
    v.durationSecs = gj::clockSecs(v.durationText);
    return v;
}

// The channel a lockup metadata part links to (its commandRuns' browseEndpoint), "" if none.
inline std::string linkedChannel(Value part)
{
    for (Value run : part.path("text", "commandRuns").items()) {
        const std::string_view id = run.path("onTap", "innertubeCommand", "browseEndpoint", "browseId").str();
        if (id.substr(0, 2) == "UC") return std::string(id);
    }
    return std::string();
}

// lockupViewModel with LOCKUP_CONTENT_TYPE_VIDEO.
inline VideoItem fromLockupVideo(Value lm)
{
    VideoItem v;
    v.id = lm["contentId"].string();
    // Thumbnail + the duration badge overlay.
    const Value tvm = lm.path("contentImage", "thumbnailViewModel");
    v.thumbnailUrl = lastSourceUrl(tvm["image"]);
    for (Value ov : tvm["overlays"].items())
        for (Value b : ov.path("thumbnailBottomOverlayViewModel", "badges").items()) {
            const std::string_view t = b.path("thumbnailBadgeViewModel", "text").str();
            // Only the time-shaped badge ("4:56"/"1:02:03") is the duration.
            if (v.durationText.empty() && gj::isClockText(t)) v.durationText = std::string(t);
        }
    if (v.thumbnailUrl.empty() && !v.id.empty())
        v.thumbnailUrl = "https://i.ytimg.com/vi/" + v.id + "/hqdefault.jpg";
    const Value md = lm.path("metadata", "lockupMetadataViewModel");
    v.title = md.path("title", "content").string();
    // Channel avatar + id from the decorated avatar's onTap browseEndpoint.
    const Value dav = md.path("image", "decoratedAvatarViewModel");
    v.channel.avatarUrl = lastSourceUrl(dav.path("avatar", "avatarViewModel", "image"));
    v.channel.id = dav.path("rendererContext", "commandContext", "onTap", "innertubeCommand", "browseEndpoint",
                            "browseId").string();
    // metadataRows: [channel] [views, date] (feeds, related) or [channel, views, date]
    // in one row (search) — but a channel's own tab omits the channel: [views, date].
    // The first part is the channel when it links to one, or when more parts follow
    // than views and date.
    const Value rows = md.path("metadata", "contentMetadataViewModel", "metadataRows");
    size_t parts = 0;
    for (Value row : rows.items()) parts += row["metadataParts"].size();
    bool first = true;
    for (Value row : rows.items())
        for (Value p : row["metadataParts"].items()) {
            const std::string txt = p.path("text", "content").string();
            if (txt.empty()) continue;
            const std::string link = linkedChannel(p);
            if (first && (!link.empty() || rows.size() > 1 || parts >= 3)) {
                v.channel.name = txt;
                if (!link.empty()) v.channel.id = link;
            }
            else if (v.viewText.empty() && gj::containsCI(txt, "view")) v.viewText = txt;
            else if (v.publishedText.empty() && txt.find("ago") != std::string::npos) v.publishedText = txt;
            first = false;
        }
    v.durationSecs = gj::clockSecs(v.durationText);
    return v;
}

// tileRenderer — the TV UI card. Self-contained leaf (like lockupViewModel); only
// TILE_CONTENT_TYPE_VIDEO becomes a VideoItem. metadata.tileMetadataRenderer carries
// title + lines (line 0 = channel, line 1 = views [+ date]); the duration rides in a
// thumbnailOverlayTimeStatusRenderer.
inline VideoItem fromTile(Value t)
{
    VideoItem v;
    v.id = t["contentId"].string();
    const Value md = t.path("metadata", "tileMetadataRenderer");
    v.title = textOf(md["title"]);
    size_t li = 0;
    for (Value line : md["lines"].items()) {
        for (Value item : line.path("lineRenderer", "items").items()) {
            const std::string text = textOf(item.path("lineItemRenderer", "text"));
            if (text.empty()) continue;
            if (li == 0 && v.channel.name.empty()) v.channel.name = text;
            else if (v.viewText.empty() && text.find("view") != std::string::npos) v.viewText = text;
            // Date lives on a later line; pick the DATED item ("3 days ago" /
            // "Streamed 2 days ago" / "Sep 5, 2024"), never the "•" separator or
            // other bullets that sit between the view count and the date.
            else if (v.publishedText.empty() && li > 0
                     && (text.find("ago") != std::string::npos || gj::hasYear(text)))
                v.publishedText = text;
        }
        ++li;
    }
    const Value h = t.path("header", "tileHeaderRenderer");
    v.thumbnailUrl = lastThumbUrl(h["thumbnail"]);   // last = largest
    for (Value ov : h["thumbnailOverlays"].items())
        if (const Value text = ov.path("thumbnailOverlayTimeStatusRenderer", "text"))
            v.durationText = textOf(text);
    if (v.thumbnailUrl.empty() && !v.id.empty())
        v.thumbnailUrl = "https://i.ytimg.com/vi/" + v.id + "/hqdefault.jpg";
    // Channel id from the long-press "Go to channel" menu (the video tile carries no
    // channel field of its own). Populating userId makes tapping the author/avatar open
    // the channel page. NOTE: the TV home tile has NO avatar, and YouTube avatar URLs
    // are opaque hashes (not constructible from the id) — a proxy (unavatar.io) is
    // rate-limited and fetching each channel is too heavy for a feed, so the home grid
    // keeps the placeholder squircle. The author avatar still shows on the watch page (parsed
    // from the /next owner).
    for (Value mi : t.path("onLongPressCommand", "showMenuCommand", "menu", "menuRenderer", "items").items()) {
        const std::string_view bid =
            mi.path("menuNavigationItemRenderer", "navigationEndpoint", "browseEndpoint", "browseId").str();
        if (bid.substr(0, 2) == "UC") {
            v.channel.id = std::string(bid);
            break;
        }
    }
    v.durationSecs = gj::clockSecs(v.durationText);
    return v;
}

// tileRenderer with TILE_CONTENT_TYPE_CHANNEL — the TV UI channel card. The authed
// FEchannels (manage-subscriptions) grid ships these instead of gridChannelRenderer:
// contentId = channelId, metadata title = name, line 0 = @handle, a "N subscribers"
// line, and the header thumbnail = the channel avatar.
inline Channel fromTileChannel(Value t)
{
    Channel u;
    u.id = t["contentId"].string();
    const Value md = t.path("metadata", "tileMetadataRenderer");
    u.name = textOf(md["title"]);
    for (Value line : md["lines"].items())
        for (Value item : line.path("lineRenderer", "items").items()) {
            const std::string text = textOf(item.path("lineItemRenderer", "text"));
            if (text.empty()) continue;
            if (u.handle.empty() && text[0] == '@') u.handle = text;
            else if (u.subscriberText.empty() && text.find("subscriber") != std::string::npos)
                u.subscriberText = text;
        }
    u.avatarUrl = lastThumbUrl(t.path("header", "tileHeaderRenderer", "thumbnail"));
    return u;
}

// tileRenderer with TILE_CONTENT_TYPE_PLAYLIST — the TVHTML5 shape for a playlist
// card. The signed-in FElibrary browse ships the user's playlists (Liked videos,
// created/saved playlists incl. YouTube Music) as THESE, not gridPlaylistRenderer/
// lockupViewModel: contentId = playlistId (LL / WL / PL…), metadata title = name,
// the header thumbnail = cover, and "N videos" rides a thumbnailOverlayTimeStatus-
// Renderer (the same overlay a video tile uses for its duration).
inline Playlist fromTilePlaylist(Value t)
{
    Playlist p;
    p.id = t["contentId"].string();
    p.title = textOf(t.path("metadata", "tileMetadataRenderer", "title"));
    const Value h = t.path("header", "tileHeaderRenderer");
    p.thumbnailUrl = lastThumbUrl(h["thumbnail"]);   // last = largest
    // "N videos" is the only text overlay on a playlist tile; take its digits
    // directly (no "video" word-match — the label is localized).
    for (Value ov : h["thumbnailOverlays"].items())
        if (const Value text = ov.path("thumbnailOverlayTimeStatusRenderer", "text")) {
            const int64_t n = digitsOf(textOf(text));
            if (n > 0) p.videoCount = n;
        }
    return p;
}

// playlistRenderer / gridPlaylistRenderer / compactPlaylistRenderer.
inline Playlist fromPlaylistRenderer(Value r)
{
    Playlist p;
    p.id = r["playlistId"].string();
    // Some shapes carry a plain string title, others a text object.
    const Value title = r["title"];
    p.title = title.isString() ? title.string() : textOf(title);
    if (const Value t = r["thumbnail"]) p.thumbnailUrl = lastThumbUrl(t);
    else p.thumbnailUrl = lastThumbUrl(r["thumbnails"][0]);
    if (r["shortBylineText"]) p.owner.name = textOf(r["shortBylineText"]);
    else if (r["longBylineText"]) p.owner.name = textOf(r["longBylineText"]);
    // Only a string videoCount counts; a numeric one falls through to the text forms.
    std::string vc = r["videoCount"].string();
    if (vc.empty()) vc = textOf(r["videoCountText"]);
    if (vc.empty()) vc = textOf(r["videoCountShortText"]);
    p.videoCount = digitsOf(vc);
    return p;
}

// lockupViewModel with LOCKUP_CONTENT_TYPE_PLAYLIST — the 2024+ channel Playlists
// tab ships these instead of gridPlaylistRenderer. The thumbnail hides under
// collectionThumbnailViewModel.primaryThumbnail and "N videos" is a thumbnail badge.
inline Playlist fromPlaylistLockup(Value lm)
{
    Playlist p;
    p.id = lm["contentId"].string();
    const Value tvm = lm.path("contentImage", "collectionThumbnailViewModel", "primaryThumbnail", "thumbnailViewModel");
    p.thumbnailUrl = lastSourceUrl(tvm["image"]);
    for (Value ov : tvm["overlays"].items())
        for (Value b : ov.path("thumbnailOverlayBadgeViewModel", "thumbnailBadges").items()) {
            const std::string_view t = b.path("thumbnailBadgeViewModel", "text").str();
            if (p.videoCount == 0 && gj::containsCI(t, "video")) p.videoCount = digitsOf(t);
        }
    p.title = lm.path("metadata", "lockupMetadataViewModel", "title", "content").string();
    return p;
}

// channelRenderer / gridChannelRenderer.
inline Channel fromUserRenderer(Value r)
{
    Channel u;
    u.id = r["channelId"].string();
    u.name = textOf(r["title"]);
    u.avatarUrl = lastThumbUrl(r["thumbnail"]);
    u.subscriberText = textOf(r["subscriberCountText"]);
    u.description = textOf(r["descriptionSnippet"]);
    return u;
}

// commentEntityPayload (frameworkUpdates mutations).
inline CommentThread fromCommentPayload(Value p)
{
    CommentThread t;
    Comment &c = t.comment;
    const Value props = p["properties"], author = p["author"], toolbar = p["toolbar"];
    c.id = props["commentId"].string();
    c.text = props.path("content", "content").string();
    c.publishedText = props["publishedTime"].string();
    c.author.id = author["channelId"].string();
    c.author.name = author["displayName"].string();
    c.author.avatarUrl = author["avatarThumbnailUrl"].string();
    c.mine = author["isCurrentUser"].boolean();
    if (c.author.avatarUrl.empty()) c.author.avatarUrl = firstSourceUrl(p.path("avatar", "image"));
    c.likeText = toolbar["likeCountNotliked"].string();
    t.replyText = toolbar["replyCount"].string();
    return t;
}

} // namespace yt
#endif
