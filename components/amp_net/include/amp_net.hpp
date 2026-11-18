#pragma once
#include <string>

// ---- WLAN / mDNS / NTP ----
// Startet WLAN als Client. Ohne Zugangsdaten, nach erfolglosen Versuchen oder bei force_setup
// laeuft zusaetzlich der Setup-Access-Point "AMProp-Setup" (Passwort aus der Konfiguration).
void net_start(bool force_setup);
bool net_connected();           // Station hat IP
bool net_setup_ap_active();
std::string net_ip();           // eigene IP (Station) oder 192.168.4.1
bool net_time_valid();          // SNTP hat die Uhr gesetzt

// ---- UDP zum Modulator ----
void udp_out_init();
void udp_out_set_target(const char *host);   // IPv4-Text oder Hostname; Aufloesung im Hintergrund
std::string udp_out_target();                // aufgeloeste Adresse, leer wenn unbekannt
void udp_send_gain(const std::string &msg);
void udp_send_sferic(const std::string &msg);
unsigned udp_out_sent();
unsigned udp_out_errors();
bool udp_out_ok();                           // letzte Sendung ohne lokalen Fehler
