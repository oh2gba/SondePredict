# uncompyle6 version 3.9.3
# Python bytecode version base 2.7 (62211)
# Decompiled from: Python 3.13.5 (main, Aug 10 2026, 12:06:59) [GCC 14.2.0]
# Embedded file name: sonde_predict_v3\db_handler.py
import sqlite3 as lite
from track import Track
from waypoint import WayPoint
import logging
logger = logging.getLogger('logger.db')
import settings, defaults, sys
PREDICTIONSTABLE = 'CREATE TABLE IF NOT EXISTS             tracks(\n                            id              INTEGER PRIMARY KEY AUTOINCREMENT,\n                            t_created       INT,\n                            forecastFor     INT,\n                            usedFc          INT,\n                            urLoc           TEXT,\n                            urLat           FLOAT,\n                            urLon           FLOAT,\n                            siteLoc         TEXT,\n                            siteLat         FLOAT,\n                            siteLon         FLOAT,\n                            asc_rate        FLOAT,\n                            desc_rate       FLOAT,\n                            burst_height    INT,\n                            impact_lat      INT,\n                            impact_lon      INT,\n                            manual_run      BOOL,\n                            calcInterval    INT,\n                            distance        INT,\n                            heading         INT,\n                            balloon_desc_gain INT\n                        );'
WAYPOINTSTABLE = 'CREATE TABLE IF NOT EXISTS             waypoints(\n                            id              INTEGER PRIMARY KEY AUTOINCREMENT,\n                            track_id        INT,\n                            lat             FLOAT,\n                            lon             FLOAT,\n                            t               INT,\n                            altitude        INT,\n                            level           INT,\n                            burst           BOOL,\n                            velocity        FLOAT,\n                            heading         FLOAT,\n                            VGRD            FLOAT,\n                            UGRD            FLOAT,\n                            validT          INT,\n                            fcT             INT\n                        );'

