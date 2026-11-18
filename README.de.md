# AM-Prop-Box – AM-Ausbreitungssimulation auf dem klassischen ESP32 (ESP-IDF 6.1)

🇬🇧 [English version: README.md](README.md)

Portierung des [AMWaveSynth Propagation Simulators](https://github.com/radiolab81/AMWaveSynthPropagationSimulator)
auf einen kleinen, eigenständigen **klassischen ESP32** (nur WLAN). Die Box simuliert die Lang-/Mittelwellen-
Ausbreitung für **einen Empfangsort** und steuert **einen Modulator** (AMWaveSynth / COHI-Player-Universal, z. B. auf
einem headless Raspberry Pi 4/5 oder PC) per UDP. Modulator und ESP32 hängen im selben WLAN. Bedienung über ein Webinterface
mit Karte, eine serielle Konsole sowie optional Jumper / Taste / LED.

Aus Senderdaten (Standort, Leistung – etwa aus einem alten WRTH) berechnet die Box Boden- und Raumwelle am Empfangsort
für das gewählte Datum und die Tageszeit und stellt laufend die Trägeramplitude jedes Senders am Modulator ein. So
entsteht ein lebendiges Band mit wandernder Dämmerungszone: Mittelwellensender kommen abends von oben nach unten
herein, nachts treffen Auslandssender per Raumwelle ein, morgens schließt sich das Band wieder. Gewitter fügen Sferics
auf HF-Ebene hinzu.

## Funktionen

* **Drei Ausbreitungsmodelle**, zur Laufzeit umschaltbar (Portierung des Originals, in float nachweislich identisch – siehe *Tests*):
  * *Klassisch (heuristisch)* – das ursprüngliche vereinfachte Modell.
  * *Klassisch+ (Norton, Hops)* – Norton-Bodenwelle, Großkreisentfernung, Multi-Hop-Raumwelle, D-Schicht-Dämpfung.
  * *ITU-R P.1147-2 (mit Jahreszeiten)* – Raumwelle nach ITU-R P.1147-2 mit parametrischer Jahreszeitenkorrektur, Norton-Bodenwelle.
* **Simulationsuhr:** Datum und Uhrzeit frei setzbar; Tempo ×1 / ×60 / ×600 / ×3600; optional echte UTC-Zeit per SNTP (nur ×1).
* **Empfänger auf der Karte** (Leaflet, wird vom ESP32 ausgeliefert): Karte anklicken oder blauen Punkt ziehen; Sender nach Empfangspegel eingefärbt; Live-Liste der hörbaren Sender mit Pegel (dBµV/m) und Weg (G = Boden-, S = Raumwelle).
* **Schwelle** der Empfängerempfindlichkeit und **Gain-Modus** *kompatibel* (linear 40 dB wie im Original) oder *physikalisch* (echte dB-Skala, Fenster 20–32 dB).
* **Szenarien** (Senderlisten als Textdateien im Flash): `1975_WRTH` (132 Sender, LW/MW Europa 1975), `Hokkaido_Sakhalin_2026` (39), `COHIPlayerUniversal_Test` (31). Umschaltung zur Laufzeit; Sender des alten Szenarios werden am Modulator stumm geschaltet (Gain 0.0).
* **Peilantennen** (`.msi`-Diagramme, Horizontalschnitt): Ferritstab, K9AY-Kardioide, Rundstrahler. Virtuelle Antenne 0–359° drehbar; die Bodenwelle erhält die volle Diagrammdämpfung, die Raumwelle in den Modellen, die das vorsehen, einen entfernungsabhängigen Anteil (Nachteffekt).
* **Sferics / Gewitter:** Gewitterzelle mit Stärke, Geschwindigkeit und Kurs auf der Karte setzen; sie zieht über die Karte und feuert Sferics (UDP 8889), deren Rate vom Abstand zum Empfänger abhängt.
* **Trägertest (Modulator-Abgleich):** setzt *alle* Träger im 1-kHz-Raster (100–1700 kHz, 1601 Pakete) auf Gain 0.0, 1.0 oder einen beliebigen Wert – im Web-UI („Modulator-Test") oder mit `carriers`. Anders als im PC-Simulator **pausiert** die Simulation währenddessen (sonst würde die nächste Berechnung den Test überschreiben); „Simulation fortsetzen“ / `carriers auto` schaltet alles auf 0.0 und setzt fort.
* **DFscope-kompatibler UDP-Eingang** (8886 Position, 8887 Kurs, `GET_CONFIG`, `GET_TRANSMITTERS`): PC-Plugins (DFscope, MovableRXLocationPlayer) können die Box steuern.
* **Einstellungen bleiben erhalten** (NVS); mDNS-Name (`amprop.local`); Setup-Access-Point, falls kein WLAN eingerichtet ist.

## Hardware

Klassisches ESP32-Modul/-Board mit 4 MB Flash. Optional:

| Funktion | GPIO |
|---|---|
| Modell-Jumper | 32 / 33 |
| Tempo-Jumper | 25 / 26 |
| Szenario-Jumper | 13 / 16 / 17 |
| Taste (zugleich BOOT) | 0 |
| Status-LED | 2 |
| Serielle Konsole | UART0, 115200 8N1 |

Jumper (gegen GND) überschreiben beim Start die gespeicherten Einstellungen; abschaltbar im Web-UI oder mit `jumpers off`.

## Bauen und flashen

    python3 tools/pack_fs.py          # www gzip + Szenarien + Antennen -> fs/ (Flash-Image 'storage')
    idf.py set-target esp32
    idf.py build flash monitor

Benötigt ESP-IDF 6.1. Komponenten (mDNS, cJSON, LittleFS) holt der Component Manager
(`main/idf_component.yml`; getestet mit mdns 1.15.1, littlefs 1.23.1, cjson 1.7.19).

## Erster Start

1. Ist kein WLAN gespeichert, nach 8 fehlgeschlagenen Versuchen oder bei gehaltener BOOT-Taste beim Einschalten öffnet der ESP den Access Point **`AMProp-Setup`** (Passwort `amprop-setup`, sofern nicht geändert).
2. `http://192.168.4.1` öffnen, WLAN, **Modulator-IP** (Standard 192.168.1.50) und Hostname eintragen, speichern; die Box startet neu.
3. Danach `http://amprop.local/` verwenden (oder die IP aus `status` in der Konsole).
4. Empfänger auf der Karte setzen, Szenario, Modell, Datum und Tempo wählen. Der Modulator erhält `Hz:Gain` auf UDP 8888 und Sferics `amp:dur` auf 8889.

Die Kartenkacheln kommen von OpenStreetMap und werden vom **Browser** geladen (der ESP32 kontaktiert den Kachelserver nie). Ist der Kachelserver nicht erreichbar oder blockt er die Anfrage, wird eine eingebaute Küstenkarte verwendet. Die `index.html` direkt als `file://`-Seite zu öffnen liefert keine Kacheln (kein Referer → 403); den ESP32 oder einen lokalen HTTP-Server verwenden.

## Serielle Konsole (UART0, `help` listet alles)

    status                      Zustand, WLAN, Modulator-Ziel, Zähler
    model 0|1|2                 Klassisch | Klassisch+ | ITU-R P.1147-2
    speed 1|60|600|3600         Simulationsgeschwindigkeit
    date T.M.J [HH:MM]          Datum/Uhrzeit setzen (UTC, schaltet NTP ab)
    ntp on|off                  Uhr folgt echter UTC-Zeit (nur Tempo x1)
    rx LAT LON                  Empfängerposition
    hdg GRAD                    Antennenkurs
    scen NAME | ant DATEI|none  Szenario / Antennendiagramm
    thr DB | gain compat|phys | win 20..32
    storm LAT LON [0|1|2] | storm spd KMH HDG | storm off
    carriers 0|1|WERT | auto    alle Träger auf Gain (Test, Simulation pausiert); auto = weiter
    modip HOST                  Modulator (IPv4 oder Name)
    wifi SSID [PASS]            WLAN speichern (danach 'reboot'); 'wifi' allein zeigt die SSID
    jumpers on|off              Jumper als Einstellquelle
    reboot

## Schnittstellen

* **UDP aus:** 8888 `Hz:Gain`, 8889 Sferics `amp:dur` (Ports einstellbar).
* **UDP ein:** 8886 `lat,lon`; 8887 `HDG:x`; `GET_CONFIG`, `GET_TRANSMITTERS` (JSON wie im Original).
* **REST:** `/api/{lists,scenario,antenna,state,rx,sim,storm,config}`; **WebSocket** `/ws` (nach dem Verbinden ein beliebiges Frame senden, z. B. `hello`, zur Anmeldung).

## Szenarien hinzufügen

Szenariodateien sind Text: `lat,lon,kHz,kW,Name`. CSV-Ordner des PC-Simulators umwandeln mit

    python3 tools/export_scenario.py <tx_sites/Ordner> <geocache_radio.db> scenarios/<Name>.txt

Orte, die nicht im Geocache stehen, werden aus `tools/city_coords.csv` ergänzt (**nicht** von einem Geocoder erzeugt; bei Bedarf korrigieren). Danach `tools/pack_fs.py` ausführen und neu flashen. `.msi`-Antennendateien ebenfalls in `scenarios/` legen.

## Tests am PC (Rechenlogik gegen das Original)

Benötigt Python 3, CMake, g++ und den entpackten Original-Ordner `AMWaveSynthPropagationSimulator-main`.

1. Referenzdaten aus dem **Original-Python** erzeugen (Argumente: Original-Ordner, Ausgabeordner, Szenario, Antenne):

        cd host_test
        mkdir -p ref
        python3 -I make_reference.py ../../AMWaveSynthPropagationSimulator-main ref \
            ../scenarios/1975_WRTH.txt ../scenarios/<antenne>.msi

2. C++-Test bauen und mit denselben Dateien starten:

        cmake -S . -B build && cmake --build build
        ./build/amp_test ref ../scenarios/1975_WRTH.txt ../scenarios/<antenne>.msi

   Gegenprobe in double: `cmake -S . -B build_d -DAMP_DOUBLE=ON && cmake --build build_d`.

3. Erwartet (float): Modellabweichung max. ca. 0,005 dB (float-Rauschen), Antenne 0 von 500 abweichend, Engine 864 Läufe / 86868 Nachrichten, 0 abweichend, Schlusszeile `Alle Pruefungen bestanden`.

Das Original gibt die Stummschalt-Nachrichten (`0.0`) in Set-Reihenfolge aus; der Test vergleicht diese als Menge, den hörbaren Teil in fester Reihenfolge. `host_test/sim_harness/` lässt die echte `sim.cpp` mit Stubs auf dem PC laufen (Entwicklerwerkzeug ohne Build-Skript; `/fs` muss auf `fs/` zeigen); mit `-DCARRIER_TEST` übersetzt prüft es den Trägertest (1601 Pakete, Pause, sauberes Fortsetzen).

## Bewusste Abweichungen vom Original

MSI-Parser liest nur den HORIZONTAL-Abschnitt; Uhr mit Tag/Monat/Jahr-Überlauf inkl. Schaltjahren; Sturmbewegung folgt der Simulationszeit, Sferics-Rate der Realzeit; UDP auch bei Zustandsänderung, nicht nur im Takt; Tempo ×1/60/600/3600; NTP nur bei ×1; Mutex statt Queues. Original-Eigenheiten bleiben: Klassik-Modell mit ebener Distanz; Sturm-Länge ohne cos(Breite).

## Stand und Grenzen

Geprüft: Numerik gegen das Original (s. o.), Firmware in QEMU und auf echter Hardware (WLAN, Modulatorsteuerung, Antennendrehung, Sferics, Web-UI). Nicht umgesetzt: OTA (Partitionen vorbereitet), OLED.

Kartendaten © OpenStreetMap-Mitwirkende (ODbL). Leaflet 1.9.4 (BSD-2), siehe `www/LEAFLET-LICENSE`. Küstenlinie aus Natural Earth über world-atlas (gemeinfrei).
