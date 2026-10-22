# uncompyle6 version 3.9.3
# Python bytecode version base 2.7 (62211)
# Decompiled from: Python 3.13.5 (main, Aug 10 2026, 12:06:59) [GCC 14.2.0]
# Embedded file name: sonde_predict_v3\set_calendar_range.py
import threading, time, helpers, wx

class SetCalendarRange(threading.Thread):

    def __init__(self, parent):
        threading.Thread.__init__(self)
        self.parent = parent
        self.helper = helpers.Helper()
        self.stop = threading.Event()
        self.interval = 2
        self.upRange = 604800
        self.lowRange = 604800
        return

    def run(self):
        c = 0
        while not self.stop.isSet():
            try:
                if c < 1:
                    t = time.time() - self.lowRange
                    lower = self.helper.epochToWxDateTime(t)
                    t = time.time() + self.upRange
                    upper = self.helper.epochToWxDateTime(t)
                    wx.CallAfter(self.parent.setCalendarRange, lower, upper)
                    c = self.interval
                time.sleep(1)
            except Exception as e:
                print e

            c -= 1

        return
