#include "rendererinternal.h"

namespace yt {

using namespace gj;

// accountItem objects sit ~7 renderer levels deep and the wrapper chain has shifted
// before (getMultiPageMenuAction vs openPopupAction); scan recursively instead of
// hard-coding the path. Captured items are re-scanned so accountItems nested
// inside one another are still collected (old behavior); the depth cap is a
// strict robustness improvement over the old uncapped walker.
struct AccountItemCollector {
    std::vector<std::string_view> *out;
    void enter(int) {}
    void leave(int) {}
    scan::Action what(std::string_view key, int)
    {
        return key == "accountItem" ? scan::Action::Capture : scan::Action::Descend;
    }
    void capture(std::string_view, std::string_view value, int)
    {
        if (value.empty() || value.front() != '{') return;   // old is_object() guard
        out->push_back(value);
        scan::document(value, *this);                        // descend into it too
    }
};

Account parseAccountsList(std::string_view response)
{
    Account out;
    std::vector<std::string_view> views;
    AccountItemCollector c;
    c.out = &views;
    scan::document(response, c);
    if (views.empty()) return out;

    std::vector<rj::AccountItem> items(views.size());
    for (size_t i = 0; i < views.size(); ++i) readJson(items[i], views[i]);
    const rj::AccountItem *pick = 0;
    for (const rj::AccountItem &it : items)
        if (it.isSelected && *it.isSelected) { pick = &it; break; }
    if (!pick) pick = &items.front();

    if (pick->accountName)   out.name = str(textOf(*pick->accountName));
    if (pick->channelHandle) out.handle = str(textOf(*pick->channelHandle));
    if (pick->accountPhoto)  out.thumbnailUrl = str(lastThumbUrl(*pick->accountPhoto));   // last = largest
    if (pick->serviceEndpoint && pick->serviceEndpoint->selectActiveIdentityEndpoint
        && pick->serviceEndpoint->selectActiveIdentityEndpoint->supportedTokens) {
        for (const rj::SupTok &tok :
             *pick->serviceEndpoint->selectActiveIdentityEndpoint->supportedTokens) {
            if (!tok.offlineCacheKeyToken) continue;
            const std::string key = str(tok.offlineCacheKeyToken->clientCacheKey);
            if (!key.empty())
                out.channelId = key.starts_with("UC") ? key : "UC" + key;
        }
    }
    return out;
}

}
