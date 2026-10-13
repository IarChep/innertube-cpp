// Diagnostic logging hook shared by the client and the transports. C++17.
#ifndef YT_LOG_H
#define YT_LOG_H
#include <functional>
#include <string_view>
namespace yt {
enum class LogLevel { Debug, Info, Warn };
// Diagnostic sink. Called on the engine thread (or a backend's own thread); must not block.
using Logger = std::function<void(LogLevel, std::string_view category, std::string_view message)>;
}
#endif
