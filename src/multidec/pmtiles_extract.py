#!/usr/bin/env python3
"""pmtiles_extract.py : extrait une region d'une archive PMTiles v3 distante
(fond de carte vectoriel OpenStreetMap de Protomaps) jusqu'a un zoom maximal,
par requetes HTTP partielles. Python 3.5+, bibliotheque standard seulement.

Usage :
  pmtiles_extract.py SOURCE_URL SORTIE.pmtiles --bbox O,S,E,N --maxzoom Z [--dry-run]

Donnees : (c) les contributeurs d'OpenStreetMap (ODbL), tuiles Protomaps.
"""
import argparse
import gzip
import json
import math
import os
import struct
import sys
import threading
import time
import urllib.request

HEADER_LEN = 127


# ---------------------------------------------------------------- tuiles

def zxy_to_tileid(z, x, y):
    """Indice de tuile PMTiles : tuiles des zooms inferieurs + courbe de Hilbert."""
    acc = ((1 << (2 * z)) - 1) // 3
    n = 1 << z
    d = 0
    s = n >> 1
    while s > 0:
        rx = 1 if (x & s) else 0
        ry = 1 if (y & s) else 0
        d += s * s * ((3 * rx) ^ ry)
        if ry == 0:
            if rx == 1:
                x = n - 1 - x
                y = n - 1 - y
            x, y = y, x
        s >>= 1
    return acc + d


def lonlat_to_tile(lon, lat, z):
    n = 1 << z
    x = int((lon + 180.0) / 360.0 * n)
    lat = max(min(lat, 85.0511), -85.0511)
    r = math.radians(lat)
    y = int((1.0 - math.log(math.tan(r) + 1.0 / math.cos(r)) / math.pi) / 2.0 * n)
    return min(max(x, 0), n - 1), min(max(y, 0), n - 1)


# ---------------------------------------------------------------- format

def read_varint(b, pos):
    shift = result = 0
    while True:
        c = b[pos]
        pos += 1
        result |= (c & 0x7F) << shift
        if not c & 0x80:
            return result, pos
        shift += 7


def write_varint(out, v):
    while True:
        c = v & 0x7F
        v >>= 7
        if v:
            out.append(c | 0x80)
        else:
            out.append(c)
            return


def decompress(data, comp):
    if comp in (0, 1):
        return data
    if comp == 2:
        return gzip.decompress(data)
    raise SystemExit("compression interne non geree : %d" % comp)


def parse_header(b):
    if b[:7] != b"PMTiles" or b[7] != 3:
        raise SystemExit("archive PMTiles v3 attendue")
    f = struct.unpack_from("<QQQQQQQQQQQBBBBBBiiiiBii", b, 8)
    keys = ["root_off", "root_len", "meta_off", "meta_len", "leaf_off", "leaf_len", "data_off", "data_len",
            "addressed", "entries", "contents", "clustered", "icomp", "tcomp", "ttype", "minz", "maxz",
            "minlon", "minlat", "maxlon", "maxlat", "cz", "clon", "clat"]
    return dict(zip(keys, f))


def parse_dir(b):
    n, p = read_varint(b, 0)
    ids, last = [], 0
    for _ in range(n):
        v, p = read_varint(b, p)
        last += v
        ids.append(last)
    runs, lens, offs = [], [], []
    for _ in range(n):
        v, p = read_varint(b, p)
        runs.append(v)
    for _ in range(n):
        v, p = read_varint(b, p)
        lens.append(v)
    for i in range(n):
        v, p = read_varint(b, p)
        offs.append(offs[i - 1] + lens[i - 1] if (v == 0 and i > 0) else v - 1)
    return list(zip(ids, runs, offs, lens))


def build_dir(entries):
    out = bytearray()
    write_varint(out, len(entries))
    last = 0
    for e in entries:
        write_varint(out, e[0] - last)
        last = e[0]
    for e in entries:
        write_varint(out, e[1])
    for e in entries:
        write_varint(out, e[3])
    for i, e in enumerate(entries):
        if i > 0 and e[2] == entries[i - 1][2] + entries[i - 1][3]:
            write_varint(out, 0)
        else:
            write_varint(out, e[2] + 1)
    return gzip.compress(bytes(out), 9)


