// Carte du decodeur multiple : fond OpenStreetMap hors ligne (PMTiles
// Protomaps servi par le Pi) et positions decodees (state.json, ecrit par
// multidec toutes les 2 s). Aucune ressource Internet.
"use strict";

const COLORS = { aircraft: "#e85d04", ship: "#0077b6", aprs: "#7209b7", sonde: "#d00000", station: "#2b9348", sensor: "#555" };
const NAMES = { aircraft: "Aircraft", ship: "Ship", aprs: "APRS", sonde: "Radiosonde", station: "Station", sensor: "Sensor" };

// Icones APRS : jeu de symboles d'aprs.fi (Heikki Hannikainen OH7LZB,
// https://github.com/hessu/aprs-symbols). Planches de 16 x 6 cases de 48 px,
// code ASCII 33 ("!") en premier : 0 table primaire "/", 1 alternative "\",
// 2 caracteres d'overlay.
const SYM_SHEETS = [0, 1, 2].map((t) => "aprs/aprs-symbols-48-" + t + ".png");
function loadImg(u) {
  return new Promise((ok, ko) => {
    const im = new Image();
    im.onload = () => ok(im);
    im.onerror = ko;
    im.src = u;
  });
}
const symSheets = Promise.all(SYM_SHEETS.map(loadImg)).catch(() => null);   // absentes : points de couleur
// radiosonde : ballon, parachute et sonde (make_sonde_icon.py), 22 x 48 px,
// la boite de la sonde (sa position) a 4,9 px du bas
const sondeIcon = loadImg("sonde.png").catch(() => null);
let sheets = null;

// "/-" -> case dans la table ; chiffre ou majuscule : table alternative avec ce
// caractere en surimpression (a-j : chiffres des positions compressees)
function symParts(sym) {
  if (typeof sym !== "string" || sym.length < 2) return null;
  const t = sym[0], c = sym.charCodeAt(1);
  if (c < 33 || c > 126) return null;
  if (t === "/") return { table: 0, code: c - 33 };
  if (t === "\\") return { table: 1, code: c - 33 };
  const ov = (t >= "a" && t <= "j") ? String.fromCharCode(48 + t.charCodeAt(0) - 97) : t;
  return /^[0-9A-Z]$/.test(ov) ? { table: 1, code: c - 33, ov: ov.charCodeAt(0) - 33 } : null;
}

// Icone de la liste : la (ou les) case(s) des planches en fond, 20 px
function symHtml(sym) {
  const p = symParts(sym), s = 20;
  const pos = (k) => -(k % 16) * s + "px " + -Math.floor(k / 16) * s + "px";
  const bg = [];
  if (p.ov !== undefined) bg.push("url(" + SYM_SHEETS[2] + ") " + pos(p.ov));
  bg.push("url(" + SYM_SHEETS[p.table] + ") " + pos(p.code));
  return '<span class="sym" title="' + esc(sym) + '" style="background:' + bg.join(",") +
    ";background-size:" + 16 * s + "px " + 6 * s + 'px"></span>';
}

const protocol = new pmtiles.Protocol();
maplibregl.addProtocol("pmtiles", protocol.tile);

