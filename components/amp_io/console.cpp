// Serielle Konsole auf UART0 (115200 8N1). "help" listet die Befehle.
#include "amp_io.hpp"
#include "amp_config.hpp"
#include "amp_net.hpp"
#include "sim.hpp"
#include "amp/model.hpp"
#include "driver/uart.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static void out(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void out(const char *fmt, ...) {
    char b[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (n > 0) uart_write_bytes(UART_NUM_0, b, (size_t)(n < (int)sizeof b ? n : (int)sizeof b - 1));
}

// Feste Texte direkt ausgeben (kein Formatpuffer, nichts wird abgeschnitten).
static void outs(const char *t) { uart_write_bytes(UART_NUM_0, t, std::strlen(t)); }

static std::vector<std::string> words(const std::string &l) {
    std::vector<std::string> v;
    size_t i = 0;
    while (i < l.size()) {
        while (i < l.size() && l[i] == ' ') ++i;
        size_t j = i;
        while (j < l.size() && l[j] != ' ') ++j;
        if (j > i) v.push_back(l.substr(i, j - i));
        i = j;
    }
    return v;
}

static void help() {
    static const char *const lines[] = {
        "Befehle:",
        "  status                      Zustand",
        "  model 0|1|2                 Klassisch | Klassisch+ | ITU-R P.1147-2",
        "  speed 1|60|600|3600         Simulationsgeschwindigkeit",
        "  date T.M.J [HH:MM]          Datum/Uhrzeit setzen (UTC, schaltet NTP ab)",
        "  ntp on|off                  Uhr folgt echter UTC-Zeit (nur Tempo x1)",
        "  rx LAT LON                  Empfaengerposition",
        "  hdg GRAD                    Antennenkurs",
        "  scen NAME | ant DATEI|none  Szenario / Antennendiagramm",
        "  thr DB | gain compat|phys | win 20..32",
        "  storm LAT LON [0|1|2] | storm spd KMH HDG | storm off",
        "  carriers 0|1|WERT | auto    alle Traeger auf Gain (Test), 'auto' = Simulation weiter",
        "  modip HOST                  Ziel (IPv4 oder Name) der UDP-Pakete",
        "  wifi SSID [PASS]            WLAN-Zugang speichern (danach 'reboot')",
        "  jumpers on|off              Jumper als Einstellquelle",
        "  reboot",
    };
    for (const char *l : lines) { outs(l); outs("\r\n"); }
}