# ---------------------------------------------------------------- reseau

class Remote:
    def __init__(self, url):
        self.url = url
        self.bytes = 0
        self.lock = threading.Lock()

    def get(self, off, length):
        for attempt in range(6):
            try:
                req = urllib.request.Request(self.url, headers={
                    "Range": "bytes=%d-%d" % (off, off + length - 1), "User-Agent": "portsdown-multidec"})
                with urllib.request.urlopen(req, timeout=60) as r:
                    data = r.read()
                if len(data) != length:
                    raise IOError("reponse courte")
                with self.lock:
                    self.bytes += length
                return data
            except Exception as e:  # nouvel essai apres une pause croissante
                if attempt == 5:
                    raise
                time.sleep(2 + attempt * 3)


def find(remote, h, entries, tid, leaf_cache):
    """Entree (offset, longueur) de la tuile tid, en descendant dans les feuilles."""
    lo, hi = 0, len(entries) - 1
    best = None
    while lo <= hi:
        m = (lo + hi) // 2
        if entries[m][0] <= tid:
            best = m
            lo = m + 1
        else:
            hi = m - 1
    if best is None:
        return None
    e = entries[best]
    if e[1] == 0:                                     # repertoire feuille
        key = e[2]
        if key not in leaf_cache:
            leaf_cache[key] = parse_dir(decompress(remote.get(h["leaf_off"] + e[2], e[3]), h["icomp"]))
        return find(remote, h, leaf_cache[key], tid, leaf_cache)
    if tid < e[0] + e[1]:
        return (e[2], e[3])
    return None


