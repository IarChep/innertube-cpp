// INNERTUBE_HTTP_BACKEND=curl (the default): libcurl multi on the engine thread.
#include "backends/curl/curltransport.h"
namespace yt {
std::unique_ptr<HttpTransport> makeDefaultTransport(const TransportConfig &config)
{
    return std::make_unique<backends::CurlTransport>(config);
}
}
