#!/usr/bin/env python3
# md_radio.py : aide du decodeur multiple pour la radio (RDS, DAB+), appele par
# run_decoder.sh. Ce qui est ecrit sur stdout est lu par multidec.
#
#   md_radio.py dab-poll <port> <canal>  lit mux.json de welle-cli (serveur web
#       local) et n'ecrit que les changements, une ligne JSON chacun :
#       {"type": "ens" | "svc" | "time" | "msg", ...} ; "time" (heure UTC du
#       multiplex, a la minute) est ecrit au changement de minute. Ecoute sur
#       le Pi : le service dont l'identifiant est dans $MD_SHM/dab_play (ecrit
#       par multidec) est joue par ffmpeg (MP3 de welle-cli) -> md_audio.
#   md_radio.py dab-scan    balayage de la bande III (canaux 5A a 13F)
#   md_radio.py fm-scan     balayage FM (87,5 a 108 MHz), stations RDS
#       Balayages : "SCANNING <texte>", "FOUND <MHz> <cle> <nom>", puis
#       "SCAN <MHz> <dB> <nom>" (station a ecouter) ou "SCANEND" (rien trouve).
#   md_radio.py gain-index <dB|auto>  indice de gain de welle-cli (-1 : auto)
#
# Environnement (multidec) : MD_GAIN, MD_PPM.
# Python 3.7 (Raspbian Buster), bibliotheque standard seulement.

import calendar
import json
import math
import os
import select
import signal
import statistics
import subprocess
import sys
import time
import urllib.request

DAB_CH = [("5A", 174.928), ("5B", 176.640), ("5C", 178.352), ("5D", 180.064), ("6A", 181.936), ("6B", 183.648),
          ("6C", 185.360), ("6D", 187.072), ("7A", 188.928), ("7B", 190.640), ("7C", 192.352), ("7D", 194.064),
          ("8A", 195.936), ("8B", 197.648), ("8C", 199.360), ("8D", 201.072), ("9A", 202.928), ("9B", 204.640),
          ("9C", 206.352), ("9D", 208.064), ("10A", 209.936), ("10N", 210.096), ("10B", 211.648), ("10C", 213.360),
          ("10D", 215.072), ("11A", 216.928), ("11N", 217.088), ("11B", 218.640), ("11C", 220.352), ("11D", 222.064),
          ("12A", 223.936), ("12N", 224.096), ("12B", 225.648), ("12C", 227.360), ("12D", 229.072), ("13A", 230.784),
          ("13B", 232.496), ("13C", 234.208), ("13D", 235.776), ("13E", 237.488), ("13F", 239.200)]

# Gains des tuners R820T / R828D (dixiemes de dB), dans l'ordre de librtlsdr
R820T_GAINS = [0, 9, 14, 27, 37, 77, 87, 125, 144, 157, 166, 197, 207, 229, 254, 280, 297, 328, 338, 364, 372, 386,
               402, 421, 434, 439, 445, 480, 496]

SCAN_PORT = 7980
MD_AUDIO = "/home/pi/rpidatv/bin/md_audio"


def out(s):
    print(s, flush=True)


def emit(obj):
    out(json.dumps(obj, ensure_ascii=False, separators=(",", ":")))


def gain_index(g):
    if g in ("", "auto"):
        return -1
    tenths = float(g) * 10
    return min(range(len(R820T_GAINS)), key=lambda i: abs(R820T_GAINS[i] - tenths))


def rtl_opts():
    o = ["-p", os.environ.get("MD_PPM", "0")]
    g = os.environ.get("MD_GAIN", "auto")
    if g != "auto":
        o += ["-g", g]
    return o