const F = ["Noto Sans Regular"];
const style = {
  version: 8,
  glyphs: "map/fonts/{fontstack}/{range}.pbf",
  sources: {
    osm: { type: "vector", url: "pmtiles://" + location.href.replace(/[^/]*$/, "") + "map/france.pmtiles",
           attribution: "© OpenStreetMap contributors · Protomaps" },
    obj: { type: "geojson", data: { type: "FeatureCollection", features: [] },
           attribution: '<a href="https://github.com/hessu/aprs-symbols">APRS symbols aprs.fi</a>' },
    trails: { type: "geojson", data: { type: "FeatureCollection", features: [] } },
    rings: { type: "geojson", data: { type: "FeatureCollection", features: [] } }
  },
  layers: [
    { id: "bg", type: "background", paint: { "background-color": "#b9d3e3" } },
    { id: "earth", type: "fill", source: "osm", "source-layer": "earth", paint: { "fill-color": "#efece6" } },
    { id: "landcover", type: "fill", source: "osm", "source-layer": "landcover",
      filter: ["in", ["get", "kind"], ["literal", ["forest", "wood"]]], paint: { "fill-color": "#dce8d2", "fill-opacity": 0.7 } },
    { id: "landuse", type: "fill", source: "osm", "source-layer": "landuse",
      filter: ["in", ["get", "kind"], ["literal", ["forest", "wood", "park", "nature_reserve", "national_park"]]],
      paint: { "fill-color": "#d5e6c8", "fill-opacity": 0.6 } },
    { id: "urban", type: "fill", source: "osm", "source-layer": "landuse",
      filter: ["in", ["get", "kind"], ["literal", ["residential", "industrial", "commercial", "urban_area"]]],
      paint: { "fill-color": "#e3ddd5" } },
    { id: "water", type: "fill", source: "osm", "source-layer": "water", filter: ["==", ["geometry-type"], "Polygon"],
      paint: { "fill-color": "#b9d3e3" } },
    { id: "rivers", type: "line", source: "osm", "source-layer": "water", filter: ["==", ["geometry-type"], "LineString"],
      paint: { "line-color": "#9cc0d6", "line-width": ["interpolate", ["linear"], ["zoom"], 6, 0.5, 12, 2] } },
    { id: "boundary-region", type: "line", source: "osm", "source-layer": "boundaries",
      filter: ["!=", ["get", "kind"], "country"],
      paint: { "line-color": "#a59fb3", "line-width": 0.8, "line-dasharray": [3, 2] } },
    { id: "boundary-country", type: "line", source: "osm", "source-layer": "boundaries",
      filter: ["==", ["get", "kind"], "country"], paint: { "line-color": "#8a7f99", "line-width": 1.6 } },
    { id: "roads-medium", type: "line", source: "osm", "source-layer": "roads",
      filter: ["in", ["get", "kind"], ["literal", ["medium_road", "minor_road"]]],
      paint: { "line-color": "#ffffff", "line-width": ["interpolate", ["linear"], ["zoom"], 8, 0.4, 13, 2.5] } },
    { id: "roads-major", type: "line", source: "osm", "source-layer": "roads", filter: ["==", ["get", "kind"], "major_road"],
      paint: { "line-color": "#f7d9a3", "line-width": ["interpolate", ["linear"], ["zoom"], 6, 0.6, 13, 4] } },
    { id: "roads-highway", type: "line", source: "osm", "source-layer": "roads", filter: ["==", ["get", "kind"], "highway"],
      paint: { "line-color": "#e8a0a8", "line-width": ["interpolate", ["linear"], ["zoom"], 5, 0.7, 13, 5] } },
    { id: "rings", type: "line", source: "rings", paint: { "line-color": "#334", "line-width": 1, "line-opacity": 0.45, "line-dasharray": [2, 2] } },
    { id: "ring-labels", type: "symbol", source: "rings", layout: { "symbol-placement": "line", "text-field": ["get", "label"],
      "text-font": F, "text-size": 11 }, paint: { "text-color": "#334", "text-halo-color": "#fff", "text-halo-width": 1.5 } },
    { id: "places", type: "symbol", source: "osm", "source-layer": "places",
      filter: ["in", ["get", "kind"], ["literal", ["locality", "region", "country"]]],
      layout: { "text-field": ["get", "name"], "text-font": F,
                "text-size": ["interpolate", ["linear"], ["zoom"], 5, ["match", ["get", "kind"], "country", 14, "region", 11, 10],
                              10, ["match", ["get", "kind"], "country", 16, "region", 13, 14]],
                "symbol-sort-key": ["get", "min_zoom"], "text-padding": 4 },
      paint: { "text-color": ["match", ["get", "kind"], "region", "#7a7389", "#3b3b3b"], "text-halo-color": "#fbfaf7", "text-halo-width": 1.6 } },
    { id: "trails", type: "line", source: "trails",
      paint: { "line-color": ["get", "color"], "line-width": ["match", ["get", "type"], "sonde", 3, 1.6], "line-opacity": 0.8 } },
    { id: "obj-dot", type: "circle", source: "obj", filter: ["!", ["any", ["has", "crs"], ["has", "icon"]]],
      paint: { "circle-color": ["get", "color"], "circle-radius": 5.5, "circle-stroke-color": "#fff", "circle-stroke-width": 1.5 } },
    { id: "obj-arrow", type: "symbol", source: "obj", filter: ["all", ["has", "crs"], ["!", ["has", "icon"]]],
      layout: { "icon-image": ["match", ["get", "type"], "aircraft", "plane", "arrow"], "icon-rotate": ["get", "crs"],
                "icon-rotation-alignment": "map",
                "icon-allow-overlap": true, "icon-size": 0.9 },
      paint: { "icon-color": ["get", "color"], "icon-halo-color": "#fff", "icon-halo-width": 1.5 } },
    // stations APRS : leur icone (image creee a la demande, styleimagemissing) ;
    // radiosonde : ballon, la boite de la sonde sur sa position
    { id: "obj-icon", type: "symbol", source: "obj", filter: ["has", "icon"],
      layout: { "icon-image": ["get", "icon"], "icon-allow-overlap": true, "icon-padding": 0,
                "icon-anchor": ["case", ["==", ["get", "icon"], "balloon"], "bottom", "center"],
                "icon-offset": ["case", ["==", ["get", "icon"], "balloon"], ["literal", [0, 4.9]], ["literal", [0, 0]]] } },
    { id: "obj-label", type: "symbol", source: "obj",
      layout: { "text-field": ["get", "label"], "text-font": F, "text-size": 12, "text-offset": [0, 1.2], "text-anchor": "top",
                "text-optional": true },
      paint: { "text-color": "#111", "text-halo-color": "#fff", "text-halo-width": 1.8 } }
  ]
};

