// Channel page shapes: the c4TabbedHeaderRenderer / pageHeaderViewModel headers and the
// channelMetadataRenderer (parsers/channelparser.cpp). Namespace rj.
#ifndef YT_SHAPES_CHANNEL_H
#define YT_SHAPES_CHANNEL_H
#include "json/shapes/renderers.h"
namespace yt::rj {

// -- channel page header(s) + metadata
struct DynamicTextVM {
    std::optional<gj::ContentText> text;
};
struct TitleW {
    std::optional<DynamicTextVM> dynamicTextViewModel;
};
struct ImageBannerVM {
    std::optional<gj::Sources> image;
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
struct C4Header {
    std::optional<std::variant<std::string, gj::Text>> title;   // c4 uses a plain string title
    std::optional<std::string> channelId;
    std::optional<gj::ThumbSet> avatar;
    std::optional<gj::ThumbSet> banner;
    std::optional<gj::Text> subscriberCountText;
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
    std::optional<gj::ThumbSet> avatar;
};
struct ChannelMetaW {
    std::optional<ChannelMeta> channelMetadataRenderer;
};
struct ChannelRoot {
    std::optional<HeaderW> header;
    std::optional<ChannelMetaW> metadata;
};

}
#endif