def power_scan(f0, f1):
    # Un passage de rtl_power (bins de 25 kHz) : [(Hz, dB)]
    cmd = ["rtl_power", "-f", "%dM:%dM:25k" % (f0, f1), "-i", "2", "-1"] + rtl_opts() + ["-"]
    try:
        txt = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=60).stdout.decode()
    except subprocess.TimeoutExpired:
        return []
    pts = {}
    for ln in txt.splitlines():
        f = [x.strip() for x in ln.split(",")]
        if len(f) < 7:
            continue
        lo, step = float(f[2]), float(f[4])
        for i, v in enumerate(f[6:]):
            try:
                x = float(v)
            except ValueError:
                continue
            if math.isfinite(x):         # rtl_power ecrit parfois "nan"
                pts[lo + i * step] = x
    return sorted(pts.items())


# ------------------------------------------------------------------ DAB+

def mux(port, timeout=3):
    with urllib.request.urlopen("http://127.0.0.1:%d/mux.json" % port, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8", "replace"))


def label(x):
    return ((x or {}).get("label") or "").strip()


def service(s):
    kind, kbps = "", 0
    for c in s.get("components") or []:
        if c.get("transportmode") == "audio":
            kind = c.get("ascty") or ""
            kbps = (c.get("subchannel") or {}).get("bitrate") or 0
            break
    return {"type": "svc", "sid": s.get("sid", ""), "label": label(s.get("label")),
            "pty": (s.get("ptystring") or "").strip(), "kind": kind, "kbps": kbps,
            "dls": label(s.get("dls")), "mp3": s.get("url_mp3") or ""}


class DabPlayer:
    # Service joue sur le Pi : ffmpeg (MP3 de welle-cli -> PCM 48 kHz stereo)
    # puis md_audio (haut-parleur et page web), dans le groupe de processus de
    # la chaine (arretes avec elle)
    def __init__(self, port):
        self.port, self.sid, self.procs, self.t = port, "", [], 0
        self.req = os.path.join(os.environ.get("MD_SHM", "/dev/shm/multidec"), "dab_play")

    def stop(self):
        for p in self.procs:
            p.terminate()
        for p in self.procs:
            try:
                p.wait(1)                 # md_audio rend la carte virtuelle aussitot
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()
        self.procs = []

    def check(self):
        try:
            with open(self.req) as f:
                want = f.read().strip()
        except OSError:
            want = ""
        alive = self.procs and all(p.poll() is None for p in self.procs)
        if want == self.sid and (alive or not want or time.time() - self.t < 3):
            return
        self.stop()
        self.sid, self.t = want, time.time()
        if want:
            # depart rapide : format impose, analyse de 2 ko seulement (sinon
            # ffmpeg lit plusieurs secondes du flux avant de jouer : 2,2 s au
            # premier depart, 6,4 s a la reprise d'un service ; ainsi ~1 s)
            ff = subprocess.Popen(["ffmpeg", "-nostdin", "-loglevel", "error", "-probesize", "2048",
                                   "-analyzeduration", "0", "-fflags", "nobuffer", "-flags", "low_delay", "-f", "mp3",
                                   "-i", "http://127.0.0.1:%d/mp3/%s" % (self.port, want), "-f", "s16le", "-ar", "48000",
                                   "-ac", "2", "-"], stdout=subprocess.PIPE)
            au = subprocess.Popen([MD_AUDIO, "-r", "48000", "-c", "2"], stdin=ff.stdout)
            ff.stdout.close()
            self.procs = [ff, au]


def dab_poll(port, chan):
    player = DabPlayer(port)
    try:
        dab_loop(port, chan, player)
    finally:
        player.stop()


def dab_loop(port, chan, player):
    last_svc, last_ens, last_min, last_snr = {}, None, None, 0
    start, fails, said = time.time(), 0, 0
    while True:
        player.check()
        try:
            j = mux(port)
            fails = 0
        except Exception:
            fails += 1
            if time.time() - start > 20 and fails > 40:
                sys.stderr.write("welle-cli ne repond plus\n")
                sys.exit(1)
            if time.time() - said > 5:
                emit({"type": "msg", "text": "Starting DAB receiver on %s" % chan})
                said = time.time()
            time.sleep(0.5)
            continue
        ens = j.get("ensemble") or {}
        svcs = j.get("services") or []
        snr = (j.get("demodulator") or {}).get("snr") or 0
        name = label(ens.get("label"))
        if not name and not svcs:
            if time.time() - said > 5:
                emit({"type": "msg", "text": "Searching for a DAB multiplex on %s" % chan})
                said = time.time()
        else:
            e = {"type": "ens", "label": name, "id": ens.get("id") or "", "n": len(svcs), "chan": chan}
            if e != last_ens or time.time() - last_snr > 5:
                emit(dict(e, snr=round(float(snr), 1)))
                if e != last_ens and said:
                    emit({"type": "msg", "text": ""})
                last_ens, last_snr = e, time.time()
        for s in svcs:
            v = service(s)
            if v["sid"] and last_svc.get(v["sid"]) != v:
                emit(v)
                last_svc[v["sid"]] = v
        # heure du multiplex (UTC, a la minute) : ecrite quand la minute change
        u = j.get("utctime") or {}
        if (u.get("year") or 0) > 2000:
            key = (u["year"], u["month"], u["day"], u["hour"], u["minutes"])
            if last_min is not None and key != last_min:
                utc = calendar.timegm(key + (0,))
                emit({"type": "time", "utc": utc, "lto": int(round(float(u.get("lto") or 0) * 60))})
            last_min = key
        time.sleep(0.25)


def welle_try(chan, wait=14):
    # welle-cli sur un canal le temps d'y lire un multiplex : (nom, services)
    p = subprocess.Popen(["welle-cli", "-c", chan, "-T", "-g", str(gain_index(os.environ.get("MD_GAIN", "auto"))),
                          "-w", str(SCAN_PORT)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    name, n, t0 = "", 0, time.time()
    try:
        while time.time() - t0 < wait and p.poll() is None:
            time.sleep(1)
            try:
                j = mux(SCAN_PORT, 2)
            except Exception:
                continue
            name, n = label((j.get("ensemble") or {}).get("label")), len(j.get("services") or [])
            if name and n:
                time.sleep(2)            # laisser arriver les derniers services
                try:
                    n = len(mux(SCAN_PORT, 2).get("services") or [])
                except Exception:
                    pass
                break
    finally:
        p.terminate()
        try:
            p.wait(5)
        except subprocess.TimeoutExpired:
            p.kill()
    return name, n


def dab_scan():
    out("SCANNING Band III power")
    pts = power_scan(174, 240)
    if not pts:
        out("SCANEND")
        return
    floor = statistics.median(v for _, v in pts)
    cand = []
    for ch, f in DAB_CH:
        vals = [v for fr, v in pts if abs(fr / 1e6 - f) < 0.65]
        if vals and statistics.mean(vals) - floor >= 6:
            cand.append((statistics.mean(vals) - floor, ch, f))
    cand.sort(reverse=True)
    tried, best = [], None
    for db, ch, f in cand:
        if len(tried) >= 8 or any(abs(f - g) < 1.5 for g in tried):
            continue
        tried.append(f)
        out("SCANNING %s %.3f" % (ch, f))
        name, n = welle_try(ch)
        if name:
            out("FOUND %.3f %s %s (%d services)" % (f, ch, name, n))
            if best is None:
                best = (f, db, name)
    if best:
        out("SCAN %.3f %.0f %s" % best)
    else:
        out("SCANEND")


# ------------------------------------------------------------------ FM RDS

def stop_group(p):
    # Arret de toute la chaine (rtl_fm compris) et attente de sa fin : la cle
    # doit etre liberee avant l'essai suivant
    try:
        os.killpg(p.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    p.wait()
    t0 = time.time()
    while time.time() - t0 < 4:
        try:
            os.killpg(p.pid, 0)
        except ProcessLookupError:
            return
        time.sleep(0.1)
    try:
        os.killpg(p.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    time.sleep(0.3)


def rds_try(mhz, wait=6.0):
    # rtl_fm | redsea le temps de lire le nom (PS) : (pi, nom). Signal faible :
    # nom partiel (-p), ou "?" si le PI seul a ete lu (3 fois au moins).
    # stdbuf : sans lui, redsea garde ses lignes en tampon, perdues a l'arret
    cmd = "rtl_fm -M fm -l 0 -A std -s 171k -F 9 -f %.1fM %s - 2>/dev/null | stdbuf -oL redsea -p -r 171k 2>/dev/null" % (
        mhz, " ".join(rtl_opts()))
    p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, start_new_session=True)
    fd, buf, t0 = p.stdout.fileno(), b"", time.time()
    pis, ps, partial = {}, "", ""
    try:
        while time.time() - t0 < wait and not ps:
            if not select.select([fd], [], [], 0.3)[0]:
                continue
            chunk = os.read(fd, 65536)
            if not chunk:
                break
            buf += chunk
            while b"\n" in buf:
                ln, buf = buf.split(b"\n", 1)
                try:
                    j = json.loads(ln.decode("utf-8", "replace"))
                except ValueError:
                    continue
                if j.get("pi"):
                    pis[j["pi"]] = pis.get(j["pi"], 0) + 1
                if j.get("ps"):
                    ps = j["ps"].strip()
                elif j.get("partial_ps", "").strip():
                    partial = j["partial_ps"].strip()
    finally:
        stop_group(p)
    pi = max(pis, key=pis.get) if pis else ""
    if not pi or (pis[pi] < 3 and not ps):
        return "", ""
    return pi, ps or partial or "?"


def fm_scan():
    out("SCANNING FM band power")
    pts = power_scan(87, 109)
    if not pts:
        out("SCANEND")
        return
    # canaux de 100 kHz : maximum des raies pour trouver les stations
    # (plancher : 20e centile, la bande est pleine), puis centre de puissance
    # des raies a +-150 kHz de la plus forte (le spectre FM est symetrique
    # autour de la porteuse) pour la frequence exacte
    cmax, top = {}, {}
    for fr, v in pts:
        k = round(fr / 1e5) / 10
        if 87.5 <= k <= 108.0 and v > cmax.get(k, -200):
            cmax[k], top[k] = v, fr
    floor = sorted(cmax.values())[len(cmax) // 5]
    found = {}
    for k, v in cmax.items():
        nb = [cmax.get(round(k + d / 10, 1), -200) for d in (-2, -1, 1, 2)]
        if v - floor >= 10 and v >= max(nb):
            w = [(fr, 10 ** (x / 10)) for fr, x in pts if abs(fr - top[k]) <= 150e3]
            c = round(sum(fr * p for fr, p in w) / sum(p for _, p in w) / 1e5) / 10
            found[c] = max(found.get(c, -200), v - floor)
    peaks = sorted(((db, k) for k, db in found.items()), reverse=True)
    best, n = None, 0
    for db, mhz in peaks[:15]:
        n += 1
        out("SCANNING %.1f (%d/%d)" % (mhz, n, min(15, len(peaks))))
        pi, ps = rds_try(mhz)
        if pi and ps:
            out("FOUND %.1f %s %s" % (mhz, pi, ps))
            if best is None:
                best = (mhz, db, ps)
    if best:
        out("SCAN %.1f %.0f %s" % best)
    else:
        out("SCANEND")


def main():
    a = sys.argv[1:]
    if a[:1] == ["dab-poll"] and len(a) == 3:
        dab_poll(int(a[1]), a[2])
    elif a[:1] == ["dab-scan"]:
        dab_scan()
    elif a[:1] == ["fm-scan"]:
        fm_scan()
    elif a[:1] == ["gain-index"] and len(a) == 2:
        out(str(gain_index(a[1])))
    else:
        sys.exit(__doc__ if __doc__ else "usage : md_radio.py dab-poll|dab-scan|fm-scan|gain-index")


if __name__ == "__main__":
    main()
