// accounts_list → the signed-in identity (rendererparser.h).
#include "rendererinternal.h"

namespace yt {

// accountItem objects sit ~7 renderer levels deep and the wrapper chain has shifted
// before (getMultiPageMenuAction vs openPopupAction): find them anywhere instead of
// hard-coding the path. Items nested inside one another are all collected (Descend); the
// walk is iterative and the document's depth is bounded, so a chain of nested
// "accountItem" keys cannot exhaust the stack (it used to: one recursion per level).
AccountInfo parseAccountsList(Value response)
{
    AccountInfo out;
    std::vector<Value> items;
    response.find({"accountItem"}, [&](std::string_view, Value v) {
        if (v.isObject()) items.push_back(v);
        return Visit::Descend;
    });
    if (items.empty()) return out;
    Value pick = items.front();
    for (Value it : items)
        if (it["isSelected"].boolean()) {
            pick = it;
            break;
        }
    out.name = textOf(pick["accountName"]);
    out.handle = textOf(pick["channelHandle"]);
    out.avatarUrl = lastThumbUrl(pick["accountPhoto"]);   // last = largest
    for (Value tok : pick.path("serviceEndpoint", "selectActiveIdentityEndpoint", "supportedTokens").items()) {
        const std::string key = tok.path("offlineCacheKeyToken", "clientCacheKey").string();
        if (!key.empty()) out.channelId = key.rfind("UC", 0) == 0 ? key : "UC" + key;
    }
    return out;
}

}
