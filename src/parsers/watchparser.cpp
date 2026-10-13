#include "rendererinternal.h"

namespace yt {

using namespace gj;

// Best-effort like count (FRAGILE — YouTube reshapes this often): the legacy
// toggleButtonRenderer.defaultText, else the modern likeButtonViewModel button title.
static std::string findLikeText(std::string_view primaryInfo)
{
    std::string_view v = findExtent(primaryInfo, "toggleButtonRenderer");
    if (!v.empty()) {
        rj::ToggleButton tb{};
        readJson(tb, v);
        if (tb.defaultText) {
            const std::string t = str(textOf(*tb.defaultText));
            if (!t.empty()) return t;
        }
    }
    v = findExtent(primaryInfo, "likeButtonViewModel");
    if (!v.empty()) {
        const std::string_view bvm = findExtent(v, "buttonViewModel");
        if (!bvm.empty()) {
            rj::ButtonVM b{};
            readJson(b, bvm);
            const std::string t = str(b.title);
            // Skip the bare "Like"/"Dislike" labels — only a count-ish value is useful.
            if (!t.empty() && t != "Like" && t != "Dislike") return t;
        }
    }
    return std::string();
}

// A button title is a real count only when it is a plain formatted integer
// ("1,234" / "1 234" / "1234"); abbreviated forms ("1.2K") stay in likeText and
// leave likeCount unknown. Returns -1 when the text carries no numeric count.
static int64_t countFromTitle(const std::string &title)
{
    if (title.empty()) return -1;
    if (!gj::hasDigit(title)) return -1;      // pure label ("Like")
    if (gj::hasAsciiLetter(title)) return -1;    // abbreviated ("1.2K")
    return digitsOf(title);                                   // strips ',' / spaces
}

// Account-tied engagement state (authed /next only): the like/dislike toggle.
// Modern WEB shape — segmentedLikeDislikeButtonViewModel nests like/dislike
// ButtonViewModels, each wrapping a toggleButtonViewModel with `isToggled`; the
// like side's button title carries the count. Legacy shape — a toggleButtonRenderer
// pair (first=like, second=dislike) each with `isToggled` + `defaultText`.
// Defensive: unfound shapes leave likeStatus=0 / likeCount=-1 (Risk R1).
static void findLikeState(std::string_view primaryInfo, int *likeStatus, int64_t *likeCount)
{
    *likeStatus = 0;
    *likeCount = -1;

    // -- Modern: like/dislikeButtonViewModel (under segmentedLikeDislikeButtonViewModel).
    const std::string_view likeVM = findExtent(primaryInfo, "likeButtonViewModel");
    const std::string_view dislikeVM = findExtent(primaryInfo, "dislikeButtonViewModel");
    if (!likeVM.empty() || !dislikeVM.empty()) {
        bool disliked = false, liked = false;
        if (!dislikeVM.empty()) {
            const std::string_view tvm = findExtent(dislikeVM, "toggleButtonViewModel");
            rj::ToggleStateVM s{};
            readJson(s, tvm.empty() ? dislikeVM : tvm);
            disliked = s.isToggled && *s.isToggled;
        }
        if (!likeVM.empty()) {
            const std::string_view tvm = findExtent(likeVM, "toggleButtonViewModel");
            rj::ToggleStateVM s{};
            readJson(s, tvm.empty() ? likeVM : tvm);
            liked = s.isToggled && *s.isToggled;
            const std::string_view bvm = findExtent(likeVM, "buttonViewModel");
            if (!bvm.empty()) {
                rj::ButtonVM b{};
                readJson(b, bvm);
                *likeCount = countFromTitle(str(b.title));
            }
        }
        *likeStatus = disliked ? 2 : (liked ? 1 : 0);
        return;
    }

    // -- Legacy: the toggleButtonRenderer pair. First = like, second = dislike.
    const std::string_view menu = findExtent(primaryInfo, "menuRenderer");
    if (menu.empty()) return;
    rj::TogglePair mr{};
    readJson(mr, menu);
    if (!mr.topLevelButtons) return;
    int idx = 0;
    for (const std::map<std::string, rj::ToggleButton> &btn : *mr.topLevelButtons) {
        std::map<std::string, rj::ToggleButton>::const_iterator it = btn.find("toggleButtonRenderer");
        if (it == btn.end()) { ++idx; continue; }
        const rj::ToggleButton &tb = it->second;
        const bool on = tb.isToggled && *tb.isToggled;
        if (idx == 0) {                                    // like button
            if (on) *likeStatus = 1;
            if (tb.defaultText) *likeCount = countFromTitle(str(textOf(*tb.defaultText)));
        } else if (idx == 1 && on) {                       // dislike button
            *likeStatus = 2;
        }
        ++idx;
    }
}

// WatchScanner — single-pass scan for parseWatchPage:
//   (a) VideoCollector logic (consume semantics) → related list
//   (b) first videoPrimaryInfoRenderer extent captured (no consume — siblings
//       must still be visited so the related list is fully collected)
//   (c) first videoSecondaryInfoRenderer extent captured (same — no consume)
struct WatchScanner : CollectorBase {
    std::vector<Video> *related;
    std::string_view primary;
    std::string_view secondary;
    std::string_view metadata;   // TVHTML5 singleColumnWatchNextResults (authed /next)

