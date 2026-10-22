# uncompyle6 version 3.9.3
# Python bytecode version base 2.7 (62211)
# Decompiled from: Python 3.13.5 (main, Aug 10 2026, 12:06:59) [GCC 14.2.0]
# Embedded file name: sonde_predict_v3\wxdata2.py
from geopy.distance import Point
import time
from datetime import datetime
import urllib2, os.path, subprocess, win32con
LEVELS = {1: 'lev_1_mb=on', 2: 'lev_2_mb=on', 3: 'lev_3_mb=on', 900: 'lev_900_mb=on', 5: 'lev_5_mb=on', 7: 'lev_7_mb=on', 
   650: 'lev_650_mb=on', 525: 'lev_525_mb=on', 400: 'lev_400_mb=on', 275: 'lev_275_mb=on', 20: 'lev_20_mb=on', 
   150: 'lev_150_mb=on', 925: 'lev_925_mb=on', 30: 'lev_30_mb=on', 800: 'lev_800_mb=on', 675: 'lev_675_mb=on', 
   550: 'lev_550_mb=on', 425: 'lev_425_mb=on', 775: 'lev_775_mb=on', 300: 'lev_300_mb=on', 175: 'lev_175_mb=on', 
   50: 'lev_50_mb=on', 950: 'lev_950_mb=on', 825: 'lev_825_mb=on', 700: 'lev_700_mb=on', 10: 'lev_10_mb=on', 
   575: 'lev_575_mb=on', 450: 'lev_450_mb=on', 325: 'lev_325_mb=on', 70: 'lev_70_mb=on', 200: 'lev_200_mb=on', 
   975: 'lev_975_mb=on', 850: 'lev_850_mb=on', 725: 'lev_725_mb=on', 600: 'lev_600_mb=on', 475: 'lev_475_mb=on', 
   350: 'lev_350_mb=on', 225: 'lev_225_mb=on', 100: 'lev_100_mb=on', 1000: 'lev_1000_mb=on', 
   875: 'lev_875_mb=on', 750: 'lev_750_mb=on', 625: 'lev_625_mb=on', 500: 'lev_500_mb=on', 
   375: 'lev_375_mb=on', 250: 'lev_250_mb=on', 125: 'lev_125_mb=on'}
LOWLEVELS = {10: 'lev_10_m_above_ground=on'}
DEBUG = True
import logging
logger = logging.getLogger('logger.wxdata')
import settings, configobj, random, wx, calendar