static void handle(const std::string &line) {
    auto w = words(line);
    if (w.empty()) return;
    const std::string &c = w[0];
    AppConfig &k = cfg();
    auto num = [&](size_t i, double def = 0) { return i < w.size() ? std::atof(w[i].c_str()) : def; };

    if (c == "help" || c == "?") help();
    else if (c == "status") {
        out("WLAN: %s  IP %s  Setup-AP: %s  Zeit(NTP): %s\r\n", net_connected() ? "verbunden" : "getrennt",
            net_ip().c_str(), net_setup_ap_active() ? "ja" : "nein", net_time_valid() ? "gueltig" : "nein");
        out("Modulator: %s -> %s  gesendet %u, Fehler %u, %s\r\n", k.mod_host, udp_out_target().c_str(),
            udp_out_sent(), udp_out_errors(), udp_out_ok() ? "ok" : "KEIN ZIEL/FEHLER");
        out("Modell %d (%s), Tempo x%u, Szenario %s, Antenne %s, Schwelle %.1f dB, Gain %s/%u dB\r\n", k.model,
            amp::model_name((amp::ModelId)k.model), (unsigned)k.speed, k.scenario, k.antenna[0] ? k.antenna : "-",
            (double)k.sens, k.gain_mode ? "physikalisch" : "kompatibel", (unsigned)k.gain_window);
        out("RX %.4f, %.4f  Kurs %.1f  Jumper %s  NTP %s\r\n", (double)k.rx_lat, (double)k.rx_lon, (double)k.heading,
            k.jumpers ? "an" : "aus", k.use_ntp ? "an" : "aus");
    }
    else if (c == "model" && w.size() > 1) sim::set_model(std::atoi(w[1].c_str()));
    else if (c == "speed" && w.size() > 1) sim::set_speed(std::atoi(w[1].c_str()));
    else if (c == "date" && w.size() > 1) {
        int d = 0, m = 0, y = 0, hh = 12, mm = 0;
        if (std::sscanf(w[1].c_str(), "%d.%d.%d", &d, &m, &y) == 3) {
            if (w.size() > 2) std::sscanf(w[2].c_str(), "%d:%d", &hh, &mm);
            sim::set_clock(y, m, d, (float)(hh + mm / 60.0));
            out("ok\r\n");
        } else out("Format: date 15.10.1975 17:30\r\n");
    }
    else if (c == "ntp" && w.size() > 1) sim::follow_ntp(w[1] == "on");
    else if (c == "rx" && w.size() > 2) sim::set_rx((float)num(1), (float)num(2));
    else if (c == "hdg" && w.size() > 1) sim::set_heading((float)num(1));
    else if (c == "scen" && w.size() > 1) out(sim::set_scenario(w[1]) ? "ok\r\n" : "Szenario nicht gefunden\r\n");
    else if (c == "ant" && w.size() > 1) out(sim::set_antenna(w[1] == "none" ? "" : w[1]) ? "ok\r\n" : "Antenne nicht gefunden\r\n");
    else if (c == "thr" && w.size() > 1) { sim::set_params((float)num(1), k.gain_mode, k.gain_window); k.sens = (float)num(1); }
    else if (c == "gain" && w.size() > 1) { k.gain_mode = w[1] == "phys" ? 1 : 0; sim::set_params(k.sens, k.gain_mode, k.gain_window); }
    else if (c == "win" && w.size() > 1) { k.gain_window = (uint8_t)num(1, 28); sim::set_params(k.sens, k.gain_mode, k.gain_window); }
    else if (c == "storm" && w.size() > 1) {
        if (w[1] == "off") sim::storm_stop();
        else if (w[1] == "spd" && w.size() > 3) sim::storm_set((float)num(2), (float)num(3));
        else if (w.size() > 2) sim::storm_start((float)num(1), (float)num(2), (int)num(3, 1));
    }
    else if (c == "carriers" && w.size() > 1) {
        if (w[1] == "auto") { sim::carriers_release(); out("Simulation laeuft wieder\r\n"); }
        else {
            float g = (float)num(1);
            if (g < 0.f || g > 1.f) out("Wert 0.0 .. 1.0 oder 'auto'\r\n");
            else { sim::carriers(g); out("alle Traeger auf %.3f, Simulation pausiert ('carriers auto' = weiter)\r\n", (double)g); }
        }
    }
    else if (c == "modip" && w.size() > 1) {
        std::strncpy(k.mod_host, w[1].c_str(), sizeof k.mod_host - 1);
        config_save();
        udp_out_set_target(k.mod_host);
        out("Ziel gespeichert\r\n");
    }
    else if (c == "wifi" && w.size() == 1)
        out("WLAN-SSID: %s  (setzen: wifi SSID [PASS], danach reboot)\r\n", k.ssid[0] ? k.ssid : "-");
    else if (c == "wifi") {
        std::strncpy(k.ssid, w[1].c_str(), sizeof k.ssid - 1);
        std::strncpy(k.pass, w.size() > 2 ? w[2].c_str() : "", sizeof k.pass - 1);
        config_save();
        out("gespeichert, 'reboot' zum Verbinden\r\n");
    }
    else if (c == "jumpers" && w.size() == 1) out("Jumper: %s\r\n", k.jumpers ? "an" : "aus");
    else if (c == "jumpers") { k.jumpers = w[1] == "on"; config_save(); }
    else if (c == "reboot") esp_restart();
    else out("unbekannt, 'help' zeigt die Befehle\r\n");
    sim::request_save();
}

static void console_task(void *) {
    std::string line;
    uint8_t ch;
    out("\r\nAM Propagation Box - 'help' fuer Befehle\r\n> ");
    for (;;) {
        if (uart_read_bytes(UART_NUM_0, &ch, 1, portMAX_DELAY) != 1) continue;
        if (ch == '\r' || ch == '\n') {
            if (!line.empty()) { out("\r\n"); handle(line); line.clear(); }
            out("> ");
        } else if (ch == 0x7f || ch == 0x08) {
            if (!line.empty()) { line.pop_back(); out("\b \b"); }
        } else if (ch >= 0x20 && line.size() < 120) {
            line += (char)ch;
            uart_write_bytes(UART_NUM_0, (const char *)&ch, 1);   // Echo
        }
    }
}

void console_start() {
    uart_driver_install(UART_NUM_0, 512, 0, 0, nullptr, 0);
    xTaskCreatePinnedToCore(console_task, "console", 4096, nullptr, 1, nullptr, 0);
}
