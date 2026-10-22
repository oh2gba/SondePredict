# uncompyle6 version 3.9.3
# Python bytecode version base 2.7 (62211)
# Decompiled from: Python 3.13.5 (main, Aug 10 2026, 12:06:59) [GCC 14.2.0]
# Embedded file name: sonde_predict_v3\settings.py
import os, time
from threading import Lock
import data_cache
startup = time.time()
cwd = os.getcwd()
tempDir = os.path.join(cwd, 'temp')
kmlDir = os.path.join(cwd, 'kml')
dbFileName = os.path.join(cwd, 'data.dat')
proxy = None
EXPECTEDWGRIBVERSION = 'v0.1.9.5.1'
wgribFileName = None
dbLock = Lock()
configFile = os.path.join(cwd, 'config.ini')
cache = data_cache.Cache()
utcT = None
overwrite_predictions = None
balloon_asc_rate = None
balloon_desc_rate = None
balloon_burst_alt = None
version = 4021
