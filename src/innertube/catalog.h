#ifndef YT_CATALOG_H
#define YT_CATALOG_H
namespace yt {

// Single home for the cross-cutting, FRAGILE constants that rotate over time.
// Re-verify periodically against yt-dlp's _base.py and the TV base.js
// (see innertube-api-research.md §5/§6 and docs/INNERTUBE_API.md).
//
// Where the other rotting constants live (kept beside the code that uses them):
//   - per-client versions + user-agents .... innertube/clientconfig.cpp
//   - nav feed IDs + search types .......... innertube/innertube.cpp
//   - search/browse body params (protobuf) . requests/bodies.cpp
//
// Header-only `static const` (internal linkage per TU — no ODR concern).
namespace Catalog {

    // EU consent cookie — without it, consent-gated regions return empty feeds.
    static const char *const kConsentCookie = "SOCS=CAISAiAD";

    // Public query-suggestion endpoint (client=firefox → clean JSON array
    // ["query",[suggestions…]]). Anonymous GET; no context/client.
    static const char *const kSuggestUrl = "https://suggestqueries.google.com/complete/search";

    // OAuth 2.0 TV "limited-input device" credentials (public, scraped from
    // YouTube-TV). Used by the device-code login (Phase 3). FRAGILE: may rotate —
    // be ready to re-scrape from the TV base.js if login starts failing.
    static const char *const kOAuthClientId     = "861556708454-d6dlm3lh05idd8npek18k6be8ba3oc68.apps.googleusercontent.com";
    static const char *const kOAuthClientSecret = "SboVhoG9s0rNafixCSGGKXAT";
    static const char *const kOAuthScope        = "http://gdata.youtube.com https://www.googleapis.com/auth/youtube-paid-content";
    static const char *const kDeviceCodeUrl     = "https://www.youtube.com/o/oauth2/device/code";
    static const char *const kTokenUrl          = "https://oauth2.googleapis.com/token";
}
}
#endif
