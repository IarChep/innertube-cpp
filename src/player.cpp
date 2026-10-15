// Pure helpers of the public player types (no I/O).
#include <yt/player.h>
namespace yt {

StoryboardFrame storyboardFrame(const StoryboardLevel &l, int64_t ms)
{
    StoryboardFrame f;
    if (l.count <= 0 || l.cols <= 0 || l.rows <= 0) return f;
    int64_t idx = (l.intervalMs > 0 && ms > 0) ? ms / l.intervalMs : 0;
    if (idx >= l.count) idx = l.count - 1;
    const int64_t perSheet = (int64_t) l.cols * l.rows;
    const int64_t sheet = idx / perSheet, cell = idx % perSheet;
    f.width = l.width;
    f.height = l.height;
    f.x = (int) (cell % l.cols) * l.width;
    f.y = (int) (cell / l.cols) * l.height;
    f.url = l.urlTemplate;
    const size_t at = f.url.find("$M");
    if (at != std::string::npos) f.url.replace(at, 2, std::to_string(sheet));
    return f;
}

}
