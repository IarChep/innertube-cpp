// A comments page: the threads (commentEntityPayload mutations), the next page's token and
// the create-comment box's createCommentParams (rendererparser.h).
#include "continuation.h"
#include "rendererinternal.h"

namespace yt {

std::vector<CommentThread> parseComments(Value response, std::string *nextToken, std::string *createCommentParams)
{
    std::vector<CommentThread> out;
    std::string params;
    // R4 best-effort: the create-comment box's submit token rides in the comments-section
    // create-comment command (commentSimpleboxRenderer / …createCommentEndpoint) as a
    // plain string field named createCommentParams. The exact host renderer is unverified
    // on-device — take the first field of that name anywhere outside a comment payload.
    response.find({"commentEntityPayload", "createCommentParams"}, [&](std::string_view key, Value v) {
        if (key == "createCommentParams") {
            if (params.empty()) params = v.string();
            return Visit::Skip;
        }
        const CommentThread t = fromCommentPayload(v);
        if (!t.comment.text.empty()) out.push_back(t);
        return Visit::SkipRest;
    });
    // The page's own token sits under onResponseReceivedEndpoints; elsewhere a reply
    // thread's token could come first.
    if (nextToken) {
        *nextToken = findContinuationTokenUnder(response, "onResponseReceivedEndpoints");
        if (nextToken->empty()) *nextToken = findContinuationToken(response);
    }
    if (createCommentParams) *createCommentParams = params;   // R4: empty when the box wasn't present
    return out;
}

}
