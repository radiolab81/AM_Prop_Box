// Gemeinsamer Gleitkommatyp. Auf dem ESP32 (Single-Precision-FPU) ist float richtig,
// double wuerde in Software emuliert. Der Host-Test kann mit -DAMP_DOUBLE bauen, um
// Portierungsfehler von Rundungsfehlern zu trennen.
#pragma once
#include <cmath>

namespace amp {
#ifdef AMP_DOUBLE
using real = double;
#else
using real = float;
#endif

constexpr real C_KM_PER_MS = real(299.792);   // Lichtgeschwindigkeit in km/ms
constexpr real PI_R = real(3.14159265358979323846);

inline real rad(real d) { return d * (PI_R / real(180)); }
inline real deg(real r) { return r * (real(180) / PI_R); }
inline real clampr(real v, real lo, real hi) { return v < lo ? lo : (v > hi ? hi : v); }
// Python-Modulo (Ergebnis hat das Vorzeichen des Teilers)
inline real pymod(real a, real b) {
    real r = std::fmod(a, b);
    if (r != 0 && ((r < 0) != (b < 0))) r += b;
    return r;
}
}  // namespace amp
