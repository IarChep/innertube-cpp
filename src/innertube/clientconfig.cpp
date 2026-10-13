#include "clientconfig.h"
namespace yt {
const ClientInfo &clientInfo(ClientId id) {
    static const ClientInfo WEB   = { "WEB", "2.20260626.01.00",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36", 1 };
    static const ClientInfo SAFARI= { "WEB", "2.20260626.01.00",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/15.5 Safari/605.1.15,gzip(gfe)", 1 };
    static const ClientInfo IOS   = { "IOS", "20.49.6",
        "com.google.ios.youtube/20.49.6 (iPhone16,2; U; CPU iOS 18_0 like Mac OS X)", 5 };
    static const ClientInfo ANDROID = { "ANDROID", "20.10.38",
        "com.google.android.youtube/20.10.38 (Linux; U; Android 14; en_US; SM-S928B) gzip", 3 };
    static const ClientInfo ANDROID_VR = { "ANDROID_VR", "1.65.10",
        "com.google.android.apps.youtube.vr.oculus/1.65.10 (Linux; U; Android 12L; eureka-user Build/SQ3A.220605.009.A1) gzip", 28 };
    // clientVersion must stay current: YouTube answers a stale TVHTML5 client with
    // "UNPLAYABLE: The page needs to be reloaded". Kept in lockstep with yt-dlp's `tv`
    // client (2026-07-12: 7.20260707.07.00 + the full Cobalt UA). Bump when TV re-breaks.
    static const ClientInfo TV    = { "TVHTML5", "7.20260707.07.00",
        "Mozilla/5.0 (ChromiumStylePlatform) Cobalt/25.lts.30.1034943-gold (unlike Gecko), Unknown_TV_Unknown_0/Unknown (Unknown, Unknown)", 7 };
    switch (id) {
        case ClientId::WEB:        return WEB;
        case ClientId::WEB_SAFARI: return SAFARI;
        case ClientId::IOS:        return IOS;
        case ClientId::ANDROID:    return ANDROID;
        case ClientId::ANDROID_VR: return ANDROID_VR;
        case ClientId::TVHTML5:    return TV;
    }
    return WEB;
}
}
