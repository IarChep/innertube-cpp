// Built with INNERTUBE_HTTP_BACKEND=none: no backend compiled in.
#include <yt/transport.h>
namespace yt {
std::unique_ptr<HttpTransport> makeDefaultTransport(const TransportConfig &) { return nullptr; }
}
