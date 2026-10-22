# uncompyle6 version 3.9.3
# Python bytecode version base 2.7 (62211)
# Decompiled from: Python 3.13.5 (main, Aug 10 2026, 12:06:59) [GCC 14.2.0]
# Embedded file name: sonde_predict_v3\waypoint.py


class WayPoint(object):
    """Class to store one waypoint of the track"""

    def __init__(self):
        self.lat = None
        self.lon = None
        self.t = None
        self.altitude = None
        self.burst = None
        self.velocity = None
        self.heading = None
        self.VGRD = None
        self.UGRD = None
        self.validT = None
        self.fcT = None
        self.id = None
        self.level = None
        return