class WxData():

    def __init__(self, parent):
        self.parent = parent
        self.tempGrbFile = os.path.join(settings.tempDir, 'temp.grb2')
        self.tempCsvFile = os.path.join(settings.tempDir, 'temp.csv')
        self.data = {}
        self.windData = {}
        self.readConfig()
        return

    def readConfig(self):
        fileName = settings.configFile
        self.config = configobj.ConfigObj(fileName, encoding='UTF8')
        return

    def wxAt(self, p, t, _type='wind'):
        rLat = self.roundToHalf(p.latitude)
        rLon = self.roundToHalf(p.longitude)
        alt = p.altitude
        try:
            calcType = int(self.config['calc_config']['windCalcType'])
        except:
            calcType = 1

        data = None
        nomads = self.getFcFileAndFolder(t)
        if nomads:
            level, level_id, level_filter = self.meter2level(rLat, rLon, alt, nomads=nomads, calcType=calcType)
            if level != None:
                data = settings.cache.get(folder=nomads['folder'], fn=nomads['fileName'], _type=_type, rLat=rLat, rLon=rLon, level=level_id)
                if data == None:
                    logger.debug('level: "%s" for alt: "%s"' % (level_id, int(alt)))
                    link = self.gribLink(rLat=rLat, rLon=rLon, nomads=nomads, level=None, level_filter=level_filter, _type=_type)
                    if link:
                        tempGrbFile = self.downloadSaveFile(link)
                        if tempGrbFile:
                            tmpCsvFile = self.grb2csv(tempGrbFile)
                            if tmpCsvFile:
                                data = self.parseCsvFile(tmpCsvFile, _type=_type)
                                data['level'] = level
                                self.delFn(tempGrbFile)
                                self.delFn(tmpCsvFile)
                                settings.cache.put(folder=nomads['folder'], fn=nomads['fileName'], _type=_type, rLat=rLat, rLon=rLon, level=level_id, data=data)
                            else:
                                logger.error('no temp file')
                        else:
                            logger.error('no tempGrbFile, tried to download %s' % link)
                    else:
                        logger.error('no link')
            else:
                logger.error('level is None')
        else:
            logger.error('no nomads')
        return (
         data, nomads)

    def getFcFileAndFolder(self, t):
        """Return the file and folder on the nomads site which contains the best possible fc for the specified t"""
        try:
            self.usedNomadsFolder
        except:
            self.usedNomadsFolder = None

        GfsPpossibilities = [
         '00', '06', '12', '18']
        try:
            t = int(t)
        except:
            logger.error('getFcFolder expected an int')
            return

        if t > time.time():
            fcT = time.time()
        else:
            fcT = t
        grid_normal = True
        if self.usedNomadsFolder != None:
            validT = self.validTime(t, self.usedNomadsFolder[0])
            if grid_normal:
                fn = 'gfs.t%sz.pgrb2full.%s.f%03d' % (
                 time.strftime('%H', time.gmtime(self.usedNomadsFolder[0])),
                 self.config['track_settings']['grid_size'],
                 int(validT))
            else:
                fn = 'gfs.t%sz.pgrb2.%s.anl' % (
                 time.strftime('%H', time.gmtime(self.usedNomadsFolder[0])),
                 self.config['track_settings']['grid_size'])
            return {'fileName': fn, 'folder': (self.usedNomadsFolder[1]), 'fcT': (self.usedNomadsFolder[0]), 'validT': validT}
        else:
            folders = []
            fcT = calendar.timegm(time.strptime(time.strftime('%Y%m%d%H', time.gmtime(fcT)), '%Y%m%d%H'))
            while len(folders) < 3:
                if time.strftime('%H', time.gmtime(fcT)) in GfsPpossibilities:
                    folder = time.strftime('gfs.%Y%m%d/%H', time.gmtime(fcT))
                    folders.append((fcT, folder))
                fcT -= 3600

            for folder in folders:
                validT = self.validTime(t, folder[0])
                if grid_normal:
                    fn = 'gfs.t%sz.pgrb2full.%s.f%03d' % (time.strftime('%H', time.gmtime(folder[0])),
                     self.config['track_settings']['grid_size'],
                     int(validT))
                else:
                    fn = 'gfs.t%sz.pgrb2.%s.anl' % (time.strftime('%H', time.gmtime(folder[0])),
                     self.config['track_settings']['grid_size'])
                link = 'https://nomads.ncep.noaa.gov/pub/data/nccf/com/gfs/prod/' + folder[1] + '/atmos/' + fn
                logger.debug('testing link: %s' % link)
                try:
                    proxy_handler = urllib2.ProxyHandler(settings.proxy)
                    opener = urllib2.build_opener(proxy_handler)
                    f = opener.open(link, timeout=5)
                    f.close()
                    opener.close()
                    proxy_handler.close()
                    self.usedNomadsFolder = folder
                    return {'fileName': fn, 'folder': (folder[1]), 'fcT': (folder[0]), 'validT': validT}
                except urllib2.URLError as e:
                    if hasattr(e, 'code'):
                        if e.code == 404:
                            if len(folders) > 1:
                                logger.debug('folder: %s, not available (yet)' % folder[1])
                                continue
                    elif hasattr(e, 'reason'):
                        reason = str(e.reason)
                    else:
                        reason = +str(e)
                    message = 'Could not get a connection to the grib server: ' + reason
                    logger.error(message)
                    return

            return

    def downloadSaveFile(self, link, retry=3):
        fn = 'tmp_' + str(int(random.random() * 1000000)).zfill(6) + '.grb2'
        fn = os.path.join(settings.tempDir, fn)
        cRetry = 0
        while cRetry < retry:
            if link:
                if cRetry > 0:
                    logger.info('Retrying...')
                try:
                    proxy_handler = urllib2.ProxyHandler(settings.proxy)
                    opener = urllib2.build_opener(proxy_handler)
                    f = opener.open(link, timeout=5)
                    data = f.read()
                    f.close()
                    opener.close()
                    proxy_handler.close()
                    output = open(fn, 'wb')
                    output.write(data)
                    output.close()
                    return fn
                except Exception as e:
                    if hasattr(e, 'reason'):
                        reason = str(e.reason)
                    elif hasattr(e, 'code'):
                        reason = str(e.code)
                    else:
                        reason = +str(e)
                    message = 'Could not get a connection to the grib server: ' + reason
                    logger.error(message)
                    return

                cRetry += 1

        return False

    def roundToHalf(self, value):
        try:
            return round(value * 2) / 2
        except Exception as e:
            logger.error('Could not round %s to .5' % value)
            return

        return

    def grb2csv(self, tempGrbFile):
        try:
            fn = 'tmp_' + str(int(random.random() * 1000000)).zfill(6) + '.csv'
            fn = os.path.join(settings.tempDir, fn)
            p = subprocess.Popen((settings.wgribFileName, tempGrbFile, '-v0', '-unix_time', '-csv', fn), stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=win32con.CREATE_NO_WINDOW)
            output, errors = p.communicate()
            return fn
        except Exception as e:
            logger.error(e)
            return False

        return

    def parseCsvFile(self, fn, _type):
        try:
            f = open(fn, 'r')
        except Exception as e:
            logger.error(str(e))

        d = {}
        if _type == 'wind':
            for raw in f:
                try:
                    line = raw.split(',')
                    line = [x.replace('"', '') for x in line]
                    meas = line[2]
                    value = float(line[6])
                    d[meas] = value
                except Exception as e:
                    logger.warning('error reading grib line: %s' % raw)

        elif _type == 'hgt':
            for raw in f:
                try:
                    line = raw.split(',')
                    meters = line[6]
                    level = line[3]
                    if 'mb' in level:
                        level = level.replace('"', '')
                        level = int(level.split(' ')[0])
                        meters = meters.replace('"', '')
                        meters = float(meters)
                        meters = int(round(meters))
                        d[meters] = level
                except Exception as e:
                    logger.error(str(e))

        f.close()
        return d

    def gribLink(self, rLat, rLon, nomads, level=None, level_filter=None, _type='wind'):
        """Figure out which link to use for downloading"""
        if _type == 'wind':
            gfs_type_filter = '&var_UGRD=on&var_VGRD=on'
            if level_filter == None:
                gfs_level_filter = '&' + str(LEVELS[level])
            else:
                gfs_level_filter = '&' + str(level_filter)
        elif _type == 'hgt':
            gfs_type_filter = '&var_HGT=on'
            gfs_level_filter = '&lev_1000_mb=on&lev_100_mb=on&lev_10_mb=on&lev_125_mb=on&lev_150_mb=on&lev_175_mb=on&lev_1_mb=on&lev_200_mb=on&lev_20_mb=on&lev_225_mb=on&lev_250_mb=on&lev_275_mb=on&lev_2_mb=on&lev_300_mb=on&lev_30_mb=on&lev_325_mb=on&lev_350_mb=on&lev_375_mb=on&lev_3_mb=on&lev_400_mb=on&lev_425_mb=on&lev_450_mb=on&lev_475_mb=on&lev_500_mb=on&lev_50_mb=on&lev_525_mb=on&lev_550_mb=on&lev_575_mb=on&lev_5_mb=on&lev_600_mb=on&lev_625_mb=on&lev_650_mb=on&lev_675_mb=on&lev_700_mb=on&lev_70_mb=on&lev_725_mb=on&lev_750_mb=on&lev_775_mb=on&lev_7_mb=on&lev_800_mb=on&lev_825_mb=on&lev_850_mb=on&lev_875_mb=on&lev_900_mb=on&lev_925_mb=on&lev_950_mb=on&lev_975_mb=on'
        else:
            return
        url = 'https://nomads.ncep.noaa.gov/cgi-bin/filter_gfs_'
        url += self.config['track_settings']['grid_size']
        url += '.pl?file='
        url += nomads['fileName']
        url += gfs_level_filter
        url += gfs_type_filter
        url += '&subregion=&leftlon='
        url += str(rLon)
        url += '&rightlon='
        url += str(rLon)
        url += '&toplat='
        url += str(rLat)
        url += '&bottomlat='
        url += str(rLat)
        url += '&dir=%2F'
        url += nomads['folder']
        url += '%2Fatmos'
        logger.debug('grib link: %s' % url)
        return url

    def validTime(self, t, fcT):
        try:
            t = int(t)
            fcT = int(fcT)
        except:
            logger.error('validTime expected an int')

        if t >= fcT:
            validTimeAbs = t - fcT
            possibilities = range(0, 193, 3)
            x = str(self.closest(validTimeAbs / 3600, possibilities))
            return x.zfill(2)
        else:
            logger.error('validTime, target time can not be earlier than the fcT!')
            return
            return

    def meter2level(self, rLat, rLon, alt, nomads=None, calcType=2):
        try:
            if alt >= 200:
                data = settings.cache.get(folder=nomads['folder'], fn=nomads['fileName'], _type='hgt', rLat=rLat, rLon=rLon)
                if data == None:
                    link = self.gribLink(rLat, rLon, nomads, _type='hgt')
                    if link:
                        tempGrbFile = self.downloadSaveFile(link)
                        if tempGrbFile:
                            tmpCsvFile = self.grb2csv(tempGrbFile)
                            if tmpCsvFile:
                                data = self.parseCsvFile(tmpCsvFile, _type='hgt')
                                self.delFn(tempGrbFile)
                                self.delFn(tmpCsvFile)
                                settings.cache.put(folder=nomads['folder'], fn=nomads['fileName'], _type='hgt', rLat=rLat, rLon=rLon, data=data)
                cAlt = self.closest(alt, data)
                level = data[cAlt]
                level_id = str(level)
                level_filter = LEVELS[level]
                return (
                 level, level_id, level_filter)
            else:
                level = self.closest(alt, LOWLEVELS)
                level_id = 'low' + str(level)
                level_filter = LOWLEVELS[level]
                return (level, level_id, level_filter)

        except Exception as e:
            logger.error('new calc error: %s' % str(e))
            return

        return

    def delFn(self, fn):
        try:
            os.remove(fn)
        except Exception as e:
            logger.error('error in delete tmp file: %s' % str(e))

        return

    def closest(self, target, collection):
        """Find closest match of a collection"""
        return min((abs(target - i), i) for i in collection)[1]


if __name__ == '__main__':
    settings.wgribFileName = 'C:\\Documents and Settings\\job\\Desktop\\sp_svn\\trunk\\v3\\wgrib2\\wgrib2.exe'
    logger = logging.getLogger('logger')
    logger.setLevel(logging.DEBUG)
    ch = logging.StreamHandler()
    ch.setLevel(logging.DEBUG)
    formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
    ch.setFormatter(formatter)
    logger.addHandler(ch)
    logger.info('Starting...')
    from geopy.distance import Point
    p = Point(24, 61, 9632)
    t = time.time()
    w = WxData()
    for x in range(10):
        print w.wxAt(p, t)
        print 'new request ------------'
        print w.wxAt(p, t)
