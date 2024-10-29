#!/usr/bin/env python3
# -*- coding: utf-8 -*-

#
# SPDX-License-Identifier: GPL-3.0
#
# GNU Radio Python Flow Graph
# Title: Lang Rx Rtlsdr
# GNU Radio version: v3.8.2.0-57-gd71cd177

import os
import errno
from gnuradio import analog
from gnuradio import audio
from gnuradio import blocks
from gnuradio import filter
from gnuradio.filter import firdes
from grc_gnuradio import blks2 as grc_blks2
from gnuradio import gr
import sys
import signal
from argparse import ArgumentParser
from gnuradio.eng_arg import eng_float, intx
from gnuradio import eng_notation
from gnuradio.fft import logpwrfft
import osmosdr
import time
import dsd

class Lang_RX_RtlSdr(gr.top_block):

    def __init__(self):
        gr.top_block.__init__(self, "Lang Rx Rtlsdr")

        ##################################################
        # Variables
        ##################################################
        self.SQL = SQL = 50
        self.Rx_Mute = Rx_Mute = False
        self.Rx_Mode = Rx_Mode = 3
        self.Rx_LO = Rx_LO = 432250000
        self.Rx_Filt_Low = Rx_Filt_Low = 300
        self.Rx_Filt_High = Rx_Filt_High = 3000
        self.RxOffset = RxOffset = 0
        self.FFT_SEL = FFT_SEL = 0
        self.AFGain = AFGain = 0

        ##################################################
        # Blocks
        ##################################################
        self.rtlsdr_source_0 = osmosdr.source(
            args="numchan=" + str(1) + " " + "driver=rtlsdr,soapy=0"
        )
        self.rtlsdr_source_0.set_time_unknown_pps(osmosdr.time_spec_t())
        self.rtlsdr_source_0.set_sample_rate(2112000)
        self.rtlsdr_source_0.set_center_freq(Rx_LO, 0)
        self.rtlsdr_source_0.set_freq_corr(0, 0)
        self.rtlsdr_source_0.set_dc_offset_mode(2, 0)
        self.rtlsdr_source_0.set_iq_balance_mode(2, 0)
        self.rtlsdr_source_0.set_gain_mode(True, 0)
        self.rtlsdr_source_0.set_antenna('', 0)
        self.rational_resampler_xxx_0 = filter.rational_resampler_fff(
                interpolation=6,
                decimation=1,
                taps=None,
                fractional_bw=None)
        self.rational_resampler_xxx_1_1 = filter.rational_resampler_ccc(
                interpolation=1,
                decimation=2,
                taps=None,
                fractional_bw=None)
        self.rational_resampler_xxx_1_0 = filter.rational_resampler_ccc(
                interpolation=1,
                decimation=4,
                taps=None,
                fractional_bw=None)
        self.rational_resampler_xxx_1 = filter.rational_resampler_ccc(
                interpolation=1,
                decimation=8,
                taps=None,
                fractional_bw=None)
        self.logpwrfft_x_0_1 = logpwrfft.logpwrfft_c(
            sample_rate=48000/ (2 ** FFT_SEL),
            fft_size=512,
            ref_scale=2,
            frame_rate=15,
            avg_alpha=0.9,
            average=True)
        self.freq_xlating_fir_filter_xxx_0 = filter.freq_xlating_fir_filter_ccc(44, firdes.low_pass(1,2112000,23000,2000), RxOffset, 2112000)
        self.dsd_block_ff_0 = dsd.dsd_block_ff(dsd.dsd_FRAME_AUTO_DETECT,dsd.dsd_MOD_AUTO_SELECT,3,False,0)
        self.blocks_udp_sink_0 = blocks.udp_sink(gr.sizeof_float*512, '127.0.0.1', 7373, 1472, False)
        self.blks2_selector_0 = grc_blks2.selector(
        	item_size=gr.sizeof_gr_complex*1,
        	num_inputs=4,
        	num_outputs=1,
        	input_index=FFT_SEL,
        	output_index=0,
        )
        self.blocks_multiply_const_vxx_6 = blocks.multiply_const_ff(Rx_Mode==6)
        self.blocks_multiply_const_vxx_2_1_0 = blocks.multiply_const_ff(1.0 + (Rx_Mode==5))
        self.blocks_multiply_const_vxx_2_1 = blocks.multiply_const_ff(Rx_Mode==5)
        self.blocks_multiply_const_vxx_2_0 = blocks.multiply_const_ff((Rx_Mode==4) * 0.2)
        self.blocks_multiply_const_vxx_2 = blocks.multiply_const_ff(Rx_Mode<4)
        self.blocks_multiply_const_vxx_1 = blocks.multiply_const_ff((AFGain/100.0) *  (not Rx_Mute))
        self.blocks_float_to_complex_0 = blocks.float_to_complex(1)
        self.blocks_complex_to_real_0_0 = blocks.complex_to_real(1)
        self.blocks_complex_to_real_0 = blocks.complex_to_real(1)
        self.blocks_complex_to_mag_0 = blocks.complex_to_mag(1)
        self.blocks_add_xx_1_0 = blocks.add_vff(1)
        self.blocks_add_xx_1 = blocks.add_vff(1)
        self.band_pass_filter_0 = filter.fir_filter_ccc(
            1,
            firdes.complex_band_pass(
                1,
                48000,
                Rx_Filt_Low,
                Rx_Filt_High,
                100,
                firdes.WIN_HAMMING,
                6.76))
        self.audio_sink_0 = audio.sink(48000, "hw:"+sys.argv[1]+",0", False)
        self.audio_sink_1 = audio.sink(48000, "plughw:Loopback,0,2", False)
        self.analog_pwr_squelch_xx_0 = analog.pwr_squelch_cc(SQL-100, 0.001, 0, False)
        self.analog_nbfm_rx_0 = analog.nbfm_rx(
        	audio_rate=48000,
        	quad_rate=48000,
        	tau=75e-6,
        	max_dev=5e3,
          )
        self.analog_agc3_xx_0 = analog.agc3_cc(1e-2, 5e-7, 0.1, 1.0, 1)
        self.analog_agc3_xx_0.set_max_gain(1000)



        ##################################################
        # Connections
        ##################################################
        self.connect((self.analog_agc3_xx_0, 0), (self.blocks_complex_to_real_0_0, 0))
        self.connect((self.analog_nbfm_rx_0, 0), (self.blocks_multiply_const_vxx_2_0, 0))
        self.connect((self.analog_nbfm_rx_0, 0), (self.blocks_multiply_const_vxx_6, 0))
        self.connect((self.analog_pwr_squelch_xx_0, 0), (self.analog_nbfm_rx_0, 0))
        self.connect((self.band_pass_filter_0, 0), (self.analog_pwr_squelch_xx_0, 0))
        self.connect((self.band_pass_filter_0, 0), (self.blocks_complex_to_mag_0, 0))
        self.connect((self.band_pass_filter_0, 0), (self.blocks_complex_to_real_0, 0))
        self.connect((self.blocks_add_xx_1, 0), (self.blocks_multiply_const_vxx_1, 0))
        self.connect((self.blocks_add_xx_1_0, 0), (self.blocks_float_to_complex_0, 0))
        self.connect((self.blocks_complex_to_mag_0, 0), (self.blocks_multiply_const_vxx_2_1, 0))
        self.connect((self.blocks_complex_to_real_0, 0), (self.blocks_multiply_const_vxx_2, 0))
        self.connect((self.blocks_complex_to_real_0_0, 0), (self.blocks_multiply_const_vxx_2_1_0, 0))
        self.connect((self.blocks_float_to_complex_0, 0), (self.analog_agc3_xx_0, 0))
        self.connect((self.blocks_multiply_const_vxx_1, 0), (self.audio_sink_0, 0))
        self.connect((self.blocks_multiply_const_vxx_1, 0), (self.audio_sink_0, 1))
        self.connect((self.blocks_multiply_const_vxx_1, 0), (self.audio_sink_1, 0))
        self.connect((self.blocks_multiply_const_vxx_2, 0), (self.blocks_add_xx_1_0, 0))
        self.connect((self.blocks_multiply_const_vxx_2_0, 0), (self.blocks_add_xx_1, 1))
        self.connect((self.blocks_multiply_const_vxx_2_1, 0), (self.blocks_add_xx_1_0, 1))
        self.connect((self.blocks_multiply_const_vxx_2_1_0, 0), (self.blocks_add_xx_1, 0))
        self.connect((self.blks2_selector_0, 0), (self.logpwrfft_x_0_1, 0))
        self.connect((self.blocks_multiply_const_vxx_6, 0), (self.dsd_block_ff_0, 0))
        self.connect((self.dsd_block_ff_0, 0), (self.rational_resampler_xxx_0, 0))
        self.connect((self.freq_xlating_fir_filter_xxx_0, 0), (self.band_pass_filter_0, 0))
        self.connect((self.freq_xlating_fir_filter_xxx_0, 0), (self.blks2_selector_0, 0))
        self.connect((self.freq_xlating_fir_filter_xxx_0, 0), (self.rational_resampler_xxx_1, 0))
        self.connect((self.freq_xlating_fir_filter_xxx_0, 0), (self.rational_resampler_xxx_1_0, 0))
        self.connect((self.freq_xlating_fir_filter_xxx_0, 0), (self.rational_resampler_xxx_1_1, 0))
        self.connect((self.logpwrfft_x_0_1, 0), (self.blocks_udp_sink_0, 0))
        self.connect((self.rational_resampler_xxx_1, 0), (self.blks2_selector_0, 1))
        self.connect((self.rational_resampler_xxx_1_0, 0), (self.blks2_selector_0, 2))
        self.connect((self.rational_resampler_xxx_1_1, 0), (self.blks2_selector_0, 3))
        self.connect((self.rtlsdr_source_0, 0), (self.freq_xlating_fir_filter_xxx_0, 0))
        self.connect((self.rational_resampler_xxx_0, 0), (self.blocks_add_xx_1, 2))


    def get_SQL(self):
        return self.SQL

    def set_SQL(self, SQL):
        self.SQL = SQL
        self.analog_pwr_squelch_xx_0.set_threshold(self.SQL-100)

    def get_Rx_Mute(self):
        return self.Rx_Mute

    def set_Rx_Mute(self, Rx_Mute):
        self.Rx_Mute = Rx_Mute
        self.blocks_multiply_const_vxx_1.set_k((self.AFGain/100.0) *  (not self.Rx_Mute))

    def get_Rx_Mode(self):
        return self.Rx_Mode

    def set_Rx_Mode(self, Rx_Mode):
        self.Rx_Mode = Rx_Mode
        self.blocks_multiply_const_vxx_6.set_k(self.Rx_Mode==6)
        self.blocks_multiply_const_vxx_2.set_k(self.Rx_Mode<4)
        self.blocks_multiply_const_vxx_2_0.set_k((self.Rx_Mode==4) * 0.2)
        self.blocks_multiply_const_vxx_2_1.set_k(self.Rx_Mode==5)
        self.blocks_multiply_const_vxx_2_1_0.set_k(1.0 + (self.Rx_Mode==5))

    def get_Rx_LO(self):
        return self.Rx_LO

    def set_Rx_LO(self, Rx_LO):
        self.Rx_LO = Rx_LO
        self.rtlsdr_source_0.set_center_freq(self.Rx_LO, 0)

    def get_Rx_Gain(self):
        return self.Rx_Gain

    def set_Rx_Gain(self, Rx_Gain):
        return

    def get_Rx_Filt_Low(self):
        return self.Rx_Filt_Low

    def set_Rx_Filt_Low(self, Rx_Filt_Low):
        self.Rx_Filt_Low = Rx_Filt_Low
        self.band_pass_filter_0.set_taps(firdes.complex_band_pass(1, 48000, self.Rx_Filt_Low, self.Rx_Filt_High, 100, firdes.WIN_HAMMING, 6.76))

    def get_Rx_Filt_High(self):
        return self.Rx_Filt_High

    def set_Rx_Filt_High(self, Rx_Filt_High):
        self.Rx_Filt_High = Rx_Filt_High
        self.band_pass_filter_0.set_taps(firdes.complex_band_pass(1, 48000, self.Rx_Filt_Low, self.Rx_Filt_High, 100, firdes.WIN_HAMMING, 6.76))

    def get_RxOffset(self):
        return self.RxOffset

    def set_RxOffset(self, RxOffset):
        self.RxOffset = RxOffset
        self.freq_xlating_fir_filter_xxx_0.set_center_freq(self.RxOffset)

    def get_FFT_SEL(self):
        return self.FFT_SEL

    def set_FFT_SEL(self, FFT_SEL):
        self.FFT_SEL = FFT_SEL
        self.blks2_selector_0.set_input_index(self.FFT_SEL)
        self.logpwrfft_x_0_1.set_sample_rate(48000/ (2 ** self.FFT_SEL))

    def get_AFGain(self):
        return self.AFGain

    def set_AFGain(self, AFGain):
        self.AFGain = AFGain
        self.blocks_multiply_const_vxx_1.set_k((self.AFGain/100.0) *  (not self.Rx_Mute))


