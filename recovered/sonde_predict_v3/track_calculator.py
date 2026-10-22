# uncompyle6 version 3.9.3
# Python bytecode version base 2.7 (62211)
# Decompiled from: Python 3.13.5 (main, Aug 10 2026, 12:06:59) [GCC 14.2.0]
# Embedded file name: sonde_predict_v3\track_calculator.py
import threading, logging, math, simplekml, os, wxdata2, time
from geopy.distance import VincentyDistance
import geopy
from waypoint import WayPoint
import settings
from helpers import Helper
logger = logging.getLogger('logger.track_calculator')
from db_handler import Db
cwd = os.getcwd()
import wx, sys, configobj
VAR_DESC_RATE = False

class CalculationWorker(threading.Thread):

    def __init__(self, tracks=None, parent=None):
        threading.Thread.__init__(self)
        self.tracks = tracks
        self.parent = parent
        logger.debug('CalculationWorker started')
        self.stop = threading.Event()
        errors = False
        return

    def run(self):
        wx.CallAfter(self.parent.setRunning)
        t_track = len(self.tracks)
        for n_track, track in enumerate(self.tracks):
            if self.stop.isSet():
                return
            p = str(n_track + 1) + '/' + str(t_track)
            wx.CallAfter(self.parent.setTaskProgress, p)
            try:
                for x in range(3):
                    track = self.doCalculation(track)
                    if track != None:
                        break
                    else:
                        logger.error('Calculation failed, retry %s' % x)

                if track:
                    _id = Db().put(track)
                    if _id:
                        logger.debug('New track got id: %s' % _id)
                    wx.CallAfter(self.parent.refreshTree)
            except Exception as e:
                logger.error('Exception in CalculationWorker: %s' % e)
                errors = True

        if not self.stop.isSet():
            wx.CallAfter(self.parent.setIdle)
        return

    def doCalculation(self, track):
        logger.debug('doCalculation started')
        errorCount = 0
        wxData = wxdata2.WxData(parent=self.parent)
        p = track.sitePoint
        h = 0
        try:
            config = configobj.ConfigObj(settings.configFile, encoding='UTF8')
            h_gain = config['track_settings'].as_float('balloon_desc_gain')
        except Exception as e:
            logger.error('Could not read h_gain: %s' % e)
            h_gain = 1

        t = float(track.forecastFor)
        n = 1
        dt = 0
        _burst = False
        trackData = []
        STEP = 1
        while True:
            if _burst:
                if h < 1:
                    break
                if True:
                    try:
                        _desc_rate = (float(track.desc_rate) + float(h) * h_gain / 1000) * STEP
                        h -= _desc_rate
                    except Exception as e:
                        logger.error(e)

                else:
                    h -= float(track.desc_rate) * STEP
                if h <= 0:
                    dt = h / float(track.desc_rate)
                    h = 0
                    t += dt
                t += STEP
                dt = STEP
            else:
                h += float(track.asc_rate) * STEP
                t += STEP
                dt = STEP
            if h >= int(track.burst_height):
                if not _burst:
                    _burst = True
            trackData.append((h, t, n, dt))
            n += 1

        for track_data in trackData:
            if self.stop.isSet():
                return
            if errorCount > 5:
                raise Exception('Too many errors')
            progress = int(float(track_data[2]) / float(len(trackData)) * 100.0)
            try:
                wx.CallAfter(self.parent.setProgress, progress)
            except:
                pass

            try:
                wayPoint = WayPoint()
                wayPoint.lat = p.latitude
                wayPoint.lon = p.longitude
                wayPoint.altitude = track_data[0]
                wayPoint.t = track_data[1]
                for x in range(5):
                    try:
                        windData, nomads = wxData.wxAt(p, track_data[1], _type='wind')
                    except Exception as e:
                        logger.error('Exception in wxAt: %s' % str(e))
                        windData = None

                    if windData != None:
                        break

                if windData == None:
                    message = 'Could not get a connection to the grib server, check your internet connection\nand refer to the log file for more information '
                    wx.CallAfter(self.parent.showErrorMessage, message=message, fatal=False)
                    raise Exception('no data from wxData')
                else:
                    wayPoint.validT = nomads['validT']
                    wayPoint.fcT = nomads['fcT']
                    wayPoint.level = windData['level']
                    try:
                        VGRD = windData['VGRD']
                        wayPoint.VGRD = VGRD
                    except:
                        VGRD = None

                    try:
                        UGRD = windData['UGRD']
                        wayPoint.UGRD = UGRD
                    except:
                        UGRD = None

                    if VGRD != None and UGRD != None:
                        velocity = self.windVelocity(UGRD, VGRD)
                        if velocity != None:
                            wayPoint.velocity = velocity
                            heading = self.sHeading(UGRD, VGRD)
                            if heading != None:
                                wayPoint.heading = heading
                                delta_d = track_data[3] * velocity
                                p = VincentyDistance(delta_d / 1000.0).destination(p, heading)
                                p.altitude = track_data[0]
                                track.new(wayPoint)
                            else:
                                logger.error('No heading from wxData')
                                errorCount += 1
                        else:
                            logger.error('No velocity from wxData')
                            errorCount += 1
                    else:
                        logger.error('No wind data from wxData')
                        logger.debug(str(windData))
                        errorCount += 1
            except Exception as e:
                logger.error('Could not calculate WayPoint at h:%s, t:%s: %s' % (track_data[0], track_data[1], e))
                track = None
                break

        if track:
            track.impact_lat = p.latitude
            track.impact_lon = p.longitude
            track.usedFc = wxData.usedNomadsFolder[0]
            track.distance = round(geopy.distance.distance(p, track.urPoint).km, 1)
            track.heading = self.heading(track.urPoint, p)
            logger.info('Done calculating')
            return track
        else:
            return

    def windVelocity(self, u, v):
        try:
            return math.sqrt(u ** 2 + v ** 2)
        except:
            return

        return

    def sHeading(self, u, v):
        try:
            x = math.degrees(math.atan2(u, v))
            if x < 0:
                x += 360
            return x
        except:
            return

        return

    def heading(self, p1, p2):
        try:
            angle = math.degrees(math.atan2(p2.longitude - p1.longitude, p2.latitude - p1.latitude))
            bearing1 = (angle + 360) % 360
            return bearing1
        except Exception as e:
            logger.debug('heading: %s' % str(e))
            return 'NA'

        return
