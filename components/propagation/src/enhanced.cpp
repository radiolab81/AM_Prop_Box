// Port von propagation_models/enhanced.py ("Klassisch+ (Norton, Hops)").
#include "amp/model.hpp"
#include "models_internal.hpp"

namespace amp {

namespace {
constexpr real GROUND_SIGMA = real(0.005);   // S/m
constexpr real FL_KHZ = 0;
constexpr real LW_SKY_CAP_DB = 35;
constexpr real HOP_LEN_KM = 2000;
constexpr real HOP_LOSS_DB = 10;
constexpr real SKY_OFFSET_DB = 0;
constexpr real SKY_ANT_REF_KM = 800;
}  // namespace

void EnhancedClassicModel::prepare(const SimContext &c) {
    ctx_ = c;
    real seasonal = (std::cos(rad(real(360) / real(365) * real(c.n_day - 172))) + 1) / 2;
    real a = c.sun_alt_rx;
    real night = a < -12 ? real(1) : (a < 0 ? std::max(real(0), -a / 12) : real(0));
    h_ion_ = 100 + seasonal * 80 + night * 70;
}

real EnhancedClassicModel::ground_wave(real d_km, real f_khz, real p_kw) const {
    real d = std::max(d_km, real(1));
    real f_mhz = f_khz / 1000;
    real lam_km = real(0.299792) / f_mhz;
    real x = real(1.8e4) * GROUND_SIGMA / f_mhz;
    real p = PI_R * d / (lam_km * x);                       // numerische Distanz
    real att = (2 + real(0.3) * p) / (2 + p + real(0.6) * p * p);
    return 107 + 10 * std::log10(p_kw) - 20 * std::log10(d) + 20 * std::log10(att);
}

static real sky_ant(real d_km) { return std::min(real(1), d_km / SKY_ANT_REF_KM); }

LinkResult EnhancedClassicModel::compute(const Tx &tx) {
    const real f = tx.freq, p_kw = std::max(tx.kw, real(1e-3));
    const real dist = gc_distance(ctx_.rx_lat, ctx_.rx_lon, tx.lat, tx.lon);

    LinkResult lr;
    lr.fs_g = ground_wave(dist, f, p_kw);
    lr.t_bw = dist / (C_KM_PER_MS * real(0.997));

    if (dist <= 50) {
        real path = 2 * std::sqrt((dist / 2) * (dist / 2) + h_ion_ * h_ion_);
        lr.fs_s = lr.fs_s_pot = -100;
        lr.t_rw = path / C_KM_PER_MS;
        lr.sky_ant_factor = sky_ant(dist);
        return lr;
    }

    real hops = std::max(real(1), dist / HOP_LEN_KM);        // stetige Hop-Zahl
    real hop_d = dist / hops;
    real path = hops * 2 * std::sqrt((hop_d / 2) * (hop_d / 2) + h_ion_ * h_ion_);
    real base = (95 + 10 * std::log10(p_kw) - (20 * std::log10(path) + 15)
                 - (hops - 1) * HOP_LOSS_DB + SKY_OFFSET_DB);

    real eff_alt = std::max(ctx_.sun_alt_rx, sun_alt(tx.lat, tx.lon, ctx_.n_day, ctx_.time_h));
    real sun_f = eff_alt > -12 ? std::max(real(0), (eff_alt + 12) / 24) : real(0);
    real q = (real(1000) + FL_KHZ) / (f + FL_KHZ);
    real loss = sun_f * 85 * (q * q);
    if (f < 300) loss = std::min(loss, LW_SKY_CAP_DB);

    lr.fs_s = base - loss;
    lr.fs_s_pot = base;
    lr.t_rw = path / C_KM_PER_MS;
    lr.sky_ant_factor = sky_ant(dist);
    return lr;
}

}  // namespace amp
