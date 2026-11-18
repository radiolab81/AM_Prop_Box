#include "amp/scenario.hpp"
#include <cstdlib>

namespace amp {

int Scenario::load_text(std::string_view text, int *bad_lines) {
    sites.clear();
    int bad = 0;
    size_t pos = 0;
    while (pos < text.size() && sites.size() < MAX_SITES) {
        size_t nl = text.find('\n', pos);
        std::string line(text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos));
        pos = (nl == std::string_view::npos) ? text.size() : nl + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        size_t a = line.find_first_not_of(" \t");
        if (a == std::string::npos || line[a] == '#') continue;

        const char *p = line.c_str() + a;
        char *e = nullptr;
        double v[4];
        bool ok = true;
        for (int i = 0; i < 4 && ok; ++i) {
            v[i] = std::strtod(p, &e);
            if (e == p || *e != ',') ok = false; else p = e + 1;
        }
        if (!ok || v[2] <= 0 || v[3] < 0) { ++bad; continue; }
        Site s;
        s.lat = real(v[0]); s.lon = real(v[1]); s.freq = real(v[2]); s.kw = real(v[3]);
        s.name = p;
        sites.push_back(std::move(s));
    }
    if (bad_lines) *bad_lines = bad;
    return (int)sites.size();
}

}  // namespace amp
