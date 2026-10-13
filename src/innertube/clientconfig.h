#ifndef YT_CLIENTCONFIG_H
#define YT_CLIENTCONFIG_H
#include <yt/catalog.h>
#include <array>
namespace yt {
using ClientTable = std::array<ClientInfo, kClientIdCount>;
ClientTable defaultClientTable();
}
#endif
