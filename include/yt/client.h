// innertube-cpp client API. C++17.
#ifndef YT_CLIENT_H
#define YT_CLIENT_H
#include <functional>
#include <string_view>
namespace yt {
enum class LogLevel { Debug, Info, Warn };
// Diagnostic sink. Called on the engine thread; must not block for long.
using Logger = std::function<void(LogLevel, std::string_view category, std::string_view message)>;
}
#endif
