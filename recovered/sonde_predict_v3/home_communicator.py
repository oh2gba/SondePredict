# uncompyle6 version 3.9.3
# Python bytecode version base 2.7 (62211)
# Decompiled from: Python 3.13.5 (main, Aug 10 2026, 12:06:59) [GCC 14.2.0]
# Embedded file name: sonde_predict_v3\home_communicator.py
import settings, helpers, threading, wx, urllib2, logging
from random import random
logger = logging.getLogger('logger.Hcomm')
logger.setLevel(logging.DEBUG)

class Hcomm(threading.Thread):

    def __init__(self, parent, config):
        threading.Thread.__init__(self)
        self.stop = threading.Event()
        self.parent = parent
        self.config = config
        return

    def run(self):
        self.checklatestApplicationVersion()
        return

    def showMessage(self, message):
        wx.MessageBox(message, 'Info', wx.OK | wx.ICON_INFORMATION)
        return

    def checklatestApplicationVersion(self):
        try:
            link = 'https://atroniq.com/downloads/sp/version.info?%s' % int(random() * 1000000)
            proxy_handler = urllib2.ProxyHandler(settings.proxy)
            opener = urllib2.build_opener(proxy_handler)
            f = opener.open(link, timeout=5)
            data = f.read()
            f.close()
            if data != None:
                newVer = int(data.split(',')[0])
                if newVer > settings.version:
                    logger.info('new version available')
                    message = data.split(',')[1]
                    if self.config['general'].as_bool('version_check'):
                        self.showMessage(message)
                else:
                    logger.info('sp is up to date')
        except Exception as e:
            logger.debug(str(e))

        return
