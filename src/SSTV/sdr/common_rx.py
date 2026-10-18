#!/usr/bin/env python2
"""
common_rx.py -- chaine de reception SSB/FM commune a RTL-SDR, PlutoSDR et
LimeSDR, utilisee par rx_rtlsdr.py / rx_pluto.py / rx_lime.py (et
rx_test.py pour les tests sans materiel).

Ecrit pour GNU Radio 3.7 (gr-osmosdr / gr-iio / gr-limesdr), la version
fournie par Raspberry Pi OS sur les installations existantes de Portsdown4 /
Langstone. Les appels de blocs (gr-osmosdr, iio.pluto_source, limesdr.source,
grc_blks2.selector, logpwrfft, blocks.udp_sink a l'ancienne signature) sont
repris de ceux effectivement utilises par Lang_TRX_Pluto.py / Lang_TRX_Lime.py
de Langstone-V2 (meme ecosysteme materiel), pas juste de la documentation
generique GNU Radio.

Chaine de traitement :

  source SDR (IQ, 1.024 Msps)
    -> filtre + decimation (freq_xlating_fir_filter_ccc)         -> 32 kHz IQ
         |-> demod USB (filtre bande complexe + partie reelle)  --\
         |-> demod LSB (filtre bande complexe + partie reelle)  ---> selector -> resampleur 15 kHz -> stdout (PCM 16 bits)
         |-> demod FM  (quadrature_demod_cf)                    --/
         |-> spectre local (logpwrfft) -> UDP (port spectre)

L'audio demodule est aussi envoye vers la sortie son du Raspberry Pi (ecoute
en direct de la reception), en parallele du flux envoye au decodeur SSTV.
Cette sortie peut etre mise en sourdine a la demande (commande MUTE) sans
affecter le decodage : sstv_radio l'utilise pour couper le son une fois
qu'une synchro SSTV est confirmee (le "bruit" SSTV n'a pas besoin d'etre
ecoute), et le retablit entre deux images.

Controle a distance (thread UDP local, voir ControlThread) :
  FREQ <hz>       change la frequence accordee
  MODE USB|LSB|FM change le mode de demodulation actif (via le selector)
  MUTE 0|1        coupe/retablit l'ecoute (n'affecte pas le decodage)
"""

import socket
import sys
import threading

from gnuradio import analog
from gnuradio import audio
from gnuradio import blocks
from gnuradio import filter as gr_filter
from gnuradio import gr
from gnuradio.fft import logpwrfft
from gnuradio.filter import firdes
from grc_gnuradio import blks2 as grc_blks2

SAMP_RATE = 1024000       # frequence d'echantillonnage IQ demandee au SDR
IF_RATE = 32000            # frequence intermediaire complexe apres decimation
AUDIO_RATE = 15000         # frequence de sortie attendue par le decodeur SSTV
DECIM = SAMP_RATE // IF_RATE  # 32

FFT_SIZE = 512
SPECTRUM_FPS = 8

MODES = {"USB": 0, "LSB": 1, "FM": 2}


class ControlThread(threading.Thread):
    """Ecoute les commandes FREQ/MODE envoyees par l'interface graphique sur
    un port UDP local et les applique a la chaine de reception."""

    def __init__(self, top_block, control_port):
        super(ControlThread, self).__init__()
        self.daemon = True
        self.top_block = top_block
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", control_port))
        self._running = True

    def stop(self):
        self._running = False
        self.sock.close()

    def run(self):
        while self._running:
            try:
                data, _ = self.sock.recvfrom(256)
            except OSError:
                break
            line = data.decode("ascii", errors="ignore").strip()
            parts = line.split()
            if not parts:
                continue
            cmd = parts[0].upper()
            try:
                if cmd == "FREQ" and len(parts) >= 2:
                    self.top_block.set_freq(float(parts[1]))
                elif cmd == "MODE" and len(parts) >= 2:
                    self.top_block.set_mode(parts[1].upper())
                elif cmd == "MUTE" and len(parts) >= 2:
                    self.top_block.set_mute(parts[1] == "1")
            except Exception as e:
                sys.stderr.write("commande de controle ignoree (%r) : %s\n" % (line, e))


