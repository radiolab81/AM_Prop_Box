// Schnittstelle der Ausbreitungsmodelle (Port von propagation_models/base.py).
// Ein Modell ist reine Physik: keine UI, kein Netz, kein Zustand ueber einen Lauf hinaus.
#pragma once
#include "amp/real.hpp"
#include "amp/geo.hpp"
#include <memory>

namespace amp {

struct SimContext {
    real rx_lat = 0, rx_lon = 0;
    int n_day = 1;          // Tag im Jahr 1..366
    real time_h = 0;        // UTC in Stunden
    real sun_alt_rx = 0;    // Sonnenhoehe am Empfaenger in Grad
};

struct Tx {
    real lat = 0, lon = 0;
    real freq = 0;          // kHz
    real kw = 0;            // Sendeleistung kW
};

struct LinkResult {
    real fs_g = 0;          // Bodenwelle dBuV/m
    real fs_s = 0;          // Raumwelle jetzt dBuV/m
    real fs_s_pot = 0;      // Raumwelle Potential (Nacht-Bestwert) dBuV/m
    real t_bw = 0;          // Laufzeit Bodenwelle ms
    real t_rw = 0;          // Laufzeit Raumwelle ms
    real sky_ant_factor = 1;// Wirkung der RX-Richtwirkung auf die Raumwelle (0..1)
};

class PropagationModel {
public:
    virtual ~PropagationModel() = default;
    virtual const char *name() const = 0;
    virtual void prepare(const SimContext &c) { ctx_ = c; }
    virtual LinkResult compute(const Tx &tx) = 0;
protected:
    SimContext ctx_;
};

enum class ModelId : int { Classic = 0, Enhanced = 1, ItuP1147 = 2 };
constexpr int MODEL_COUNT = 3;

std::unique_ptr<PropagationModel> create_model(ModelId id);
const char *model_name(ModelId id);

}  // namespace amp
