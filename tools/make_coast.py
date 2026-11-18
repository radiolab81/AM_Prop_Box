#!/usr/bin/env python3
"""TopoJSON (world-atlas countries-110m, Natural Earth, Public Domain) -> kleine GeoJSON-Datei
mit Kuesten und Landesgrenzen als MultiLineString. Offline-Rueckfall fuer die Karte.

  python3 make_coast.py www/vendor/countries-110m.topo.json www/coast.json
"""
import json, sys

src, dst = sys.argv[1:3]
t = json.load(open(src))
sx, sy = t["transform"]["scale"]; tx, ty = t["transform"]["translate"]
lines = []
for arc in t["arcs"]:
    x = y = 0
    pts = []
    for dx, dy in arc:
        x += dx; y += dy
        pts.append([round(x * sx + tx, 2), round(y * sy + ty, 2)])
    lines.append(pts)
json.dump({"type": "MultiLineString", "coordinates": lines}, open(dst, "w"), separators=(",", ":"))
print(f"{len(lines)} Linienzuege, {sum(len(l) for l in lines)} Punkte -> {dst}")