class RxTopBlock(gr.top_block):
    """Chaine de reception commune. ``make_source(tb)`` doit renvoyer un
    couple (bloc_source_gnuradio, fonction_de_reaccord(freq_hz))."""

    def __init__(self, make_source, freq, mode, spectrum_port):
        gr.top_block.__init__(self, "rpi_sstv_rx")

        self.freq = freq
        self.source, self._retune = make_source(self)

        xlate_taps = firdes.low_pass(1, SAMP_RATE, IF_RATE / 2.2, IF_RATE / 6,
                                      firdes.WIN_HAMMING, 6.76)
        self.xlate = gr_filter.freq_xlating_fir_filter_ccc(
            DECIM, xlate_taps, 0, SAMP_RATE)

        # -- demodulation USB --
        # Bande resserree sur ce qu'utilise reellement le decodeur SSTV
        # (third_party/sstv_library/sstv_decoder.cpp) : reference de bruit
        # 600-1000 Hz, synchro 1000-1400 Hz, pixels jusqu'a 2300 Hz. 500-2400
        # Hz laisse une marge de ~100 Hz de chaque cote sans empieter sur ces
        # bandes, au lieu de 300-2700 Hz (bien plus large qu'une voie SSB
        # vocale classique, donc plus de bruit inutile en entree du
        # decodeur). Meme logique que le max_dev resserre pour la FM.
        usb_taps = firdes.complex_band_pass(1, IF_RATE, 500, 2400, 300,
                                             firdes.WIN_HAMMING, 6.76)
        self.usb_filter = gr_filter.fir_filter_ccc(1, usb_taps)
        self.usb_to_real = blocks.complex_to_real(1)
        self.usb_agc = analog.agc2_ff(0.1, 0.01, 0.6, 1.0)

        # -- demodulation LSB --
        lsb_taps = firdes.complex_band_pass(1, IF_RATE, -2400, -500, 300,
                                             firdes.WIN_HAMMING, 6.76)
        self.lsb_filter = gr_filter.fir_filter_ccc(1, lsb_taps)
        self.lsb_to_real = blocks.complex_to_real(1)
        self.lsb_agc = analog.agc2_ff(0.1, 0.01, 0.6, 1.0)

        # -- demodulation FM --
        # analog.nbfm_rx (et non un quadrature_demod_cf brut) : meme bloc que
        # Lang_RX_Rtlhf.py (portsdown4/src/langstone), qui filtre en fonction
        # de la deviation reelle (max_dev) au lieu de demoduler toute la
        # largeur du premier filtre (~14.5 kHz) -- plus etroit, donc moins de
        # bruit hors-canal et une meilleure sensibilite sur un signal FM
        # etroit comme le SSTV ISS.
        self.fm_demod = analog.nbfm_rx(audio_rate=IF_RATE, quad_rate=IF_RATE,
                                        tau=75e-6, max_dev=5e3)
        self.fm_agc = analog.agc2_ff(0.1, 0.01, 0.6, 1.0)

        # -- selection du mode actif (meme bloc que Langstone : grc_blks2.selector) --
        self.selector = grc_blks2.selector(
            item_size=gr.sizeof_float * 1,
            num_inputs=3,
            num_outputs=1,
            input_index=MODES.get(mode, 0),
            output_index=0,
        )

        # -- retour a 15 kHz pour le decodeur SSTV --
        self.resampler = gr_filter.rational_resampler_fff(15, DECIM, None, None)
        self.to_short = blocks.float_to_short(1, 32000.0)
        self.audio_out = blocks.file_descriptor_sink(gr.sizeof_short, 1)

        # -- ecoute en direct (sortie son du Pi), coupable a la demande sans
        # affecter le decodage ci-dessus (meme technique que les gains
        # Rx_Mode de Langstone : multiply_const_vff bascule entre 0.0 et 1.0) --
        self.mute_gate = blocks.multiply_const_vff((1.0,))
        self.audio_monitor = audio.sink(AUDIO_RATE, "", True)

        # -- spectre local (autour de la frequence accordee) --
        self.spec_fft = logpwrfft.logpwrfft_c(
            sample_rate=IF_RATE,
            fft_size=FFT_SIZE,
            ref_scale=2,
            frame_rate=SPECTRUM_FPS,
            avg_alpha=0.9,
            average=True,
        )
        # payload_size doit couvrir une trame entiere (4*FFT_SIZE octets),
        # sinon udp_sink la fragmente sur plusieurs paquets et le cote C++
        # (qui attend un seul paquet de exactement cette taille) ne recoit
        # jamais rien d'entier. Uniquement du trafic loopback (127.0.0.1),
        # donc aucune limite MTU reseau reelle a respecter ici.
        self.spec_udp = blocks.udp_sink(gr.sizeof_float * FFT_SIZE,
                                         "127.0.0.1", spectrum_port,
                                         gr.sizeof_float * FFT_SIZE, False)

        # -- connexions --
        self.connect(self.source, self.xlate)

        self.connect(self.xlate, self.usb_filter, self.usb_to_real, self.usb_agc)
        self.connect((self.usb_agc, 0), (self.selector, 0))

        self.connect(self.xlate, self.lsb_filter, self.lsb_to_real, self.lsb_agc)
        self.connect((self.lsb_agc, 0), (self.selector, 1))

        self.connect(self.xlate, self.fm_demod, self.fm_agc)
        self.connect((self.fm_agc, 0), (self.selector, 2))

        self.connect(self.selector, self.resampler, self.to_short, self.audio_out)
        self.connect((self.resampler, 0), (self.mute_gate, 0))
        self.connect(self.mute_gate, self.audio_monitor)

        self.connect(self.xlate, self.spec_fft, self.spec_udp)

    def set_freq(self, freq):
        self.freq = freq
        self._retune(freq)

    def set_mode(self, mode):
        if mode in MODES:
            self.selector.set_input_index(MODES[mode])

    def set_mute(self, muted):
        self.mute_gate.set_k((0.0,) if muted else (1.0,))


