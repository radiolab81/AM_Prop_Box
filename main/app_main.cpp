// AM-Ausbreitungsbox: Startablauf.
#include "amp_config.hpp"
#include "amp_io.hpp"
#include "amp_net.hpp"
#include "amp_web.hpp"
#include "sim.hpp"
#include "amp/engine.hpp"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "main";

static void mount_fs() {
    esp_vfs_littlefs_conf_t c = {};
    c.base_path = "/fs";
    c.partition_label = "storage";
    c.format_if_mount_failed = false;     // nie automatisch formatieren: Szenarien liegen dort
    c.dont_mount = false;
    esp_err_t e = esp_vfs_littlefs_register(&c);
    if (e != ESP_OK) ESP_LOGE(TAG, "LittleFS nicht eingebunden (%s) - Image flashen: idf.py flash", esp_err_to_name(e));
}

extern "C" void app_main(void) {
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    config_load();
    // Selbsttest: Fliesskomma-Formatierung (Picolibc) - sonst waeren alle UDP-Pakete unbrauchbar
    ESP_LOGI(TAG, "Selbsttest Gain-Format: %s (erwartet 1017000:0.123457)", amp::format_gain_msg({1017000, 0.1234567f}).c_str());
    mount_fs();
    gpio_ui_init();
    bool setup = gpio_setup_requested();     // BOOT-Taste beim Start gehalten

    net_start(setup);
    udp_out_init();
    sim::init();
    udp_in_start();
    web_start();
    console_start();
    gpio_ui_start();
    ESP_LOGI(TAG, "bereit: http://%s.local/ bzw. http://%s/", cfg().hostname, net_ip().c_str());
}
