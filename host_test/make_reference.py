#!/usr/bin/env python3
"""Referenzdaten aus dem ORIGINAL-Python-Code erzeugen (Modelle, Antenne, Engine-Logik).

  python3 -I make_reference.py <AMWaveSynthPropagationSimulator-Ordner> <ausgabeordner> <scenario.txt> <msi>

Schreibt models.csv (Einzelstrecken), engine.csv (Nachrichtenfolge ueber einen Tag),
antenna.csv (Daempfungen). Die C++-Tests vergleichen dagegen.
"""
import math, random, sys, types

src, outdir, scen_path, msi_path = sys.argv[1:5]
sys.path.insert(0, src)
sys.modules["tkinter"] = types.ModuleType("tkinter")   # nur fuer den Import der Engine

import propagation_models as pm
from propagation_models import SimContext, Tx, sun_alt
from Antenna_Engine import AntennaEngine

names = ["Klassisch (heuristisch)", "Klassisch+ (Norton, Hops)", "ITU-R P.1147-2 (mit Jahreszeiten)"]
rnd = random.Random(1975)

# ---- 1. Einzelstrecken -------------------------------------------------------------
with open(f"{outdir}/models.csv", "w") as fh:
    for mi, name in enumerate(names):
        model = pm.create_model(name)
        for i in range(3000):
            rx_lat, rx_lon = rnd.uniform(-60, 70), rnd.uniform(-180, 180)
            n_day, t = rnd.randint(1, 366), rnd.uniform(0, 24)
            kind = i % 6
            if kind == 0:   # < 50 km
                tx_lat, tx_lon = rx_lat + rnd.uniform(-.3, .3), rx_lon + rnd.uniform(-.3, .3)
            elif kind in (1, 2):   # 50..1500 km
                tx_lat, tx_lon = rx_lat + rnd.uniform(-10, 10), rx_lon + rnd.uniform(-15, 15)
            else:
                tx_lat, tx_lon = rnd.uniform(-60, 70), rnd.uniform(-180, 180)
            tx_lat = max(-89, min(89, tx_lat))
            f = rnd.choice([153, 189, 279, 531, 810, 1017, 1494, 1700, rnd.uniform(150, 1700)])
            kw = rnd.choice([0.1, 1, 50, 250, 1000])
            alt = sun_alt(rx_lat, rx_lon, n_day, t)
            model.prepare(SimContext(rx_lat, rx_lon, n_day, t, alt))
            r = model.compute(Tx(tx_lat, tx_lon, f, kw))
            fh.write(",".join(repr(x) for x in (mi, rx_lat, rx_lon, n_day, t, tx_lat, tx_lon, f, kw,
                     r.fs_g, r.fs_s, r.fs_s_pot, r.t_bw, r.t_rw, r.sky_ant_factor)) + "\n")

# ---- 2. Antenne --------------------------------------------------------------------
ant = AntennaEngine(None)
assert ant.load_file(msi_path)
with open(f"{outdir}/antenna.csv", "w") as fh:
    for i in range(500):
        ant.antenna_heading = rnd.uniform(0, 359.99)
        a = (rnd.uniform(-60, 70), rnd.uniform(-180, 180), rnd.uniform(-60, 70), rnd.uniform(-180, 180))
        fh.write(",".join(repr(x) for x in (ant.antenna_heading, *a, ant.get_attenuation(*a))) + "\n")

# ---- 3. Engine-Logik (run_simulation ohne UI) ----------------------------------------
def gain_from_level(level, sens, mode, win):
    if mode == "physical":
        return max(0.0, min(1.0, 10 ** ((level - sens - win) / 20.0)))
    return max(0.0, min(1.0, (level - sens) / 40.0))

sites = []
for line in open(scen_path, encoding="utf-8"):
    if line.startswith("#") or not line.strip():
        continue
    la, lo, f, kw, nm = line.rstrip("\n").split(",", 4)
    sites.append((float(la), float(lo), float(f), float(kw), nm))

def fmt(g):   # Original: f"{freq_hz}:{round(gain,6)}"
    return str(round(g, 6))

with open(f"{outdir}/engine.csv", "w") as fh:
    cfg = [("compat", 28, 20.0), ("physical", 28, 20.0), ("physical", 22, 15.0)]
    case = 0
    for mi, name in enumerate(names):
        model = pm.create_model(name)
        for (mode, win, sens) in cfg:
            for use_ant in (False, True):
                ant.antenna_heading = 37.3 if use_ant else 0.0
                last = set()
                rx = (50.1, 8.6)
                n_day = 288
                for step in range(48):
                    t = step * 0.5
                    alt = sun_alt(rx[0], rx[1], n_day, t)
                    model.prepare(SimContext(rx[0], rx[1], n_day, t, alt))
                    res = []
                    for (la, lo, f, kw, nm) in sites:
                        lr = model.compute(Tx(la, lo, f, kw))
                        al = ant.get_attenuation(rx[0], rx[1], la, lo) if use_ant else 0.0
                        sa = al * lr.sky_ant_factor
                        fs_g = lr.fs_g + al
                        cur = lr.fs_s + sa
                        base = lr.fs_s_pot + sa
                        res.append({"f": f, "pot": max(fs_g, base), "now": max(fs_g, cur)})
                    res.sort(key=lambda x: x["pot"], reverse=True)
                    res.sort(key=lambda x: x["now"], reverse=True)
                    now_act, msgs = set(), []
                    for r in res:
                        if r["now"] > sens:
                            fhz = int(r["f"] * 1000)
                            now_act.add(fhz)
                            msgs.append(f"{fhz}:{fmt(gain_from_level(r['now'], sens, mode, win))}")
                    for fhz in last - now_act:
                        msgs.append(f"{fhz}:0.0")
                    last = now_act
                    fh.write(f"{mi}|{mode}|{win}|{sens}|{int(use_ant)}|{step}|" + ";".join(msgs) + "\n")
print("Referenz geschrieben")
