#include <doctest/doctest.h>
#include "conformance/transport_conformance.h"
#include "threadedtransport.h"
TEST_CASE("the self-driven reference backend passes the transport conformance suite") {
    const auto failures = yt::conformance::runTransportConformance(
        [](const yt::TransportConfig &c) { return std::make_unique<examples::ThreadedTransport>(c); });
    for (const auto &f : failures) MESSAGE(f);
    CHECK(failures.empty());
}
