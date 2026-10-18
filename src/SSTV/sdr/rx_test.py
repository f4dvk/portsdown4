#!/usr/bin/env python2
"""
Generateur de signal de test pour RPI_SSTV, sans materiel SDR.

Ce script n'utilise pas GNU Radio : il se contente d'exposer la meme
interface externe que rx_rtlsdr.py / rx_pluto.py / rx_lime.py (port de
controle FREQ/MODE, trames de spectre UDP, audio PCM 15 kHz sur la sortie
standard), en rejouant en boucle un fichier .wav de test. Cela permet de
valider sstv_radio (accord, changement de mode, spectre, decodage SSTV)
sans avoir a brancher une cle RTL-SDR, un PlutoSDR ou un LimeSDR.

Ecrit en Python 2 (comme common_rx.py) pour etre lance par sstv_radio de la
meme facon que les autres backends, meme s'il ne depend pas de GNU Radio.

Utilisation :
  rx_test.py --freq 14230000 --mode USB --wav ../test_wav/test_pd120.wav
"""

import argparse
import math
import random
import socket
import struct
import sys
import threading
import time
import wave

FFT_SIZE = 512


class FakeState(object):
    def __init__(self, freq, mode):
        self.freq = freq
        self.mode = mode
        self.lock = threading.Lock()


def control_thread(state, control_port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", control_port))
    while True:
        data, _ = sock.recvfrom(256)
        parts = data.decode("ascii", "ignore").split()
        if not parts:
            continue
        with state.lock:
            if parts[0].upper() == "FREQ" and len(parts) >= 2:
                state.freq = float(parts[1])
            elif parts[0].upper() == "MODE" and len(parts) >= 2:
                state.mode = parts[1].upper()


def spectrum_thread(state, spectrum_port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    addr = ("127.0.0.1", spectrum_port)
    t = 0.0
    while True:
        # Bruit de fond + une "bosse" pres de la porteuse (bin 0), pour imiter
        # l'ordre brut (non recentre) que produit reellement logpwrfft : DC en
        # premier, la aussi pour valider le recentrage fait cote affichage
        # (sstv_radio applique le fftshift, pas ce generateur).
        bump = int(20 * math.sin(t)) % FFT_SIZE
        bins = [-70 + random.uniform(-3, 3) for _ in range(FFT_SIZE)]
        for di in range(-4, 4):
            bins[(bump + di) % FFT_SIZE] += 25
        sock.sendto(struct.pack("<%df" % FFT_SIZE, *bins), addr)
        t += 0.3
        time.sleep(1.0 / 8)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--freq", type=float, required=True)
    p.add_argument("--mode", default="USB", choices=["USB", "LSB", "FM"])
    p.add_argument("--control-port", type=int, default=7355)
    p.add_argument("--spectrum-port", type=int, default=7356)
    p.add_argument("--device-args", default="")  # ignore, pour compatibilite d'appel
    p.add_argument("--gain", type=float, default=None)  # ignore
    p.add_argument("--wav", required=True, help="Fichier .wav 15 kHz mono 16 bits a rejouer en boucle")
    args = p.parse_args()

    state = FakeState(args.freq, args.mode)
    t1 = threading.Thread(target=control_thread, args=(state, args.control_port))
    t1.daemon = True
    t1.start()
    t2 = threading.Thread(target=spectrum_thread, args=(state, args.spectrum_port))
    t2.daemon = True
    t2.start()

    out = sys.stdout
    chunk_frames = 1500  # 0.1 s a 15 kHz

    while True:
        wf = wave.open(args.wav, "rb")
        try:
            assert wf.getframerate() == 15000 and wf.getnchannels() == 1, \
                "rx_test.py attend un .wav 15 kHz mono (voir test_wav/)"
            while True:
                data = wf.readframes(chunk_frames)
                if not data:
                    break
                out.write(data)
                out.flush()
                time.sleep(chunk_frames / 15000.0)
        finally:
            wf.close()


if __name__ == "__main__":
    main()
