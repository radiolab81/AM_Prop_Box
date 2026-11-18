// Host-Harness: laeuft den ECHTEN sim.cpp (Task, Szenario/Antenne aus /fs, JSON, UDP-Nachrichten)
// am PC. amp_net-Funktionen sind Attrappen, die die gesendeten Pakete mitschreiben.
#include "sim.hpp"
#include "amp_config.hpp"
#include "amp_net.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

static std::mutex g_m;
static std::vector<std::string> g_gain, g_sf;
static std::atomic<unsigned> g_sent{0};
std::string net_ip() { return "192.168.1.77"; }
bool net_connected() { return true; }
bool net_setup_ap_active() { return false; }
bool net_time_valid() { return false; }
std::string udp_out_target() { return "192.168.1.50"; }
void udp_send_gain(const std::string &m) { std::lock_guard<std::mutex> l(g_m); g_gain.push_back(m); ++g_sent; }
void udp_send_sferic(const std::string &m) { std::lock_guard<std::mutex> l(g_m); g_sf.push_back(m); ++g_sent; }
unsigned udp_out_sent() { return g_sent; }
unsigned udp_out_errors() { return 0; }
bool udp_out_ok() { return true; }

static void wr(const char *path, const std::string &s) { FILE *f = std::fopen(path, "w"); std::fwrite(s.data(), 1, s.size(), f); std::fclose(f); }

#ifdef CARRIER_TEST
// Traegertest: Anzahl, Reihenfolge, Pause der Simulation, sauberes Fortsetzen.
#define EXPECT(c, m) do { if (!(c)) { std::printf("FEHLER: %s\n", m); return 1; } } while (0)
int main() {
    config_load();
    cfg().speed = 1;
    sim::init();
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    { std::lock_guard<std::mutex> l(g_m); g_gain.clear(); }
    sim::carriers(1.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    std::vector<std::string> v;
    { std::lock_guard<std::mutex> l(g_m); v = g_gain; g_gain.clear(); }
    std::printf("carriers(1.0): %zu Pakete, erstes %s, letztes %s\n", v.size(), v.front().c_str(), v.back().c_str());
    EXPECT(v.size() == 1601, "1601 Pakete erwartet");
    EXPECT(v.front() == "100000:1.0" || v.front().rfind("100000:1", 0) == 0, "erstes Paket");
    EXPECT(v.back().rfind("1700000:1", 0) == 0, "letztes Paket");
    EXPECT(sim::state_json().find("\"hold\":1.0") != std::string::npos, "hold im Zustand");
    // waehrend der Pause darf die Simulation nichts senden (auch nicht bei Aenderung)
    sim::set_rx(48.0f, 11.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    { std::lock_guard<std::mutex> l(g_m); EXPECT(g_gain.empty(), "Simulation muss pausieren"); }
    sim::carriers(0.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    { std::lock_guard<std::mutex> l(g_m); EXPECT(g_gain.size() == 1601, "zweiter Test 1601 Pakete"); EXPECT(g_gain.front().rfind("100000:0", 0) == 0, "0.0"); g_gain.clear(); }
    sim::carriers_release();
    std::this_thread::sleep_for(std::chrono::milliseconds(4000));
    { std::lock_guard<std::mutex> l(g_m);
      std::printf("nach auto: %zu Pakete (1601 Nullen + Simulation)\n", g_gain.size());
      EXPECT(g_gain.size() > 1601, "Nullen + Simulationspakete erwartet");
      EXPECT(g_gain[1600].rfind("1700000:0", 0) == 0, "Nullen komplett vor der Simulation");
      bool audible = false; for (size_t i = 1601; i < g_gain.size(); ++i) if (g_gain[i].find(":0.0") == std::string::npos) audible = true;
      EXPECT(audible, "Simulation sendet wieder hoerbare Sender"); }
    EXPECT(sim::state_json().find("\"hold\":null") != std::string::npos, "hold aufgehoben");
    std::puts("Traegertest OK");
    return 0;
}
#else
int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : "/tmp";
    config_load();
    cfg().speed = 60;               // Zeitraffer: ein Lauf je Sekunde
    sim::init();
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    sim::set_clock(1975, 10, 15, 20.0f);
    sim::set_rx(50.1f, 8.6f);
    sim::set_antenna("Antenna_Ferrite_Rod_Loop.msi");
    sim::set_heading(37.3f);
    sim::storm_start(50.5f, 9.0f, 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(3500));
    std::string base = out;
    wr((base + "/state.json").c_str(), sim::state_json());
    wr((base + "/scenario.json").c_str(), sim::scenario_json());
    wr((base + "/lists.json").c_str(), sim::lists_json());
    wr((base + "/antenna.json").c_str(), sim::antenna_json());
    wr((base + "/dfconfig.json").c_str(), sim::dfscope_config_json());
    wr((base + "/dftx.json").c_str(), sim::dfscope_transmitters_json());
    std::lock_guard<std::mutex> l(g_m);
    std::printf("gain-Pakete: %zu, sferics: %zu\n", g_gain.size(), g_sf.size());
    for (size_t i = 0; i < g_gain.size() && i < 3; ++i) std::printf("  gain[%zu] = %s\n", i, g_gain[i].c_str());
    for (size_t i = 0; i < g_sf.size() && i < 3; ++i) std::printf("  sferic[%zu] = %s\n", i, g_sf[i].c_str());
    return 0;
}
#endif
