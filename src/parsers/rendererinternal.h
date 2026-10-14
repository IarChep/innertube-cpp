#ifndef YT_RENDERERINTERNAL_H
#define YT_RENDERERINTERNAL_H
// Internal (non-public) shared layer for the per-domain renderer parsers.
// Holds the Glaze-reflected renderer shapes (namespace rj), the struct→CT
// converters, and the scanner-visitor scaffolding shared across the six
// videolist/watch/playlist/channel/comment/account TUs that rendererparser.cpp
// was split into. Everything is `inline` so the six TUs share one definition
// (COMDAT-folded by the linker) with no duplicate-symbol / unused-static churn.
//
#include "rendererparser.h"
#include "json/tokenscan.h"
#include "json/jsonscan.h"
#include "textmatch.h"
#include <map>
#include <vector>

namespace yt {

using namespace gj;

// ---------------------------------------------------------------------------
// Typed partial views of the renderer shapes (Glaze reflection — field names
// are the JSON keys; unknown keys are skipped by gj::kIn). All plain std types:
// this block stays Qt-free.
// ---------------------------------------------------------------------------
namespace rj {

// -- videoRenderer / compactVideoRenderer / gridVideoRenderer / playlistVideoRenderer
struct ChanThumbWithLink {
    std::optional<ThumbSet> thumbnail;
};
struct ChanThumbSupported {
    std::optional<ChanThumbWithLink> channelThumbnailWithLinkRenderer;
};
struct VideoR {
    std::optional<std::string> videoId;
    std::optional<Text> title;
    std::optional<Text> ownerText;
    std::optional<Text> longBylineText;
    std::optional<Text> shortBylineText;
    std::optional<Text> lengthText;
    std::optional<Text> viewCountText;
    std::optional<Text> publishedTimeText;
    std::optional<ThumbSet> thumbnail;
    std::optional<ChanThumbSupported> channelThumbnailSupportedRenderers;
};

// -- lockupViewModel (video + playlist shapes share one struct; contentType picks)
struct BadgeVM {
    std::optional<std::string> text;
};
struct BadgeW {
    std::optional<BadgeVM> thumbnailBadgeViewModel;
};
struct BottomOverlay {
    std::optional<std::vector<BadgeW>> badges;
};
struct OverlayBadgeVM {
    std::optional<std::vector<BadgeW>> thumbnailBadges;
};
struct OverlayW {
    std::optional<BottomOverlay> thumbnailBottomOverlayViewModel;
    std::optional<OverlayBadgeVM> thumbnailOverlayBadgeViewModel;
};
struct ThumbVM {
    std::optional<Sources> image;
    std::optional<std::vector<OverlayW>> overlays;
};
struct PrimThumbW {
    std::optional<ThumbVM> thumbnailViewModel;
};
struct CollectionThumb {
    std::optional<PrimThumbW> primaryThumbnail;
};
struct ContentImage {
    std::optional<ThumbVM> thumbnailViewModel;              // video lockups
    std::optional<CollectionThumb> collectionThumbnailViewModel;   // playlist lockups
};
struct BrowseEP {
    std::optional<std::string> browseId;
};
struct ITCommand {
    std::optional<BrowseEP> browseEndpoint;
};
struct OnTap {
    std::optional<ITCommand> innertubeCommand;
};
struct CmdCtx {
    std::optional<OnTap> onTap;
};
struct RendererCtx {
    std::optional<CmdCtx> commandContext;
};
struct AvatarVM {
    std::optional<Sources> image;
};
struct AvatarW {
    std::optional<AvatarVM> avatarViewModel;
};
struct DecoratedAvatar {
    std::optional<AvatarW> avatar;
    std::optional<RendererCtx> rendererContext;
};
struct ImageW {
    std::optional<DecoratedAvatar> decoratedAvatarViewModel;
};
struct MetaPart {
    std::optional<ContentText> text;
};
struct MetaRow {
    std::optional<std::vector<MetaPart>> metadataParts;
};
struct ContentMetaVM {
    std::optional<std::vector<MetaRow>> metadataRows;
};
struct MetaW {
    std::optional<ContentMetaVM> contentMetadataViewModel;
};
struct LockupMetaVM {
    std::optional<ContentText> title;
    std::optional<ImageW> image;
    std::optional<MetaW> metadata;
};
struct LockupMetaW {
    std::optional<LockupMetaVM> lockupMetadataViewModel;
};
struct Lockup {
    std::optional<std::string> contentId;
    std::optional<std::string> contentType;
    std::optional<ContentImage> contentImage;
    std::optional<LockupMetaW> metadata;
};

// -- tileRenderer (TVHTML5)
struct LineItem {
    std::optional<Text> text;
};
struct LineItemW {
    std::optional<LineItem> lineItemRenderer;
};
struct LineR {
    std::optional<std::vector<LineItemW>> items;
};
struct LineW {
    std::optional<LineR> lineRenderer;
};
struct TileMeta {
    std::optional<Text> title;
    std::optional<std::vector<LineW>> lines;
};
struct TileMetaW {
    std::optional<TileMeta> tileMetadataRenderer;
};
struct TimeStatus {
    std::optional<Text> text;
};
struct TileOverlay {
    std::optional<TimeStatus> thumbnailOverlayTimeStatusRenderer;
};
struct TileHeader {
    std::optional<ThumbSet> thumbnail;
    std::optional<std::vector<TileOverlay>> thumbnailOverlays;
};
struct TileHeaderW {
    std::optional<TileHeader> tileHeaderRenderer;
};
// tileRenderer.onLongPressCommand → the "Go to channel" menu entry carries the
// channelId (a video tile has no channelThumbnail/channel field of its own).
struct MenuNavItem  { std::optional<ITCommand> navigationEndpoint; };
struct MenuItemW    { std::optional<MenuNavItem> menuNavigationItemRenderer; };
struct MenuR        { std::optional<std::vector<MenuItemW>> items; };
struct MenuW        { std::optional<MenuR> menuRenderer; };
struct ShowMenuCmd  { std::optional<MenuW> menu; };
struct LongPressCmd { std::optional<ShowMenuCmd> showMenuCommand; };
struct Tile {
    std::optional<std::string> contentId;
    std::optional<std::string> contentType;
    std::optional<TileMetaW> metadata;
    std::optional<TileHeaderW> header;
    std::optional<LongPressCmd> onLongPressCommand;
};

// -- playlistRenderer / gridPlaylistRenderer / compactPlaylistRenderer
struct PlaylistR {
    std::optional<std::string> playlistId;
    // Some shapes carry a plain string title, others a text object.
    std::optional<std::variant<std::string, Text>> title;
    std::optional<ThumbSet> thumbnail;
    std::optional<std::vector<ThumbSet>> thumbnails;
    std::optional<Text> shortBylineText;
    std::optional<Text> longBylineText;
    // jstr() was string-strict here: a numeric videoCount falls through to the
    // text variants below, so only the string alternative is consumed.
    std::optional<FlexInt> videoCount;
    std::optional<Text> videoCountText;
    std::optional<Text> videoCountShortText;
};

// -- channelRenderer / gridChannelRenderer
struct UserR {
    std::optional<std::string> channelId;
    std::optional<Text> title;
    std::optional<ThumbSet> thumbnail;
    std::optional<Text> subscriberCountText;
    std::optional<Text> descriptionSnippet;
};

// -- channel page header(s) + metadata
struct DynamicTextVM {
    std::optional<ContentText> text;
};
struct TitleW {
    std::optional<DynamicTextVM> dynamicTextViewModel;
};
struct ImageBannerVM {
    std::optional<Sources> image;
};
struct BannerW {
    std::optional<ImageBannerVM> imageBannerViewModel;
};
struct PageHeaderVM {
    std::optional<TitleW> title;
    std::optional<ImageW> image;
    std::optional<BannerW> banner;
    std::optional<MetaW> metadata;
};
struct PageHeaderContent {
    std::optional<PageHeaderVM> pageHeaderViewModel;
};
struct PageHeader {
    std::optional<PageHeaderContent> content;
};
// Authed-only: the signed-in user's subscribe state on this channel. Present
// only in AUTHED WEB responses (R1) — absent → subscribed stays false.
struct SubscribeButtonR {
    std::optional<bool> subscribed;
};
struct SubscribeButton {
    std::optional<SubscribeButtonR> subscribeButtonRenderer;
};
struct C4Header {
    std::optional<std::variant<std::string, Text>> title;   // c4 uses a plain string title
    std::optional<std::string> channelId;
    std::optional<ThumbSet> avatar;
    std::optional<ThumbSet> banner;
    std::optional<Text> subscriberCountText;
    std::optional<SubscribeButton> subscribeButton;
};
struct HeaderW {
    std::optional<C4Header> c4TabbedHeaderRenderer;
    std::optional<PageHeader> pageHeaderRenderer;
};
struct ChannelMeta {
    std::optional<std::string> externalId;
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<ThumbSet> avatar;
};
struct ChannelMetaW {
    std::optional<ChannelMeta> channelMetadataRenderer;
};
struct ChannelRoot {
    std::optional<HeaderW> header;
    std::optional<ChannelMetaW> metadata;
};

// -- commentEntityPayload
struct CommentProps {
    std::optional<ContentText> content;
    std::optional<std::string> publishedTime;
};
struct CommentAuthor {
    std::optional<std::string> displayName;
};
struct CommentAvatar {
    std::optional<Sources> image;
};
struct CommentPayload {
    std::optional<CommentProps> properties;
    std::optional<CommentAuthor> author;
    std::optional<CommentAvatar> avatar;
};

// -- accountItem (accounts_list)
struct OfflineTok {
    std::optional<std::string> clientCacheKey;
};
struct SupTok {
    std::optional<OfflineTok> offlineCacheKeyToken;
};
struct SelectEP {
    std::optional<std::vector<SupTok>> supportedTokens;
};
struct ServiceEP {
    std::optional<SelectEP> selectActiveIdentityEndpoint;
};
struct AccountItem {
    std::optional<Text> accountName;
    std::optional<Text> channelHandle;
    std::optional<ThumbSet> accountPhoto;
    std::optional<bool> isSelected;
    std::optional<ServiceEP> serviceEndpoint;
};

// -- watch page pieces (extracted from findRenderer() subtrees)
struct PrimaryInfo {
    std::optional<Text> title;
};
struct ViewCountR {
    std::optional<Text> viewCount;
};
struct ToggleButton {
    std::optional<Text> defaultText;
    std::optional<bool> isToggled;            // authed /next: true when this side is engaged
};
struct ButtonVM {
    std::optional<std::string> title;
};
// Modern like/dislike toggle state: segmentedLikeDislikeButtonViewModel nests a
// like/dislikeButtonViewModel, each wrapping a toggleButtonViewModel carrying the
// engaged flag. Read from the toggleButtonViewModel extent.
struct ToggleStateVM {
    std::optional<bool> isToggled;
};
// Legacy like/dislike: menuRenderer.topLevelButtons[] — [0]=like, [1]=dislike,
// each a {"toggleButtonRenderer": {...}} object (mapped by its single key).
struct TogglePair {
    std::optional<std::vector<std::map<std::string, ToggleButton>>> topLevelButtons;
};
struct OwnerR {
    std::optional<Text> title;
    std::optional<ThumbSet> thumbnail;
    std::optional<ITCommand> navigationEndpoint;   // {browseEndpoint:{browseId}}
    // Authed /next only: the viewer's subscribe state for this channel (same shape as
    // the channel header's subscribeButton). Absent (unauthed) → subscribed stays false.
    std::optional<SubscribeButton> subscribeButton;
};

// frameworkUpdates.entityBatchUpdate.mutations[].payload.likeStatusEntity — the authed
// viewer's like/dislike on the video ("LIKE" / "DISLIKE" / "INDIFFERENT"). The TV /next
// carries the like STATE here (not in a toggleButton), so it restores like/dislike.
struct LikeStatusEntity { std::optional<std::string> likeStatus; };

} // namespace rj

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
inline Video fromVideoRenderer(const rj::VideoR &r)
{
    Video v;
    v.id = str(r.videoId);
    v.title = str(textOf(r.title));
    // Presence-based fallback (not value-based): mirrors the old ternary on contains().
    if (r.ownerText)            v.channelName = str(textOf(*r.ownerText));
    else if (r.longBylineText)  v.channelName = str(textOf(*r.longBylineText));
    v.duration = str(textOf(r.lengthText));
    v.thumbnailUrl = str(lastThumbUrl(r.thumbnail));   // native (WebP); decoded by the qwebp plugin
    if (v.thumbnailUrl.empty() && !v.id.empty())
        v.thumbnailUrl = "https://i.ytimg.com/vi/" + v.id + "/hqdefault.jpg";
    v.largeThumbnailUrl = v.thumbnailUrl;
    // Channel avatar: videoRenderer.channelThumbnailSupportedRenderers
    //   .channelThumbnailWithLinkRenderer.thumbnail.thumbnails[].url
    if (r.channelThumbnailSupportedRenderers
        && r.channelThumbnailSupportedRenderers->channelThumbnailWithLinkRenderer)
        v.avatarUrl = str(lastThumbUrl(
            r.channelThumbnailSupportedRenderers->channelThumbnailWithLinkRenderer->thumbnail));
    if (r.viewCountText)
        v.viewCount = digitsOf(str(textOf(*r.viewCountText)));
    if (r.publishedTimeText)                    // "2 days ago" — shown on the delegate
        v.date = str(textOf(*r.publishedTimeText));
    return v;
}

inline Video fromLockupVideo(const rj::Lockup &lm)
{
    Video v;
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
                    if (v.duration.empty() && gj::isClockText(t))
                        v.duration = t;
                }
            }
    }
    if (v.thumbnailUrl.empty() && !v.id.empty())
        v.thumbnailUrl = "https://i.ytimg.com/vi/" + v.id + "/hqdefault.jpg";
    v.largeThumbnailUrl = v.thumbnailUrl;
    if (lm.metadata && lm.metadata->lockupMetadataViewModel) {
        const rj::LockupMetaVM &md = *lm.metadata->lockupMetadataViewModel;
        if (md.title) v.title = str(contentOf(md.title));
        // Channel avatar + id from the decorated avatar's onTap browseEndpoint.
        if (md.image && md.image->decoratedAvatarViewModel) {
            const rj::DecoratedAvatar &dav = *md.image->decoratedAvatarViewModel;
            if (dav.avatar && dav.avatar->avatarViewModel)
                v.avatarUrl = str(lastSourceUrl(dav.avatar->avatarViewModel->image));
            if (dav.rendererContext && dav.rendererContext->commandContext) {
                const rj::CmdCtx &cc = *dav.rendererContext->commandContext;
                if (cc.onTap && cc.onTap->innertubeCommand && cc.onTap->innertubeCommand->browseEndpoint)
                    v.channelId = str(cc.onTap->innertubeCommand->browseEndpoint->browseId);
            }
        }
        // metadataRows: row 0 = channel name; a later row = "N views" + "date ago".
        if (md.metadata && md.metadata->contentMetadataViewModel
            && md.metadata->contentMetadataViewModel->metadataRows) {
            int ri = 0;
            for (const rj::MetaRow &row : *md.metadata->contentMetadataViewModel->metadataRows) {
                if (row.metadataParts) {
                    for (const rj::MetaPart &p : *row.metadataParts) {
                        const std::string txt = str(contentOf(p.text));
                        if (txt.empty()) continue;
                        if (v.channelName.empty() && ri == 0)                                        v.channelName = txt;
                        else if (v.viewText.empty() && gj::containsCI(txt, "view")) v.viewText = txt;
                        else if (v.date.empty() && txt.contains("ago"))                           v.date = txt;
                    }
                }
                ++ri;
            }
        }
    }
    return v;
}

