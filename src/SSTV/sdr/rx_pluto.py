#!/usr/bin/env python2
"""
Point d'entree Adalm PlutoSDR pour RPI_SSTV (voir common_rx.py).

Par defaut, se connecte au Pluto via son adresse IP habituelle
(ip:192.168.2.1, USB) ; utiliser --uri pour un Pluto en reseau ou avec une
autre adresse, par exemple :

  --uri ip:192.168.3.1
"""

import argparse
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common_rx


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--freq", type=float, required=True, help="Frequence en Hz")
    p.add_argument("--mode", default="USB", choices=["USB", "LSB", "FM"])
    p.add_argument("--control-port", type=int, default=7355)
    p.add_argument("--spectrum-port", type=int, default=7356)
    p.add_argument("--uri", default="ip:192.168.2.1", help="Adresse du Pluto (gr-iio)")
    p.add_argument("--device-args", default="",
                    help="Alias de --uri (pour l'appel uniforme depuis sstv_radio)")
    p.add_argument("--gain", type=float, default=None, help="Gain manuel en dB (defaut : AGC materiel)")
    args = p.parse_args()

    uri = args.device_args if args.device_args else args.uri
    make_source = common_rx.make_pluto_source(uri, args.gain)
    common_rx.run(make_source, args.freq, args.mode, args.control_port, args.spectrum_port)


if __name__ == "__main__":
    main()
