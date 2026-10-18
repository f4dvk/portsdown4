#!/usr/bin/env python2
"""Point d'entree RTL-SDR pour RPI_SSTV (voir common_rx.py)."""

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
    p.add_argument("--device-args", default="", help="Arguments gr-osmosdr additionnels (ex: serial=...)")
    p.add_argument("--gain", type=float, default=None, help="Gain manuel en dB (defaut : AGC materiel)")
    p.add_argument("--dc-offset", default="automatic", choices=["off", "manual", "automatic"],
                    help="Correction du pic DC au centre du spectre (defaut : automatic)")
    args = p.parse_args()

    make_source = common_rx.make_rtlsdr_source(args.device_args, args.gain, args.dc_offset)
    common_rx.run(make_source, args.freq, args.mode, args.control_port, args.spectrum_port)


if __name__ == "__main__":
    main()