class Db:

    def __init__(self):
        with settings.dbLock:
            try:
                try:
                    con = lite.connect(settings.dbFileName)
                    cur = con.cursor()
                    cur.execute(PREDICTIONSTABLE)
                    cur.execute(WAYPOINTSTABLE)
                    con.commit()
                except lite.Error as e:
                    raise Exception('Error %s:' % e.args[0])
                    if con:
                        con.rollback()

            finally:
                if con:
                    con.close()

        return

    def delete(self, ids):
        if not type(ids) == list:
            raise Exception('Expecting a list of ids')
        with settings.dbLock:
            con = lite.connect(settings.dbFileName)
            cur = con.cursor()
            try:
                try:
                    for id in ids:
                        try:
                            id = int(id)
                        except:
                            continue

                        cur.execute('DELETE FROM tracks WHERE id == ?', (id,))
                        cur.execute('DELETE FROM waypoints WHERE track_id == ?', (id,))

                    con.commit()
                except lite.Error as e:
                    raise Exception('Error %s:' % e.args[0])
                    if con:
                        con.rollback()

            finally:
                if con:
                    con.close()

        return

    def get(self, id=None, wayPoints=True):
        with settings.dbLock:
            tracks = []
            if id != None:
                try:
                    id = int(id)
                except:
                    raise Exception('id must be an integer')

            con = lite.connect(settings.dbFileName)
            cur = con.cursor()
            if id == None:
                cur.execute('SELECT id, t_created, forecastFor, usedFc,\n                        urLoc, urLat, urLon, siteLoc, siteLat, siteLon, asc_rate,\n                        desc_rate, burst_height, impact_lat, impact_lon, manual_run,\n                        calcInterval, distance, heading, balloon_desc_gain FROM tracks ORDER BY forecastFor DESC')
            else:
                cur.execute('SELECT id, t_created, forecastFor, usedFc,\n                        urLoc, urLat, urLon, siteLoc, siteLat, siteLon, asc_rate,\n                        desc_rate, burst_height, impact_lat, impact_lon, manual_run,\n                        calcInterval, distance, heading, balloon_desc_gain FROM tracks WHERE id == ?', (id,))
            while True:
                row = cur.fetchone()
                if row == None:
                    break
                track = Track()
                if wayPoints == True:
                    track.wayPoints = self._getWayPoints(row[0])
                track.id = row[0]
                track.t_created = row[1]
                track.forecastFor = row[2]
                track.usedFc = row[3]
                track.urLoc = row[4]
                track.urLat = row[5]
                track.urLon = row[6]
                track.siteLoc = row[7]
                track.siteLat = row[8]
                track.siteLon = row[9]
                track.asc_rate = row[10]
                track.desc_rate = row[11]
                track.burst_height = row[12]
                track.impact_lat = row[13]
                track.impact_lon = row[14]
                track.manual_run = row[15]
                track.calcInterval = row[16]
                track.distance = row[17]
                track.heading = row[18]
                track.balloon_desc_gain = row[19]
                tracks.append(track)

            con.close()
            return tracks
        return

    def _getWayPoints(self, id):
        try:
            id = int(id)
        except:
            raise Exception('id must be an integer')

        wps = []
        con = lite.connect(settings.dbFileName)
        cur = con.cursor()
        cur.execute('SELECT track_id, lat, lon, t, altitude, level, burst, velocity, heading,\n                    VGRD, UGRD, validT, fcT FROM waypoints WHERE track_id == ?', (id,))
        while True:
            row = cur.fetchone()
            if row == None:
                break
            wp = WayPoint()
            wp.track_id = row[0]
            wp.lat = row[1]
            wp.lon = row[2]
            wp.t = row[3]
            wp.altitude = row[4]
            wp.level = row[5]
            wp.burst = row[6]
            wp.velocity = row[7]
            wp.heading = row[8]
            wp.VGRD = row[9]
            wp.UGRD = row[10]
            wp.validT = row[11]
            wp.fcT = row[12]
            wps.append(wp)

        con.close()
        return wps

    def put(self, track):
        logger.debug('put req')
        with settings.dbLock:
            if not isinstance(track, Track):
                raise Exception('Expecting an instance of Track')
            try:
                try:
                    con = lite.connect(settings.dbFileName)
                    cur = con.cursor()
                    if settings.overwrite_predictions:
                        logger.debug('overwrite enabled')
                        cur.execute('SELECT id FROM tracks WHERE forecastFor == ? AND siteLoc == ? AND urLoc == ? AND t_created == ?', (
                         track.forecastFor, track.siteLoc, track.urLoc, track.t_created))
                        ids = cur.fetchall()
                        if ids:
                            for id in ids:
                                cur.execute('DELETE FROM tracks WHERE id == ?', (id[0],))
                                cur.execute('DELETE FROM waypoints WHERE track_id == ?', (id[0],))
                                logger.debug('deleted track id: %s' % id[0])

                        else:
                            logger.debug('nothing to be deleted')
                    logger.debug(track.balloon_desc_gain)
                    cur.execute('INSERT INTO tracks(t_created, forecastFor, usedFc,\n                    urLoc, urLat, urLon, siteLoc, siteLat, siteLon, asc_rate,\n                    desc_rate, burst_height, impact_lat, impact_lon, manual_run,\n                    calcInterval, distance, heading, balloon_desc_gain) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)', (
                     track.t_created, track.forecastFor, track.usedFc, track.urLoc, track.urLat,
                     track.urLon, track.siteLoc, track.siteLat, track.siteLon,
                     track.asc_rate, track.desc_rate, track.burst_height,
                     track.impact_lat, track.impact_lon, track.manual_run, track.calcInterval, track.distance, track.heading, track.balloon_desc_gain))
                    id = cur.lastrowid
                    for wp in track.wayPoints:
                        cur.execute('INSERT INTO waypoints(track_id, lat, lon, t,\n                        altitude, level, burst, velocity, heading, VGRD, UGRD, validT, fcT) VALUES( ?,\n                        ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)', (id, wp.lat, wp.lon, wp.t,
                         wp.altitude, wp.level, wp.burst, wp.velocity, wp.heading, wp.VGRD, wp.UGRD, wp.validT, wp.fcT))

                    con.commit()
                except lite.Error as e:
                    logger.error(str(e))
                    raise Exception('Error in db put: %s' % e.args[0])
                    if con:
                        con.rollback()
                    id = None

            finally:
                if con:
                    con.close()
                    return id

        return


if __name__ == '__main__':
    db = Db()
    t = Track()
    t.asc_rate = 123
    wp = WayPoint()
    wp.altitude = 25000
    t.wayPoints = [wp]
    print db.put(t)
    print '-' * 25
    x = db.get(id='1')[0]
    print x.asc_rate
    print x.wayPoints[0].altitude
    y = db.get(wayPoints=False)[0]
    print y.wayPoints
