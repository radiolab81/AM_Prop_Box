#!/usr/bin/env python3
"""Dateisystem-Image vorbereiten: www/ (gzip), scenarios/*.txt, scenarios/*.msi -> fs/

  python3 tools/pack_fs.py            (im Projektordner ausfuehren)

Danach 'idf.py build flash' (das Image wird aus fs/ erzeugt). Szenarien und Antennen koennen
spaeter auch einzeln ersetzt werden: neue Dateien nach fs/scenarios bzw. fs/antennas legen
und erneut flashen (die Partition 'storage').
"""
import glob, gzip, os, shutil

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
fs = os.path.join(root, "fs")
shutil.rmtree(fs, ignore_errors=True)
for d in ("www/vendor", "scenarios", "antennas"):
    os.makedirs(os.path.join(fs, d))

skip = {"countries-110m.topo.json", "LEAFLET-LICENSE"}   # Quelldaten, nicht noetig auf dem Geraet
total = 0
for path in glob.glob(os.path.join(root, "www", "**", "*"), recursive=True):
    if os.path.isdir(path) or os.path.basename(path) in skip:
        continue
    rel = os.path.relpath(path, os.path.join(root, "www"))
    out = os.path.join(fs, "www", rel + ".gz")
    with open(path, "rb") as f, gzip.GzipFile(out, "wb", compresslevel=9, mtime=0) as g:
        g.write(f.read())
    total += os.path.getsize(out)
    print(f"{rel}: {os.path.getsize(path)} -> {os.path.getsize(out)} Byte")

for f in glob.glob(os.path.join(root, "scenarios", "*.txt")):
    shutil.copy(f, os.path.join(fs, "scenarios")); total += os.path.getsize(f)
for f in glob.glob(os.path.join(root, "scenarios", "*.msi")) + glob.glob(os.path.join(root, "scenarios", "*.ant")):
    shutil.copy(f, os.path.join(fs, "antennas")); total += os.path.getsize(f)
print(f"Image-Inhalt gesamt: {total} Byte (Partition 'storage': 851968 Byte)")
