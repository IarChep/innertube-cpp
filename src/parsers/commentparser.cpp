#include "rendererinternal.h"
#include "continuation.h"

namespace yt {

using namespace gj;

// CommentScanner — single-pass: collects commentEntityPayload (consume
// semantics, same as the old CommentCollector) AND captures the first
// top-level onResponseReceivedEndpoints extent (for the token mini-scan)
// AND records the first continuation token found OUTSIDE that extent.
//
// Token precedence after the scan reproduces the old two-call logic exactly:
//   if (token inside onRRE extent)   → use it          (same: findContinuationTokenUnder)
//   else if (token outside extent)   → use it          (same: findContinuationToken on whole doc)
struct CommentScanner : CollectorBase {
    std::vector<Comment> *out;
    std::string_view onRRE;          // first top-level onResponseReceivedEndpoints extent
    std::string outsideToken;        // first token found outside the onRRE extent
    std::string createParams;        // first createCommentParams string found (R4)
    bool haveCreateParams = false;

    scan::Action what(std::string_view key, int d)
    {
        if (consumed()) return scan::Action::Skip;
        if (key == "commentEntityPayload") { consume(); return scan::Action::Capture; }
        // R4 best-effort: the create-comment box's submit token. It rides in the
        // comments-section create-comment command (commentSimpleboxRenderer /
        // ...createCommentEndpoint) as a plain string field named createCommentParams.
        // The exact host renderer is unverified on-device — scrape the first field of
        // that name anywhere in the response. It sits OUTSIDE any commentEntityPayload
        // subtree, so the leaf guard above never masks it.
        if (!haveCreateParams && key == "createCommentParams") return scan::Action::Capture;
        // Capture the top-level onRRE extent once (depth 0 = immediate child of root).
        if (d == 0 && key == "onResponseReceivedEndpoints" && onRRE.empty())
            return scan::Action::Capture;
        // Track first token outside the onRRE extent (it cannot be inside since
        // we captured onRRE above — once captured, the scanner does not descend into it).
        if (outsideToken.empty() && gj::isTokenKey(key)) return scan::Action::Capture;
        return scan::Action::Descend;
    }
    void capture(std::string_view key, std::string_view value, int d)
    {
        if (key == "commentEntityPayload") {
            rj::CommentPayload p{};
            readJson(p, value);
            const Comment c = fromCommentPayload(p);
            if (!c.body.empty()) out->push_back(c);
            return;
        }
        if (!haveCreateParams && key == "createCommentParams") {
            // The captured extent is the raw JSON string literal (with quotes);
            // Glaze reads+unescapes it into std::string.
            std::string s;
            readJson(s, value);
            if (!s.empty()) { createParams = s; haveCreateParams = true; }
            return;
        }
        if (d == 0 && key == "onResponseReceivedEndpoints" && onRRE.empty()) {
            onRRE = value;
            return;
        }
        if (gj::isTokenKey(key)) {
            gj::captureToken(key, value, &outsideToken);
            return;
        }
    }
};

std::vector<Comment> parseComments(std::string_view response, std::string *nextToken,
                                 std::string *createCommentParams)
{
    std::vector<Comment> out;
    CommentScanner s;
    s.out = &out;
    scan::document(response, s);
    if (nextToken) {
        // Prefer token inside onResponseReceivedEndpoints (mini-scan), else
        // the first token found outside it during the main scan.
        std::string t;
        if (!s.onRRE.empty()) t = findContinuationToken(s.onRRE);
        if (t.empty() && !s.outsideToken.empty())
            t = std::string(s.outsideToken);
        *nextToken = t;
    }
    if (createCommentParams)   // R4: empty when the create-comment box wasn't present
        *createCommentParams = std::string(s.createParams);
    return out;
}

}
