// Port von propagation_models/classic.py ("Klassisch (heuristisch)").
// Absichtlich unveraendert uebernommen, inkl. ebener Distanznaeherung, damit die
// Ergebnisse identisch zu frueheren Versionen des PC-Simulators bleiben.
#include "amp/model.hpp"
#include "models_internal.hpp"

namespace amp {

void ClassicModel::prepare(const SimContext &c) {
    ctx_ = c;
    real seasonal = (std::cos(rad(real(360) / real(365) * real(c.n_day - 172))) + 1) / 2;  // 0=Sommer, 1=Winter
    real a = c.sun_alt_rx;
    real night = a < -12 ? real(1) : (a < 0 ? std::max(real(0), -a / 12) : real(0));
    h_ion_ = 100 + seasonal * 80 + night * 70;
}

LinkResult ClassicModel::compute(const Tx &tx) {
    const real f = tx.freq, p = std::max(tx.kw, real(1e-3));
    const real dlat = ctx_.rx_lat - tx.lat;
    const real dlon = std::cos(rad(ctx_.rx_lat)) * (ctx_.rx_lon - tx.lon);
    const real dist = std::sqrt(dlat * dlat + dlon * dlon) * real(111.32);

    // 1. Bodenwelle
    real fs_g = 107 + 10 * std::log10(p) - (20 * std::log10(dist + 1) + (f / 180) * (dist / 25));

    // 2. Raumwelle ueber virtuelle Hoehe
    real path_rw = 2 * std::sqrt((dist / 2) * (dist / 2) + h_ion_ * h_ion_);
    real sky_base = dist > 50 ? 95 + 10 * std::log10(p) - (20 * std::log10(path_rw) + 15) : real(-100);

    // D-Schicht-Absorption
    real eff_alt = std::max(ctx_.sun_alt_rx, sun_alt(tx.lat, tx.lon, ctx_.n_day, ctx_.time_h));
    real sun_f = eff_alt > -12 ? std::max(real(0), (eff_alt + 12) / 24) : real(0);
    real r = real(1000) / f;
    real sky_loss = sun_f * 85 * (r * r);

    LinkResult lr;
    lr.fs_g = fs_g;
    lr.fs_s = sky_base - sky_loss;
    lr.fs_s_pot = sky_base;
    lr.t_bw = dist / (C_KM_PER_MS * real(0.997));
    lr.t_rw = path_rw / C_KM_PER_MS;
    lr.sky_ant_factor = 1;
    return lr;
}

}  // namespace amp
