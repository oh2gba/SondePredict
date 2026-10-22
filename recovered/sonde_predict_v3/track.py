# uncompyle6 version 3.9.3
# Python bytecode version base 2.7 (62211)
# Decompiled from: Python 3.13.5 (main, Aug 10 2026, 12:06:59) [GCC 14.2.0]
# Embedded file name: sonde_predict_v3\track.py
import time

class Track(object):
    """One track"""

    def __init__(self):
        self.wayPoints = []
        self.t_created = int(time.time())
        self.forecastFor = None
        self.usedFc = None
        self.urLoc = None
        self.urLat = None
        self.urLon = None
        self.urPoint = None
        self.siteLoc = None
        self.siteLat = None
        self.siteLon = None
        self.sitePoint = None
        self.asc_rate = None
        self.desc_rate = None
        self.burst_height = None
        self.impact_lat = None
        self.impact_lon = None
        self.balloon_desc_gain = None
        self.calcInterval = None
        self.manual_run = None
        self.id = None
        self.distance = None
        self.heading = None
        return

    def new(self, wayPoint):
        """Append a new wayPoint"""
        self.wayPoints.append(wayPoint)
        return
