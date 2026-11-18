// Persistente Einstellungen (NVS). Ein Blob mit Versionskennung; bei neuer Version Defaults.
#pragma once
#include <cstdint>

struct AppConfig {
    uint32_t magic;                 // CFG_MAGIC
    char ssid[33];
    char pass[65];
    char mod_host[64];              // IPv4-Text oder Hostname (z. B. "pi.local"), Ziel der UDP-Pakete
    uint16_t port_gain;             // 8888
    uint16_t port_sferics;          // 8889
    uint8_t model;                  // 0 Klassisch, 1 Klassisch+, 2 ITU-R P.1147-2
    uint8_t gain_mode;              // 0 kompatibel, 1 physikalisch
    uint8_t gain_window;            // 20..32 dB
    uint8_t jumpers;                // 1 = Jumper bestimmen Modell/Tempo/Szenario beim Start und bei Aenderung
    uint8_t use_ntp;                // 1 = Uhr folgt der echten UTC-Zeit (nur bei Tempo x1)
    uint16_t speed;                 // 1, 60, 600, 3600
    float sens;                     // Schwelle dBuV/m
    float rx_lat, rx_lon;
    float heading;                  // Antennenkurs
    char scenario[32];              // Dateiname ohne .txt
    char antenna[32];               // Dateiname (.msi), leer = keine
    char hostname[24];              // mDNS-Name
    char ap_pass[24];               // Passwort des Setup-Access-Points
};

constexpr uint32_t CFG_MAGIC = 0x414d5033;   // "AMP3"

AppConfig &cfg();
void config_load();
bool config_save();
void config_defaults(AppConfig &c);
