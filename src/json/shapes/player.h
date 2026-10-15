// /player answer shapes: playabilityStatus, streamingData (formats, adaptiveFormats,
// manifests, SABR), the ustreamer config, videoDetails, captions, storyboards,
// playbackTracking, microformat and the offline slate (parsers/playerparser.cpp).
// Namespace pj.
#ifndef YT_SHAPES_PLAYER_H
#define YT_SHAPES_PLAYER_H
#include "json/shapes/common.h"
#include <string>
#include <vector>
namespace yt::pj {

struct Range {
    std::optional<gj::FlexInt> start;
    std::optional<gj::FlexInt> end;
};
struct AudioTrackJ {
    std::optional<std::string> displayName;
    std::optional<std::string> id;
    std::optional<bool> audioIsDefault;
};
struct FormatJ {
    std::optional<gj::FlexInt> itag;
    std::optional<std::string> url;
    std::optional<std::string> signatureCipher;
    std::optional<std::string> cipher;
    std::optional<std::string> mimeType;
    std::optional<std::string> qualityLabel;
    std::optional<gj::FlexInt> width;
    std::optional<gj::FlexInt> height;
    std::optional<gj::FlexInt> fps;
    std::optional<gj::FlexInt> bitrate;
    std::optional<gj::FlexInt> averageBitrate;
    std::optional<gj::FlexInt> contentLength;
    std::optional<gj::FlexInt> approxDurationMs;
    std::optional<gj::FlexInt> lastModified;
    std::optional<Range> initRange;
    std::optional<Range> indexRange;
    std::optional<AudioTrackJ> audioTrack;
    std::optional<std::string> xtags;
    std::optional<bool> isDrc;
};
struct StreamingJ {
    std::optional<gj::FlexInt> expiresInSeconds;
    std::optional<std::vector<FormatJ>> formats;
    std::optional<std::vector<FormatJ>> adaptiveFormats;
    std::optional<std::string> hlsManifestUrl;
    std::optional<std::string> dashManifestUrl;
    std::optional<std::string> serverAbrStreamingUrl;
};
struct UstreamerJ {
    std::optional<std::string> videoPlaybackUstreamerConfig;
};
struct MediaCommonJ {
    std::optional<UstreamerJ> mediaUstreamerRequestConfig;
};
struct PlayerConfigJ {
    std::optional<MediaCommonJ> mediaCommonConfig;
};
struct StatusJ {
    std::optional<std::string> status;
    std::optional<std::string> reason;
    std::optional<gj::FlexInt> desktopLegacyAgeGateReason;
};
struct DetailsJ {
    std::optional<std::string> videoId;
    std::optional<std::string> title;
    std::optional<std::string> author;
    std::optional<std::string> channelId;
    std::optional<std::string> shortDescription;
    std::optional<gj::FlexInt> lengthSeconds;
    std::optional<gj::FlexInt> viewCount;
    std::optional<bool> isLive;
    std::optional<bool> isLiveContent;
    std::optional<bool> isUpcoming;
    std::optional<gj::ThumbSet> thumbnail;
};
struct TrackJ {
    std::optional<std::string> baseUrl;
    std::optional<std::string> languageCode;
    std::optional<gj::Text> name;
    std::optional<std::string> kind;
    std::optional<bool> isTranslatable;
};
struct TracklistJ {
    std::optional<std::vector<TrackJ>> captionTracks;
};
struct CaptionsJ {
    std::optional<TracklistJ> playerCaptionsTracklistRenderer;
};
struct SpecJ {
    std::optional<std::string> spec;
};
struct StoryboardsJ {
    std::optional<SpecJ> playerStoryboardSpecRenderer;
};
struct UrlJ {
    std::optional<std::string> baseUrl;
};
struct TrackingJ {
    std::optional<UrlJ> videostatsPlaybackUrl;
    std::optional<UrlJ> videostatsWatchtimeUrl;
};
struct BroadcastJ {
    std::optional<std::string> startTimestamp;
};
struct MicroRendererJ {
    std::optional<BroadcastJ> liveBroadcastDetails;
};
struct MicroformatJ {
    std::optional<MicroRendererJ> playerMicroformatRenderer;
};
struct Root {
    std::optional<StatusJ> playabilityStatus;
    std::optional<StreamingJ> streamingData;
    std::optional<PlayerConfigJ> playerConfig;
    std::optional<DetailsJ> videoDetails;
    std::optional<CaptionsJ> captions;
    std::optional<StoryboardsJ> storyboards;
    std::optional<TrackingJ> playbackTracking;
    std::optional<MicroformatJ> microformat;
};
struct SlateJ {
    std::optional<gj::FlexInt> scheduledStartTime;
    std::optional<gj::Text> mainText;
    std::optional<gj::Text> subtitleText;
};

}
#endif
