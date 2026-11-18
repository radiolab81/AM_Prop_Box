(() => {
'use strict';
const $ = id => document.getElementById(id);
const api = async (path, method = 'GET', body) => {
  const r = await fetch(path, body === undefined ? {method} : {method, headers: {'Content-Type': 'application/json'}, body: JSON.stringify(body)});
  if (!r.ok) throw new Error((await r.text()) || r.status);
  return r.json();
};
const say = (t, bad) => { const m = $('msg'); m.textContent = t; m.style.color = bad ? 'var(--bad)' : 'var(--mute)'; };
const put = (path, body) => api(path, 'PUT', body).catch(e => say('Fehler: ' + e.message, true));
const debounce = (fn, ms = 250) => { let t; return (...a) => { clearTimeout(t); t = setTimeout(() => fn(...a), ms); }; };
const pad = n => String(n).padStart(2, '0');
const R = Math.PI / 180;

// ---------------------------------------------------------------- Karte
const map = L.map('map', {worldCopyJump: true, zoomControl: true}).setView([50, 10], 4);
let tilesOk = false, tilesErr = 0;
const tiles = L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png',
  {maxZoom: 9, attribution: '© OpenStreetMap-Mitwirkende'}).addTo(map);
tiles.on('tileload', () => { tilesOk = true; });
tiles.on('tileerror', () => { if (++tilesErr > 6 && !tilesOk) map.removeLayer(tiles); });
fetch('coast.json').then(r => r.json()).then(g => {
  L.geoJSON(g, {style: {color: '#6b7c8f', weight: 1, opacity: .7}, interactive: false}).addTo(map);
}).catch(() => {});

const terminator = L.polygon([], {color: '#000', weight: 0, fillOpacity: .22, interactive: false}).addTo(map);
const antPoly = L.polygon([], {color: '#1f6feb', weight: 1, fillOpacity: .15, interactive: false}).addTo(map);
const stormCircle = L.circle([0, 0], {radius: 80000, color: '#8e24aa', weight: 1, fillOpacity: .12, interactive: false});
const stormLine = L.polyline([], {color: '#8e24aa', weight: 2, interactive: false});
let rxMarker = null, rxDrag = false, sites = [], markers = [], pattern = [], last = null, placing = false;

function dest(lat, lon, brg, km) {              // Zielpunkt auf der Kugel
  const d = km / 6371, b = brg * R, p = lat * R, l = lon * R;
  const p2 = Math.asin(Math.sin(p) * Math.cos(d) + Math.cos(p) * Math.sin(d) * Math.cos(b));
  const l2 = l + Math.atan2(Math.sin(b) * Math.sin(d) * Math.cos(p), Math.cos(d) - Math.sin(p) * Math.sin(p2));
  return [p2 / R, ((l2 / R + 540) % 360) - 180];
}
const levelColor = v => {                       // dBµV/m -> Farbe (blass = leise, kraeftig = laut)
  const t = Math.max(0, Math.min(1, (v - 0) / 70));
  return `hsl(${120 - 120 * t},75%,${55 - 15 * t}%)`;
};

function drawTerminator(s) {
  const [y, m, d] = s.d.split('-').map(Number);
  const n = Math.round((Date.UTC(y, m - 1, d) - Date.UTC(y, 0, 0)) / 864e5);
  const dec = 23.45 * Math.sin(2 * Math.PI / 365 * (n - 81)) * R;
  const sunLon = -(s.h - 12) * 15;
  const pts = [];
  for (let lon = -180; lon <= 180; lon += 3) {
    const lat = Math.atan(-Math.cos((lon - sunLon) * R) / Math.tan(dec || 1e-6)) / R;
    pts.push([lat, lon]);
  }
  const pole = dec > 0 ? -90 : 90;              // Nachtseite liegt auf der dem Sonnenpol abgewandten Seite
  terminator.setLatLngs([[...pts, [pole, 180], [pole, -180]]]);
}

function drawAntenna(s) {
  if (!pattern.length) { antPoly.setLatLngs([]); return; }
  const pts = [];
  for (let b = 0; b < 360; b += 4) {
    const rel = (((Math.round(b - s.hdg)) % 360) + 360) % 360;
    pts.push(dest(s.rx[0], s.rx[1], b, 700 * Math.pow(10, (pattern[rel] || 0) / 20)));
  }
  antPoly.setLatLngs(pts);
}

function drawRx(s) {
  if (!rxMarker) {
    rxMarker = L.marker(s.rx, {draggable: true, icon: L.divIcon({className: '', html: '<div class="rx"></div>', iconSize: [18, 18], iconAnchor: [9, 9]}), zIndexOffset: 1000}).addTo(map);
    rxMarker.on('dragstart', () => { rxDrag = true; });
    rxMarker.on('dragend', () => { rxDrag = false; const p = rxMarker.getLatLng(); put('/api/rx', {lat: +p.lat.toFixed(5), lon: +p.lng.toFixed(5)}); });
  } else if (!rxDrag) rxMarker.setLatLng(s.rx);
}

function drawHold(s) {
  const h = s.hold;
  $('carinfo').textContent = h == null ? '' : `Trägertest aktiv: alle Träger auf ${(+h).toFixed(3)} – Simulation pausiert`;
  $('carinfo').style.color = h == null ? '' : 'var(--bad)';
  $('carauto').disabled = h == null;
}

function drawStorm(s) {
  if (!s.storm) { map.removeLayer(stormCircle); map.removeLayer(stormLine); $('storminfo').textContent = ''; return; }
  const p = [s.storm.lat, s.storm.lon];
  stormCircle.setLatLng(p).addTo(map);
  stormLine.setLatLngs([p, dest(p[0], p[1], s.storm.hdg, 120)]).addTo(map);
  $('storminfo').textContent = `Sturm: Kurs ${s.storm.hdg}°, ${s.storm.spd} km/h, noch ${s.storm.life} min`;
}

function buildMarkers() {
  markers.forEach(m => map.removeLayer(m));
  markers = sites.map(st => {
    const m = L.circleMarker([st[0], st[1]], {radius: 4, color: '#555', weight: 1, fillColor: '#aaa', fillOpacity: .8}).addTo(map);
    const tip = document.createElement('div');
    tip.textContent = `${st[4]}\n${st[2]} kHz, ${st[3]} kW`;
    tip.style.whiteSpace = 'pre';
    m.bindTooltip(tip);
    return m;
  });
}

function drawSites(s) {
  const lvl = new Map();
  s.st.forEach(r => lvl.set(r[0], r));
  markers.forEach((m, i) => {
    const r = lvl.get(i);
    if (!r) return;
    const aud = r[5] === 1;
    m.setStyle({fillColor: aud ? levelColor(r[1]) : '#9aa3b2', fillOpacity: aud ? .95 : .35, radius: aud ? 6 : 3.5});
    const tip = m.getTooltip().getContent();
    tip.textContent = `${sites[i][4]}\n${sites[i][2]} kHz, ${sites[i][3]} kW\n${r[1].toFixed(1)} dBµV/m (${r[4]})`;
  });
}

// Pegel-Verlauf: Delta gegenüber dem vorherigen Rechenlauf (nur bei neuer Simulationszeit fortgeschrieben)
let curNow = new Map(), prevNow = new Map(), runKey = '', ctxKey = '';
function trackDelta(s) {
  const ck = [s.scen, s.m, s.rx[0], s.rx[1], s.ant, s.hdg, s.sens, s.gm, s.gw].join('|');
  if (ck !== ctxKey) { ctxKey = ck; prevNow = new Map(); curNow = new Map(); runKey = ''; }
  const rk = s.d + ' ' + s.h;
  if (rk === runKey) return;                       // gleicher Zustand (z. B. Polling): Basis nicht verschieben
  runKey = rk;
  prevNow = curNow;
  curNow = new Map(s.st.map(r => [r[0], r[1]]));
}

function drawList(s) {
  trackDelta(s);
  const tb = $('list');
  tb.textContent = '';
  let n = 0;
  for (const r of s.st) {
    if (r[5] !== 1) continue;
    const site = sites[r[0]];
    if (!site) continue;
    ++n;
    const tr = document.createElement('tr'); tr.className = 'st';
    const p = prevNow.get(r[0]);
    const dv = p === undefined ? null : r[1] - p;
    const dtxt = dv === null ? '' : Math.abs(dv) < 0.05 ? '=' : (dv > 0 ? '+' : '\u2212') + Math.abs(dv).toFixed(1);
    const dcls = 'n' + (dv === null || Math.abs(dv) < 0.05 ? '' : dv > 0 ? ' up' : ' dn');
    for (const [txt, cls] of [[site[4], ''], [site[2], 'n'], [r[1].toFixed(1), 'n'], [dtxt, dcls], [r[4], '']]) {
      const td = document.createElement('td'); td.textContent = txt; if (cls) td.className = cls; tr.appendChild(td);
    }
    tr.onclick = () => map.flyTo([site[0], site[1]], Math.max(map.getZoom(), 5));
    tb.appendChild(tr);
  }
  $('audn').textContent = n ? `(${n} von ${s.st.length})` : '';
  if (!n) { const tr = document.createElement('tr'), td = document.createElement('td'); td.colSpan = 5; td.textContent = 'Keine Station über der Schwelle'; tr.appendChild(td); tb.appendChild(tr); }
}

// ---------------------------------------------------------------- Zustand anwenden
const busy = {};                                 // Steuerelement -> bis wann Nutzereingabe Vorrang hat
const touch = id => { busy[id] = Date.now() + 2500; };
const free = id => !(busy[id] > Date.now()) && document.activeElement !== $(id);
function setv(id, v) { if (free(id)) $(id).value = v; }

function apply(s) {
  last = s;
  const hh = Math.floor(s.h), mm = Math.floor((s.h - hh) * 60), ss = Math.floor(((s.h - hh) * 60 - mm) * 60);
  $('clock').textContent = `${pad(hh)}:${pad(mm)}:${pad(ss)}`;
  $('sun').textContent = `${s.d} · Sonnenhöhe am Empfänger ${s.sun}° · ${s.sun < -12 ? 'Nacht' : s.sun < 0 ? 'Dämmerung' : 'Tag'}`;
  $('rx').textContent = `(${s.rx[0].toFixed(3)}, ${s.rx[1].toFixed(3)})`;
  setv('date', s.d); setv('time', `${pad(hh)}:${pad(mm)}`);
  setv('model', s.m); setv('speed', s.sp); setv('gm', s.gm);
  setv('thr', s.sens); setv('gw', s.gw); setv('hdg', Math.round(s.hdg));
  $('thrv').textContent = Math.round(s.sens); $('gwv').textContent = s.gw; $('hdgv').textContent = Math.round(s.hdg);
  $('gwbox').style.display = s.gm === 1 ? '' : 'none';
  if (free('ntp')) $('ntp').checked = !!s.ntp;
  if (free('scen')) $('scen').value = s.scen;
  if (free('ant')) $('ant').value = s.ant;
  const u = s.udp;
  $('udp').innerHTML = '';
  const pill = document.createElement('span'); pill.className = 'pill ' + (u.ok ? 'ok' : 'bad');
  pill.textContent = u.dst ? (u.ok ? 'sendet an ' + u.dst : 'Sendefehler an ' + u.dst) : 'Ziel unbekannt';
  $('udp').append(pill, ` ${u.sent} Pakete, ${u.err} Fehler`);
  $('net').textContent = `${s.net.sta ? 'WLAN verbunden' : 'kein WLAN'} · ${s.net.ip}${s.net.ap ? ' · Setup-Zugang aktiv' : ''}${s.net.time ? '' : ' · Uhr nicht per NTP'}`;
  drawRx(s); drawTerminator(s); drawAntenna(s); drawStorm(s); drawHold(s); drawSites(s); drawList(s);
}

// ---------------------------------------------------------------- Steuerung
const sendSim = debounce(b => put('/api/sim', b));
const bindSim = (id, key, conv = Number) => $(id).addEventListener('input', () => { touch(id); sendSim({[key]: conv($(id).value)}); });
bindSim('model', 'model'); bindSim('speed', 'speed');
$('ntp').addEventListener('change', () => { touch('ntp'); put('/api/sim', {ntp: $('ntp').checked ? 1 : 0}); });
$('setclock').onclick = () => {
  const [y, mo, d] = $('date').value.split('-').map(Number), [h, mi] = ($('time').value || '12:00').split(':').map(Number);
  if (!y) return say('Datum fehlt', true);
  put('/api/sim', {clock: {y, mo, d, h: h + mi / 60}}).then(() => say('Zeit gesetzt'));
};
const sendParams = debounce(() => put('/api/sim', {sens: +$('thr').value, gm: +$('gm').value, gw: +$('gw').value}));
for (const id of ['thr', 'gm', 'gw']) $(id).addEventListener('input', () => {
  touch(id); $('thrv').textContent = $('thr').value; $('gwv').textContent = $('gw').value;
  $('gwbox').style.display = $('gm').value === '1' ? '' : 'none'; sendParams();
});
$('scen').onchange = () => { touch('scen'); put('/api/sim', {scenario: $('scen').value}).then(loadScenario); };
$('ant').onchange = () => { touch('ant'); put('/api/antenna', {file: $('ant').value}).then(loadAntenna); };
const sendHdg = debounce(() => put('/api/antenna', {heading: +$('hdg').value}), 120);
$('hdg').addEventListener('input', () => { touch('hdg'); $('hdgv').textContent = $('hdg').value; sendHdg(); });

const carriers = v => put('/api/sim', {carriers: v});
$('car0').onclick = () => carriers(0);
$('car1').onclick = () => carriers(1);
$('carset').onclick = () => { const v = +$('carv').value; if (v >= 0 && v <= 1) carriers(v); else say('Gain 0.0 – 1.0', true); };
$('carauto').onclick = () => carriers(-1);
$('stormset').onclick = () => { placing = true; say('Karte anklicken, um die Sturmzelle zu setzen'); };
$('stormoff').onclick = () => put('/api/storm', {op: 'stop'});
for (const id of ['sspd', 'shdg']) $(id).addEventListener('change', () => put('/api/storm', {op: 'set', spd: +$('sspd').value, hdg: +$('shdg').value}));
map.on('click', e => {
  const lat = +e.latlng.lat.toFixed(4), lon = +(((e.latlng.lng + 540) % 360) - 180).toFixed(4);
  if (placing) {
    placing = false; say('');
    put('/api/storm', {op: 'start', lat, lon, profile: +$('sprof').value, spd: +$('sspd').value, hdg: +$('shdg').value});
  } else put('/api/rx', {lat, lon});
});

$('saveconf').onclick = async () => {
  const b = {mod_host: $('modhost').value.trim(), ssid: $('ssid').value.trim(), hostname: $('host').value.trim(), jumpers: $('jump').checked ? 1 : 0};
  if ($('pass').value) b.pass = $('pass').value;
  try { const r = await api('/api/config', 'PUT', b); say(r.reboot ? 'Gespeichert – Gerät startet neu …' : 'Gespeichert'); $('pass').value = ''; }
  catch (e) { say('Fehler: ' + e.message, true); }
};

// ---------------------------------------------------------------- Laden
const fill = (id, items, label = x => x) => { const s = $(id); s.textContent = ''; items.forEach((x, i) => { const o = document.createElement('option'); o.value = typeof x === 'string' && id !== 'model' ? x : i; o.textContent = label(x); s.appendChild(o); }); };
async function loadScenario() { const j = await api('/api/scenario'); sites = j.sites; buildMarkers(); if (last) apply(last); }
async function loadAntenna() { const j = await api('/api/antenna'); pattern = j.pattern; if (last) drawAntenna(last); }
async function init() {
  try {
    const l = await api('/api/lists');
    fill('model', l.models);
    fill('scen', l.scenarios);
    const a = $('ant'); a.textContent = ''; const none = document.createElement('option'); none.value = ''; none.textContent = '(keine)'; a.appendChild(none);
    l.antennas.forEach(x => { const o = document.createElement('option'); o.value = x; o.textContent = x; a.appendChild(o); });
    const c = await api('/api/config');
    $('modhost').value = c.mod_host; $('ssid').value = c.ssid; $('host').value = c.hostname; $('jump').checked = !!c.jumpers;
    await Promise.all([loadScenario(), loadAntenna()]);
    apply(await api('/api/state'));
    const first = last; if (first && first.rx) map.setView(first.rx, 5);
  } catch (e) { say('Laden fehlgeschlagen: ' + e.message, true); }
  connect();
}

let ws, poll;
function connect() {
  try { ws = new WebSocket(`ws://${location.host}/ws`); } catch (e) { return startPoll(); }
  ws.onopen = () => { ws.send('hello'); clearInterval(poll); poll = null; };
  ws.onmessage = ev => { try { apply(JSON.parse(ev.data)); } catch (e) { /* unvollstaendiger Frame */ } };
  ws.onclose = () => { startPoll(); setTimeout(connect, 3000); };
}
function startPoll() {
  if (poll) return;
  poll = setInterval(() => api('/api/state').then(apply).catch(() => {}), 2000);
}
init();
})();
