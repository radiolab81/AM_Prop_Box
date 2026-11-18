#include "amp/storm.hpp"
#include <algorithm>
#include <cstdio>

namespace amp {

std::string Sferic::text() const {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%d:%.1f", amp, (double)dur_ms);
    return buf;
}

StormParams profile_params(StormProfile p) {
    StormParams s;
    switch (p) {
        case StormProfile::Weak:   s.amp = 800;  s.rate = 2;  break;
        case StormProfile::Medium: s.amp = 2200; s.rate = 7;  break;
        case StormProfile::Strong: s.amp = 4500; s.rate = 15; break;
    }
    return s;
}

void Storm::move(real dt_sim_s) {
    if (!active) return;
    real speed_deg_s = (params.speed_kmh / 3600) / 111;
    real r = rad(params.heading_deg);
    lat += std::cos(r) * speed_deg_s * dt_sim_s;      // wie im Original: Laengengrad ohne cos(lat)
    lon += std::sin(r) * speed_deg_s * dt_sim_s;
    life_min -= dt_sim_s / 60;
    if (life_min <= 0) active = false;
}

real Storm::distance_km(real rx_lat, real rx_lon) const {
    real a = (lat - rx_lat) * 111, b = (lon - rx_lon) * 85;
    return std::sqrt(a * a + b * b);
}

Sferic Storm::tick(real rx_lat, real rx_lon, bool fast, Rng &rng) const {
    Sferic s;
    if (!active) return s;
    real rate_limit = fast ? real(0.01) : real(0.05);
    if (rng.uniform() >= real(params.rate) * rate_limit) return s;
    real dist = distance_km(rx_lat, rx_lon);
    real q = std::max(real(0), dist - 20) / 80;
    real attenuation = 1 / (1 + q * q);
    int amp = (int)(real(params.amp) * attenuation * rng.uniform(real(0.7), real(1.3)));
    if (amp > 10) {
        s.fire = true; s.amp = amp; s.dur_ms = rng.uniform(5, 45);
    }
    return s;
}

}  // namespace amp
