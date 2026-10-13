#include "rendererinternal.h"

namespace yt {

using namespace gj;

std::string parseText(std::string_view field)
{
    Text t{};
    readJson(t, field);
    return str(textOf(t));
}

struct VideoCollector : CollectorBase {
    std::vector<Video> *out;
    scan::Action what(std::string_view key, int)
    {
        if (consumed()) return scan::Action::Skip;
        if (key == "lockupViewModel" || key == "tileRenderer" || isVideoKind(key)) {
            consume();
            return scan::Action::Capture;
        }
        return scan::Action::Descend;
    }
    void capture(std::string_view key, std::string_view value, int)
    {
        // lockupViewModel is a self-contained leaf (no nested video renderers); only
        // the VIDEO content type becomes a Video (playlist/other lockups skipped).
        if (key == "lockupViewModel") {
            rj::Lockup lm{};
            readJson(lm, value);
            if (lm.contentType && *lm.contentType == "LOCKUP_CONTENT_TYPE_VIDEO")
                out->push_back(fromLockupVideo(lm));
        }
        // tileRenderer is likewise a self-contained leaf (TVHTML5 feeds).
        else if (key == "tileRenderer") {
            rj::Tile t{};
            readJson(t, value);
            if (t.contentType && *t.contentType == "TILE_CONTENT_TYPE_VIDEO")
                out->push_back(fromTile(t));
        }
        else {
            rj::VideoR r{};
            readJson(r, value);
            out->push_back(fromVideoRenderer(r));
        }
    }
};

std::vector<Video> parseVideoList(std::string_view response, std::string *nextToken)
{
    std::vector<Video> out;
    if (nextToken) {
        WithToken<VideoCollector> w;
        w.inner.out = &out;
        scan::document(response, w);
        *nextToken = std::string(w.token);
    } else {
        VideoCollector c;
        c.out = &out;
        scan::document(response, c);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Direct single-renderer entry points (tests + callers holding the inner object).
// ---------------------------------------------------------------------------
Video parseVideoRenderer(std::string_view r)
{
    rj::VideoR s{};
    readJson(s, r);
    return fromVideoRenderer(s);
}

Video parseLockupViewModel(std::string_view lm)
{
    rj::Lockup s{};
    readJson(s, lm);
    return fromLockupVideo(s);
}

Video parseTileRenderer(std::string_view t)
{
    rj::Tile s{};
    readJson(s, t);
    return fromTile(s);
}

}
