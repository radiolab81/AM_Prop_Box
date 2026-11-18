#include "amp/antenna.hpp"
#include "amp/geo.hpp"
#include <cctype>
#include <cstdlib>

namespace amp {

void Antenna::reset() {
    pattern_.fill(0);
    points_ = 0;
    active_ = false;
    heading_ = 0;
    filename_.clear();
}

bool Antenna::load_text(std::string_view text, std::string_view filename) {
    reset();
    bool horizontal = false;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string_view line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = (nl == std::string_view::npos) ? text.size() + 1 : nl + 1;

        std::string s(line);
        for (auto &ch : s) ch = (char)std::toupper((unsigned char)ch);
        size_t a = s.find_first_not_of(" \t\r");
        if (a == std::string::npos || s[a] == '#') continue;
        if (s.find("HORIZONTAL") != std::string::npos) { horizontal = true; continue; }
        if (s.find("VERTICAL") != std::string::npos) { horizontal = false; continue; }
        if (!horizontal) continue;

        char *e1 = nullptr, *e2 = nullptr;
        const char *p = s.c_str() + a;
        double ang = std::strtod(p, &e1);
        if (e1 == p) continue;
        double val = std::strtod(e1, &e2);
        if (e2 == e1) continue;
        if (val > 0) val = -val;
        int idx = ((int)ang % 360 + 360) % 360;
        pattern_[idx] = real(val);
        ++points_;
    }
    active_ = points_ > 0;
    if (active_) filename_ = std::string(filename);
    return true;
}

real Antenna::attenuation(real rx_lat, real rx_lon, real tx_lat, real tx_lon) const {
    if (!active_) return 0;
    real b = bearing(rx_lat, rx_lon, tx_lat, tx_lon);
    int rel = (((int)std::nearbyint(b - heading_)) % 360 + 360) % 360;   // wie Python round() (halbe zu gerade)
    return pattern_[rel];
}

}  // namespace amp