# ---------------------------------------------------------------- extraction

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source")
    ap.add_argument("output")
    ap.add_argument("--bbox", required=True, help="ouest,sud,est,nord (degres)")
    ap.add_argument("--maxzoom", type=int, required=True)
    ap.add_argument("--dry-run", action="store_true", help="taille seulement")
    ap.add_argument("--threads", type=int, default=4)
    a = ap.parse_args()
    w, s, e, n = [float(v) for v in a.bbox.split(",")]

    remote = Remote(a.source)
    first = remote.get(0, 16384)
    h = parse_header(first)
    root = parse_dir(decompress(first[h["root_off"]:h["root_off"] + h["root_len"]], h["icomp"]))
    maxz = min(a.maxzoom, h["maxz"])

    # tuiles voulues -> contenu dans la source
    leaf_cache = {}
    wanted = []                                       # (tileid, src_off, src_len)
    per_zoom = []
    for z in range(h["minz"], maxz + 1):
        x0, y0 = lonlat_to_tile(w, n, z)
        x1, y1 = lonlat_to_tile(e, s, z)
        cnt = 0
        size = 0
        seen = set()
        for x in range(x0, x1 + 1):
            for y in range(y0, y1 + 1):
                tid = zxy_to_tileid(z, x, y)
                r = find(remote, h, root, tid, leaf_cache)
                if r:
                    wanted.append((tid, r[0], r[1]))
                    cnt += 1
                    if r not in seen:
                        seen.add(r)
                        size += r[1]
        per_zoom.append((z, cnt, size))
        print("zoom %2d : %6d tuiles, %8.1f Mo" % (z, cnt, size / 1e6), flush=True)
    wanted.sort()
    unique = sorted(set((o, l) for _, o, l in wanted))
    total = sum(l for _, l in unique)
    print("Total : %d tuiles, %d contenus, %.1f Mo de tuiles" % (len(wanted), len(unique), total / 1e6), flush=True)
    if a.dry_run:
        return

    # telechargement : contenus voisins regroupes (ecart < 64 ko, bloc <= 8 Mo)
    spans = []
    for o, l in unique:
        if spans and o - (spans[-1][0] + spans[-1][1]) < 65536 and o + l - spans[-1][0] <= 8 << 20:
            spans[-1][1] = o + l - spans[-1][0]
            spans[-1][2].append((o, l))
        else:
            spans.append([o, l, [(o, l)]])
    tmp = a.output + ".part"
    newoff = {}
    out_lock = threading.Lock()
    done = [0]
    data_f = open(tmp, "wb")
    pos = [0]
    order = {k: i for i, k in enumerate(unique)}
    pending = {}
    next_i = [0]

    def flush_ready():
        # ecrit les contenus dans l'ordre de la source (archive groupee)
        while next_i[0] < len(unique) and unique[next_i[0]] in pending:
            k = unique[next_i[0]]
            b = pending.pop(k)
            newoff[k] = (pos[0], len(b))
            data_f.write(b)
            pos[0] += len(b)
            next_i[0] += 1

    def worker(items):
        for sp in items:
            blob = remote.get(h["data_off"] + sp[0], sp[1])
            with out_lock:
                for o, l in sp[2]:
                    pending[(o, l)] = blob[o - sp[0]:o - sp[0] + l]
                flush_ready()
                done[0] += 1
                if done[0] % 20 == 0 or done[0] == len(spans):
                    print("  %d/%d blocs, %.1f Mo" % (done[0], len(spans), remote.bytes / 1e6), flush=True)

    chunks = [spans[i::a.threads] for i in range(a.threads)]
    th = [threading.Thread(target=worker, args=(c,)) for c in chunks]
    for t in th:
        t.start()
    for t in th:
        t.join()
    with out_lock:
        flush_ready()
    data_f.close()
    if len(newoff) != len(unique):
        raise SystemExit("telechargement incomplet")

    # repertoires : suites de tuiles consecutives de meme contenu regroupees
    entries = []
    for tid, o, l in wanted:
        no, nl = newoff[(o, l)]
        if entries and entries[-1][0] + entries[-1][1] == tid and entries[-1][2] == no and entries[-1][3] == nl:
            entries[-1] = (entries[-1][0], entries[-1][1] + 1, no, nl)
        else:
            entries.append((tid, 1, no, nl))
    root_b = build_dir(entries)
    leaves_b = b""
    leaf_size = 4096
    while HEADER_LEN + len(root_b) > 16384:          # racine dans les 16 premiers ko
        leaves, rootents = bytearray(), []
        for i in range(0, len(entries), leaf_size):
            part = entries[i:i + leaf_size]
            lb = build_dir(part)
            rootents.append((part[0][0], 0, len(leaves), len(lb)))
            leaves += lb
        root_b = build_dir(rootents)
        leaves_b = bytes(leaves)
        leaf_size *= 2
    meta = remote.get(h["meta_off"], h["meta_len"]) if h["meta_len"] else gzip.compress(b"{}")
    if h["icomp"] != 2:
        meta = gzip.compress(decompress(meta, h["icomp"]))

    root_off = HEADER_LEN
    meta_off = root_off + len(root_b)
    leaf_off = meta_off + len(meta)
    data_off = leaf_off + len(leaves_b)
    hdr = bytearray(b"PMTiles\x03")
    hdr += struct.pack("<QQQQQQQQQQQBBBBBBiiiiBii", root_off, len(root_b), meta_off, len(meta), leaf_off,
                       len(leaves_b), data_off, pos[0], len(wanted), len(entries), len(unique), 1, 2, h["tcomp"],
                       h["ttype"], h["minz"], maxz, int(w * 1e7), int(s * 1e7), int(e * 1e7), int(n * 1e7),
                       min(6, maxz), int((w + e) / 2 * 1e7), int((s + n) / 2 * 1e7))
    assert len(hdr) == HEADER_LEN
    with open(a.output + ".new", "wb") as f:
        f.write(hdr)
        f.write(root_b)
        f.write(meta)
        f.write(leaves_b)
        with open(tmp, "rb") as d:
            while True:
                b = d.read(1 << 20)
                if not b:
                    break
                f.write(b)
    os.remove(tmp)
    os.replace(a.output + ".new", a.output)
    print("Ecrit : %s (%.1f Mo)" % (a.output, os.path.getsize(a.output) / 1e6), flush=True)


if __name__ == "__main__":
    main()
