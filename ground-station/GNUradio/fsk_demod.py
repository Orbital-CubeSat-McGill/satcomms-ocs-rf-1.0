#!/usr/bin/env python3
# -*- coding: utf-8 -*-

#
# SPDX-License-Identifier: GPL-3.0
#
# GNU Radio Python Flow Graph
# Title: FSK_demodulator
# GNU Radio version: 3.10.12.0

from PyQt5 import Qt
from gnuradio import qtgui
from PyQt5 import QtCore
from gnuradio import analog
import math
from gnuradio import blocks
from gnuradio import digital
from gnuradio import filter
from gnuradio.filter import firdes
from gnuradio import gr
from gnuradio.fft import window
import sys
import signal
from PyQt5 import Qt
from argparse import ArgumentParser
from gnuradio.eng_arg import eng_float, intx
from gnuradio import eng_notation
from gnuradio import soapy
import fsk_demod_epy_block_0 as epy_block_0  # embedded python block
import os
import time
import sip
import threading



class fsk_demod(gr.top_block, Qt.QWidget):

    def __init__(self, log_dir=''):
        gr.top_block.__init__(self, "FSK_demodulator", catch_exceptions=True)
        Qt.QWidget.__init__(self)
        self.setWindowTitle("FSK_demodulator")
        qtgui.util.check_set_qss()
        try:
            self.setWindowIcon(Qt.QIcon.fromTheme('gnuradio-grc'))
        except BaseException as exc:
            print(f"Qt GUI: Could not set Icon: {str(exc)}", file=sys.stderr)
        self.top_scroll_layout = Qt.QVBoxLayout()
        self.setLayout(self.top_scroll_layout)
        self.top_scroll = Qt.QScrollArea()
        self.top_scroll.setFrameStyle(Qt.QFrame.NoFrame)
        self.top_scroll_layout.addWidget(self.top_scroll)
        self.top_scroll.setWidgetResizable(True)
        self.top_widget = Qt.QWidget()
        self.top_scroll.setWidget(self.top_widget)
        self.top_layout = Qt.QVBoxLayout(self.top_widget)
        self.top_grid_layout = Qt.QGridLayout()
        self.top_layout.addLayout(self.top_grid_layout)

        self.settings = Qt.QSettings("gnuradio/flowgraphs", "fsk_demod")

        try:
            geometry = self.settings.value("geometry")
            if geometry:
                self.restoreGeometry(geometry)
        except BaseException as exc:
            print(f"Qt GUI: Could not restore geometry: {str(exc)}", file=sys.stderr)
        self.flowgraph_started = threading.Event()

        ##################################################
        # Parameters
        ##################################################
        self.log_dir = log_dir

        ##################################################
        # Variables
        ##################################################
        self.run_id = run_id = time.strftime('%Y%m%dT%H%M%SZ', time.gmtime())
        self.record_iq = record_iq = False
        self.logs = logs = os.path.abspath(log_dir or os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])), os.pardir, 'logs'))
        self.vga_gain = vga_gain = 24
        self.sq_thresh = sq_thresh = -75
        self.samp_rate = samp_rate = 8e6
        self.rf_gain = rf_gain = 16
        self.offset_hz = offset_hz = 0
        self.log_file = log_file = os.path.join(logs, run_id + '.log')
        self.iq_file = iq_file = os.path.join(logs, time.strftime('%Y%m%dT%H%M%SZ', time.gmtime()) + '.iq') if record_iq else os.devnull
        self.fsk_deviation = fsk_deviation = 25e3
        self.freq = freq = 905e6
        self.decim = decim = 16
        self.amp = amp = False

        ##################################################
        # Blocks
        ##################################################

        self._sq_thresh_range = qtgui.Range(-100, 0, 1, -75, 100)
        self._sq_thresh_win = qtgui.RangeWidget(self._sq_thresh_range, self.set_sq_thresh, "sq", "counter", float, QtCore.Qt.Horizontal)
        self.top_layout.addWidget(self._sq_thresh_win)
        self._rf_gain_range = qtgui.Range(0, 40, 8, 16, 200)
        self._rf_gain_win = qtgui.RangeWidget(self._rf_gain_range, self.set_rf_gain, "LNA Gain (dB)", "slider", float, QtCore.Qt.Horizontal)
        self.top_layout.addWidget(self._rf_gain_win)
        _record_iq_check_box = Qt.QCheckBox("Record IQ")
        self._record_iq_choices = {True: True, False: False}
        self._record_iq_choices_inv = dict((v,k) for k,v in self._record_iq_choices.items())
        self._record_iq_callback = lambda i: Qt.QMetaObject.invokeMethod(_record_iq_check_box, "setChecked", Qt.Q_ARG("bool", self._record_iq_choices_inv[i]))
        self._record_iq_callback(self.record_iq)
        _record_iq_check_box.stateChanged.connect(lambda i: self.set_record_iq(self._record_iq_choices[bool(i)]))
        self.top_layout.addWidget(_record_iq_check_box)
        self._offset_hz_range = qtgui.Range(-50e3, 50e3, 500, 0, 200)
        self._offset_hz_win = qtgui.RangeWidget(self._offset_hz_range, self.set_offset_hz, "Offset (Hz)", "counter_slider", float, QtCore.Qt.Horizontal)
        self.top_layout.addWidget(self._offset_hz_win)
        self._fsk_deviation_range = qtgui.Range(0, 50e3, 5e3, 25e3, 200)
        self._fsk_deviation_win = qtgui.RangeWidget(self._fsk_deviation_range, self.set_fsk_deviation, "'fsk_deviation'", "counter", float, QtCore.Qt.Horizontal)
        self.top_layout.addWidget(self._fsk_deviation_win)
        self._freq_range = qtgui.Range(900e6, 930e6, 1e6, 905e6, 200)
        self._freq_win = qtgui.RangeWidget(self._freq_range, self.set_freq, "'freq'", "counter_slider", float, QtCore.Qt.Horizontal)
        self.top_layout.addWidget(self._freq_win)
        self.soapy_hackrf_source_0 = None
        dev = 'driver=hackrf'
        stream_args = ''
        tune_args = ['']
        settings = ['']

        self.soapy_hackrf_source_0 = soapy.source(dev, "fc32", 1, '',
                                  stream_args, tune_args, settings)
        self.soapy_hackrf_source_0.set_sample_rate(0, samp_rate)
        self.soapy_hackrf_source_0.set_bandwidth(0, 0)
        self.soapy_hackrf_source_0.set_frequency(0, (freq + 500e3))
        self.soapy_hackrf_source_0.set_gain(0, 'AMP', amp)
        self.soapy_hackrf_source_0.set_gain(0, 'LNA', min(max(rf_gain, 0.0), 40.0))
        self.soapy_hackrf_source_0.set_gain(0, 'VGA', min(max(vga_gain, 0.0), 62.0))
        self.qtgui_time_sink_x_0_0 = qtgui.time_sink_f(
            1024, #size
            samp_rate / decim / 10, #samp_rate
            'Demod', #name
            2, #number of inputs
            None # parent
        )
        self.qtgui_time_sink_x_0_0.set_update_time(0.10)
        self.qtgui_time_sink_x_0_0.set_y_axis(-1, 1)

        self.qtgui_time_sink_x_0_0.set_y_label('Amplitude', "")

        self.qtgui_time_sink_x_0_0.enable_tags(True)
        self.qtgui_time_sink_x_0_0.set_trigger_mode(qtgui.TRIG_MODE_FREE, qtgui.TRIG_SLOPE_POS, 0.0, 0, 0, "")
        self.qtgui_time_sink_x_0_0.enable_autoscale(True)
        self.qtgui_time_sink_x_0_0.enable_grid(False)
        self.qtgui_time_sink_x_0_0.enable_axis_labels(True)
        self.qtgui_time_sink_x_0_0.enable_control_panel(False)
        self.qtgui_time_sink_x_0_0.enable_stem_plot(False)


        labels = ['Signal 1', 'Signal 2', 'Signal 3', 'Signal 4', 'Signal 5',
            'Signal 6', 'Signal 7', 'Signal 8', 'Signal 9', 'Signal 10']
        widths = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]
        colors = ['blue', 'red', 'green', 'black', 'cyan',
            'magenta', 'yellow', 'dark red', 'dark green', 'dark blue']
        alphas = [1.0, 1.0, 1.0, 1.0, 1.0,
            1.0, 1.0, 1.0, 1.0, 1.0]
        styles = [3, 1, 1, 1, 1,
            1, 1, 1, 1, 1]
        markers = [0, -1, -1, -1, -1,
            -1, -1, -1, -1, -1]


        for i in range(2):
            if len(labels[i]) == 0:
                self.qtgui_time_sink_x_0_0.set_line_label(i, "Data {0}".format(i))
            else:
                self.qtgui_time_sink_x_0_0.set_line_label(i, labels[i])
            self.qtgui_time_sink_x_0_0.set_line_width(i, widths[i])
            self.qtgui_time_sink_x_0_0.set_line_color(i, colors[i])
            self.qtgui_time_sink_x_0_0.set_line_style(i, styles[i])
            self.qtgui_time_sink_x_0_0.set_line_marker(i, markers[i])
            self.qtgui_time_sink_x_0_0.set_line_alpha(i, alphas[i])

        self._qtgui_time_sink_x_0_0_win = sip.wrapinstance(self.qtgui_time_sink_x_0_0.qwidget(), Qt.QWidget)
        self.top_layout.addWidget(self._qtgui_time_sink_x_0_0_win)
        self.qtgui_freq_sink_x_0_0 = qtgui.freq_sink_c(
            1024, #size
            window.WIN_BLACKMAN_hARRIS, #wintype
            0, #fc
            (samp_rate / decim), #bw
            'XIR Filter', #name
            1,
            None # parent
        )
        self.qtgui_freq_sink_x_0_0.set_update_time(0.10)
        self.qtgui_freq_sink_x_0_0.set_y_axis((-140), 10)
        self.qtgui_freq_sink_x_0_0.set_y_label('Relative Gain', 'dB')
        self.qtgui_freq_sink_x_0_0.set_trigger_mode(qtgui.TRIG_MODE_FREE, 0.0, 0, "")
        self.qtgui_freq_sink_x_0_0.enable_autoscale(False)
        self.qtgui_freq_sink_x_0_0.enable_grid(False)
        self.qtgui_freq_sink_x_0_0.set_fft_average(1.0)
        self.qtgui_freq_sink_x_0_0.enable_axis_labels(True)
        self.qtgui_freq_sink_x_0_0.enable_control_panel(False)
        self.qtgui_freq_sink_x_0_0.set_fft_window_normalized(False)



        labels = ['', '', '', '', '',
            '', '', '', '', '']
        widths = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]
        colors = ["blue", "red", "green", "black", "cyan",
            "magenta", "yellow", "dark red", "dark green", "dark blue"]
        alphas = [1.0, 1.0, 1.0, 1.0, 1.0,
            1.0, 1.0, 1.0, 1.0, 1.0]

        for i in range(1):
            if len(labels[i]) == 0:
                self.qtgui_freq_sink_x_0_0.set_line_label(i, "Data {0}".format(i))
            else:
                self.qtgui_freq_sink_x_0_0.set_line_label(i, labels[i])
            self.qtgui_freq_sink_x_0_0.set_line_width(i, widths[i])
            self.qtgui_freq_sink_x_0_0.set_line_color(i, colors[i])
            self.qtgui_freq_sink_x_0_0.set_line_alpha(i, alphas[i])

        self._qtgui_freq_sink_x_0_0_win = sip.wrapinstance(self.qtgui_freq_sink_x_0_0.qwidget(), Qt.QWidget)
        self.top_layout.addWidget(self._qtgui_freq_sink_x_0_0_win)
        self.qtgui_freq_sink_x_0 = qtgui.freq_sink_c(
            1024, #size
            window.WIN_BLACKMAN_hARRIS, #wintype
            0, #fc
            samp_rate, #bw
            'Source', #name
            1,
            None # parent
        )
        self.qtgui_freq_sink_x_0.set_update_time(0.2)
        self.qtgui_freq_sink_x_0.set_y_axis((-140), 10)
        self.qtgui_freq_sink_x_0.set_y_label('Relative Gain', 'dB')
        self.qtgui_freq_sink_x_0.set_trigger_mode(qtgui.TRIG_MODE_FREE, 0.0, 0, "")
        self.qtgui_freq_sink_x_0.enable_autoscale(False)
        self.qtgui_freq_sink_x_0.enable_grid(False)
        self.qtgui_freq_sink_x_0.set_fft_average(1.0)
        self.qtgui_freq_sink_x_0.enable_axis_labels(True)
        self.qtgui_freq_sink_x_0.enable_control_panel(False)
        self.qtgui_freq_sink_x_0.set_fft_window_normalized(False)



        labels = ['', '', '', '', '',
            '', '', '', '', '']
        widths = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]
        colors = ["blue", "red", "green", "black", "cyan",
            "magenta", "yellow", "dark red", "dark green", "dark blue"]
        alphas = [1.0, 1.0, 1.0, 1.0, 1.0,
            1.0, 1.0, 1.0, 1.0, 1.0]

        for i in range(1):
            if len(labels[i]) == 0:
                self.qtgui_freq_sink_x_0.set_line_label(i, "Data {0}".format(i))
            else:
                self.qtgui_freq_sink_x_0.set_line_label(i, labels[i])
            self.qtgui_freq_sink_x_0.set_line_width(i, widths[i])
            self.qtgui_freq_sink_x_0.set_line_color(i, colors[i])
            self.qtgui_freq_sink_x_0.set_line_alpha(i, alphas[i])

        self._qtgui_freq_sink_x_0_win = sip.wrapinstance(self.qtgui_freq_sink_x_0.qwidget(), Qt.QWidget)
        self.top_layout.addWidget(self._qtgui_freq_sink_x_0_win)
        self.freq_xlating_fir_filter_xxx_0 = filter.freq_xlating_fir_filter_ccc(decim, firdes.low_pass(1.0, samp_rate, 50e3, 40e3), (-500e3 + offset_hz), samp_rate)
        self.epy_block_0 = epy_block_0.blk(expect='greenbear', log_file=log_file, samp_rate=samp_rate, freq=freq, rf_gain=rf_gain, vga_gain=vga_gain, amp=amp, offset_hz=offset_hz, sq_thresh=sq_thresh, iq_file=iq_file)
        self.digital_symbol_sync_xx_0 = digital.symbol_sync_ff(
            digital.TED_GARDNER,
            10,
            0.15,
            1.0,
            1.0,
            0.1,
            1,
            digital.constellation_bpsk().base(),
            digital.IR_MMSE_8TAP,
            128,
            [])
        self.digital_binary_slicer_fb_0 = digital.binary_slicer_fb()
        self.blocks_moving_average_xx_0 = blocks.moving_average_ff(10, 0.1, 4000, 1)
        self.blocks_file_sink_0 = blocks.file_sink(gr.sizeof_char*1, iq_file, False)
        self.blocks_file_sink_0.set_unbuffered(False)
        self.blocks_copy_0 = blocks.copy(gr.sizeof_gr_complex*1)
        self.blocks_copy_0.set_enabled(record_iq)
        self.blocks_complex_to_interleaved_char_0 = blocks.complex_to_interleaved_char(False, 128.0)
        self.blocks_char_to_float_0 = blocks.char_to_float(1, 1)
        self.analog_simple_squelch_cc_0 = analog.simple_squelch_cc(sq_thresh, 0.01)
        self.analog_quadrature_demod_cf_0 = analog.quadrature_demod_cf(((samp_rate / decim) / (2 * math.pi * fsk_deviation)))


        ##################################################
        # Connections
        ##################################################
        self.connect((self.analog_quadrature_demod_cf_0, 0), (self.blocks_moving_average_xx_0, 0))
        self.connect((self.analog_simple_squelch_cc_0, 0), (self.analog_quadrature_demod_cf_0, 0))
        self.connect((self.blocks_char_to_float_0, 0), (self.qtgui_time_sink_x_0_0, 1))
        self.connect((self.blocks_complex_to_interleaved_char_0, 0), (self.blocks_file_sink_0, 0))
        self.connect((self.blocks_copy_0, 0), (self.blocks_complex_to_interleaved_char_0, 0))
        self.connect((self.blocks_moving_average_xx_0, 0), (self.digital_symbol_sync_xx_0, 0))
        self.connect((self.digital_binary_slicer_fb_0, 0), (self.blocks_char_to_float_0, 0))
        self.connect((self.digital_symbol_sync_xx_0, 0), (self.digital_binary_slicer_fb_0, 0))
        self.connect((self.digital_symbol_sync_xx_0, 0), (self.epy_block_0, 0))
        self.connect((self.digital_symbol_sync_xx_0, 0), (self.qtgui_time_sink_x_0_0, 0))
        self.connect((self.freq_xlating_fir_filter_xxx_0, 0), (self.analog_simple_squelch_cc_0, 0))
        self.connect((self.freq_xlating_fir_filter_xxx_0, 0), (self.qtgui_freq_sink_x_0_0, 0))
        self.connect((self.soapy_hackrf_source_0, 0), (self.blocks_copy_0, 0))
        self.connect((self.soapy_hackrf_source_0, 0), (self.freq_xlating_fir_filter_xxx_0, 0))
        self.connect((self.soapy_hackrf_source_0, 0), (self.qtgui_freq_sink_x_0, 0))


    def closeEvent(self, event):
        self.settings = Qt.QSettings("gnuradio/flowgraphs", "fsk_demod")
        self.settings.setValue("geometry", self.saveGeometry())
        self.stop()
        self.wait()

        event.accept()

    def get_log_dir(self):
        return self.log_dir

    def set_log_dir(self, log_dir):
        self.log_dir = log_dir
        self.set_logs(os.path.abspath(self.log_dir or os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])), os.pardir, 'logs')))

    def get_run_id(self):
        return self.run_id

    def set_run_id(self, run_id):
        self.run_id = run_id
        self.set_log_file(os.path.join(self.logs, self.run_id + '.log'))

    def get_record_iq(self):
        return self.record_iq

    def set_record_iq(self, record_iq):
        self.record_iq = record_iq
        self._record_iq_callback(self.record_iq)
        self.set_iq_file(os.path.join(self.logs, time.strftime('%Y%m%dT%H%M%SZ', time.gmtime()) + '.iq') if self.record_iq else os.devnull)
        self.blocks_copy_0.set_enabled(self.record_iq)

    def get_logs(self):
        return self.logs

    def set_logs(self, logs):
        self.logs = logs
        self.set_log_file(os.path.join(self.logs, self.run_id + '.log'))
        self.set_iq_file(os.path.join(self.logs, time.strftime('%Y%m%dT%H%M%SZ', time.gmtime()) + '.iq') if self.record_iq else os.devnull)

    def get_vga_gain(self):
        return self.vga_gain

    def set_vga_gain(self, vga_gain):
        self.vga_gain = vga_gain
        self.epy_block_0.vga_gain = self.vga_gain
        self.soapy_hackrf_source_0.set_gain(0, 'VGA', min(max(self.vga_gain, 0.0), 62.0))

    def get_sq_thresh(self):
        return self.sq_thresh

    def set_sq_thresh(self, sq_thresh):
        self.sq_thresh = sq_thresh
        self.epy_block_0.sq_thresh = self.sq_thresh
        self.analog_simple_squelch_cc_0.set_threshold(self.sq_thresh)

    def get_samp_rate(self):
        return self.samp_rate

    def set_samp_rate(self, samp_rate):
        self.samp_rate = samp_rate
        self.analog_quadrature_demod_cf_0.set_gain(((self.samp_rate / self.decim) / (2 * math.pi * self.fsk_deviation)))
        self.freq_xlating_fir_filter_xxx_0.set_taps(firdes.low_pass(1.0, self.samp_rate, 50e3, 40e3))
        self.qtgui_freq_sink_x_0.set_frequency_range(0, self.samp_rate)
        self.qtgui_freq_sink_x_0_0.set_frequency_range(0, (self.samp_rate / self.decim))
        self.qtgui_time_sink_x_0_0.set_samp_rate(self.samp_rate / self.decim / 10)
        self.soapy_hackrf_source_0.set_sample_rate(0, self.samp_rate)

    def get_rf_gain(self):
        return self.rf_gain

    def set_rf_gain(self, rf_gain):
        self.rf_gain = rf_gain
        self.epy_block_0.rf_gain = self.rf_gain
        self.soapy_hackrf_source_0.set_gain(0, 'LNA', min(max(self.rf_gain, 0.0), 40.0))

    def get_offset_hz(self):
        return self.offset_hz

    def set_offset_hz(self, offset_hz):
        self.offset_hz = offset_hz
        self.epy_block_0.offset_hz = self.offset_hz
        self.freq_xlating_fir_filter_xxx_0.set_center_freq((-500e3 + self.offset_hz))

    def get_log_file(self):
        return self.log_file

    def set_log_file(self, log_file):
        self.log_file = log_file

    def get_iq_file(self):
        return self.iq_file

    def set_iq_file(self, iq_file):
        self.iq_file = iq_file
        self.blocks_file_sink_0.open(self.iq_file)
        self.epy_block_0.iq_file = self.iq_file

    def get_fsk_deviation(self):
        return self.fsk_deviation

    def set_fsk_deviation(self, fsk_deviation):
        self.fsk_deviation = fsk_deviation
        self.analog_quadrature_demod_cf_0.set_gain(((self.samp_rate / self.decim) / (2 * math.pi * self.fsk_deviation)))

    def get_freq(self):
        return self.freq

    def set_freq(self, freq):
        self.freq = freq
        self.epy_block_0.freq = self.freq
        self.soapy_hackrf_source_0.set_frequency(0, (self.freq + 500e3))

    def get_decim(self):
        return self.decim

    def set_decim(self, decim):
        self.decim = decim
        self.analog_quadrature_demod_cf_0.set_gain(((self.samp_rate / self.decim) / (2 * math.pi * self.fsk_deviation)))
        self.qtgui_freq_sink_x_0_0.set_frequency_range(0, (self.samp_rate / self.decim))
        self.qtgui_time_sink_x_0_0.set_samp_rate(self.samp_rate / self.decim / 10)

    def get_amp(self):
        return self.amp

    def set_amp(self, amp):
        self.amp = amp
        self.epy_block_0.amp = self.amp
        self.soapy_hackrf_source_0.set_gain(0, 'AMP', self.amp)



def argument_parser():
    parser = ArgumentParser()
    parser.add_argument(
        "--log-dir", dest="log_dir", type=str, default='',
        help="Set Log directory [default=%(default)r]")
    return parser


def main(top_block_cls=fsk_demod, options=None):
    if options is None:
        options = argument_parser().parse_args()

    qapp = Qt.QApplication(sys.argv)

    tb = top_block_cls(log_dir=options.log_dir)

    tb.start()
    tb.flowgraph_started.set()

    tb.show()

    def sig_handler(sig=None, frame=None):
        tb.stop()
        tb.wait()

        Qt.QApplication.quit()

    signal.signal(signal.SIGINT, sig_handler)
    signal.signal(signal.SIGTERM, sig_handler)

    timer = Qt.QTimer()
    timer.start(500)
    timer.timeout.connect(lambda: None)

    qapp.exec_()

if __name__ == '__main__':
    main()