    scan::Action what(std::string_view key, int)
    {
        if (consumed()) return scan::Action::Skip;
        // Capture the info renderer extents without marking the parent as consumed,
        // so sibling renderer keys are still visited (the related list collection).
        if (primary.empty() && key == "videoPrimaryInfoRenderer")
            return scan::Action::Capture;
        if (secondary.empty() && key == "videoSecondaryInfoRenderer")
            return scan::Action::Capture;
        if (metadata.empty() && key == "videoMetadataRenderer")
            return scan::Action::Capture;
        // VideoCollector logic for related videos (consume semantics).
        if (related && (key == "lockupViewModel" || key == "tileRenderer" || isVideoKind(key))) {
            consume();
            return scan::Action::Capture;
        }
        return scan::Action::Descend;
    }
    void capture(std::string_view key, std::string_view value, int)
    {
        if (key == "videoPrimaryInfoRenderer" && primary.empty()) {
            primary = value; return;
        }
        if (key == "videoSecondaryInfoRenderer" && secondary.empty()) {
            secondary = value; return;
        }
        if (key == "videoMetadataRenderer" && metadata.empty()) {
            metadata = value; return;
        }
        // Video renderer capture (same logic as VideoCollector).
        if (!related) return;
        if (key == "lockupViewModel") {
            rj::Lockup lm{};
            readJson(lm, value);
            if (lm.contentType && *lm.contentType == "LOCKUP_CONTENT_TYPE_VIDEO")
                related->push_back(fromLockupVideo(lm));
        } else if (key == "tileRenderer") {
            rj::Tile t{};
            readJson(t, value);
            if (t.contentType && *t.contentType == "TILE_CONTENT_TYPE_VIDEO")
                related->push_back(fromTile(t));
        } else {
            rj::VideoR r{};
            readJson(r, value);
            related->push_back(fromVideoRenderer(r));
        }
    }
};

void parseWatchPage(std::string_view response, Video *primary, std::vector<Video> *related)
{
    WatchScanner ws;
    ws.related = related;
    if (related) related->clear();
    scan::document(response, ws);

    if (!primary) return;
    Video v;
    const std::string_view pri = ws.primary;
    if (!pri.empty()) {
        rj::PrimaryInfo info{};
        readJson(info, pri);
        v.title = str(textOf(info.title));
        const std::string_view vcr = findExtent(pri, "videoViewCountRenderer");
        if (!vcr.empty()) {
            rj::ViewCountR r{};
            readJson(r, vcr);
            if (r.viewCount) v.viewText = str(textOf(*r.viewCount));
        }
        v.likeText = findLikeText(pri);
        { int64_t n = -1; findLikeState(pri, &v.likeStatus, &n); if (n >= 0) v.likeCount = n; }
    }
    const std::string_view sec = ws.secondary;
    if (!sec.empty()) {
        const std::string_view owner = findExtent(sec, "videoOwnerRenderer");
        if (!owner.empty()) {
            rj::OwnerR r{};
            readJson(r, owner);
            v.channelName = str(textOf(r.title));
            if (r.thumbnail) v.avatarUrl = str(lastThumbUrl(*r.thumbnail));
            if (r.navigationEndpoint && r.navigationEndpoint->browseEndpoint)
                v.channelId = str(r.navigationEndpoint->browseEndpoint->browseId);
            // The viewer's subscribe state (authed /next only) — feeds the VideoPage button.
            if (r.subscribeButton && r.subscribeButton->subscribeButtonRenderer
                && r.subscribeButton->subscribeButtonRenderer->subscribed)
                v.subscribed = *r.subscribeButton->subscribeButtonRenderer->subscribed;
        }
        // Description: modern attributedDescription.content (plain string) or legacy runs.
        const std::string_view ad = scan::topLevelValue(sec, "attributedDescription");
        if (!ad.empty() && ad.front() == '{') {
            ContentText c{};
            readJson(c, ad);
            v.description = str(contentOf(std::optional<ContentText>(c)));
        }
        if (v.description.empty()) {
            const std::string_view d = scan::topLevelValue(sec, "description");
            if (!d.empty()) {
                Text t{};
                readJson(t, d);
                v.description = str(textOf(t));
            }
        }
    }
    // --- TVHTML5 shape (authed singleColumnWatchNextResults) --------------------
    // Signed-in /next carries a videoMetadataRenderer instead of videoPrimaryInfo/
    // videoSecondaryInfoRenderer: title/views/date live in it, owner→avatar/channel in
    // its videoOwnerRenderer, the like button outside it. Fill whatever the WEB path
    // left empty (a WEB response has meta empty, so this is a no-op there).
    const std::string_view meta = ws.metadata;
    if (!meta.empty()) {
        if (v.title.empty()) {
            const std::string_view t = scan::topLevelValue(meta, "title");
            if (!t.empty() && t.front() == '{') { Text tt{}; readJson(tt, t); v.title = str(textOf(tt)); }
        }
        if (v.viewText.empty()) {
            const std::string_view vcr = findExtent(meta, "videoViewCountRenderer");
            if (!vcr.empty()) { rj::ViewCountR r{}; readJson(r, vcr); if (r.viewCount) v.viewText = str(textOf(*r.viewCount)); }
        }
        if (v.date.empty()) {
            const std::string_view pt = scan::topLevelValue(meta, "publishedTimeText");
            if (!pt.empty() && pt.front() == '{') { Text tt{}; readJson(tt, pt); v.date = str(textOf(tt)); }
        }
        const std::string_view owner = findExtent(meta, "videoOwnerRenderer");
        if (!owner.empty()) {
            rj::OwnerR r{}; readJson(r, owner);
            if (v.channelName.empty()) v.channelName = str(textOf(r.title));
            if (v.avatarUrl.empty() && r.thumbnail) v.avatarUrl = str(lastThumbUrl(*r.thumbnail));
            if (v.channelId.empty() && r.navigationEndpoint && r.navigationEndpoint->browseEndpoint)
                v.channelId = str(r.navigationEndpoint->browseEndpoint->browseId);
            // The viewer's subscribe state (authed /next only) — feeds the VideoPage button.
            if (r.subscribeButton && r.subscribeButton->subscribeButtonRenderer
                && r.subscribeButton->subscribeButtonRenderer->subscribed)
                v.subscribed = *r.subscribeButton->subscribeButtonRenderer->subscribed;
        }
        // Description: the TV /next carries it in the structured-description engagement
        // panel (expandableVideoDescriptionBodyRenderer.descriptionBodyText), not in a
        // secondary renderer.
        if (v.description.empty()) {
            const std::string_view db = findExtent(response, "expandableVideoDescriptionBodyRenderer");
            if (!db.empty()) {
                const std::string_view dbt = scan::topLevelValue(db, "descriptionBodyText");
                if (!dbt.empty() && dbt.front() == '{') { Text t{}; readJson(t, dbt); v.description = str(textOf(t)); }
            }
        }
        // Like COUNT — YouTube's OWN displayed count (e.g. "8.8K"), from the like
        // control's likeCountText. (The TV toggleButtons are Captions/Surround; the like
        // count is on likeButtonRenderer, elsewhere in the tree — scan the whole response.
        // This replaces the RYD like fallback: RYD is dislikes-only now.)
        if (v.likeText.empty()) {
            const std::string_view lb = findExtent(response, "likeButtonRenderer");
            if (!lb.empty()) {
                const std::string_view lct = scan::topLevelValue(lb, "likeCountText");
                if (!lct.empty() && lct.front() == '{') { Text t{}; readJson(t, lct); v.likeText = str(textOf(t)); }
            }
        }
        // Like STATE — the viewer's like/dislike, from the authed likeStatusEntity in
        // frameworkUpdates. This is how the TV /next carries it (there is no like
        // toggleButton), so it restores the like/dislike button on every (re)entry.
        if (v.likeStatus == 0) {
            const std::string_view lse = findExtent(response, "likeStatusEntity");
            if (!lse.empty()) {
                rj::LikeStatusEntity e{}; readJson(e, lse);
                if (e.likeStatus) {
                    if      (*e.likeStatus == "LIKE")    v.likeStatus = 1;
                    else if (*e.likeStatus == "DISLIKE") v.likeStatus = 2;
                }
            }
        }
    }
    *primary = v;
}

}