const map = new maplibregl.Map({ container: "map", style, center: [2.5, 46.6], zoom: 5, maxZoom: 13, attributionControl: true });
map.addControl(new maplibregl.NavigationControl(), "top-left");
map.addControl(new maplibregl.ScaleControl({ unit: "metric" }));

// fleche (avions, navires, objets en mouvement) : image SDF coloree par type
map.on("load", () => {
  const s = 32, c = document.createElement("canvas");
  c.width = c.height = s;
  const g = c.getContext("2d");
  g.fillStyle = "#000";
  g.beginPath(); g.moveTo(s / 2, 2); g.lineTo(s - 6, s - 4); g.lineTo(s / 2, s - 11); g.lineTo(6, s - 4); g.closePath(); g.fill();
  map.addImage("arrow", g.getImageData(0, 0, s, s), { sdf: true });
  // avion vu de dessus, nez vers le haut (meme dessin que le radar de l'ecran)
  const PLANE = [[0, -1], [0.12, -0.82], [0.12, -0.28], [1, 0.12], [1, 0.3], [0.12, 0.08], [0.09, 0.62],
                 [0.4, 0.84], [0.4, 0.98], [0, 0.88], [-0.4, 0.98], [-0.4, 0.84], [-0.09, 0.62],
                 [-0.12, 0.08], [-1, 0.3], [-1, 0.12], [-0.12, -0.28], [-0.12, -0.82]];
  g.clearRect(0, 0, s, s);
  g.beginPath();
  PLANE.forEach(([x, y], i) => g[i ? "lineTo" : "moveTo"](s / 2 + x * 14, s / 2 + y * 14));
  g.closePath(); g.fill();
  map.addImage("plane", g.getImageData(0, 0, s, s), { sdf: true });
  Promise.all([symSheets, sondeIcon]).then(([im, sonde]) => {
    if (sonde) map.addImage("balloon", sonde, { pixelRatio: 2 });
    sheets = im;
    refresh();
    setInterval(refresh, 2000);
  });
});

// Icone APRS "aprs:/-" demandee par la couche obj-icon : sa case, plus le
// caractere d'overlay, recopies dans une image de 48 px (24 px a l'ecran)
map.on("styleimagemissing", (e) => {
  const p = sheets && e.id.startsWith("aprs:") ? symParts(e.id.slice(5)) : null;
  if (!p || map.hasImage(e.id)) return;
  const s = 48, c = document.createElement("canvas");
  c.width = c.height = s;
  const g = c.getContext("2d");
  const cell = (im, k) => g.drawImage(im, (k % 16) * s, Math.floor(k / 16) * s, s, s, 0, 0, s, s);
  cell(sheets[p.table], p.code);
  if (p.ov !== undefined) cell(sheets[2], p.ov);
  map.addImage(e.id, g.getImageData(0, 0, s, s), { pixelRatio: 2 });
});

