#include "sim.hpp"
#include "json_util.hpp"
#include "amp_config.hpp"
#include "amp_net.hpp"
#include "amp/engine.hpp"
#include "amp/simclock.hpp"
#include "amp/storm.hpp"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <dirent.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <vector>

namespace sim {

static const char *TAG = "sim";
static const char *SCEN_DIR = "/fs/scenarios";
static const char *ANT_DIR = "/fs/antennas";

static std::mutex g_mx;
static amp::Scenario g_scen;
static amp::Antenna g_ant;
// Engine und Uhr werden NICHT als globale Objekte gebaut: Konstruktoren mit Fliesskomma-Rechnung laufen
// sonst in der statischen Initialisierung, bevor die FPU des ESP32 nutzbar ist (Coprocessor exception).
static amp::Engine *g_eng_p = nullptr;
#define g_eng (*g_eng_p)
static amp::SimClock g_clk;
static amp::Storm g_storm;
static amp::Rng g_rng;
static amp::RunOutput g_last;
static amp::EngineParams g_par;
static amp::real g_rx_lat = 50.1f, g_rx_lon = 8.6f;
static int g_speed = 1;
static bool g_dirty = true;
static bool g_save_pending = false;
static int64_t g_save_due_us = 0;
static StateCallback g_cb = nullptr;
// Traegertest (alle Traeger im 1-kHz-Raster 100..1700 kHz auf einen Gain setzen)
static bool g_hold = false;           // Simulation pausiert, bis "auto"
static float g_hold_val = 0;
static float g_burst_val = 0;
static int g_burst_next = -1;         // naechste zu sendende Frequenz in Hz, -1 = nichts offen
static bool g_force_state = false;
static std::vector<amp::GainMsg> g_pending_mute;   // beim naechsten Lauf zu senden

// ---------------------------------------------------------------- Dateien
static bool read_file(const std::string &path, std::string &out) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[512];
    size_t n;
    out.clear();
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

static bool safe_name(const std::string &n) {
    if (n.empty() || n.size() > 31) return false;
    for (char c : n) if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.')) return false;
    return n.find("..") == std::string::npos;
}

static std::vector<std::string> list_dir(const char *dir, const char *ext) {
    std::vector<std::string> v;
    DIR *d = opendir(dir);
    if (!d) return v;
    size_t el = std::strlen(ext);
    while (dirent *e = readdir(d)) {
        std::string n = e->d_name;
        if (n.size() > el && n.compare(n.size() - el, el, ext) == 0) v.push_back(n);
    }
    closedir(d);
    std::sort(v.begin(), v.end());
    return v;
}

static bool load_scenario_locked(const std::string &name) {
    if (!safe_name(name)) return false;
    std::string txt;
    if (!read_file(std::string(SCEN_DIR) + "/" + name + ".txt", txt)) return false;
    // Zuerst stummschalten: Frequenzen des alten Szenarios bekommen 0.0
    auto mute = g_eng.mute_all();
    g_pending_mute.insert(g_pending_mute.end(), mute.begin(), mute.end());
    int bad = 0;
    int n = g_scen.load_text(txt, &bad);
    g_scen.name = name;
    ESP_LOGI(TAG, "Szenario %s: %d Sender, %d fehlerhafte Zeilen", name.c_str(), n, bad);
    std::strncpy(cfg().scenario, name.c_str(), sizeof cfg().scenario - 1);
    g_dirty = true;
    return n > 0;
}

static bool load_antenna_locked(const std::string &file) {
    if (file.empty()) { g_ant.reset(); cfg().antenna[0] = 0; g_dirty = true; return true; }
    if (!safe_name(file)) return false;
    std::string txt;
    if (!read_file(std::string(ANT_DIR) + "/" + file, txt)) return false;
    g_ant.load_text(txt, file);
    if (!g_ant.active()) return false;
    g_ant.set_heading(cfg().heading);
    std::strncpy(cfg().antenna, file.c_str(), sizeof cfg().antenna - 1);
    g_dirty = true;
    return true;
}

// ---------------------------------------------------------------- Zustand -> JSON
static const char *mode_str(amp::PathMode m) {
    return m == amp::PathMode::Ground ? "G" : (m == amp::PathMode::Sky ? "S" : "GS");
}

static std::string build_state_locked() {
    std::string o;
    o.reserve(512 + g_last.results.size() * 40);
    json_appendf(o, "{\"d\":\"%04d-%02d-%02d\",\"h\":%.4f,\"sun\":%.1f,\"m\":%d,\"sp\":%d,",
                 g_clk.year(), g_clk.month(), g_clk.day(), g_clk.hours(), (double)g_last.sun_alt_rx,
                 (int)g_eng.model(), g_speed);
    json_appendf(o, "\"rx\":[%.5f,%.5f],\"hdg\":%.1f,\"sens\":%.1f,\"gm\":%d,\"gw\":%d,\"ntp\":%d,",
                 (double)g_rx_lat, (double)g_rx_lon, (double)g_ant.heading(), (double)g_par.sens,
                 (int)g_par.gain_mode, (int)g_par.gain_window, (int)cfg().use_ntp);
    o += "\"scen\":\"" + json_escape(g_scen.name) + "\",\"ant\":\"" + json_escape(g_ant.active() ? g_ant.filename() : "") + "\",";
    if (g_storm.active)
        json_appendf(o, "\"storm\":{\"lat\":%.4f,\"lon\":%.4f,\"spd\":%.0f,\"hdg\":%.0f,\"life\":%.0f,\"amp\":%d,\"rate\":%d},",
                     (double)g_storm.lat, (double)g_storm.lon, (double)g_storm.params.speed_kmh,
                     (double)g_storm.params.heading_deg, (double)g_storm.life_min, g_storm.params.amp, g_storm.params.rate);
    else
        o += "\"storm\":null,";
    if (g_hold) json_appendf(o, "\"hold\":%.4f,", (double)g_hold_val);
    else o += "\"hold\":null,";
    json_appendf(o, "\"udp\":{\"ok\":%d,\"sent\":%u,\"err\":%u,\"dst\":\"%s\"},",
                 udp_out_ok() ? 1 : 0, udp_out_sent(), udp_out_errors(), udp_out_target().c_str());
    json_appendf(o, "\"net\":{\"ip\":\"%s\",\"sta\":%d,\"ap\":%d,\"time\":%d},\"st\":[",
                 net_ip().c_str(), net_connected() ? 1 : 0, net_setup_ap_active() ? 1 : 0, net_time_valid() ? 1 : 0);
    bool first = true;
    for (const auto &r : g_last.results) {
        json_appendf(o, "%s[%d,%.1f,%.1f,%.1f,\"%s\",%d]", first ? "" : ",", r.site, (double)r.now,
                     (double)r.fs_g, (double)r.fs_s, mode_str(r.mode), r.audible ? 1 : 0);
        first = false;
    }
    o += "]}";
    return o;
}

// ---------------------------------------------------------------- Task
static void sim_task(void *) {
    int64_t last_us = esp_timer_get_time();
    int64_t last_run_us = 0;
    int last_minute = -1;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        int64_t now_us = esp_timer_get_time();
        double dt = (double)(now_us - last_us) / 1e6;
        last_us = now_us;

        std::vector<amp::GainMsg> msgs;
        std::vector<std::string> sferics;
        std::string state;
        std::vector<int> burst_f;
        float burst_v = 0;
        {
            std::lock_guard<std::mutex> l(g_mx);
            // --- Uhr
            if (g_speed == 1 && cfg().use_ntp && net_time_valid()) {
                time_t t = time(nullptr);
                tm tmv;
                gmtime_r(&t, &tmv);
                g_clk.set(tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                          tmv.tm_hour + tmv.tm_min / 60.0 + tmv.tm_sec / 3600.0);
            } else {
                g_clk.advance_seconds(dt * g_speed);
            }
            // --- Sturm (Bewegung in Simulationszeit, Sferics-Rate in Echtzeit)
            g_storm.move((amp::real)(dt * g_speed));
            amp::Sferic s = g_storm.tick(g_rx_lat, g_rx_lon, g_speed > 1, g_rng);
            if (s.fire) sferics.push_back(s.text());

            // --- Rechenlauf: bei Aenderung (max. 5/s), im Takt der Simulationsminute bzw. jede Sekunde
            int minute = (int)(g_clk.hours() * 60.0);
            bool tick = g_speed == 1 ? (minute != last_minute) : (now_us - last_run_us >= 1000000);
            bool run = !g_hold && g_burst_next < 0 && ((g_dirty && now_us - last_run_us >= 200000) || tick);
            // --- Traegertest: 1601 Pakete in Haeppchen von 100 je 100 ms (schont den UDP-Puffer)
            if (g_burst_next >= 0) {
                burst_v = g_burst_val;
                for (int n = 0; n < 100 && g_burst_next >= 0; ++n) {
                    burst_f.push_back(g_burst_next);
                    g_burst_next += 1000;
                    if (g_burst_next > 1700000) { g_burst_next = -1; g_dirty = true; g_force_state = true; }
                }
            }
            if (run && !g_scen.sites.empty()) {
                g_dirty = false;
                last_minute = minute;
                last_run_us = now_us;
                g_eng.run(g_scen, g_rx_lat, g_rx_lon, g_clk.n_day(), (amp::real)g_clk.hours(), g_ant, g_par, true, g_last);
                msgs.swap(g_last.msgs);
                if (!g_pending_mute.empty()) {
                    // Stummschaltung alter Sender: nur Frequenzen, die jetzt nicht wieder aktiv sind
                    for (auto &m : g_pending_mute) {
                        bool again = false;
                        for (auto &x : msgs) if (x.freq_hz == m.freq_hz) { again = true; break; }
                        if (!again) msgs.push_back(m);
                    }
                    g_pending_mute.clear();
                }
                state = build_state_locked();
            }
            if (state.empty() && g_force_state) { g_force_state = false; state = build_state_locked(); }
            // --- Konfiguration verzoegert sichern (Schonung des Flash)
            if (g_save_pending && now_us >= g_save_due_us) {
                g_save_pending = false;
                cfg().rx_lat = g_rx_lat; cfg().rx_lon = g_rx_lon;
                cfg().heading = (float)g_ant.heading();
                cfg().model = (uint8_t)g_eng.model();
                cfg().speed = (uint16_t)g_speed;
                cfg().sens = (float)g_par.sens;
                cfg().gain_mode = (uint8_t)g_par.gain_mode;
                cfg().gain_window = (uint8_t)g_par.gain_window;
                config_save();
            }
        }
        for (int f : burst_f) udp_send_gain(amp::format_gain_msg({f, burst_v}));
        for (auto &m : msgs) udp_send_gain(amp::format_gain_msg(m));
        for (auto &s : sferics) udp_send_sferic(s);
        if (!state.empty() && g_cb) g_cb(state);
    }
}

