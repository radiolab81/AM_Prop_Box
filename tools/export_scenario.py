#!/usr/bin/env python3
"""Szenario fuer die ESP32-Box erzeugen.

Liest alle CSVs eines tx_sites/<Szenario>/-Ordners des PC-Simulators (Zeilen:
Stadt,Land,kHz,kW,Name), loest die Orte ueber den vorhandenen geocache_radio.db auf
(kein Netz noetig) und schreibt EINE Textdatei: lat,lon,kHz,kW,Name.

  python3 export_scenario.py <tx_sites/Ordner> <geocache_radio.db> <ausgabe.txt> [city_coords.csv]

Orte, die nicht im Cache stehen, werden aus tools/city_coords.csv ergaenzt (Standard, falls
vorhanden; Cache hat Vorrang). Dessen Koordinaten sind Stadtmittelpunkte aus Allgemeinwissen.

Unaufloesbare Orte werden gemeldet und uebersprungen (im PC-Simulator zuerst laden,
dann steht der Ort im Cache).
"""
import csv, glob, os, sqlite3, sys


def load_extra(path):
    extra = {}
    if path and os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            if line.startswith("#") or not line.strip():
                continue
            key, lat, lon = line.rstrip("\n").rsplit(",", 2)
            extra[key.upper()] = (float(lat), float(lon))
    return extra


def main(folder, db, out, extra_path=None):
    if extra_path is None:
        extra_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "city_coords.csv")
    extra = load_extra(extra_path)
    from_extra = 0
    con = sqlite3.connect(db)
    cur = con.cursor()
    rows, failed = [], 0
    for f in sorted(glob.glob(os.path.join(folder, "*.csv"))):
        with open(f, encoding="utf-8-sig") as fh:
            for row in csv.reader(fh):
                if len(row) < 5:
                    continue
                city, country, freq, kw, prog = [x.strip() for x in row[:5]]
                res = cur.execute("SELECT lat, lon FROM locations WHERE key=?",
                                  (f"{city},{country}".upper(),)).fetchone()
                if not res and f"{city},{country}".upper() in extra:
                    res = extra[f"{city},{country}".upper()]
                    from_extra += 1
                if not res:
                    print(f"nicht im Cache: {city},{country}", file=sys.stderr)
                    failed += 1
                    continue
                try:
                    float(freq); float(kw)
                except ValueError:
                    failed += 1
                    continue
                rows.append((res[0], res[1], freq, kw, prog.replace("\n", " ")))
    with open(out, "w", encoding="utf-8") as fh:
        fh.write(f"# {os.path.basename(folder.rstrip('/'))}: lat,lon,kHz,kW,Name\n")
        for lat, lon, freq, kw, prog in rows:
            fh.write(f"{lat:.5f},{lon:.5f},{freq},{kw},{prog}\n")
    print(f"{len(rows)} Sender geschrieben, {failed} uebersprungen, davon {from_extra} Orte aus city_coords.csv -> {out}")


if __name__ == "__main__":
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__)
    main(*sys.argv[1:])
