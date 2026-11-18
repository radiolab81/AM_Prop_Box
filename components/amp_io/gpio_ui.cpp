// Jumper, Taster, Status-LED (klassischer ESP32, DevKit-Belegung; Pins unten anpassbar).
// Jumper schliessen nach GND (interner Pull-up), geschlossen = 1.
//   Modell  : GPIO 32 (Bit0), 33 (Bit1)   00 Klassisch, 01 Klassisch+, 10 ITU-R P.1147-2
//   Tempo   : GPIO 25 (Bit0), 26 (Bit1)   00 x1, 01 x60, 10 x600, 11 x3600
//   Szenario: GPIO 13, 16, 17 (Bit0..2)   Nummer in der alphabetischen Liste der Szenario-Dateien
//   Taster  : GPIO 0 (BOOT-Taste)         kurz = Gain-Modus umschalten, beim Start gehalten = Setup-AP
//   LED     : GPIO 2                      Blinkmuster siehe led_task
// Vermieden: Strapping-Pins 5, 12, 15 und Flash-Pins 6..11; GPIO 34..39 haben keinen Pull-up.
#include "amp_io.hpp"
#include "amp_config.hpp"
#include "amp_net.hpp"
#include "sim.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <dirent.h>
#include <algorithm>
#include <string>
#include <vector>

static const char *TAG = "gpio";
static const gpio_num_t PIN_MODEL[2] = {GPIO_NUM_32, GPIO_NUM_33};
static const gpio_num_t PIN_SPEED[2] = {GPIO_NUM_25, GPIO_NUM_26};
static const gpio_num_t PIN_SCEN[3] = {GPIO_NUM_13, GPIO_NUM_16, GPIO_NUM_17};
static const gpio_num_t PIN_BTN = GPIO_NUM_0;
static const gpio_num_t PIN_LED = GPIO_NUM_2;
static const int SPEEDS[4] = {1, 60, 600, 3600};

static void in_pin(gpio_num_t p) {
    gpio_config_t c = {};
    c.pin_bit_mask = 1ULL << p;
    c.mode = GPIO_MODE_INPUT;
    c.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&c);
}

static int read_bits(const gpio_num_t *pins, int n) {
    int v = 0;
    for (int i = 0; i < n; ++i) if (gpio_get_level(pins[i]) == 0) v |= 1 << i;
    return v;
}

void gpio_ui_init() {
    for (auto p : PIN_MODEL) in_pin(p);
    for (auto p : PIN_SPEED) in_pin(p);
    for (auto p : PIN_SCEN) in_pin(p);
    in_pin(PIN_BTN);
    gpio_config_t o = {};
    o.pin_bit_mask = 1ULL << PIN_LED;
    o.mode = GPIO_MODE_OUTPUT;
    gpio_config(&o);
}

bool gpio_setup_requested() { return gpio_get_level(PIN_BTN) == 0; }

static std::string scenario_by_index(int idx) {
    std::vector<std::string> v;
    if (DIR *d = opendir("/fs/scenarios")) {
        while (dirent *e = readdir(d)) {
            std::string n = e->d_name;
            if (n.size() > 4 && n.compare(n.size() - 4, 4, ".txt") == 0) v.push_back(n.substr(0, n.size() - 4));
        }
        closedir(d);
    }
    std::sort(v.begin(), v.end());
    return idx < (int)v.size() ? v[(size_t)idx] : std::string();
}

static void ui_task(void *) {
    int model = -1, speed = -1, scen = -1;      // zuletzt angewendeter Jumperzustand
    int stable_cnt = 0, cand = -1;
    int btn_prev = 1;
    int tick = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(50));
        ++tick;

        // --- Jumper: Aenderung erst nach 3 gleichen Lesungen (150 ms) uebernehmen
        if (cfg().jumpers) {
            int m = read_bits(PIN_MODEL, 2), s = read_bits(PIN_SPEED, 2), c = read_bits(PIN_SCEN, 3);
            int code = m | (s << 2) | (c << 4);
            if (code == cand) ++stable_cnt; else { cand = code; stable_cnt = 0; }
            if (stable_cnt == 3 && (m != model || s != speed || c != scen)) {
                // Beim ersten Mal (Start) setzen die Jumper alles; spaeter nur, was sich geaendert hat
                if (model < 0 || m != model) { if (m < 3) sim::set_model(m); }
                if (speed < 0 || s != speed) sim::set_speed(SPEEDS[s]);
                if (scen < 0 || c != scen) { std::string n = scenario_by_index(c); if (!n.empty()) sim::set_scenario(n); }
                ESP_LOGI(TAG, "Jumper: Modell %d, Tempo x%d, Szenario %d", m, SPEEDS[s], c);
                model = m; speed = s; scen = c;
            }
        }

        // --- Taster: kurzer Druck schaltet den Gain-Modus
        int b = gpio_get_level(PIN_BTN);
        if (btn_prev == 0 && b == 1) {
            AppConfig &c = cfg();
            c.gain_mode = c.gain_mode ? 0 : 1;
            sim::set_params(c.sens, c.gain_mode, c.gain_window);
            ESP_LOGI(TAG, "Gain-Modus: %s", c.gain_mode ? "physikalisch" : "kompatibel");
        }
        btn_prev = b;

        // --- LED: Dauerlicht = Verbindung und UDP-Ziel ok; langsames Blinken = kein WLAN;
        //     schnelles Blinken = WLAN ok, aber Modulator nicht erreichbar/kein Ziel; Doppelblitz = Setup-AP
        bool on;
        if (net_setup_ap_active()) on = (tick % 20) < 2 || ((tick % 20) >= 4 && (tick % 20) < 6);
        else if (!net_connected()) on = (tick % 20) < 10;
        else if (!udp_out_ok()) on = (tick % 4) < 2;
        else on = true;
        gpio_set_level(PIN_LED, on ? 1 : 0);
    }
}

void gpio_ui_start() { xTaskCreatePinnedToCore(ui_task, "ui", 3072, nullptr, 2, nullptr, 0); }
