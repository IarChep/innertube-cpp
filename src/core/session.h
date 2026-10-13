// The engine's live request identity (engine-thread only). Mutations must go through
// IHttp so the memoized per-client context/headers are invalidated.
#ifndef YT_CORE_SESSION_H
#define YT_CORE_SESSION_H
#include <string>
#include "innertube/clientconfig.h"
namespace yt {
struct Session {
    std::string visitorData;
    std::string hl = "en";
    std::string gl = "US";
    std::string bearer;   // OAuth access token; rides TVHTML5 only
    ClientTable clients = defaultClientTable();   // per-client identity (overridable)
};
}
#endif
