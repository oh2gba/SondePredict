# uncompyle6 version 3.9.3
# Python bytecode version base 2.7 (62211)
# Decompiled from: Python 3.13.5 (main, Aug 10 2026, 12:06:59) [GCC 14.2.0]
# Embedded file name: sonde_predict_v3\helpers.py
import os, subprocess, win32con, time, calendar, wx
from track import Track
import logging, settings
logger = logging.getLogger('logger.helper')

class Helper:

    def __init__(self):
        return

    def epochToParentId(self, epoch):
        """ID consisting of "dmy" to sort tracks in the tree"""
        try:
            t = int(epoch)
        except:
            raise Exception('Expecting an int')

        if not settings.utcT:
            return time.strftime('%d%m%y', time.localtime(t))
        else:
            return time.strftime('%d%m%y', time.gmtime(t))

        return

    def epochToReadableShort(self, epoch):
        try:
            t = int(epoch)
        except:
            raise Exception('Expecting an int')

        if not settings.utcT:
            return time.strftime('%a, %d %b', time.localtime(t))
        else:
            return time.strftime('%a, %d %b', time.gmtime(t))

        return

    def epochToReadableLong(self, epoch, seconds=False):
        try:
            t = int(epoch)
        except:
            raise Exception('Expecting an int, got: %s' % type(epoch))

        if not settings.utcT:
            if seconds:
                return time.strftime('%a, %d %b %Y %H:%M:%S', time.localtime(t))
            else:
                return time.strftime('%a, %d %b %Y %H:%M', time.localtime(t))

        else:
            if seconds:
                return time.strftime('%a, %d %b %Y %H:%M:%S', time.gmtime(t))
            else:
                return time.strftime('%a, %d %b %Y %H:%M', time.gmtime(t))

        return

    def epochToWxDateTime(self, epoch):
        try:
            t = int(epoch)
        except:
            raise Exception('Expecting an int, got: %s' % type(epoch))

        if not settings.utcT:
            t = time.strftime('%d,%m,%Y', time.localtime(t)).split(',')
        else:
            t = time.strftime('%d,%m,%Y', time.gmtime(t)).split(',')
        wxDt = wx.DateTime()
        wxDt.Set(int(t[0]), int(t[1]) - 1, int(t[2]))
        return wxDt

    def wxDateTimeToEpoch(self, wxDateTimeObj):
        if not isinstance(wxDateTimeObj, wx._misc.DateTime):
            raise Exception('Expecting an instance of wx._misc.DateTime')
        try:
            try:
                day = str(wxDateTimeObj.GetDay())
                day = day.zfill(2)
                month = int(wxDateTimeObj.GetMonth()) + 1
                month = str(month)
                month = month.zfill(2)
                year = str(wxDateTimeObj.GetYear())
                hour = str(wxDateTimeObj.GetHour())
                hour = hour.zfill(2)
                tString = '%s %s %s %s' % (day, month, year, hour)
                struct = time.strptime(tString, '%d %m %Y %H')
                if not settings.utcT:
                    epoch = time.mktime(struct)
                else:
                    epoch = calendar.timegm(struct)
            except Exception as e:
                print e
                epoch = None

        finally:
            return epoch

    def epochToHour(self, epoch):
        try:
            t = int(epoch)
        except:
            raise Exception('Expecting an int')

        if not settings.utcT:
            return time.strftime('%H', time.localtime(t))
        else:
            return time.strftime('%H', time.gmtime(t))

        return

    def makeDetailsTxt(self, track):
        if not isinstance(track, Track):
            raise Exception('Expecting an instance of Track')
        txt = 'Prediction:\n\n'
        txt += 'Generated\t: %s\n' % self.epochToReadableLong(track.t_created)
        txt += 'FC generated\t: %s\n' % self.epochToReadableLong(track.usedFc)
        txt += 'Launch site\t: %s\n' % track.siteLoc
        txt += 'Your location\t: %s\n' % track.urLoc
        txt += 'Ascending rate\t: %s\n' % track.asc_rate
        txt += 'Descending rate\t: %s\n' % track.desc_rate
        txt += 'Descending gain\t: %s\n' % track.balloon_desc_gain
        txt += 'Burst altitude\t: %s\n' % track.burst_height
        txt += 'Dist. to impact\t: %s km\n' % track.distance
        txt += 'Heading\t\t: %s\n' % self.parseHeading(track.heading)
        txt += 'Impact latitude\t: %s\n' % round(float(track.impact_lat), 3)
        txt += 'Impact longitude\t: %s\n' % round(float(track.impact_lon), 3)
        if bool(track.manual_run):
            txt += '\nManually run'
        return txt

    def parseHeading(self, data):
        try:
            data = float(data)
        except:
            logger.error('Could not parse heading %s' % data)
            return 'NA'

        try:
            if data < 22.5 or data >= 337.5:
                return 'North'
            if data >= 22.5 and data < 67.5:
                return 'North East'
            if data >= 67.5 and data < 112.5:
                return 'East'
            if data >= 112.5 and data < 157.5:
                return 'South East'
            if data >= 157.5 and data < 202.5:
                return 'South'
            if data >= 202.5 and data < 247.5:
                return 'South West'
            if data >= 247.5 and data < 292.5:
                return 'West'
            if data >= 292.5 and data < 337.5:
                return 'North West'
        except Exception as e:
            return 'NA'
            logger.error('Could not parse windDir data: %s' % str(e))

        return

    def wgribTest(self):
        """Find wgrib and check if it opens and with which version.
        return is the path plus filename, if not found 'None' is returned.
        """
        try:
            paths = os.environ['PATH'].split(';')
        except Exception as e:
            logger.warning('Could not determine the system path: %s' % str(e))
            paths = []

        paths.append(settings.cwd)
        paths.append(os.path.join(settings.cwd, 'wgrib2'))
        while paths:
            path = paths.pop(-1)
            logger.debug('looking for wgrib in %s' % str(path))
            fileName = os.path.join(path, 'wgrib2.exe')
            try:
                if os.path.exists(fileName):
                    p = subprocess.Popen((fileName, '-version'), stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=win32con.CREATE_NO_WINDOW)
                    output, errors = p.communicate()
                    version = output.split(' ')[0]
                    if version == settings.EXPECTEDWGRIBVERSION:
                        logger.info('wgrib2 %s found here: %s' % (version, fileName))
                    else:
                        logger.error('wgrib2 found, with wrong version, expected %s, but got %s. Not sure if this will work!' % (EXPECTEDWGRIBVERSION, version))
                    settings.wgribFileName = fileName
                    return True
                logger.debug('wgrib not found here...')
            except Exception as e:
                logger.error(e)
                return False

        logger.error('wgrib not found in any of the possible folders...')
        return False


if __name__ == '__main__':
    pass