// ---------------------------------------------------------------- Oeffentliche API
void init() {
    g_eng_p = new amp::Engine();
    {
        std::lock_guard<std::mutex> l(g_mx);
        AppConfig &c = cfg();
        g_rx_lat = c.rx_lat; g_rx_lon = c.rx_lon;
        g_speed = c.speed ? c.speed : 1;
        g_eng.set_model((amp::ModelId)c.model);
        g_par.sens = c.sens;
        g_par.gain_mode = c.gain_mode ? amp::GainMode::Physical : amp::GainMode::Compat;
        g_par.gain_window = (amp::real)(c.gain_window < 20 ? 20 : (c.gain_window > 32 ? 32 : c.gain_window));
        g_rng.seed(esp_random());
        if (!load_scenario_locked(c.scenario)) {
            auto l = list_dir(SCEN_DIR, ".txt");
            if (!l.empty()) load_scenario_locked(l[0].substr(0, l[0].size() - 4));
        }
        if (c.antenna[0]) load_antenna_locked(c.antenna);
        g_ant.set_heading(c.heading);
        g_clk.set(1975, 10, 15, 12.0);   // wird bei Tempo x1 + NTP sofort von der echten UTC-Zeit ersetzt
    }
    xTaskCreatePinnedToCore(sim_task, "sim", 8192, nullptr, 5, nullptr, 1);
}

