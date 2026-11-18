#include "amp/geo.hpp"
#include <algorithm>

namespace amp {

real sun_decl(int n_day) {
    return real(23.45) * std::sin(rad(real(360) / real(365) * real(n_day - 81)));
}

real sun_alt(real lat, real lon, int n_day, real time_h) {
    real decl = sun_decl(n_day);
    real hour_angle = (time_h - 12) * 15 + lon;
    real s = std::sin(rad(lat)) * std::sin(rad(decl)) +
             std::cos(rad(lat)) * std::cos(rad(decl)) * std::cos(rad(hour_angle));
    return deg(std::asin(clampr(s, -1, 1)));
}

real gc_distance(real lat1, real lon1, real lat2, real lon2) {
    real p1 = rad(lat1), p2 = rad(lat2);
    real dl = rad(lon2 - lon1);
    real s1 = std::sin((p2 - p1) / 2), s2 = std::sin(dl / 2);
    real a = s1 * s1 + std::cos(p1) * std::cos(p2) * s2 * s2;
    return 2 * real(6371.0) * std::asin(std::min(real(1), std::sqrt(a)));
}

void gc_intermediate(real lat1, real lon1, real lat2, real lon2, real frac,
                     real &out_lat, real &out_lon) {
    real p1 = rad(lat1), l1 = rad(lon1), p2 = rad(lat2), l2 = rad(lon2);
    real d = gc_distance(lat1, lon1, lat2, lon2) / real(6371.0);
    if (d < real(1e-9)) { out_lat = lat1; out_lon = lon1; return; }
    real a = std::sin((1 - frac) * d) / std::sin(d);
    real b = std::sin(frac * d) / std::sin(d);
    real x = a * std::cos(p1) * std::cos(l1) + b * std::cos(p2) * std::cos(l2);
    real y = a * std::cos(p1) * std::sin(l1) + b * std::cos(p2) * std::sin(l2);
    real z = a * std::sin(p1) + b * std::sin(p2);
    out_lat = deg(std::atan2(z, std::hypot(x, y)));
    out_lon = deg(std::atan2(y, x));
}

real bearing(real lat1, real lon1, real lat2, real lon2) {
    real a1 = rad(lat1), a2 = rad(lat2), dl = rad(lon2 - lon1);
    real y = std::sin(dl) * std::cos(a2);
    real x = std::cos(a1) * std::sin(a2) - std::sin(a1) * std::cos(a2) * std::cos(dl);
    return pymod(deg(std::atan2(y, x)) + 360, 360);
}

}  // namespace amp
