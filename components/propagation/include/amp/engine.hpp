// Port von Propagation_Engine.py (ohne UI): Modelle, Antenne, Schwelle, Gain-Mapping und
// die Verwaltung der zuletzt aktiven Frequenzen (stumm gewordene Sender bekommen genau
// einmal Gain 0.0).
#pragma once
#include "amp/model.hpp"
#include "amp/antenna.hpp"
#include "amp/scenario.hpp"
#include <array>
#include <set>
#include <string>
#include <vector>

namespace amp {

enum class GainMode { Compat, Physical };

struct EngineParams {
    real sens = 20;                  // Schwelle dBuV/m (Slider des Originals)
    GainMode gain_mode = GainMode::Compat;
    real gain_window = 28;           // dB, 20..32 (nur Physical)
};

enum class PathMode { Ground, Sky, Both };   // "GrW", "SkW", "GrW+SkW"

struct StationResult {
    int site = 0;                    // Index im Szenario
    real pot = 0, now = 0, fs_g = 0, fs_s = 0, t_bw = 0, t_rw = 0;
    PathMode mode = PathMode::Ground;
    bool audible = false;            // now > sens
};

struct GainMsg {
    int freq_hz = 0;
    real gain = 0;
};

struct RunOutput {
    real sun_alt_rx = 0;
    std::vector<StationResult> results;   // nach "now" absteigend sortiert
    std::vector<GainMsg> msgs;            // zu sendende Gain-Pakete (nur bei send_udp)
};

real gain_from_level(real level, const EngineParams &p);
// Text wie Python f"{round(g,6)}": "1.0", "0.5", "0.123457"
std::string format_gain(real g);
std::string format_gain_msg(const GainMsg &m);

class Engine {
public:
    Engine();
    void set_model(ModelId id);
    ModelId model() const { return model_id_; }

    void run(const Scenario &sc, real rx_lat, real rx_lon, int n_day, real time_h,
             const Antenna &ant, const EngineParams &params, bool send_udp, RunOutput &out);

    // Alle zuletzt aktiven Frequenzen auf 0.0 setzen (z. B. bei Szenariowechsel).
    std::vector<GainMsg> mute_all();

private:
    std::array<std::unique_ptr<PropagationModel>, MODEL_COUNT> models_;
    ModelId model_id_ = ModelId::Classic;
    std::set<int> last_active_;
};

}  // namespace amp