void set_state_callback(StateCallback cb) { g_cb = cb; }

void carriers(float gain) {
    if (!(gain >= 0.f)) gain = 0.f;
    if (gain > 1.f) gain = 1.f;
    std::lock_guard<std::mutex> l(g_mx);
    g_hold = true; g_hold_val = gain;
    g_burst_val = gain; g_burst_next = 100000;
    g_force_state = true;
}
void carriers_release() {
    std::lock_guard<std::mutex> l(g_mx);
    if (!g_hold) return;
    g_hold = false;
    g_eng.mute_all();                           // Engine vergisst ihre aktiven Sender
    g_burst_val = 0; g_burst_next = 100000;     // sauberer Start: alles 0.0, danach rechnet die Simulation wieder
    g_dirty = true; g_force_state = true;
}
void request_save() {
    g_save_pending = true;
    g_save_due_us = esp_timer_get_time() + 5000000;   // 5 s nach der letzten Aenderung
}

void set_rx(float lat, float lon) {
    if (!(lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180)) return;
    std::lock_guard<std::mutex> l(g_mx);
    g_rx_lat = lat; g_rx_lon = lon; g_dirty = true;
    request_save();
}
void set_heading(float deg) {
    std::lock_guard<std::mutex> l(g_mx);
    g_ant.set_heading(deg); cfg().heading = (float)g_ant.heading(); g_dirty = true;
    request_save();
}
void set_model(int id) {
    if (id < 0 || id >= amp::MODEL_COUNT) return;
    std::lock_guard<std::mutex> l(g_mx);
    g_eng.set_model((amp::ModelId)id); g_dirty = true;
    request_save();
}
void set_speed(int sp) {
    if (sp != 1 && sp != 60 && sp != 600 && sp != 3600) return;
    std::lock_guard<std::mutex> l(g_mx);
    g_speed = sp; g_dirty = true;
    request_save();
}
void set_clock(int y, int mo, int d, float h) {
    std::lock_guard<std::mutex> l(g_mx);
    g_clk.set(y, mo, d, h);
    cfg().use_ntp = 0;          // wie das Bewegen des Zeitreglers im Original: ab jetzt eigene Zeit
    g_dirty = true;
    request_save();
}
void follow_ntp(bool on) {
    std::lock_guard<std::mutex> l(g_mx);
    cfg().use_ntp = on ? 1 : 0; g_dirty = true;
    request_save();
}
bool set_scenario(const std::string &name) {
    std::lock_guard<std::mutex> l(g_mx);
    bool ok = load_scenario_locked(name);
    if (ok) request_save();
    return ok;
}
bool set_antenna(const std::string &file) {
    std::lock_guard<std::mutex> l(g_mx);
    bool ok = load_antenna_locked(file);
    if (ok) request_save();
    return ok;
}
void set_params(float sens, int gm, int gw) {
    std::lock_guard<std::mutex> l(g_mx);
    if (sens >= -20 && sens <= 80) g_par.sens = sens;
    g_par.gain_mode = gm ? amp::GainMode::Physical : amp::GainMode::Compat;
    if (gw >= 20 && gw <= 32) g_par.gain_window = (amp::real)gw;
    g_dirty = true;
    request_save();
}
void storm_start(float lat, float lon, int profile) {
    std::lock_guard<std::mutex> l(g_mx);
    amp::StormParams p = amp::profile_params(profile == 0 ? amp::StormProfile::Weak
                                             : profile == 2 ? amp::StormProfile::Strong : amp::StormProfile::Medium);
    p.speed_kmh = g_storm.params.speed_kmh; p.heading_deg = g_storm.params.heading_deg;
    g_storm.params = p;
    g_storm.start(lat, lon);
}
void storm_set(float speed_kmh, float heading_deg) {
    std::lock_guard<std::mutex> l(g_mx);
    g_storm.params.speed_kmh = speed_kmh < 0 ? 0 : (speed_kmh > 200 ? 200 : speed_kmh);
    g_storm.params.heading_deg = heading_deg;
}
void storm_stop() { std::lock_guard<std::mutex> l(g_mx); g_storm.stop(); }