DC_OFFSET_MODES = {"off": 0, "manual": 1, "automatic": 2}


def make_rtlsdr_source(device_args, gain, dc_offset="automatic"):
    """RTL-SDR via gr-osmosdr (osmosdr.source), comme dans la plupart des
    flowgraphs GNU Radio de cette generation."""
    def _make(tb):
        import osmosdr
        args = "rtl=0"
        if device_args:
            args += "," + device_args
        src = osmosdr.source(args="numchan=1 " + args)
        src.set_sample_rate(SAMP_RATE)
        src.set_center_freq(tb.freq, 0)
        src.set_freq_corr(0, 0)
        try:
            # 0=off, 1=manuel, 2=automatique : supprime le pic de fuite
            # d'oscillateur local (DC) au centre du spectre, frequent sur les
            # tuners R820T des cles RTL-SDR. Reglable (--dc-offset) car ce
            # mode automatique peut, selon le pilote, affecter le niveau du
            # signal : le desactiver ("off") permet de verifier si c'est la
            # cause d'une perte de sensibilite constatee.
            src.set_dc_offset_mode(DC_OFFSET_MODES.get(dc_offset, 2), 0)
        except Exception as e:
            sys.stderr.write("dc_offset_mode ignore : %s\n" % e)
        if gain is None:
            src.set_gain_mode(True, 0)
        else:
            src.set_gain_mode(False, 0)
            src.set_gain(gain, 0)
        src.set_bandwidth(0, 0)

        def _retune(freq):
            src.set_center_freq(freq, 0)
        return src, _retune
    return _make


def make_pluto_source(uri, gain):
    """PlutoSDR via gr-iio (iio.pluto_source), exactement comme
    Lang_TRX_Pluto.py de Langstone-V2."""
    def _make(tb):
        from gnuradio import iio
        bandwidth = 2000000
        buffer_size = 0x800
        if gain is None:
            gain_mode, manual_gain = "slow_attack", 64.0
        else:
            gain_mode, manual_gain = "manual", gain
        src = iio.pluto_source(uri, int(tb.freq), SAMP_RATE, bandwidth,
                                buffer_size, True, True, True,
                                gain_mode, manual_gain, "", True)

        def _retune(freq):
            src.set_params(int(freq), SAMP_RATE, bandwidth, True, True, True,
                            gain_mode, manual_gain, "", True)
        return src, _retune
    return _make


def make_lime_source(device_args, gain):
    """LimeSDR via gr-limesdr (module Python 'limesdr'), exactement comme
    Lang_TRX_Lime.py de Langstone-V2."""
    def _make(tb):
        import limesdr
        src = limesdr.source(device_args or "", 0, "")
        src.set_sample_rate(SAMP_RATE)
        src.set_center_freq(tb.freq, 0)
        src.set_bandwidth(2e6, 0)
        src.set_gain(gain if gain is not None else 40, 0)
        src.set_antenna(255, 0)
        src.calibrate(3e6, 0)

        def _retune(freq):
            src.set_center_freq(freq, 0)
        return src, _retune
    return _make


def run(make_source, freq, mode, control_port, spectrum_port):
    tb = RxTopBlock(make_source, freq, mode, spectrum_port)
    ctrl = ControlThread(tb, control_port)
    ctrl.start()
    try:
        tb.start()
        tb.wait()
    except KeyboardInterrupt:
        pass
    finally:
        ctrl.stop()
        tb.stop()
        tb.wait()
