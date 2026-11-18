// Port von Storm_Simulation.py ohne UI: Sturmzelle bewegen und Sferics erzeugen.
// Bewegung haengt an der Simulationszeit, die Sferics-Rate an der Echtzeit (Hoerbarkeit).
#pragma once
#include "amp/real.hpp"
#include <cstdint>
#include <string>

namespace amp {

class Rng {
public:
    explicit Rng(uint32_t seed = 2463534242u) : s_(seed ? seed : 1) {}
    void seed(uint32_t v) { s_ = v ? v : 1; }
    uint32_t next() { s_ ^= s_ << 13; s_ ^= s_ >> 17; s_ ^= s_ << 5; return s_; }
    real uniform() { return real(next() >> 8) / real(16777216.0); }          // [0,1)
    real uniform(real a, real b) { return a + (b - a) * uniform(); }
private:
    uint32_t s_;
};

enum class StormProfile { Weak, Medium, Strong };

struct StormParams {
    int amp = 2200;          // Grundamplitude
    int rate = 7;            // Ereignisse je Sekunde (Skala des Originals)
    real speed_kmh = 40;
    real heading_deg = 270;
};

struct Sferic {
    bool fire = false;
    int amp = 0;
    real dur_ms = 0;
    std::string text() const;   // "amp:dur" (Port 8889)
};

constexpr real STORM_LIFETIME_MIN = 120;

StormParams profile_params(StormProfile p);

class Storm {
public:
    bool active = false;
    real lat = 50, lon = 10;
    real life_min = STORM_LIFETIME_MIN;
    StormParams params;

    void start(real la, real lo) { lat = la; lon = lo; life_min = STORM_LIFETIME_MIN; active = true; }
    void stop() { active = false; }

    // dt_sim_s: vergangene Simulationszeit; verbraucht auch die Lebensdauer.
    void move(real dt_sim_s);
    // Ein Sferic-Wurf pro 100-ms-Tick. fast = Zeitraffer (niedrigere Ereignisrate).
    Sferic tick(real rx_lat, real rx_lon, bool fast, Rng &rng) const;
    real distance_km(real rx_lat, real rx_lon) const;
};

}  // namespace amp
