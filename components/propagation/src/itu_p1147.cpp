// Port von propagation_models/itu_p1147.py ("ITU-R P.1147-2 (mit Jahreszeiten)").
// Vereinfachungen und nicht umgesetzte Teile wie im Original (siehe dessen Kopfkommentar).
#include "amp/model.hpp"
#include "models_internal.hpp"

namespace amp {

namespace {
constexpr real GROUND_SIGMA = real(0.005);
constexpr real GROUND_EPS = 15;
constexpr real SUNSPOT_R = 0;
constexpr real LF_MF_BORDER_KHZ = 300;
constexpr real LT_DAY_MF = 43;
constexpr real LT_DAY_LF = 26;
constexpr real SKY_ANT_REF_KM = 800;
constexpr bool SEASONAL = true;
constexpr real SEASON_NIGHT_MF_F1 = 530, SEASON_NIGHT_MF_A1 = 15;
constexpr real SEASON_NIGHT_MF_F2 = 1700, SEASON_NIGHT_MF_A2 = 3;
constexpr real SEASON_NIGHT_LF_PK = 8;
constexpr real SEASON_DAY_MF_AMP = 11;

real poly_ss(real t) { return std::max(real(0), real(12.40) - real(9.248) * t + real(2.892) * t * t - real(0.3343) * t * t * t); }
real poly_sr(real t) { return std::max(real(0), real(9.6) + real(12.2) * t + real(5.62) * t * t + real(0.86) * t * t * t); }

real night_mf_shape(real n) {
    return real(0.7) * std::cos(4 * PI_R * (n - 80) / real(365)) +
           real(0.3) * std::cos(2 * PI_R * (n - 355) / real(365));
}
}  // namespace

ITUP1147Model::ITUP1147Model() {
    // Jahresmittel und Spannweite der Saisonform (wie _norm_night_mf im Original)
    double sum = 0, lo = 1e9, hi = -1e9;
    for (int i = 1; i <= 365; ++i) {
        double v = night_mf_shape(real(i));
        sum += v; lo = std::min(lo, v); hi = std::max(hi, v);
    }
    nm_mean_ = real(sum / 365.0);
    nm_range_ = real(hi - lo);
}

real ITUP1147Model::norm_night_mf(real n) const {
    return (night_mf_shape(n) - nm_mean_) / nm_range_;
}

real ITUP1147Model::ground_wave(real d, real f_khz, real p_kw) const {
    d = std::max(d, real(1));
    real lam_m = real(299792.458) / f_khz;
    real x = real(1.8e4) * GROUND_SIGMA / (f_khz / 1000);
    real q = (GROUND_EPS + 1) / x;
    real cosb = 1 / std::sqrt(1 + q * q);
    real pn = PI_R * d * 1000 * cosb / (lam_m * x);
    real att = (2 + real(0.3) * pn) / (2 + pn + real(0.6) * pn * pn);
    return real(109.5) + 10 * std::log10(p_kw) - 20 * std::log10(d) + 20 * std::log10(att);
}

real ITUP1147Model::geomag_lat(real lat, real lon) {
    real a = rad(lat);
    real s = std::sin(a) * std::sin(rad(real(78.5))) +
             std::cos(a) * std::cos(rad(real(78.5))) * std::cos(rad(69 + lon));
    return clampr(deg(std::asin(clampr(s, -1, 1))), -60, 60);
}

real ITUP1147Model::k_factor(real lat, real lon) {
    real t = std::tan(rad(geomag_lat(lat, lon)));
    return 2 * PI_R + real(4.95) * t * t;
}

real ITUP1147Model::lt_loss(real lat, real lon, bool lf) const {
    real T = pymod(ctx_.time_h + lon / 15, 24);
    real l_day = lf ? LT_DAY_LF : LT_DAY_MF;
    real phi = rad(lat), dec = rad(sun_decl(ctx_.n_day));
    real cosh0 = (std::sin(rad(real(-0.833))) - std::sin(phi) * std::sin(dec)) /
                 (std::cos(phi) * std::cos(dec) + real(1e-12));
    if (cosh0 >= 1) return 0;                    // Polarnacht
    if (cosh0 <= -1) return l_day;               // Polartag
    real h = deg(std::acos(cosh0)) / 15;
    real tr = 12 - h, ts = 12 + h;
    if (T >= tr && T <= ts) {                    // Tag
        real t_sr = T - tr, t_ss = T - ts;
        if (t_ss > -1) return poly_ss(t_ss);
        if (t_sr < 1) return poly_sr(t_sr);
        real e_sr = poly_sr(1), e_ss = poly_ss(-1);
        real r_sr = e_sr + (l_day - e_sr) * std::min(real(1), (t_sr - 1) / 2);
        real r_ss = e_ss + (l_day - e_ss) * std::min(real(1), (-t_ss - 1) / 2);
        return std::min(r_sr, r_ss);
    }
    if (T > ts) {                                // Abend/Nacht
        real t = T - ts;
        return t < 4 ? poly_ss(t) : real(0);
    }
    real t = T - tr;                             // Nacht vor Sonnenaufgang (t < 0)
    return t > -3 ? poly_sr(t) : real(0);
}

void ITUP1147Model::season_delta(int n_day, real f_khz, real mid_lat, bool lf, real w_day,
                                 real &night_out, real &now_out) const {
    if (!SEASONAL) { night_out = now_out = 0; return; }
    real n = real(n_day) + (mid_lat < 0 ? real(182.6) : real(0));
    n = pymod(n - 1, 365) + 1;
    real lat_w = real(0.2) + real(0.8) * std::min(real(1), std::fabs(mid_lat) / 35);
    real night, day;
    if (lf) {
        night = real(0.5) * SEASON_NIGHT_LF_PK * std::cos(2 * PI_R * (n - 172) / real(365));
        day = 0;
    } else {
        real fc = std::min(std::max(f_khz, SEASON_NIGHT_MF_F1), SEASON_NIGHT_MF_F2);
        real pk = SEASON_NIGHT_MF_A1 + (SEASON_NIGHT_MF_A2 - SEASON_NIGHT_MF_A1) *
                  (fc - SEASON_NIGHT_MF_F1) / (SEASON_NIGHT_MF_F2 - SEASON_NIGHT_MF_F1);
        night = pk * norm_night_mf(n);
        day = SEASON_DAY_MF_AMP * std::cos(2 * PI_R * (n - 355) / real(365));
    }
    night_out = lat_w * night;
    now_out = lat_w * ((1 - w_day) * night + w_day * day);
}

LinkResult ITUP1147Model::compute(const Tx &tx) {
    const real d = gc_distance(ctx_.rx_lat, ctx_.rx_lon, tx.lat, tx.lon);
    const real f = tx.freq;
    const real p_kw = std::max(tx.kw, real(1e-3));
    const real pdb = 10 * std::log10(p_kw);
    const bool lf = f < LF_MF_BORDER_KHZ;

    LinkResult lr;
    lr.fs_g = ground_wave(d, f, p_kw);
    lr.t_bw = d / (C_KM_PER_MS * real(0.997));
    const real p = std::sqrt(d * d + 40000);
    lr.t_rw = p / C_KM_PER_MS;
    lr.sky_ant_factor = std::min(real(1), d / SKY_ANT_REF_KM);
    if (d < 50) {                                 // P.1147 gilt ab 50 km
        lr.fs_s = lr.fs_s_pot = -100;
        return lr;
    }

    real mid_lat, mid_lon;
    gc_intermediate(ctx_.rx_lat, ctx_.rx_lon, tx.lat, tx.lon, real(0.5), mid_lat, mid_lon);
    real k;
    if (d > 3000) {
        real la, lo, lb, lp;
        gc_intermediate(ctx_.rx_lat, ctx_.rx_lon, tx.lat, tx.lon, real(0.25), la, lo);
        gc_intermediate(ctx_.rx_lat, ctx_.rx_lon, tx.lat, tx.lon, real(0.75), lb, lp);
        k = (k_factor(la, lo) + k_factor(lb, lp)) / 2;
    } else {
        k = k_factor(mid_lat, mid_lon);
    }

    const real a_const = lf ? real(110.2) : real(107.0);
    const real la_loss = k * p / 1000;
    real lr_loss = 0;
    if (!lf) {
        real phi = std::fabs(geomag_lat(mid_lat, mid_lon));
        if (phi > 45) lr_loss = ((phi - 45) / 3) * (SUNSPOT_R / 100) * (p / 1000);
    }
    const real e_night = pdb + a_const - 20 * std::log10(p) - la_loss - lr_loss;
    const real lt = lt_loss(mid_lat, mid_lon, lf);
    const real l_day = lf ? LT_DAY_LF : LT_DAY_MF;
    real d_night, d_now;
    season_delta(ctx_.n_day, f, mid_lat, lf, std::min(real(1), lt / l_day), d_night, d_now);
    lr.fs_s = e_night - lt + d_now;
    lr.fs_s_pot = e_night + d_night;
    return lr;
}

}  // namespace amp
