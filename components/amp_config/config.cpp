#include "amp_config.hpp"
#include "nvs.h"
#include "esp_log.h"
#include <cstring>

static const char *TAG = "config";
static AppConfig g_cfg;

AppConfig &cfg() { return g_cfg; }

void config_defaults(AppConfig &c) {
    std::memset(&c, 0, sizeof c);
    c.magic = CFG_MAGIC;
    std::strcpy(c.mod_host, "192.168.1.50");
    c.port_gain = 8888;
    c.port_sferics = 8889;
    c.model = 0;
    c.gain_mode = 0;
    c.gain_window = 28;
    c.jumpers = 1;
    c.use_ntp = 1;
    c.speed = 1;
    c.sens = 20.0f;
    c.rx_lat = 50.1f;
    c.rx_lon = 8.6f;
    c.heading = 0;
    std::strcpy(c.scenario, "1975_WRTH");
    std::strcpy(c.hostname, "amprop");
    std::strcpy(c.ap_pass, "amprop-setup");
}

void config_load() {
    config_defaults(g_cfg);
    nvs_handle_t h;
    if (nvs_open("amprop", NVS_READONLY, &h) != ESP_OK) { ESP_LOGW(TAG, "keine gespeicherte Konfiguration"); return; }
    AppConfig tmp;
    size_t len = sizeof tmp;
    if (nvs_get_blob(h, "cfg", &tmp, &len) == ESP_OK && len == sizeof tmp && tmp.magic == CFG_MAGIC) {
        g_cfg = tmp;
        // Zeichenketten sicher terminieren
        g_cfg.ssid[sizeof g_cfg.ssid - 1] = 0;  g_cfg.pass[sizeof g_cfg.pass - 1] = 0;
        g_cfg.mod_host[sizeof g_cfg.mod_host - 1] = 0;
        g_cfg.scenario[sizeof g_cfg.scenario - 1] = 0; g_cfg.antenna[sizeof g_cfg.antenna - 1] = 0;
        g_cfg.hostname[sizeof g_cfg.hostname - 1] = 0; g_cfg.ap_pass[sizeof g_cfg.ap_pass - 1] = 0;
        ESP_LOGI(TAG, "Konfiguration geladen");
    } else {
        ESP_LOGW(TAG, "Konfiguration unbrauchbar (Groesse/Version), Defaults");
    }
    nvs_close(h);
}

bool config_save() {
    nvs_handle_t h;
    if (nvs_open("amprop", NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_blob(h, "cfg", &g_cfg, sizeof g_cfg) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    if (!ok) ESP_LOGE(TAG, "Speichern fehlgeschlagen");
    return ok;
}
