// Port von Antenna_Engine.py: horizontales Richtdiagramm aus .msi/.ant-Text.
#pragma once
#include "amp/real.hpp"
#include <array>
#include <string>
#include <string_view>

namespace amp {

class Antenna {
public:
    // Liest den Dateitext. Nur der HORIZONTAL-Abschnitt wird ausgewertet (das Original liest
    // zusaetzlich VERTICAL-Zeilen in dasselbe Diagramm; bei den mitgelieferten Dateien ist
    // dieser Abschnitt leer, daher identisch). Positive Werte werden als Daempfung negiert.
    bool load_text(std::string_view text, std::string_view filename = {});
    void reset();

    bool active() const { return active_; }
    const std::string &filename() const { return filename_; }
    real heading() const { return heading_; }
    void set_heading(real h) { heading_ = pymod(h, 360); }
    // Dichte Tabelle 0..359 (fehlende Winkel 0 dB wie im Original)
    const std::array<real, 360> &pattern() const { return pattern_; }
    int points() const { return points_; }

    // Daempfung in dB (<= 0) fuer einen Sender in Peilung vom Empfaenger
    real attenuation(real rx_lat, real rx_lon, real tx_lat, real tx_lon) const;

private:
    std::array<real, 360> pattern_{};
    int points_ = 0;
    bool active_ = false;
    real heading_ = 0;
    std::string filename_;
};

}  // namespace amp