let centered = false;
map.on("error", (e) => {
  console.error("map:", e && e.error ? e.error.message || e.error : e);
  if (e && e.sourceId === "osm") document.getElementById("nomap").style.display = "block";
});

function km(a, b) {
  const r = Math.PI / 180, dl = (b.lat - a.lat) * r, dn = (b.lon - a.lon) * r;
  const h = Math.sin(dl / 2) ** 2 + Math.cos(a.lat * r) * Math.cos(b.lat * r) * Math.sin(dn / 2) ** 2;
  return 12742 * Math.asin(Math.sqrt(h));
}

function ring(st, radius) {
  const pts = [];
  for (let i = 0; i <= 96; i++) {
    const t = i / 96 * 2 * Math.PI;
    pts.push([st.lon + radius / (111.32 * Math.cos(st.lat * Math.PI / 180)) * Math.sin(t), st.lat + radius / 110.57 * Math.cos(t)]);
  }
  return { type: "Feature", properties: { label: radius + " km" }, geometry: { type: "LineString", coordinates: pts } };
}

function esc(s) { return String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c])); }

function age(s) { return s < 60 ? s + " s" : (s < 3600 ? Math.round(s / 60) + " min" : Math.round(s / 3600) + " h"); }

// Tableau des capteurs (comme l'ecran du Pi) : le plus recent en haut ; les
// valeurs (separees par deux espaces dans "info") en pastilles
function sensorTable(st) {
  const ss = (st.sensors || []).slice().sort((a, b) => a.age - b.age);
  const sub = (st.running ? "Receiving on " + st.freq + " MHz" : "Receiver stopped") + " · " + ss.length + " sensor" +
    (ss.length === 1 ? "" : "s");
  document.getElementById("stitle").textContent = "Multi Decoder : " + st.title;
  document.getElementById("ssub").textContent = sub;
  setHead('<th>Sensor</th><th>ID</th><th>Values</th><th class="num opt">RSSI dB</th><th class="num">Age</th>' +
          '<th class="num opt">Count</th>');
  document.getElementById("sbody").innerHTML = ss.length ? ss.map((s) =>
    "<tr><td><b>" + esc(s.model) + "</b></td><td>" + esc(s.id) + "</td><td>" +
    String(s.info || "").split(/\s{2,}/).filter(Boolean).map((v) => '<span class="val">' + esc(v) + "</span>").join("") +
    '</td><td class="num opt">' + (s.rssi !== undefined ? s.rssi.toFixed(1) : "") + '</td><td class="num">' + age(s.age) +
    '</td><td class="num opt">' + s.count + "</td></tr>").join("") :
    '<tr><td colspan="6" class="none">No sensor received yet</td></tr>';
}

function setHead(html) {
  const h = document.getElementById("sthead");
  if (h.dataset.html !== html) { h.innerHTML = "<tr>" + html + "</tr>"; h.dataset.html = html; }
}

// Ecoute DAB+ dans le navigateur : flux MP3 de welle-cli (port 7979 du Pi)
let playing = "";
function stopPlayer() {
  const a = document.getElementById("player");
  if (playing) { a.pause(); a.removeAttribute("src"); a.load(); playing = ""; }
}
function togglePlay(id) {
  const a = document.getElementById("player"), was = playing;
  stopPlayer();
  if (was !== id) {
    a.src = "http://" + location.hostname + ":7979/mp3/" + id;
    a.play().catch(() => {});
    playing = id;
  }
  document.querySelectorAll(".play").forEach((b) => b.classList.toggle("on", b.dataset.id === playing));
}

