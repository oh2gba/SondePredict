# uncompyle6 version 3.9.3
# Python bytecode version base 2.7 (62211)
# Decompiled from: Python 3.13.5 (main, Aug 10 2026, 12:06:59) [GCC 14.2.0]
# Embedded file name: sonde_predict_v3\data_cache.py
import threading, time, logging
logger = logging.getLogger('logger.cache')
from sys import getsizeof
import track, track_calculator

class Cache(threading.Thread):

    def __init__(self):
        threading.Thread.__init__(self)
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.data = {}
        self.start()
        self.size = 0
        self.m = 0
        return

    def put(self, folder, fn, _type, data, rLat, rLon, level=None):
        if level == None:
            level = '_noLevel'
        with self.lock:
            if folder not in self.data:
                self.data[folder] = {}
            if fn not in self.data[folder]:
                self.data[folder][fn] = {}
            if _type not in self.data[folder][fn]:
                self.data[folder][fn][_type] = {}
            if rLat not in self.data[folder][fn][_type]:
                self.data[folder][fn][_type][rLat] = {}
            if rLon not in self.data[folder][fn][_type][rLat]:
                self.data[folder][fn][_type][rLat][rLon] = {}
            if level not in self.data[folder][fn][_type][rLat][rLon]:
                self.data[folder][fn][_type][rLat][rLon][level] = {'data': None, 't': None}
            self.data[folder][fn][_type][rLat][rLon][level]['data'] = data
            self.data[folder][fn][_type][rLat][rLon][level]['t'] = int(time.time())
        return

    def get(self, folder, fn, _type, rLat, rLon, level=None):
        if level == None:
            level = '_noLevel'
        with self.lock:
            try:
                data = self.data[folder][fn][_type][rLat][rLon][level]['data']
            except:
                data = None

        return data

    def run(self):
        interval = 60
        maxAge = 360
        counter = interval
        while not self.stop.isSet():
            if counter < 1:
                try:
                    self.size = getsizeof(self.data)
                except Exception as e:
                    logger.error('Could not determine the cache size: %s' % str(e))
                    self.size = 0

                try:
                    toDelete = []
                    for folder in self.data:
                        for fn in self.data[folder]:
                            for _type in self.data[folder][fn]:
                                for rLat in self.data[folder][fn][_type]:
                                    for rLon in self.data[folder][fn][_type][rLat]:
                                        for level in self.data[folder][fn][_type][rLat][rLon]:
                                            t = self.data[folder][fn][_type][rLat][rLon][level]['t']
                                            data = self.data[folder][fn][_type][rLat][rLon][level]['data']
                                            if time.time() - t > maxAge:
                                                toDelete.append((folder, fn, _type, rLat, rLon, level))

                    for item in toDelete:
                        logger.debug('deleting item from cache: %s' % str(item))
                        with self.lock:
                            del self.data[item[0]][item[1]][item[2]][item[3]][item[4]][item[5]]

                    counter = interval
                except Exception as e:
                    logger.error(str(e))

            time.sleep(1)
            counter -= 1

        return
