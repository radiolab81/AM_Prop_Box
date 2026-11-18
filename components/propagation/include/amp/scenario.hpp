// Szenario = Liste von Sendern. Dateiformat (vom PC-Tool erzeugt), eine Zeile je Sender:
//   lat,lon,freq_kHz,kW,Name
// Zeilen mit '#' sind Kommentare. Der Name geht bis zum Zeilenende.
#pragma once
#include "amp/real.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace amp {

struct Site {
    real lat = 0, lon = 0;
    real freq = 0;   // kHz
    real kw = 0;
    std::string name;
};

struct Scenario {
    std::string name;
    std::vector<Site> sites;
    static constexpr size_t MAX_SITES = 400;

    // Rueckgabe: Anzahl uebernommener Sender; fehlerhafte Zeilen werden gezaehlt.
    int load_text(std::string_view text, int *bad_lines = nullptr);
};

}  // namespace amp
