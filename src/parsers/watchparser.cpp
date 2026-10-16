// The watch page (/next): the video's own header (title, views, date, likes, owner,
// description) in its WEB and signed-in TVHTML5 shapes, and the related list
// (rendererparser.h).
#include "continuation.h"
#include "rendererinternal.h"

namespace yt {

using gj::first;

namespace {

// Best-effort like count (FRAGILE — YouTube reshapes this often): the legacy
// toggleButtonRenderer.defaultText, else the modern likeButtonViewModel button title.
std::string findLikeText(Value primaryInfo)
{
    if (const Value tb = first(primaryInfo, "toggleButtonRenderer")) {
        const std::string t = textOf(tb["defaultText"]);
        if (!t.empty()) return t;
    }
    const Value bvm = first(first(primaryInfo, "likeButtonViewModel"), "buttonViewModel");
    const std::string t = bvm["title"].string();
    // Skip the bare "Like"/"Dislike" labels — only a count-ish value is useful.
    if (!t.empty() && t != "Like" && t != "Dislike") return t;
    return std::string();
}

// A button title is a real count only when it is a plain formatted integer
// ("1,234" / "1 234" / "1234"); abbreviated forms ("1.2K") stay in likeText and
// leave likeCount unknown. Returns -1 when the text carries no numeric count.
int64_t countFromTitle(const std::string &title)
{
    if (title.empty()) return -1;
    if (!gj::hasDigit(title)) return -1;          // pure label ("Like")
    if (gj::hasAsciiLetter(title)) return -1;     // abbreviated ("1.2K")
    return digitsOf(title);                       // strips ',' / spaces
}

// The engaged flag of a like/dislike view-model: its toggleButtonViewModel's, else its own.
bool toggled(Value buttonViewModel)
{
    const Value tvm = first(buttonViewModel, "toggleButtonViewModel");
    return (tvm ? tvm : buttonViewModel)["isToggled"].boolean();
}

// Account-tied engagement state (authed /next only): the like/dislike toggle.
// Modern WEB shape — segmentedLikeDislikeButtonViewModel nests like/dislike
// ButtonViewModels, each wrapping a toggleButtonViewModel with `isToggled`; the
// like side's button title carries the count. Legacy shape — a toggleButtonRenderer
// pair (first=like, second=dislike) each with `isToggled` + `defaultText`.
// Defensive: unfound shapes leave likeStatus=0 / likeCount=-1 (Risk R1).
void findLikeState(Value primaryInfo, int *likeStatus, int64_t *likeCount)
{
    *likeStatus = 0;
    *likeCount = -1;

    // -- Modern: like/dislikeButtonViewModel (under segmentedLikeDislikeButtonViewModel).
    const Value likeVM = first(primaryInfo, "likeButtonViewModel");
    const Value dislikeVM = first(primaryInfo, "dislikeButtonViewModel");
    if (likeVM || dislikeVM) {
        const bool disliked = dislikeVM && toggled(dislikeVM);
        const bool liked = likeVM && toggled(likeVM);
        if (const Value bvm = first(likeVM, "buttonViewModel")) *likeCount = countFromTitle(bvm["title"].string());
        *likeStatus = disliked ? 2 : (liked ? 1 : 0);
        return;
    }

    // -- Legacy: the toggleButtonRenderer pair. First = like, second = dislike.
    int idx = 0;
    for (Value btn : first(primaryInfo, "menuRenderer")["topLevelButtons"].items()) {
        const Value tb = btn["toggleButtonRenderer"];
        if (tb) {
            const bool on = tb["isToggled"].boolean();
            if (idx == 0) {                               // like button
                if (on) *likeStatus = 1;
                if (tb["defaultText"]) *likeCount = countFromTitle(textOf(tb["defaultText"]));
            } else if (idx == 1 && on) {                  // dislike button
                *likeStatus = 2;
            }
        }
        ++idx;
    }
}

// The owner block (videoOwnerRenderer) into v.channel; only empty fields are filled.
void ownerInto(Value owner, ChannelRef &c)
{
    if (!owner) return;
    if (c.name.empty()) c.name = textOf(owner["title"]);
    if (c.avatarUrl.empty()) c.avatarUrl = lastThumbUrl(owner["thumbnail"]);
    if (c.id.empty()) c.id = owner.path("navigationEndpoint", "browseEndpoint", "browseId").string();
    // The viewer's subscribe state (authed /next only).
    if (const Value s = owner.path("subscribeButton", "subscribeButtonRenderer", "subscribed")) c.subscribed = s.boolean();
}

} // namespace

void parseWatchPage(Value response, Video *video, std::string *relatedToken)
{
    Video v;
    // One walk: the related list (renderer leaves, as in parseVideoList) and the first
    // primary/secondary/metadata info renderers. The info renderers are taken without
    // skipping the rest of their object, so sibling renderers are still collected.
    Value pri, sec, meta;
    response.find({"videoPrimaryInfoRenderer", "videoSecondaryInfoRenderer", "videoMetadataRenderer",
                   "lockupViewModel", "tileRenderer", "videoRenderer", "compactVideoRenderer",
                   "gridVideoRenderer", "playlistVideoRenderer"},
                  [&](std::string_view key, Value r) {
        if (key == "videoPrimaryInfoRenderer") {
            if (!pri) pri = r;
            return Visit::Skip;
        }
        if (key == "videoSecondaryInfoRenderer") {
            if (!sec) sec = r;
            return Visit::Skip;
        }
        if (key == "videoMetadataRenderer") {
            if (!meta) meta = r;
            return Visit::Skip;
        }
        if (key == "lockupViewModel") {
            if (r["contentType"].str() == "LOCKUP_CONTENT_TYPE_VIDEO") v.related.items.push_back(fromLockupVideo(r));
        } else if (key == "tileRenderer") {
            if (r["contentType"].str() == "TILE_CONTENT_TYPE_VIDEO") v.related.items.push_back(fromTile(r));
        } else {
            v.related.items.push_back(fromVideoRenderer(r));
        }
        return Visit::SkipRest;
    });
    // The related list's continuation sits at its end, under secondaryResults (WEB); a
    // whole-document search would find the comments section's token first.
    if (relatedToken) *relatedToken = findContinuationToken(first(response, "secondaryResults"));

    if (pri) {
        v.title = textOf(pri["title"]);
        v.publishedText = textOf(pri["dateText"]);
        if (const Value vc = first(pri, "videoViewCountRenderer")["viewCount"]) {
            v.viewText = textOf(vc);
            v.viewCount = digitsOf(v.viewText);           // the WEB page shows the exact count
        }
        v.likeText = findLikeText(pri);
        int likeStatus = 0;
        int64_t n = -1;
        findLikeState(pri, &likeStatus, &n);
        if (n >= 0) v.likeCount = n;
        v.myRating = likeStatus == 1 ? Rating::Like : likeStatus == 2 ? Rating::Dislike : Rating::None;
    }
    if (sec) {
        ownerInto(first(sec, "videoOwnerRenderer"), v.channel);
        // Description: modern attributedDescription.content (plain string) or legacy runs.
        v.description = sec.path("attributedDescription", "content").string();
        if (v.description.empty()) v.description = textOf(sec["description"]);
    }
    // --- TVHTML5 shape (authed singleColumnWatchNextResults) --------------------
    // Signed-in /next carries a videoMetadataRenderer instead of videoPrimaryInfo/
    // videoSecondaryInfoRenderer: title/views/date live in it, owner→avatar/channel in
    // its videoOwnerRenderer, the like button outside it. Fill whatever the WEB path
    // left empty (a WEB response has meta empty, so this is a no-op there).
    if (meta) {
        if (v.title.empty()) v.title = textOf(meta["title"]);
        if (v.viewText.empty()) v.viewText = textOf(first(meta, "videoViewCountRenderer")["viewCount"]);
        if (v.publishedText.empty()) v.publishedText = textOf(meta["publishedTimeText"]);
        ownerInto(first(meta, "videoOwnerRenderer"), v.channel);
        // Description: the TV /next carries it in the structured-description engagement
        // panel (expandableVideoDescriptionBodyRenderer.descriptionBodyText), not in a
        // secondary renderer.
        if (v.description.empty())
            v.description = textOf(first(response, "expandableVideoDescriptionBodyRenderer")["descriptionBodyText"]);
        // Like COUNT — YouTube's OWN displayed count (e.g. "8.8K"), from the like
        // control's likeCountText. (The TV toggleButtons are Captions/Surround; the like
        // count is on likeButtonRenderer, elsewhere in the tree — scan the whole response.
        // This replaces the RYD like fallback: RYD is dislikes-only now.)
        if (v.likeText.empty()) v.likeText = textOf(first(response, "likeButtonRenderer")["likeCountText"]);
        // Like STATE — the viewer's like/dislike, from the authed likeStatusEntity in
        // frameworkUpdates. This is how the TV /next carries it (there is no like
        // toggleButton), so it restores the like/dislike button on every (re)entry.
        if (v.myRating == Rating::None) {
            const std::string_view s = first(response, "likeStatusEntity")["likeStatus"].str();
            if (s == "LIKE") v.myRating = Rating::Like;
            else if (s == "DISLIKE") v.myRating = Rating::Dislike;
        }
    }
    if (video) *video = std::move(v);
}

}
