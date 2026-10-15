// Google OAuth 2.0 device-flow answers (oauth2.googleapis.com device/code and token,
// RFC 8628): fixed snake_case schemas (core/chains.cpp). Namespace oj — services
// outside InnerTube.
#ifndef YT_SHAPES_OAUTH_H
#define YT_SHAPES_OAUTH_H
#include "json/ytjson.h"
#include <optional>
#include <string>
namespace yt::oj {

struct DeviceCode {
    std::optional<std::string> device_code;
    std::optional<std::string> user_code;
    std::optional<std::string> verification_url;
    std::optional<std::string> verification_uri;
    std::optional<gj::FlexInt> interval;
    std::optional<gj::FlexInt> expires_in;
};
struct Token {
    std::optional<std::string> access_token;
    std::optional<std::string> refresh_token;
    std::optional<std::string> error;
    std::optional<gj::FlexInt> expires_in;
};

}
#endif
