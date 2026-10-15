#ifndef YT_RENDERERINTERNAL_H
#define YT_RENDERERINTERNAL_H
// Internal (non-public) shared layer for the per-domain renderer parsers.
// Holds the struct→CT converters over the renderer shapes (json/shapes/, namespace
// rj), and the scanner-visitor scaffolding shared across the six
// videolist/watch/playlist/channel/comment/account TUs that rendererparser.cpp
// was split into. Everything is `inline` so the six TUs share one definition
// (COMDAT-folded by the linker) with no duplicate-symbol / unused-static churn.
//
#include "rendererparser.h"
#include "json/shapes/account.h"
#include "json/shapes/channel.h"
#include "json/shapes/comments.h"
#include "json/shapes/renderers.h"
#include "json/shapes/watch.h"
#include "json/tokenscan.h"
#include "json/jsonscan.h"
#include "textmatch.h"
#include <map>
#include <vector>

namespace yt {

using namespace gj;


// ---------------------------------------------------------------------------
// Glaze field → std::string: identity for plain strings, "" for an absent optional.
// ---------------------------------------------------------------------------
inline const std::string &str(const std::string &s) { return s; }
inline std::string str(const std::optional<std::string> &s) { return s ? str(*s) : std::string(); }

// "digits only" → number (view counts arrive as "1,234 views"): gj::digitsOf.
using gj::digitsOf;

// ---------------------------------------------------------------------------
// struct → CT conversions (the only Qt-aware part of the parse).
// ---------------------------------------------------------------------------
inline VideoItem fromVideoRenderer(const rj::VideoR &r)
{
    VideoItem v;
    v.id = str(r.videoId);
    v.title = str(textOf(r.title));
    // Presence-based fallback (not value-based): mirrors the old ternary on contains().
    if (r.ownerText)            v.channel.name = str(textOf(*r.ownerText));
    else if (r.longBylineText)  v.channel.name = str(textOf(*r.longBylineText));
    v.durationText = str(textOf(r.lengthText));
    v.thumbnailUrl = str(lastThumbUrl(r.thumbnail));   // native (WebP); decoded by the qwebp plugin
    if (v.thumbnailUrl.empty() && !v.id.empty())
        v.thumbnailUrl = "https://i.ytimg.com/vi/" + v.id + "/hqdefault.jpg";
    // Channel avatar: videoRenderer.channelThumbnailSupportedRenderers
    //   .channelThumbnailWithLinkRenderer.thumbnail.thumbnails[].url
    if (r.channelThumbnailSupportedRenderers
        && r.channelThumbnailSupportedRenderers->channelThumbnailWithLinkRenderer)
        v.channel.avatarUrl = str(lastThumbUrl(
            r.channelThumbnailSupportedRenderers->channelThumbnailWithLinkRenderer->thumbnail));
    if (r.viewCountText) {                      // the exact count ("1,234 views"), unlike lockups
        v.viewText = str(textOf(*r.viewCountText));
        v.viewCount = digitsOf(v.viewText);
    }
    if (r.publishedTimeText)                    // "2 days ago" — shown on the delegate
        v.publishedText = str(textOf(*r.publishedTimeText));
    v.durationSecs = gj::clockSecs(v.durationText);
    return v;
}

// The channel a lockup metadata part links to (its commandRuns' browseEndpoint), "" if none.
inline std::string linkedChannel(const rj::MetaPart &p)
{
    if (!p.text || !p.text->commandRuns) return std::string();
    for (const rj::CmdCtx &run : *p.text->commandRuns)
        if (run.onTap && run.onTap->innertubeCommand && run.onTap->innertubeCommand->browseEndpoint) {
            std::string id = str(run.onTap->innertubeCommand->browseEndpoint->browseId);
            if (id.starts_with("UC")) return id;
        }
    return std::string();
}

inline VideoItem fromLockupVideo(const rj::Lockup &lm)
{
    VideoItem v;
    v.id = str(lm.contentId);
    // Thumbnail + the duration badge overlay.
    if (lm.contentImage && lm.contentImage->thumbnailViewModel) {
        const rj::ThumbVM &tvm = *lm.contentImage->thumbnailViewModel;
        v.thumbnailUrl = str(lastSourceUrl(tvm.image));
        if (tvm.overlays)
            for (const rj::OverlayW &ov : *tvm.overlays) {
                if (!ov.thumbnailBottomOverlayViewModel) continue;
                const rj::BottomOverlay &bo = *ov.thumbnailBottomOverlayViewModel;
                if (!bo.badges) continue;
                for (const rj::BadgeW &b : *bo.badges) {
                    if (!b.thumbnailBadgeViewModel) continue;
                    const std::string t = str(b.thumbnailBadgeViewModel->text);
                    // Only the time-shaped badge ("4:56"/"1:02:03") is the duration.
                    if (v.durationText.empty() && gj::isClockText(t))
                        v.durationText = t;
                }
            }
    }
    if (v.thumbnailUrl.empty() && !v.id.empty())
        v.thumbnailUrl = "https://i.ytimg.com/vi/" + v.id + "/hqdefault.jpg";
    if (lm.metadata && lm.metadata->lockupMetadataViewModel) {
        const rj::LockupMetaVM &md = *lm.metadata->lockupMetadataViewModel;
        if (md.title) v.title = str(contentOf(md.title));
        // Channel avatar + id from the decorated avatar's onTap browseEndpoint.
        if (md.image && md.image->decoratedAvatarViewModel) {
            const rj::DecoratedAvatar &dav = *md.image->decoratedAvatarViewModel;
            if (dav.avatar && dav.avatar->avatarViewModel)
                v.channel.avatarUrl = str(lastSourceUrl(dav.avatar->avatarViewModel->image));
            if (dav.rendererContext && dav.rendererContext->commandContext) {
                const rj::CmdCtx &cc = *dav.rendererContext->commandContext;
                if (cc.onTap && cc.onTap->innertubeCommand && cc.onTap->innertubeCommand->browseEndpoint)
                    v.channel.id = str(cc.onTap->innertubeCommand->browseEndpoint->browseId);
            }
        }
        // metadataRows: [channel] [views, date] (feeds, related) or [channel, views, date]
        // in one row (search) — but a channel's own tab omits the channel: [views, date].
        // The first part is the channel when it links to one, or when more parts follow
        // than views and date.
        if (md.metadata && md.metadata->contentMetadataViewModel
            && md.metadata->contentMetadataViewModel->metadataRows) {
            const std::vector<rj::MetaRow> &rows = *md.metadata->contentMetadataViewModel->metadataRows;
            size_t parts = 0;
            for (const rj::MetaRow &row : rows) if (row.metadataParts) parts += row.metadataParts->size();
            bool first = true;
            for (const rj::MetaRow &row : rows) {
                if (!row.metadataParts) continue;
                for (const rj::MetaPart &p : *row.metadataParts) {
                    const std::string txt = p.text ? str(p.text->content) : std::string();
                    if (txt.empty()) continue;
                    const std::string link = linkedChannel(p);
                    if (first && (!link.empty() || rows.size() > 1 || parts >= 3)) {
                        v.channel.name = txt;
                        if (!link.empty()) v.channel.id = link;
                    }
                    else if (v.viewText.empty() && gj::containsCI(txt, "view")) v.viewText = txt;
                    else if (v.publishedText.empty() && txt.contains("ago"))    v.publishedText = txt;
                    first = false;
                }
            }
        }
    }
    v.durationSecs = gj::clockSecs(v.durationText);
    return v;
}

// tileRenderer — the TV UI card. Self-contained leaf (like lockupViewModel); only
// TILE_CONTENT_TYPE_VIDEO becomes a VideoItem. metadata.tileMetadataRenderer carries
// title + lines (line 0 = channel, line 1 = views [+ date]); the duration rides in a
// thumbnailOverlayTimeStatusRenderer.
inline VideoItem fromTile(const rj::Tile &t)
{
    VideoItem v;
    v.id = str(t.contentId);
    if (t.metadata && t.metadata->tileMetadataRenderer) {
        const rj::TileMeta &md = *t.metadata->tileMetadataRenderer;
        if (md.title) v.title = str(textOf(*md.title));
        if (md.lines) {
            for (size_t li = 0; li < md.lines->size(); ++li) {
                const rj::LineW &line = (*md.lines)[li];
                if (!line.lineRenderer || !line.lineRenderer->items) continue;
                for (const rj::LineItemW &item : *line.lineRenderer->items) {
                    if (!item.lineItemRenderer || !item.lineItemRenderer->text) continue;
                    const std::string text = str(textOf(*item.lineItemRenderer->text));
                    if (text.empty()) continue;
                    if (li == 0 && v.channel.name.empty()) v.channel.name = text;
                    else if (v.viewText.empty() && text.contains("view")) v.viewText = text;
                    // Date lives on a later line; pick the DATED item ("3 days ago" /
                    // "Streamed 2 days ago" / "Sep 5, 2024"), never the "•" separator or
                    // other bullets that sit between the view count and the date.
                    else if (v.publishedText.empty() && li > 0
                             && (text.contains("ago")
                                 || gj::hasYear(text)))
                        v.publishedText = text;
                }
            }
        }
    }
    if (t.header && t.header->tileHeaderRenderer) {
        const rj::TileHeader &h = *t.header->tileHeaderRenderer;
        if (h.thumbnail && h.thumbnail->thumbnails && !h.thumbnail->thumbnails->empty()) {
            v.thumbnailUrl = str(lastThumbUrl(*h.thumbnail));   // last = largest
        }
        if (h.thumbnailOverlays)
            for (const rj::TileOverlay &ov : *h.thumbnailOverlays)
                if (ov.thumbnailOverlayTimeStatusRenderer
                    && ov.thumbnailOverlayTimeStatusRenderer->text)
                    v.durationText = str(textOf(*ov.thumbnailOverlayTimeStatusRenderer->text));
    }
    if (v.thumbnailUrl.empty() && !v.id.empty()) {
        v.thumbnailUrl = "https://i.ytimg.com/vi/" + v.id + "/hqdefault.jpg";
    }
    // Channel id from the long-press "Go to channel" menu (the video tile carries no
    // channel field of its own). Populating userId makes tapping the author/avatar open
    // the channel page. NOTE: the TV home tile has NO avatar, and YouTube avatar URLs
    // are opaque hashes (not constructible from the id) — a proxy (unavatar.io) is
    // rate-limited and fetching each channel is too heavy for a feed, so the home grid
    // keeps the placeholder squircle. The author avatar still shows on the watch page (parsed
    // from the /next owner).
    if (t.onLongPressCommand && t.onLongPressCommand->showMenuCommand
        && t.onLongPressCommand->showMenuCommand->menu
        && t.onLongPressCommand->showMenuCommand->menu->menuRenderer
        && t.onLongPressCommand->showMenuCommand->menu->menuRenderer->items) {
        for (const rj::MenuItemW &mi : *t.onLongPressCommand->showMenuCommand->menu->menuRenderer->items) {
            if (mi.menuNavigationItemRenderer && mi.menuNavigationItemRenderer->navigationEndpoint
                && mi.menuNavigationItemRenderer->navigationEndpoint->browseEndpoint
                && mi.menuNavigationItemRenderer->navigationEndpoint->browseEndpoint->browseId) {
                const std::string bid = str(*mi.menuNavigationItemRenderer->navigationEndpoint->browseEndpoint->browseId);
                if (bid.starts_with("UC")) { v.channel.id = bid; break; }
            }
        }
    }
    v.durationSecs = gj::clockSecs(v.durationText);
    return v;
}

// tileRenderer with TILE_CONTENT_TYPE_CHANNEL — the TV UI channel card. The authed
// FEchannels (manage-subscriptions) grid ships these instead of gridChannelRenderer:
// contentId = channelId, metadata title = name, line 0 = @handle, a "N subscribers"
// line, and the header thumbnail = the channel avatar.
inline Channel fromTileChannel(const rj::Tile &t)
{
    Channel u;
    u.id = str(t.contentId);
    if (t.metadata && t.metadata->tileMetadataRenderer) {
        const rj::TileMeta &md = *t.metadata->tileMetadataRenderer;
        if (md.title) u.name = str(textOf(*md.title));
        if (md.lines)
            for (const rj::LineW &line : *md.lines) {
                if (!line.lineRenderer || !line.lineRenderer->items) continue;
                for (const rj::LineItemW &item : *line.lineRenderer->items) {
                    if (!item.lineItemRenderer || !item.lineItemRenderer->text) continue;
                    const std::string text = str(textOf(*item.lineItemRenderer->text));
                    if (text.empty()) continue;
                    if (u.handle.empty() && text.starts_with('@')) u.handle = text;
                    else if (u.subscriberText.empty()
                             && text.contains("subscriber")) u.subscriberText = text;
                }
            }
    }
    if (t.header && t.header->tileHeaderRenderer && t.header->tileHeaderRenderer->thumbnail) {
        std::string url = str(lastThumbUrl(*t.header->tileHeaderRenderer->thumbnail));
        if (url.starts_with("//")) url.insert(0, "https:");   // protocol-relative
        u.avatarUrl = url;
    }
    return u;
}

// tileRenderer with TILE_CONTENT_TYPE_PLAYLIST — the TVHTML5 shape for a playlist
// card. The signed-in FElibrary browse ships the user's playlists (Liked videos,
// created/saved playlists incl. YouTube Music) as THESE, not gridPlaylistRenderer/
// lockupViewModel: contentId = playlistId (LL / WL / PL…), metadata title = name,
// the header thumbnail = cover, and "N videos" rides a thumbnailOverlayTimeStatus-
// Renderer (the same overlay a video tile uses for its duration).
inline Playlist fromTilePlaylist(const rj::Tile &t)
{
    Playlist p;
    p.id = str(t.contentId);
    if (t.metadata && t.metadata->tileMetadataRenderer) {
        const rj::TileMeta &md = *t.metadata->tileMetadataRenderer;
        if (md.title) p.title = str(textOf(*md.title));
    }
    if (t.header && t.header->tileHeaderRenderer) {
        const rj::TileHeader &h = *t.header->tileHeaderRenderer;
        if (h.thumbnail && h.thumbnail->thumbnails && !h.thumbnail->thumbnails->empty()) {
            std::string url = str(lastThumbUrl(*h.thumbnail));   // last = largest
            if (url.starts_with("//")) url.insert(0, "https:");   // protocol-relative
            p.thumbnailUrl = url;
        }
        // "N videos" is the only text overlay on a playlist tile; take its digits
        // directly (no "video" word-match — the label is localized).
        if (h.thumbnailOverlays)
            for (const rj::TileOverlay &ov : *h.thumbnailOverlays)
                if (ov.thumbnailOverlayTimeStatusRenderer
                    && ov.thumbnailOverlayTimeStatusRenderer->text) {
                    const int64_t n = digitsOf(str(textOf(*ov.thumbnailOverlayTimeStatusRenderer->text)));
                    if (n > 0) p.videoCount = n;
                }
    }
    return p;
}

inline Playlist fromPlaylistRenderer(const rj::PlaylistR &r)
{
    Playlist p;
    p.id = str(r.playlistId);
    if (r.title) {
        if (const gj::Text *t = std::get_if<gj::Text>(&*r.title)) p.title = str(textOf(*t));
        else if (const std::string *s = std::get_if<std::string>(&*r.title)) p.title = str(*s);
    }
    if (r.thumbnail)
        p.thumbnailUrl = str(lastThumbUrl(*r.thumbnail));
    else if (r.thumbnails && !r.thumbnails->empty())
        p.thumbnailUrl = str(lastThumbUrl(r.thumbnails->front()));
    if (r.shortBylineText)     p.owner.name = str(textOf(*r.shortBylineText));
    else if (r.longBylineText) p.owner.name = str(textOf(*r.longBylineText));
    std::string vc;
    if (r.videoCount)
        if (const std::string *s = std::get_if<std::string>(&*r.videoCount)) vc = str(*s);
    if (vc.empty() && r.videoCountText)      vc = str(textOf(*r.videoCountText));
    if (vc.empty() && r.videoCountShortText) vc = str(textOf(*r.videoCountShortText));
    p.videoCount = digitsOf(vc);
    return p;
}

// lockupViewModel with LOCKUP_CONTENT_TYPE_PLAYLIST — the 2024+ channel Playlists
// tab ships these instead of gridPlaylistRenderer. The thumbnail hides under
// collectionThumbnailViewModel.primaryThumbnail and "N videos" is a thumbnail badge.
inline Playlist fromPlaylistLockup(const rj::Lockup &lm)
{
    Playlist p;
    p.id = str(lm.contentId);
    if (lm.contentImage && lm.contentImage->collectionThumbnailViewModel
        && lm.contentImage->collectionThumbnailViewModel->primaryThumbnail
        && lm.contentImage->collectionThumbnailViewModel->primaryThumbnail->thumbnailViewModel) {
        const rj::ThumbVM &tvm =
            *lm.contentImage->collectionThumbnailViewModel->primaryThumbnail->thumbnailViewModel;
        p.thumbnailUrl = str(lastSourceUrl(tvm.image));
        if (tvm.overlays)
            for (const rj::OverlayW &ov : *tvm.overlays) {
                if (!ov.thumbnailOverlayBadgeViewModel) continue;
                const rj::OverlayBadgeVM &b = *ov.thumbnailOverlayBadgeViewModel;
                if (!b.thumbnailBadges) continue;
                for (const rj::BadgeW &bb : *b.thumbnailBadges) {
                    if (!bb.thumbnailBadgeViewModel) continue;
                    const std::string t = str(bb.thumbnailBadgeViewModel->text);
                    if (p.videoCount == 0 && gj::containsCI(t, "video"))
                        p.videoCount = digitsOf(t);
                }
            }
    }
    if (lm.metadata && lm.metadata->lockupMetadataViewModel
        && lm.metadata->lockupMetadataViewModel->title)
        p.title = str(contentOf(lm.metadata->lockupMetadataViewModel->title));
    return p;
}

inline Channel fromUserRenderer(const rj::UserR &r)
{
    Channel u;
    u.id = str(r.channelId);
    u.name = str(textOf(r.title));
    if (r.thumbnail)           u.avatarUrl = str(lastThumbUrl(*r.thumbnail));
    if (r.subscriberCountText) u.subscriberText = str(textOf(*r.subscriberCountText));
    if (r.descriptionSnippet)  u.description = str(textOf(*r.descriptionSnippet));
    return u;
}

inline CommentThread fromCommentPayload(const rj::CommentPayload &p)
{
    CommentThread t;
    Comment &c = t.comment;
    if (p.properties) {
        c.id = str(p.properties->commentId);
        c.text = str(contentOf(p.properties->content));
        c.publishedText = str(p.properties->publishedTime);
    }
    if (p.author) {
        c.author.id = str(p.author->channelId);
        c.author.name = str(p.author->displayName);
        c.author.avatarUrl = str(p.author->avatarThumbnailUrl);
        c.mine = p.author->isCurrentUser.value_or(false);
    }
    if (c.author.avatarUrl.empty() && p.avatar) c.author.avatarUrl = str(firstSourceUrl(p.avatar->image));
    if (p.toolbar) {
        c.likeText = str(p.toolbar->likeCountNotliked);
        t.replyText = str(p.toolbar->replyCount);
    }
    return t;
}

// ---------------------------------------------------------------------------
// The recursive collectors — single-pass scanner visitors. Leaf semantics
// mirror the old walkers: a matched renderer subtree is typed-parsed and NOT
// descended into, and no further keys of that object are considered.
// ---------------------------------------------------------------------------

// Per-open-object leaf flag shared by the collector visitors: set when a
// renderer key was captured at this object, so its remaining members are
// skipped (the old walkers' early `return`).
struct CollectorBase {
    std::vector<char> open;
    void enter(int) { open.push_back(0); }
    void leave(int) { open.pop_back(); }
    bool consumed() const { return !open.empty() && open.back(); }
    void consume() { if (!open.empty()) open.back() = 1; }
};

// ---------------------------------------------------------------------------
// WithToken<Inner> — wraps a renderer collector and additionally captures the
// FIRST continuation token found in document order. Equivalent to a separate
// findContinuationToken pass because token wrappers never occur inside a
// captured (consumed) renderer subtree (verified across the fixture corpus;
// the golden dump is the regression gate).
// ---------------------------------------------------------------------------
template <class Inner>
struct WithToken {
    Inner inner;
    std::string token;
    void enter(int d) { inner.enter(d); }
    void leave(int d) { inner.leave(d); }
    scan::Action what(std::string_view key, int d)
    {
        if (token.empty() && gj::isTokenKey(key)) return scan::Action::Capture;
        return inner.what(key, d);
    }
    void capture(std::string_view key, std::string_view value, int d)
    {
        if (gj::isTokenKey(key)) { gj::captureToken(key, value, &token); return; }
        inner.capture(key, value, d);
    }
};

// Video renderer kinds (all share the VideoR shape). playlistVideoRenderer lets
// a VLxxxx browse (a playlist's contents) populate a VideoModel directly.
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

// First occurrence (document-order DFS) of `key` anywhere in the tree → its
// value extent. Empty when absent.
struct KeyFinder {
    std::string_view want;
    std::string_view got;
    void enter(int) {}
    void leave(int) {}
    scan::Action what(std::string_view key, int)
    {
        if (!got.empty()) return scan::Action::Skip;
        return key == want ? scan::Action::Capture : scan::Action::Descend;
    }
    void capture(std::string_view, std::string_view v, int) { if (got.empty()) got = v; }
};

inline std::string_view findExtent(std::string_view json, std::string_view key)
{
    KeyFinder f{key, {}};
    scan::document(json, f);
    return f.got;
}

} // namespace yt
#endif