def docommands(tb):
  try:
    os.mkfifo("/tmp/langstoneRx")
  except OSError as oe:
    if oe.errno != errno.EEXIST:
      raise
  ex=False
  lastbase=0
  while not ex:
    fifoin=open("/tmp/langstoneRx",'r')
    while True:
       try:
        with fifoin as filein:
         for line in filein:
           line=line.strip()
           if line[0]=='Q':
              ex=True
           if line[0]=='U':
              value=int(line[1:])
              tb.set_Rx_Mute(value)
           if line[0]=='H':
              value=int(line[1:])
              if value==1:
                  tb.lock()
              if value==0:
                  tb.unlock()
           if line[0]=='O':
              value=int(line[1:])
              tb.set_RxOffset(value)
           if line[0]=='V':
              value=int(line[1:])
              tb.set_AFGain(value)
           if line[0]=='L':
              value=int(line[1:])
              tb.set_Rx_LO(value)
           if line[0]=='A':
              value=int(line[1:])
              tb.set_Rx_Gain(value)
           if line[0]=='S':
              value=int(line[1:])
              tb.set_SQL(value)
           if line[0]=='F':
              value=int(line[1:])
              tb.set_Rx_Filt_High(value)
           if line[0]=='I':
              value=int(line[1:])
              tb.set_Rx_Filt_Low(value)
           if line[0]=='M':
              value=int(line[1:])
              tb.set_Rx_Mode(value)
           if line[0]=='W':
              value=int(line[1:])
              tb.set_FFT_SEL(value)

       except:
         break


def main(top_block_cls=Lang_RX_RtlSdr, options=None):
    tb = top_block_cls()

    #def sig_handler(sig=None, frame=None):
    #    tb.stop()
    #    tb.wait()

    #    sys.exit(0)

    #signal.signal(signal.SIGINT, sig_handler)
    #signal.signal(signal.SIGTERM, sig_handler)

    tb.start()
    docommands(tb)
    tb.stop()
    tb.wait()


if __name__ == '__main__':
    main()
