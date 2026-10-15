#include <yt/catalog.h>
namespace yt {

std::vector<FeedEntry> topicFeeds()
{
    // Anonymous topic feeds verified to return real video lists: YouTube's
    // auto-generated topic channels (UC…) + the news destination (FE…). The
    // Trending/Explore/Gaming/Movies "FE" destinations reject a plain browse.
    return {
        {"News", "FEnews_destination"},
        {"Live", "UC4R8DWoMoI7CAwX8_LjQHig"},
        {"Learning", "UCtFRv9O2AHqOZjjynzrv-xg"},
        {"Music", "UC-9-kyTW8ZkZNDHQJ6FgpwQ"},
        {"Fashion & Beauty", "UCrpQ4p1Ql_hG8rKXIKM1MOQ"},
        {"Sports", "UCEgdi0XIXXZ-qJOFPf4JSKw"},
    };
}

}
