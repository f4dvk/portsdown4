#!/usr/bin/env python3
"""Pilotage de l'emission DATV d'un Pluto ou d'une LibreSDR sous firmware
Tezuka (F5OEO), par son broker MQTT (port 1883).

Le firmware recoit le flux TS en UDP et le module lui-meme (DVB-S ou DVB-S2).
Commandes : cmd/pluto/<cle>/..., etat publie chaque seconde sous
dt/pluto/<cle>/..., ou <cle> est l'indicatif enregistre dans la radio
(fw_printenv call) : on la decouvre en ecoutant dt/pluto/+/.

Utilisation (appele par a.sh et par l'interface) :
  tezuka_ctl.py --host IP start --mode dvbs2-ts --sr 333000 --constel qpsk
                --fec 2/3 --frame long --pilots 0 --freq 437000000
                --gain -10 --ts 0.0.0.0:1314
  tezuka_ctl.py --host IP carrier --freq 437000000 --gain -10
  tezuka_ctl.py [--host IP] stop     (sans --host : lu dans portsdown_config.txt)
  tezuka_ctl.py --host IP status

Client MQTT 3.1.1 minimal (QoS 0) : aucune dependance hors Python 3.
"""

import argparse
import os
import socket
import struct
import sys
import time

PCONFIG = "/home/pi/rpidatv/scripts/portsdown_config.txt"
HOSTFILE = "/tmp/tezuka_host"   # derniere radio mise en emission
WAIT_STATUS_S = 4.0             # telemetrie publiee chaque seconde


class MqttError(Exception):
    pass


def _enc_str(s):
    b = s.encode("utf-8")
    return struct.pack("!H", len(b)) + b


def _enc_len(n):
    out = bytearray()
    while True:
        byte = n % 128
        n //= 128
        if n:
            byte |= 0x80
        out.append(byte)
        if not n:
            return bytes(out)