// tileRenderer — the TV UI card. Self-contained leaf (like lockupViewModel); only
// TILE_CONTENT_TYPE_VIDEO becomes a Video. metadata.tileMetadataRenderer carries
// title + lines (line 0 = channel, line 1 = views [+ date]); the duration rides in a
// thumbnailOverlayTimeStatusRenderer.
inline Video fromTile(const rj::Tile &t)
{
    Video v;
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
                    if (li == 0 && v.channelName.empty()) v.channelName = text;
                    else if (v.viewText.empty() && text.contains("view")) v.viewText = text;
                    // Date lives on a later line; pick the DATED item ("3 days ago" /
                    // "Streamed 2 days ago" / "Sep 5, 2024"), never the "•" separator or
                    // other bullets that sit between the view count and the date.
                    else if (v.date.empty() && li > 0
                             && (text.contains("ago")
                                 || gj::hasYear(text)))
                        v.date = text;
                }
            }
        }
    }
    if (t.header && t.header->tileHeaderRenderer) {
        const rj::TileHeader &h = *t.header->tileHeaderRenderer;
        if (h.thumbnail && h.thumbnail->thumbnails && !h.thumbnail->thumbnails->empty()) {
            v.thumbnailUrl = str(lastThumbUrl(*h.thumbnail));   // last = largest
            v.largeThumbnailUrl = v.thumbnailUrl;
        }
        if (h.thumbnailOverlays)
            for (const rj::TileOverlay &ov : *h.thumbnailOverlays)
                if (ov.thumbnailOverlayTimeStatusRenderer
                    && ov.thumbnailOverlayTimeStatusRenderer->text)
                    v.duration = str(textOf(*ov.thumbnailOverlayTimeStatusRenderer->text));
    }
    if (v.thumbnailUrl.empty() && !v.id.empty()) {
        v.thumbnailUrl = "https://i.ytimg.com/vi/" + v.id + "/hqdefault.jpg";
        v.largeThumbnailUrl = v.thumbnailUrl;
    }
    // Channel id from the long-press "Go to channel" menu (the video tile carries no
    // channel field of its own). Populating userId makes tapping the author/avatar open
    // the channel page. NOTE: the TV home tile has NO avatar, and YouTube avatar URLs
    // are opaque hashes (not constructible from the id) — a proxy (unavatar.io) is
    // rate-limited and fetching each channel is too heavy for a feed, so the home grid
    // keeps the placeholder squircle. The author avatar still shows on VideoPage (parsed
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
                if (bid.starts_with("UC")) { v.channelId = bid; break; }
            }
        }
    }
    v.url = "https://www.youtube.com/watch?v=" + v.id;
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
                    else if (u.subscriberCount.empty()
                             && text.contains("subscriber")) u.subscriberCount = text;
                }
            }
    }
    if (t.header && t.header->tileHeaderRenderer && t.header->tileHeaderRenderer->thumbnail) {
        std::string url = str(lastThumbUrl(*t.header->tileHeaderRenderer->thumbnail));
        if (url.starts_with("//")) url.insert(0, "https:");   // protocol-relative
        u.thumbnailUrl = url;
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
                    if (n > 0) p.videoCount = (int) n;
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
    if (r.shortBylineText)     p.channelName = str(textOf(*r.shortBylineText));
    else if (r.longBylineText) p.channelName = str(textOf(*r.longBylineText));
    std::string vc;
    if (r.videoCount)
        if (const std::string *s = std::get_if<std::string>(&*r.videoCount)) vc = str(*s);
    if (vc.empty() && r.videoCountText)      vc = str(textOf(*r.videoCountText));
    if (vc.empty() && r.videoCountShortText) vc = str(textOf(*r.videoCountShortText));
    p.videoCount = (int)digitsOf(vc);
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
                        p.videoCount = (int)digitsOf(t);
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
    if (r.thumbnail)           u.thumbnailUrl = str(lastThumbUrl(*r.thumbnail));
    if (r.subscriberCountText) u.subscriberCount = str(textOf(*r.subscriberCountText));
    if (r.descriptionSnippet)  u.description = str(textOf(*r.descriptionSnippet));
    return u;
}

inline Comment fromCommentPayload(const rj::CommentPayload &p)
{
    Comment c;
    if (p.properties) {
        c.body = str(contentOf(p.properties->content));
        c.date = str(p.properties->publishedTime);
    }
    if (p.author) c.author = str(p.author->displayName);
    if (p.avatar) c.thumbnailUrl = str(firstSourceUrl(p.avatar->image));
    return c;
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
