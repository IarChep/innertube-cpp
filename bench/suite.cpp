// JSONTestSuite (nst/JSONTestSuite, fetched by fetch.sh) against yt::json: every y_ file
// must be accepted, every n_ file refused; i_ files are implementation-defined and counted.
#include "json/json.h"
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
int main(int argc, char **argv)
{
    const std::string dir = argc > 1 ? argv[1] : "ext/JSONTestSuite/test_parsing";
    int wrong = 0, total = 0, iAccepted = 0, iRefused = 0;
    DIR *d = opendir(dir.c_str());
    if (!d) return 2;
    while (dirent *e = readdir(d)) {
        const std::string n = e->d_name;
        if (n.size() < 3 || n[1] != '_') continue;
        std::ifstream f(dir + "/" + n, std::ios::binary);
        std::stringstream s;
        s << f.rdbuf();
        const bool ok = yt::json::Doc(s.str()).ok();
        ++total;
        if (n[0] == 'y' && !ok) { ++wrong; std::printf("refused a valid document: %s\n", n.c_str()); }
        if (n[0] == 'n' && ok) { ++wrong; std::printf("accepted an invalid document: %s\n", n.c_str()); }
        if (n[0] == 'i') ++(ok ? iAccepted : iRefused);
    }
    closedir(d);
    std::printf("%d files, %d wrong; implementation-defined: %d accepted, %d refused\n", total, wrong, iAccepted, iRefused);
    return wrong != 0;
}