// ---------------------------------------------------------------- JSON
std::string state_json() {
    std::lock_guard<std::mutex> l(g_mx);
    return build_state_locked();
}

std::string scenario_json() {
    std::lock_guard<std::mutex> l(g_mx);
    std::string o = "{\"name\":\"" + json_escape(g_scen.name) + "\",\"sites\":[";
    bool first = true;
    for (const auto &s : g_scen.sites) {
        json_appendf(o, "%s[%.4f,%.4f,%g,%g,\"", first ? "" : ",", (double)s.lat, (double)s.lon, (double)s.freq, (double)s.kw);
        o += json_escape(s.name) + "\"]";
        first = false;
    }
    o += "]}";
    return o;
}

std::string lists_json() {
    std::string o = "{\"scenarios\":[";
    auto sc = list_dir(SCEN_DIR, ".txt");
    for (size_t i = 0; i < sc.size(); ++i)
        o += (i ? ",\"" : "\"") + json_escape(sc[i].substr(0, sc[i].size() - 4)) + "\"";
    o += "],\"antennas\":[";
    auto an = list_dir(ANT_DIR, ".msi");
    auto an2 = list_dir(ANT_DIR, ".ant");
    an.insert(an.end(), an2.begin(), an2.end());
    for (size_t i = 0; i < an.size(); ++i) o += (i ? ",\"" : "\"") + json_escape(an[i]) + "\"";
    o += "],\"models\":[";
    for (int i = 0; i < amp::MODEL_COUNT; ++i)
        o += std::string(i ? ",\"" : "\"") + amp::model_name((amp::ModelId)i) + "\"";
    o += "]}";
    return o;
}

