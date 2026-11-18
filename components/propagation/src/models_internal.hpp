#pragma once
#include "amp/model.hpp"
#include <algorithm>

namespace amp {

class ClassicModel : public PropagationModel {
public:
    const char *name() const override { return "Klassisch (heuristisch)"; }
    void prepare(const SimContext &c) override;
    LinkResult compute(const Tx &tx) override;
private:
    real h_ion_ = 100;
};

class EnhancedClassicModel : public PropagationModel {
public:
    const char *name() const override { return "Klassisch+ (Norton, Hops)"; }
    void prepare(const SimContext &c) override;
    LinkResult compute(const Tx &tx) override;
private:
    real ground_wave(real d_km, real f_khz, real p_kw) const;
    real h_ion_ = 100;
};

class ITUP1147Model : public PropagationModel {
public:
    ITUP1147Model();
    const char *name() const override { return "ITU-R P.1147-2 (mit Jahreszeiten)"; }
    LinkResult compute(const Tx &tx) override;
private:
    real ground_wave(real d, real f_khz, real p_kw) const;
    static real geomag_lat(real lat, real lon);
    static real k_factor(real lat, real lon);
    real lt_loss(real lat, real lon, bool lf) const;
    real norm_night_mf(real n) const;
    void season_delta(int n_day, real f_khz, real mid_lat, bool lf, real w_day,
                      real &night_out, real &now_out) const;
    real nm_mean_ = 0, nm_range_ = 1;
};

}  // namespace amp