class Mqtt:
    def __init__(self, host, port=1883, timeout=3.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.buf = b""
        cid = "portsdown-%d" % os.getpid()
        var = _enc_str("MQTT") + bytes([4, 0x02]) + struct.pack("!H", 30)
        self._send(0x10, var + _enc_str(cid))
        ptype, body = self._read_packet(timeout)
        if ptype != 0x20 or len(body) < 2 or body[1] != 0:
            raise MqttError("connexion MQTT refusee")
        self.pid = 0

    def _send(self, header, payload):
        self.sock.sendall(bytes([header]) + _enc_len(len(payload)) + payload)

    def _recv_exact(self, n, deadline):
        while len(self.buf) < n:
            left = deadline - time.time()
            if left <= 0:
                raise socket.timeout()
            self.sock.settimeout(left)
            chunk = self.sock.recv(4096)
            if not chunk:
                raise MqttError("connexion MQTT fermee")
            self.buf += chunk
        data, self.buf = self.buf[:n], self.buf[n:]
        return data

    def _read_packet(self, timeout):
        deadline = time.time() + timeout
        header = self._recv_exact(1, deadline)[0]
        mult, length = 1, 0
        while True:
            b = self._recv_exact(1, deadline)[0]
            length += (b & 0x7F) * mult
            mult *= 128
            if not b & 0x80:
                break
        return header & 0xF0, self._recv_exact(length, deadline)

    def subscribe(self, topic):
        self.pid += 1
        self._send(0x82, struct.pack("!H", self.pid) + _enc_str(topic) + b"\x00")

    def publish(self, topic, value):
        self._send(0x30, _enc_str(topic) + str(value).encode("utf-8"))

    def messages(self, duration):
        """Messages recus pendant 'duration' secondes : (topic, valeur)."""
        end = time.time() + duration
        while True:
            left = end - time.time()
            if left <= 0:
                return
            try:
                ptype, body = self._read_packet(left)
            except socket.timeout:
                return
            if ptype == 0x30:
                tlen = struct.unpack("!H", body[:2])[0]
                topic = body[2:2 + tlen].decode("utf-8", "replace")
                yield topic, body[2 + tlen:].decode("utf-8", "replace")

    def close(self):
        try:
            self._send(0xE0, b"")
            self.sock.close()
        except OSError:
            pass


class Tezuka:
    """Une radio Tezuka : cle MQTT decouverte et dernier etat connu."""

    def __init__(self, host):
        self.host = host
        self.mqtt = Mqtt(host)
        self.mqtt.subscribe("dt/pluto/+/#")
        self.key = None
        self.state = {}
        self.update(WAIT_STATUS_S, need=("tx/mute", "tx/dvbs2/tssourceaddress"))
        if self.key is None:
            raise MqttError("aucune radio Tezuka ne repond sur %s" % host)

    def update(self, duration, need=()):
        """Lit la telemetrie ; s'arrete des que les cles 'need' sont connues."""
        for topic, value in self.mqtt.messages(duration):
            parts = topic.split("/", 3)
            if len(parts) < 4:
                continue
            if self.key is None:
                self.key = parts[2]
            if parts[2] == self.key:
                self.state[parts[3]] = value
            if need and self.key and all(k in self.state for k in need):
                return

    def cmd(self, path, value):
        self.mqtt.publish("cmd/pluto/%s/%s" % (self.key, path), value)

    def wait_value(self, path, value, timeout):
        self.state.pop(path, None)
        end = time.time() + timeout
        while time.time() < end:
            self.update(end - time.time(), need=(path,))
            if self.state.get(path) == value:
                return True
            self.state.pop(path, None)
        return False

    def on_air(self):
        """Leve la coupure, puis verifie sur un second cycle de telemetrie
        qu'elle n'a pas ete remise (le firmware coupe en quittant le mode
        'pass', et ce message peut arriver apres le notre)."""
        for _ in range(3):
            self.cmd("tx/mute", 0)
            if self.wait_value("tx/mute", "0", 2.5):
                time.sleep(0.5)
                if self.wait_value("tx/mute", "0", 2.5):
                    return True
        return False


def config_host():
    """Adresse de la radio Tezuka selon portsdown_config.txt (ou None)."""
    conf = {}
    try:
        with open(PCONFIG) as f:
            for line in f:
                if "=" in line:
                    k, v = line.strip().split("=", 1)
                    conf[k] = v
    except OSError:
        return None
    mode = conf.get("modeoutput", "")
    if mode == "PLUTOTZ":
        return conf.get("plutoip") or None
    if mode == "LIBRETZ":
        ip = conf.get("libreip", "")
        return "libre" if ip in ("", "dhcp") else ip
    return None


def do_start(radio, a):
    radio.cmd("tx/mute", 1)
    radio.cmd("tx/stream/mode", a.mode)
    time.sleep(0.5)          # le changement de mode reinitialise le modulateur
    radio.cmd("tx/dvbs2/sr", a.sr)
    radio.cmd("tx/dvbs2/constel", a.constel)
    radio.cmd("tx/dvbs2/frame", a.frame)
    radio.cmd("tx/dvbs2/pilots", a.pilots)
    radio.cmd("tx/dvbs2/fecmode", "fixed")
    radio.cmd("tx/dvbs2/fec", a.fec)
    radio.cmd("tx/dvbs2/tssourcemode", 0)
    # Chaque changement d'adresse ouvre une nouvelle socket dans la radio :
    # ne l'envoyer que si elle differe.
    if radio.state.get("tx/dvbs2/tssourceaddress") != a.ts:
        radio.cmd("tx/dvbs2/tssourceaddress", a.ts)
    radio.cmd("tx/frequency", a.freq)
    radio.cmd("tx/gain", a.gain)
    time.sleep(0.3)
    ok = radio.on_air()
    radio.update(1.5, need=("tx/stream/mode",))
    print("Tezuka %s (%s) : mode %s, %s Hz, gain %s dB, %s" % (
        radio.host, radio.key, radio.state.get("tx/stream/mode", "?"),
        a.freq, a.gain, "en emission" if ok else "COUPURE NON LEVEE"))
    return ok


def do_carrier(radio, a):
    radio.cmd("tx/mute", 1)
    radio.cmd("tx/stream/mode", "test")   # porteuse pure (I constant, Q nul)
    time.sleep(0.5)
    radio.cmd("tx/frequency", a.freq)
    radio.cmd("tx/gain", a.gain)
    time.sleep(0.3)
    ok = radio.on_air()
    print("Tezuka %s (%s) : porteuse %s Hz, gain %s dB, %s" % (
        radio.host, radio.key, a.freq, a.gain,
        "en emission" if ok else "COUPURE NON LEVEE"))
    return ok


def main():
    p = argparse.ArgumentParser(description="Emission DATV via firmware Tezuka (MQTT)")
    p.add_argument("--host")
    sub = p.add_subparsers(dest="action")
    s = sub.add_parser("start")
    s.add_argument("--mode", choices=("dvbs2-ts", "dvbs"), default="dvbs2-ts")
    s.add_argument("--sr", type=int, required=True)
    s.add_argument("--constel", choices=("qpsk", "8psk", "16apsk", "32apsk"), default="qpsk")
    s.add_argument("--fec", required=True)
    s.add_argument("--frame", choices=("long", "short"), default="long")
    s.add_argument("--pilots", choices=("0", "1"), default="0")
    s.add_argument("--freq", type=int, required=True)
    s.add_argument("--gain", type=float, required=True)
    s.add_argument("--ts", required=True, help="adresse:port d'ecoute du TS dans la radio")
    c = sub.add_parser("carrier")
    c.add_argument("--freq", type=int, required=True)
    c.add_argument("--gain", type=float, required=True)
    sub.add_parser("stop")
    sub.add_parser("status")
    a = p.parse_args()

    host = a.host
    if not host and a.action == "stop":
        host = config_host()
        if not host and os.path.exists(HOSTFILE):
            with open(HOSTFILE) as f:
                host = f.read().strip()
    if not host or not a.action:
        p.print_usage(sys.stderr)
        return 2

    try:
        radio = Tezuka(host)
        if a.action == "stop":
            radio.cmd("tx/mute", 1)
            ok = radio.wait_value("tx/mute", "1", 2.5)
            print("Tezuka %s (%s) : emission coupee%s" % (host, radio.key, "" if ok else " (non confirme)"))
        elif a.action == "status":
            radio.update(1.5)
            for k in sorted(radio.state):
                print("%s %s" % (k, radio.state[k]))
            ok = True
        else:
            with open(HOSTFILE, "w") as f:
                f.write(host + "\n")
            ok = do_start(radio, a) if a.action == "start" else do_carrier(radio, a)
        radio.mqtt.close()
        return 0 if ok else 1
    except (OSError, MqttError) as e:
        print("Tezuka %s : %s" % (host, e), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