std::string antenna_json() {
    std::lock_guard<std::mutex> l(g_mx);
    if (!g_ant.active()) return "{\"name\":\"\",\"pattern\":[]}";
    std::string o = "{\"name\":\"" + json_escape(g_ant.filename()) + "\",\"pattern\":[";
    for (int i = 0; i < 360; ++i) json_appendf(o, "%s%.1f", i ? "," : "", (double)g_ant.pattern()[(size_t)i]);
    o += "]}";
    return o;
}

std::string dfscope_config_json() {
    std::lock_guard<std::mutex> l(g_mx);
    if (!g_ant.active()) return "{\"status\": \"NO_ANTENNA_LOADED\"}";
    std::string o = "{\"status\": \"OK\", \"filename\": \"" + json_escape(g_ant.filename()) + "\", \"pattern\": {";
    for (int i = 0; i < 360; ++i) json_appendf(o, "%s\"%d\": %.2f", i ? ", " : "", i, (double)g_ant.pattern()[(size_t)i]);
    o += "}}";
    return o;
}

std::string dfscope_transmitters_json() {
    std::lock_guard<std::mutex> l(g_mx);
    std::string o = "{\"status\": \"OK\", \"transmitters\": [";
    bool first = true;
    for (const auto &s : g_scen.sites) {
        json_appendf(o, "%s{\"lat\": %.5f, \"lon\": %.5f, \"freq\": \"%g\"}", first ? "" : ", ",
                     (double)s.lat, (double)s.lon, (double)s.freq);
        first = false;
    }
    o += "]}";
    return o;
}

}  // namespace sim
