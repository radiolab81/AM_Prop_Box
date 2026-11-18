#include "amp_net.hpp"
#include "amp_config.hpp"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <cstdlib>
#include <cstring>
#include <ctime>

static const char *TAG = "net";
static EventGroupHandle_t s_ev;
static constexpr int BIT_GOT_IP = BIT0;
static esp_netif_t *s_sta = nullptr, *s_ap = nullptr;
static volatile bool s_connected = false, s_ap_on = false;
static int s_fail = 0;
static bool s_force_setup = false;
static char s_ip[16] = "0.0.0.0";

static void start_ap() {
    if (s_ap_on) return;
    wifi_config_t ap = {};
    std::strcpy((char *)ap.ap.ssid, "AMProp-Setup");
    ap.ap.ssid_len = (uint8_t)std::strlen("AMProp-Setup");
    std::strncpy((char *)ap.ap.password, cfg().ap_pass, sizeof(ap.ap.password) - 1);
    ap.ap.channel = 1;
    ap.ap.max_connection = 3;
    ap.ap.authmode = std::strlen(cfg().ap_pass) >= 8 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    s_ap_on = true;
    ESP_LOGW(TAG, "Setup-Access-Point 'AMProp-Setup' aktiv (192.168.4.1)");
}

static void stop_ap() {
    if (!s_ap_on || s_force_setup) return;
    esp_wifi_set_mode(WIFI_MODE_STA);
    s_ap_on = false;
    ESP_LOGI(TAG, "Setup-Access-Point beendet");
}

static void on_wifi(void *, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (cfg().ssid[0]) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (!cfg().ssid[0]) return;
        if (++s_fail >= 8) start_ap();           // nach 8 Fehlversuchen Setup-AP anbieten
        esp_wifi_connect();                       // und weiter versuchen
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto *e = (ip_event_got_ip_t *)data;
        esp_ip4addr_ntoa(&e->ip_info.ip, s_ip, sizeof s_ip);
        s_connected = true;
        s_fail = 0;
        ESP_LOGI(TAG, "IP %s", s_ip);
        xEventGroupSetBits(s_ev, BIT_GOT_IP);
        stop_ap();
    }
}

void net_start(bool force_setup) {
    s_force_setup = force_setup;
    s_ev = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta = esp_netif_create_default_wifi_sta();
    s_ap = esp_netif_create_default_wifi_ap();
    (void)s_ap;
    esp_netif_set_hostname(s_sta, cfg().hostname);

    wifi_init_config_t wic = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wic));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi, nullptr));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    wifi_config_t sta = {};
    std::strncpy((char *)sta.sta.ssid, cfg().ssid, sizeof(sta.sta.ssid) - 1);
    std::strncpy((char *)sta.sta.password, cfg().pass, sizeof(sta.sta.password) - 1);
    sta.sta.threshold.authmode = cfg().pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);               // gleichmaessige UDP-Latenz

    if (force_setup || !cfg().ssid[0]) start_ap();

    // mDNS: erreichbar als <hostname>.local
    if (mdns_init() == ESP_OK) {
        mdns_hostname_set(cfg().hostname);
        mdns_instance_name_set("AM Propagation Box");
        mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
    }

    // SNTP (nur sinnvoll mit Internet; ohne bleibt die Uhr auf dem Simulationswert)
    setenv("TZ", "UTC0", 1);
    tzset();
    esp_sntp_config_t sc = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sc);

    // Auf IP warten (max. 15 s); danach laeuft alles weiter, auch im Setup-AP
    xEventGroupWaitBits(s_ev, BIT_GOT_IP, pdFALSE, pdTRUE, pdMS_TO_TICKS(15000));
    if (!s_connected && cfg().ssid[0]) ESP_LOGW(TAG, "kein WLAN-Zugang, Setup-AP bleibt erreichbar");
}

bool net_connected() { return s_connected; }
bool net_setup_ap_active() { return s_ap_on; }
std::string net_ip() { return s_ap_on && !s_connected ? "192.168.4.1" : std::string(s_ip); }
bool net_time_valid() { return time(nullptr) > 1700000000; }
