#include "amp/engine.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace amp {

real gain_from_level(real level, const EngineParams &p) {
    if (p.gain_mode == GainMode::Physical) {
        real g = std::pow(real(10), (level - p.sens - p.gain_window) / 20);
        return clampr(g, 0, 1);
    }
    return clampr((level - p.sens) / 40, 0, 1);
}

std::string format_gain(real g) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.6f", (double)g);
    std::string s(buf);
    while (s.size() > 2 && s.back() == '0' && s[s.size() - 2] != '.') s.pop_back();
    return s;
}

std::string format_gain_msg(const GainMsg &m) {
    return std::to_string(m.freq_hz) + ":" + format_gain(m.gain);
}

Engine::Engine() {
    for (int i = 0; i < MODEL_COUNT; ++i) models_[i] = create_model((ModelId)i);
}

void Engine::set_model(ModelId id) {
    if ((int)id < 0 || (int)id >= MODEL_COUNT) id = ModelId::Classic;
    model_id_ = id;
}

void Engine::run(const Scenario &sc, real rx_lat, real rx_lon, int n_day, real time_h,
                 const Antenna &ant, const EngineParams &params, bool send_udp, RunOutput &out) {
    out.results.clear();
    out.msgs.clear();

    PropagationModel &m = *models_[(int)model_id_];
    out.sun_alt_rx = sun_alt(rx_lat, rx_lon, n_day, time_h);
    SimContext ctx;
    ctx.rx_lat = rx_lat; ctx.rx_lon = rx_lon; ctx.n_day = n_day; ctx.time_h = time_h;
    ctx.sun_alt_rx = out.sun_alt_rx;
    m.prepare(ctx);

    out.results.reserve(sc.sites.size());
    for (size_t i = 0; i < sc.sites.size(); ++i) {
        const Site &s = sc.sites[i];
        LinkResult lr = m.compute(Tx{s.lat, s.lon, s.freq, s.kw});
        real ant_loss = ant.attenuation(rx_lat, rx_lon, s.lat, s.lon);
        real sky_ant = ant_loss * lr.sky_ant_factor;
        real fs_g = lr.fs_g + ant_loss;
        real cur_sky = lr.fs_s + sky_ant;
        real sky_base = lr.fs_s_pot + sky_ant;

        StationResult r;
        r.site = (int)i;
        r.pot = std::max(fs_g, sky_base);
        r.now = std::max(fs_g, cur_sky);
        r.fs_g = fs_g; r.fs_s = cur_sky;
        r.t_bw = lr.t_bw; r.t_rw = lr.t_rw;
        r.mode = std::fabs(fs_g - cur_sky) < 10 ? PathMode::Both
                 : (cur_sky > fs_g ? PathMode::Sky : PathMode::Ground);
        r.audible = r.now > params.sens;
        out.results.push_back(r);
    }
    // Original sortiert erst nach "pot", dann nach "now" (beide stabil, absteigend): bei gleichem
    // "now" bleibt die "pot"-Reihenfolge erhalten. Gleiche Reihenfolge hier.
    std::stable_sort(out.results.begin(), out.results.end(),
                     [](const StationResult &a, const StationResult &b) { return a.pot > b.pot; });
    std::stable_sort(out.results.begin(), out.results.end(),
                     [](const StationResult &a, const StationResult &b) { return a.now > b.now; });

    if (!send_udp) return;

    std::set<int> now_active;
    for (const auto &r : out.results) {
        if (!r.audible) continue;
        int f_hz = (int)std::lround((double)sc.sites[r.site].freq * 1000.0);
        now_active.insert(f_hz);
        out.msgs.push_back({f_hz, gain_from_level(r.now, params)});
    }
    for (int f_hz : last_active_)
        if (!now_active.count(f_hz)) out.msgs.push_back({f_hz, 0});
    last_active_ = std::move(now_active);
}

std::vector<GainMsg> Engine::mute_all() {
    std::vector<GainMsg> v;
    for (int f : last_active_) v.push_back({f, 0});
    last_active_.clear();
    return v;
}

}  // namespace amp
