#pragma once
#include "amp/real.hpp"

namespace amp {
real sun_decl(int n_day);
real sun_alt(real lat, real lon, int n_day, real time_h);
real gc_distance(real lat1, real lon1, real lat2, real lon2);                 // km
void gc_intermediate(real lat1, real lon1, real lat2, real lon2, real frac,
                     real &out_lat, real &out_lon);
real bearing(real lat1, real lon1, real lat2, real lon2);                     // Grad, 0 = Nord
}  // namespace amp
