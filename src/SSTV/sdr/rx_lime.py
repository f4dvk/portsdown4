#!/usr/bin/env python2
"""
Point d'entree LimeSDR / LimeSDR Mini pour RPI_SSTV (voir common_rx.py).

Utiliser --device-args pour cibler un LimeSDR particulier si plusieurs sont
branches (numero de serie), par exemple :

  --device-args "1234567890"
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
    p.add_argument("--device-args", default="", help="Numero de serie du LimeSDR (optionnel)")
    p.add_argument("--gain", type=float, default=None, help="Gain manuel en dB (defaut : 40 dB)")
    args = p.parse_args()

    make_source = common_rx.make_lime_source(args.device_args, args.gain)
    common_rx.run(make_source, args.freq, args.mode, args.control_port, args.spectrum_port)


if __name__ == "__main__":
    main()
