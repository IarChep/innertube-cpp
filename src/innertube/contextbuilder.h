#ifndef YT_CONTEXTBUILDER_H
#define YT_CONTEXTBUILDER_H
#include <string>
#include <vector>
#include "clientconfig.h"
#include "core/session.h"
namespace yt {
struct Header { std::string name, value; };
class ContextBuilder {
public:
    // The serialized `context` VALUE (a JSON object) for the youtubei payload —
    // client identity + hl/gl/visitorData + the minimum-viable user/request
    // sub-contexts. The transport splices it into every POST body.
    static std::string contextJson(ClientId id, const Session &s);
    static std::vector<Header> headers(ClientId id, const Session &s);
};
}
#endif
