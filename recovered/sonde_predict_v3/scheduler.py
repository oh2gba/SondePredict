# uncompyle6 version 3.9.3
# Python bytecode version base 2.7 (62211)
# Decompiled from: Python 3.13.5 (main, Aug 10 2026, 12:06:59) [GCC 14.2.0]
# Embedded file name: sonde_predict_v3\scheduler.py
import threading, logging, time, settings
from helpers import Helper
import wx, configobj
from track import Track
from geopy.distance import Point
import datetime, calendar, helpers, track_calculator, sys
TEST = False
logger = logging.getLogger('logger.scheduler')

class Scheduler(threading.Thread):

    def __init__(self, parent, once=False):
        self.parent = parent
        self.readConfig()
        threading.Thread.__init__(self)
        self.stop = threading.Event()
        self.reload = threading.Event()
        if not once:
            self.start()
        else:
            self.doTask()
        return

    def readConfig(self):
        self.config = configobj.ConfigObj(settings.configFile, encoding='UTF8')
        return

    def run(self):
        logger.debug('scheduler started')
        interval = int(self.config['schedule']['interval']) * 3600
        if TEST:
            interval = TEST
        counter = interval
        while not self.stop.isSet():
            if self.reload.isSet():
                logger.debug('reload settings')
                self.config.reload()
                interval = int(self.config['schedule']['interval']) * 3600
                if TEST:
                    interval = TEST
                self.reload.clear()
            if counter < 1:
                if TEST:
                    self.stop.set()
                counter = interval
                self.doTask()
            counter -= 1
            time.sleep(1)

        logger.debug('scheduler stopped')
        return

    def doTask(self):
        logger.info('Scheduler initiates run')
        tracks = []
        try:
            for day in self.config['schedule']['days']:
                for hour in self.config['schedule']['hours']:
                    track = Track()
                    try:
                        urLoc = self.config['schedule']['urLoc']
                        urLat = self.config['schedule']['urLat']
                        urLon = self.config['schedule']['urLon']
                        urAlt = self.config['schedule']['urAlt']
                        urPoint = Point(urLat, urLon, urAlt)
                        track.urLoc = urLoc
                        track.urLat = urLat
                        track.urLon = urLon
                        track.urPoint = urPoint
                    except Exception as e:
                        logger.debug('urPoint None: %s' % str(e))
                        break

                    try:
                        siteLoc = self.config['schedule']['siteLoc']
                        siteLat = self.config['schedule']['siteLat']
                        siteLon = self.config['schedule']['siteLon']
                        siteAlt = self.config['schedule']['siteAlt']
                        sitePoint = Point(siteLat, siteLon, siteAlt)
                        track.siteLoc = siteLoc
                        track.siteLat = siteLat
                        track.siteLon = siteLon
                        track.sitePoint = sitePoint
                    except:
                        logger.debug('sitePoint: None')
                        break

                    try:
                        hour = int(hour)
                        day = int(day)
                        timestamp = datetime.datetime.utcnow()
                        timestamp = timestamp.replace(hour=hour, minute=0, second=0)
                        timestamp = timestamp + datetime.timedelta(days=day)
                        timestamp = calendar.timegm(timestamp.timetuple())
                    except Exception as e:
                        logger.debug('forecastFor: %s' % str(e))
                        break

                    try:
                        track.forecastFor = timestamp
                        track.manual_run = False
                        track.asc_rate = self.config['track_settings']['balloon_asc_rate']
                        track.desc_rate = self.config['track_settings']['balloon_desc_rate']
                        track.balloon_desc_gain = self.config['track_settings']['balloon_desc_gain']
                        track.burst_height = self.config['track_settings']['balloon_burst_alt']
                    except Exception as e:
                        logger.error('Could not start manual run: %s' % str(e))
                        break

                    tracks.append(track)

            t = track_calculator.CalculationWorker(tracks=tracks, parent=self.parent)
            self.parent.threads.append(t)
            t.start()
        except Exception as e:
            logger.error(str(e))

        return
