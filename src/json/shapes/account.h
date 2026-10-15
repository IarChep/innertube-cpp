// Account and sign-in shapes answered by InnerTube: accounts_list items
// (parsers/accountparser.cpp) and the mdx/handoff sign-in QR (core/chains.cpp).
// Namespace rj.
#ifndef YT_SHAPES_ACCOUNT_H
#define YT_SHAPES_ACCOUNT_H
#include "json/shapes/common.h"
namespace yt::rj {

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
    std::optional<gj::Text> accountName;
    std::optional<gj::Text> channelHandle;
    std::optional<gj::ThumbSet> accountPhoto;
    std::optional<bool> isSelected;
    std::optional<ServiceEP> serviceEndpoint;
};


// -- mdx/handoff (the TV sign-in QR): rapidQrRenderer.qrCodeRenderer.qrCodeImage
struct QrCode {
    std::optional<gj::ThumbSet> qrCodeImage;
};
struct RapidQr {
    std::optional<QrCode> qrCodeRenderer;
};
struct Handoff {
    std::optional<RapidQr> rapidQrRenderer;
};

}
#endif