// Radio : station (RDS) ou multiplex (DAB+), heure diffusee et ecart de
// l'horloge du Pi, puis stations FM (balayage) ou services du multiplex
function radioTable(st) {
  const r = st.radio, dab = r.kind === "dab";
  const sv = (r.services || []).slice().sort(dab ? (a, b) => a.name.localeCompare(b.name) : (a, b) => (a.mhz || 0) - (b.mhz || 0));
  document.getElementById("stitle").textContent = "Multi Decoder : " + st.title;
  document.getElementById("ssub").textContent = (st.running ? "Receiving on " + st.freq + " MHz" : "Receiver stopped") +
    " · " + sv.length + (dab ? " services" : " station" + (sv.length === 1 ? "" : "s"));
  let tl = "Time : not received yet";
  if (r.utc !== undefined) {
    const d = new Date((r.utc + r.lto * 60) * 1000), p = (n) => String(n).padStart(2, "0");
    const t = p(d.getUTCDate()) + "/" + p(d.getUTCMonth() + 1) + "/" + d.getUTCFullYear() + " " + p(d.getUTCHours()) + ":" +
      p(d.getUTCMinutes()) + " UTC" + (r.lto >= 0 ? "+" : "") + (r.lto % 60 ? (r.lto / 60).toFixed(1) : r.lto / 60);
    const cls = Math.abs(r.diff) < 1 ? "ok" : (Math.abs(r.diff) < 5 ? "warn" : "bad");
    tl = (dab ? "DAB " : "RDS ") + esc(t) + ' &nbsp; <span class="' + cls + '">Pi clock ' + (r.diff >= 0 ? "+" : "") +
      r.diff.toFixed(1) + " s</span>" + (r.ntp ? " (NTP)" : "") + (r.tage > 90 ? " · received " + age(r.tage) + " ago" : "");
  } else if (r.ntp) {
    tl += " · Pi clock : NTP synchronized";
  }
  const info = dab ? esc(r.extra) : esc([r.pty, r.tp ? "TP" : "", r.ta ? "TA" : ""].filter(Boolean).join(" · "));
  document.getElementById("rinfo").innerHTML =
    '<div class="rname">' + esc(r.name || (st.running ? "Waiting..." : "-")) + (r.id ? '<span class="rid">' +
    (dab ? "ID " : "PI ") + esc(r.id) + "</span>" : "") + "</div>" +
    (info ? '<div class="rline">' + info + "</div>" : "") +
    (!dab && r.extra ? '<div class="rline">' + esc(r.extra) + "</div>" : "") +
    '<div class="rline">' + tl + "</div>" + (!dab && r.text ? '<div class="rtext">' + esc(r.text) + "</div>" : "");
  if (dab) {
    setHead('<th>Service</th><th>Type</th><th class="opt">PTY</th><th>Now playing</th><th class="num">Listen</th>');
    document.getElementById("sbody").innerHTML = sv.length ? sv.map((s) =>
      "<tr><td><b>" + esc(s.name || s.id) + "</b></td><td>" + esc(s.kind) + '</td><td class="opt">' + esc(s.pty) +
      "</td><td>" + esc(s.info) + '</td><td class="num"><button class="play' + (playing === s.id ? " on" : "") +
      '" data-id="' + esc(s.id) + '">&#9654;</button></td></tr>').join("") :
      '<tr><td colspan="5" class="none">No service received yet</td></tr>';
    document.querySelectorAll(".play").forEach((b) => b.addEventListener("click", () => togglePlay(b.dataset.id)));
  } else {
    setHead('<th class="num">MHz</th><th>Station</th><th class="opt">PI</th><th class="opt">PTY</th><th>RadioText</th>');
    const fq = parseFloat(st.freq);
    document.getElementById("sbody").innerHTML = sv.length ? sv.map((s) =>
      "<tr" + (Math.abs((s.mhz || 0) - fq) < 0.05 ? ' class="cur"' : "") + '><td class="num">' + (s.mhz ? s.mhz.toFixed(1) : "") +
      "</td><td><b>" + esc(s.name || "-") + '</b></td><td class="opt">' + esc(s.kind) + '</td><td class="opt">' + esc(s.pty) +
      "</td><td>" + esc(s.info) + "</td></tr>").join("") :
      '<tr><td colspan="5" class="none">No RDS station received yet</td></tr>';
  }
}

