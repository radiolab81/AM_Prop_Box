// Simulationsdienst: haelt Szenario, Antenne, Modelle, Uhr und Sturm; rechnet im eigenen Task
// und sendet die Ergebnisse per UDP. Alle Funktionen sind threadsicher (ein Mutex).
#pragma once
#include <string>

namespace sim {

using StateCallback = void (*)(const std::string &json);

void init();                                    // Szenario/Antenne laden, Task starten
void set_state_callback(StateCallback cb);      // wird nach jedem Rechenlauf mit dem Zustand gerufen

// ---- Steuerung ----
void set_rx(float lat, float lon);
void set_heading(float deg);
void set_model(int id);
void set_speed(int speed);                      // 1, 60, 600, 3600
void set_clock(int year, int month, int day, float hours);   // schaltet NTP-Folgen aus
void follow_ntp(bool on);
bool set_scenario(const std::string &name);     // Datei /fs/scenarios/<name>.txt
bool set_antenna(const std::string &file);      // /fs/antennas/<file>; "" = keine
void set_params(float sens, int gain_mode, int gain_window);
void storm_start(float lat, float lon, int profile);   // 0 schwach, 1 mittel, 2 stark
void storm_set(float speed_kmh, float heading_deg);
void storm_stop();
void carriers(float gain);                      // Traegertest: alle Traeger 100..1700 kHz (1-kHz-Raster) auf gain 0..1,
                                                // Simulation pausiert bis carriers_release()
void carriers_release();                        // alle auf 0.0, Simulation laeuft wieder
void request_save();                            // Konfiguration verzoegert sichern

// ---- Abfragen (JSON) ----
std::string state_json();                       // Zustand inkl. Pegel je Sender (fuer WebSocket)
std::string scenario_json();                    // Senderliste des aktuellen Szenarios
std::string lists_json();                       // vorhandene Szenarien, Antennen, Modellnamen
std::string antenna_json();                     // Antennenmuster fuer die Karte: {"name":..,"pattern":[360]}
std::string dfscope_config_json();              // Antwort auf UDP "GET_CONFIG" (Port 8887)
std::string dfscope_transmitters_json();        // Antwort auf UDP "GET_TRANSMITTERS"

}  // namespace sim
