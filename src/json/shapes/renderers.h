// InnerTube renderer shapes shared by every list surface (browse, search, /next related,
// playlists) — the list items: videoRenderer family, lockupViewModel, TV tileRenderer,
// playlistRenderer family, channelRenderer — and the navigation / image / subscribe
// pieces the channel and watch shapes reuse. Namespace rj (one namespace for every
// renderer header: they share pieces). Read by parsers/rendererinternal.h.
#ifndef YT_SHAPES_RENDERERS_H
#define YT_SHAPES_RENDERERS_H
#include "json/shapes/common.h"
#include <map>
#include <string>
#include <variant>
#include <vector>
namespace yt::rj {

// -- videoRenderer / compactVideoRenderer / gridVideoRenderer / playlistVideoRenderer
struct ChanThumbWithLink {
    std::optional<gj::ThumbSet> thumbnail;
};
struct ChanThumbSupported {
    std::optional<ChanThumbWithLink> channelThumbnailWithLinkRenderer;
};
struct VideoR {
    std::optional<std::string> videoId;
    std::optional<gj::Text> title;
    std::optional<gj::Text> ownerText;
    std::optional<gj::Text> longBylineText;
    std::optional<gj::Text> shortBylineText;
    std::optional<gj::Text> lengthText;
    std::optional<gj::Text> viewCountText;
    std::optional<gj::Text> publishedTimeText;
    std::optional<gj::ThumbSet> thumbnail;
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
    std::optional<gj::Sources> image;
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
    std::optional<gj::Sources> image;
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
struct MetaText {
    std::optional<std::string> content;
    std::optional<std::vector<CmdCtx>> commandRuns;   // a linked run: the channel's browseEndpoint
};
struct MetaPart {
    std::optional<MetaText> text;
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
    std::optional<gj::ContentText> title;
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
    std::optional<gj::Text> text;
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
    std::optional<gj::Text> title;
    std::optional<std::vector<LineW>> lines;
};
struct TileMetaW {
    std::optional<TileMeta> tileMetadataRenderer;
};
struct TimeStatus {
    std::optional<gj::Text> text;
};
struct TileOverlay {
    std::optional<TimeStatus> thumbnailOverlayTimeStatusRenderer;
};
struct TileHeader {
    std::optional<gj::ThumbSet> thumbnail;
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
    std::optional<std::variant<std::string, gj::Text>> title;
    std::optional<gj::ThumbSet> thumbnail;
    std::optional<std::vector<gj::ThumbSet>> thumbnails;
    std::optional<gj::Text> shortBylineText;
    std::optional<gj::Text> longBylineText;
    // jstr() was string-strict here: a numeric videoCount falls through to the
    // text variants below, so only the string alternative is consumed.
    std::optional<gj::FlexInt> videoCount;
    std::optional<gj::Text> videoCountText;
    std::optional<gj::Text> videoCountShortText;
};

// -- channelRenderer / gridChannelRenderer
struct UserR {
    std::optional<std::string> channelId;
    std::optional<gj::Text> title;
    std::optional<gj::ThumbSet> thumbnail;
    std::optional<gj::Text> subscriberCountText;
    std::optional<gj::Text> descriptionSnippet;
};

// -- subscribeButtonRenderer (channel header, authed /next owner)
// Authed-only: the signed-in user's subscribe state on this channel. Present
// only in AUTHED WEB responses (R1) — absent → subscribed stays false.
struct SubscribeButtonR {
    std::optional<bool> subscribed;
};
struct SubscribeButton {
    std::optional<SubscribeButtonR> subscribeButtonRenderer;
};

}
#endif