async function refresh() {
  let st;
  try {
    const r = await fetch("state.json?" + Date.now(), { cache: "no-store" });
    if (!r.ok) throw new Error(r.status);
    st = await r.json();
  } catch (e) {
    document.getElementById("sub").textContent = "Multi Decoder not running";
    return;
  }
  // capteurs ISM 433/868 et radio (RDS, DAB+) : pas de position, un tableau a
  // la place de la carte
  const ism = st.decoder === "ism", table = ism || !!st.radio;
  if (table !== document.body.classList.contains("sensors")) {
    document.body.classList.toggle("sensors", table);
    if (!table) map.resize();            // la carte etait masquee
  }
  document.getElementById("rinfo").style.display = st.radio ? "block" : "none";
  if (!st.radio) stopPlayer();
  if (st.radio) {
    radioTable(st);
    return;
  }
  if (ism) {
    sensorTable(st);
    return;
  }
  const objs = st.objects || [], station = st.station;
  const feats = [], trails = [];
  for (const o of objs) {
    const color = COLORS[o.type] || "#333";
    // etiquette : nom (vol, navire...) ; APRS et radiosonde : indicatif ou numero de serie
    const p = { type: o.type, color, label: o.name && o.type !== "aprs" && o.type !== "sonde" ? o.name : o.id };
    if (o.crs !== undefined && (o.type === "aircraft" || (o.spd !== undefined && o.spd > 1))) p.crs = o.crs;
    if (o.type === "aprs" && sheets && symParts(o.sym)) p.icon = "aprs:" + o.sym;
    if (o.type === "sonde" && map.hasImage("balloon")) p.icon = "balloon";
    feats.push({ type: "Feature", properties: p, geometry: { type: "Point", coordinates: [o.lon, o.lat] } });
    if (o.trail && o.trail.length > 1) {
      trails.push({ type: "Feature", properties: { color, type: o.type },
                    geometry: { type: "LineString", coordinates: o.trail.map((t) => [t[1], t[0]]) } });
    }
  }
  if (st.track && st.track.points && st.track.points.length > 1) {
    trails.push({ type: "Feature", properties: { color: COLORS.sonde, type: "sonde" },
                  geometry: { type: "LineString", coordinates: st.track.points.map((t) => [t[1], t[0]]) } });
  }
  if (station) {
    feats.push({ type: "Feature", properties: { type: "home", color: "#ffb703", label: "Station" },
                 geometry: { type: "Point", coordinates: [station.lon, station.lat] } });
    const radii = st.decoder === "adsb" ? [50, 100, 200] : (st.decoder === "ft8" || st.decoder === "wspr" ? [500, 1000, 2000] : [25, 50, 100]);
    map.getSource("rings").setData({ type: "FeatureCollection", features: radii.map((r) => ring(station, r)) });
    if (!centered) { map.jumpTo({ center: [station.lon, station.lat], zoom: st.decoder === "ft8" || st.decoder === "wspr" ? 4 : 7 }); centered = true; }
  }
  map.getSource("obj").setData({ type: "FeatureCollection", features: feats });
  map.getSource("trails").setData({ type: "FeatureCollection", features: trails });

  document.getElementById("title").textContent = "Multi Decoder : " + st.title;
  document.getElementById("sub").textContent = (st.running ? "Receiving on " + st.freq + " MHz" : "Receiver stopped") +
    " · " + objs.length + " on map";
  const rows = objs.map((o) => ({ o, d: station ? km(station, o) : 0 })).sort((a, b) => a.d - b.d);
  document.getElementById("list").innerHTML = rows.map(({ o, d }, i) => {
    const bits = [];
    if (o.alt !== undefined) bits.push(o.type === "aircraft" ? Math.round(o.alt / 0.3048) + " ft" : Math.round(o.alt) + " m");
    if (o.spd !== undefined) bits.push(Math.round(o.spd) + " kt");
    if (station) bits.push(d.toFixed(d < 10 ? 1 : 0) + " km");
    bits.push(age(o.age));
    const mark = (o.type === "aprs" && sheets && symParts(o.sym)) ? symHtml(o.sym) :
      '<span class="dot" style="background:' + (COLORS[o.type] || "#333") + '"></span>';
    return '<div class="row" data-i="' + i + '">' + mark + "<b>" +
      esc(o.id) + "</b> " + esc(o.name && o.name !== o.id ? o.name : "") + '<div class="meta">' + esc(NAMES[o.type] || o.type) + " · " +
      esc(bits.join(" · ")) + (o.info ? "<br>" + esc(o.info) : "") + "</div></div>";
  }).join("");
  document.querySelectorAll(".row").forEach((el) => el.addEventListener("click", () => {
    const o = rows[+el.dataset.i].o;
    map.flyTo({ center: [o.lon, o.lat], zoom: Math.max(map.getZoom(), 9) });
  }));
}
