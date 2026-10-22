# uncompyle6 version 3.9.3
# Python bytecode version base 2.7 (62211)
# Decompiled from: Python 3.13.5 (main, Aug 10 2026, 12:06:59) [GCC 14.2.0]
# Embedded file name: sonde_predict_v3\set_clock.py
import threading, time, helpers, wx

class SetClock(threading.Thread):

    def __init__(self, parent):
        threading.Thread.__init__(self)
        self.parent = parent
        self.helper = helpers.Helper()
        self.stop = threading.Event()
        return

    def run(self):
        while not self.stop.isSet():
            try:
                string = self.helper.epochToReadableLong(time.time(), seconds=False) + ' UTC'
                wx.CallAfter(self.parent.setClock, string)
                time.sleep(1)
            except Exception as e:
                print e

        return
