# uncompyle6 version 3.9.3
# Python bytecode version base 2.7 (62211)
# Decompiled from: Python 3.13.5 (main, Aug 10 2026, 12:06:59) [GCC 14.2.0]
# Embedded file name: sonde_predict_v3\sp.py
import os, sys, wx, logging, configobj, validate
from Queue import Queue
from gui.spUI import MainFrame
import settings, defaults
from helpers import *
import db_handler
from track import Track
from waypoint import WayPoint
from geopy.distance import Point
from track_calculator import CalculationWorker
import settings_dialog
from create_kml import CreateKml
import scheduler, set_calendar_range, set_clock, home_communicator, threading, settings_image, globe_image, information_image
CONSOLELOG = True
FILELOG = True
CONFIGFILE = 'config.ini'
TEMPDIR = 'temp'
KMLDIR = 'kml'
logger = logging.getLogger('logger')
logger.setLevel(logging.DEBUG)
ch = logging.StreamHandler()
ch.setLevel(logging.DEBUG)
fh = logging.FileHandler('log.txt')
fh.setLevel(logging.ERROR)
formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
fh.setFormatter(formatter)
ch.setFormatter(formatter)
if FILELOG:
    logger.addHandler(fh)
if CONSOLELOG:
    logger.addHandler(ch)
logger.info('Starting...')

class Main(MainFrame, Helper):

    def __init__(self, parent):
        MainFrame.__init__(self, parent)
        self.addTools()
        img = globe_image.Globe.getIcon()
        self.SetIcon(img)
        self.m_statusBar1.SetStatusText('Sonde Predict', 0)
        self.scheduler = False
        self.readConfig()
        settings_dialog.Main(self).setSettings()
        self.checkFolders()
        self.threads = []
        if not Helper().wgribTest():
            self.showErrorMessage('wgrib2 not found!', fatal=True)
            return
        self.db = db_handler.Db()
        self.refreshTree()
        self.refreshNameBoxes()
        c = set_calendar_range.SetCalendarRange(parent=self)
        self.threads.append(c)
        c.start()
        c = set_clock.SetClock(parent=self)
        self.threads.append(c)
        c.start()
        c = home_communicator.Hcomm(self, self.config)
        self.threads.append(c)
        c.start()
        self.onEnableScheduler()
        self.setIdle()
        self.setSchedulerOptions()
        self.setManualRunSettingsChange()
        threading.Timer(10, self.removeThreads).start()
        return

    def removeThreads(self):
        if self:
            toBeDeleted = []
            for i, t in enumerate(self.threads):
                if not t.isAlive():
                    toBeDeleted.append(i)

            for i in toBeDeleted:
                try:
                    del self.threads[i]
                except:
                    pass

            threading.Timer(10, self.removeThreads).start()
        return

    def addTools(self):
        """Add tools to toolbar here, now the image I get loaded from a .py"""
        ID_SETTINGS_DIALOG = wx.NewId()
        ID_ABOUT_DIALOG = wx.NewId()
        img = settings_image.settings_image.GetBitmap()
        self.m_toolBar1.AddLabelTool(ID_SETTINGS_DIALOG, u'settings', img, wx.NullBitmap, wx.ITEM_NORMAL, u'', 'Settings dialog', None)
        wx.EVT_TOOL(self, ID_SETTINGS_DIALOG, self.onSettings)
        self.m_toolBar1.AddSeparator()
        img = information_image.Information.GetBitmap()
        self.m_toolBar1.AddLabelTool(ID_ABOUT_DIALOG, u'about', img, wx.NullBitmap, wx.ITEM_NORMAL, u'', 'About', None)
        wx.EVT_TOOL(self, ID_ABOUT_DIALOG, self.onAbout)
        self.m_toolBar1.AddSeparator()
        self.m_toolBar1.Realize()
        return

    def setRunning(self):
        self.m_button1.SetLabel('Stop')
        self.m_button8.Enable(False)
        return

    def setIdle(self):
        self.m_statusBar1.SetStatusText('', 1)
        self.m_statusBar1.SetStatusText('', 2)
        self.m_button1.SetLabel('Run')
        self.m_button8.Enable(True)
        return

    def setTaskProgress(self, progress):
        txt = 'Task: ' + str(progress)
        self.m_statusBar1.SetStatusText(txt, 1)
        return

    def setProgress(self, progress):
        txt = 'Progress: ' + str(progress) + '%'
        self.m_statusBar1.SetStatusText(txt, 2)
        return

    def onStartManualScheduledTask(self, *args, **kwargs):
        scheduler.Scheduler(self, once=True)
        return

    def onEnableScheduler(self, *args, **kwargs):
        if self.m_checkBox2.GetValue():
            self.m_button1.Enable(False)
            self.m_button8.Enable(False)
            self.scheduler = scheduler.Scheduler(self)
        else:
            self.m_button1.Enable(True)
            self.m_button8.Enable(True)
            try:
                self.scheduler.stop.set()
                self.scheduler = False
            except:
                pass

        return

    def setSchedulerOptions(self):
        if 'schedule' in self.config:
            self.m_checkBox2.Enable(True)
            self.m_button8.Enable(True)
        else:
            self.m_checkBox2.Enable(False)
            self.m_button8.Enable(False)
        return

    def reloadScheduler(self):
        self.setSchedulerOptions()
        if self.scheduler:
            self.scheduler.reload.set()
        return

    def onSettings(self, *args, **kwargs):
        for child in self.GetChildren():
            if isinstance(child, settings_dialog.Main):
                child.Show()
                return

        settings_dialog.Main(self).Show()
        return

    def shutdown(self, *args, **kwds):
        logger.debug('shutdown')
        for child in self.GetChildren():
            if isinstance(child, settings_dialog.Main):
                child.Destroy()

        self.Hide()
        self.saveConfig()
        try:
            self.scheduler.stop.set()
            self.scheduler.join()
        except Exception as e:
            logger.debug('scheduler not stopped: %s' % str())

        try:
            settings.cache.stop.set()
        except:
            pass

        logger.debug('number of threads running: %s' % str(len(self.threads)))
        for t in self.threads:
            logger.debug('stopping: %s' % str(t))
            t.stop.set()
            t.join()

        settings.cache.join()
        self.Destroy()
        logger.debug('Shutdown OK')
        return

    def saveConfig(self):
        xPos, yPos = self.GetPosition()
        if 'window' not in self.config:
            self.config['window'] = {}
        self.config['window']['xPos'] = str(xPos)
        self.config['window']['yPos'] = str(yPos)
        xSize, ySize = self.GetSize()
        self.config['window']['xSize'] = str(xSize)
        self.config['window']['ySize'] = str(ySize)
        logger.debug('saving config: %s' % self.config.filename)
        self.config.write()
        return

    def readConfig(self):
        fileName = settings.configFile
        logger.debug('reading config file: %s' % fileName)
        spec = defaults.DEFAULTCONFIG.split('\n')
        self.config = configobj.ConfigObj(fileName, encoding='UTF8', configspec=spec)
        validator = validate.Validator()
        results = self.config.validate(validator, copy=True)
        try:
            self.config.write()
        except Exception as e:
            logger.error(str(e))

        try:
            if 'window' in self.config:
                if 'xPos' in self.config['window'] and 'yPos' in self.config['window']:
                    self.SetPosition((int(self.config['window']['xPos']), int(self.config['window']['yPos'])))
                if 'xSize' in self.config['window'] and 'ySize' in self.config['window']:
                    self.SetSize((int(self.config['window']['xSize']), int(self.config['window']['ySize'])))
        except Exception as e:
            logger.error(str(e))

        return

    def showErrorMessage(self, message='None', fatal=True):
        if fatal:
            title = 'FATAL!'
        else:
            title = 'Error'
        wx.MessageBox(message, title, wx.OK | wx.ICON_ERROR, parent=self)
        if fatal:
            self.shutdown()
        return

    def checkFolders(self):
        try:
            if not os.path.exists(settings.tempDir):
                os.makedirs(settings.tempDir)
                logger.debug('temp folder created')
        except Exception as e:
            self.showErrorMessage('Could not create the "temp" folder:\n%s' % str(e))

        try:
            if not os.path.exists(settings.kmlDir):
                os.makedirs(settings.kmlDir)
                logger.debug('kml folder created')
        except Exception as e:
            self.showErrorMessage('Could not create the "kml" folder:\n%s' % str(e))

        return

    def onManualRun(self, *args, **kwargs):
        logger.debug('on manual run')
        for t in self.threads:
            if isinstance(t, CalculationWorker):
                if t.isAlive():
                    logger.debug('thread found and alive, stopped it')
                    t.stop.set()
                    self.setIdle()
                    return

        logger.debug('no thread found, starting a new one')
        self.runPredictions()
        return

    def runPredictions(self, *args, **kwds):
        logger.debug('runPredictions')
        track = Track()
        try:
            id = self.manualYourLoc.GetSelection()
            clientData = self.manualYourLoc.GetClientData(id)
            logger.debug('Item %s, has config id: %s' % (id, clientData))
            if id != wx.NOT_FOUND:
                urLoc = self.config['locations'][clientData]['name']
                urLat = self.config['locations'][clientData]['lat']
                urLon = self.config['locations'][clientData]['lon']
                urAlt = self.config['locations'][clientData]['alt']
                urPoint = Point(urLat, urLon, urAlt)
                track.balloon_desc_gain = self.config['track_settings']['balloon_desc_gain']
                track.urLoc = urLoc
                track.urLat = urLat
                track.urLon = urLon
                track.urPoint = urPoint
            else:
                return
        except Exception as e:
            logger.debug('urPoint None: %s' % str(e))
            urPoint = None
            return

        try:
            id = self.manualLaunchLoc.GetSelection()
            clientData = self.manualYourLoc.GetClientData(id)
            if id != wx.NOT_FOUND:
                siteLoc = self.config['locations'][clientData]['name']
                siteLat = self.config['locations'][clientData]['lat']
                siteLon = self.config['locations'][clientData]['lon']
                siteAlt = self.config['locations'][clientData]['alt']
                sitePoint = Point(siteLat, siteLon, siteAlt)
                track.siteLoc = siteLoc
                track.siteLat = siteLat
                track.siteLon = siteLon
                track.sitePoint = sitePoint
            else:
                return
        except Exception as e:
            logger.debug('sitePoint: %s' % e)
            sitePoint = None
            return

        try:
            selectedDate = self.manualDatepicker.GetDate()
            hour = int(self.manualTimeBox.GetValue())
            selectedDate.SetHour(hour)
            timestamp = Helper().wxDateTimeToEpoch(selectedDate)
        except Exception as e:
            logger.debug('forecastFor: %s' % str(e))
            timestamp = None
            return

        try:
            track.forecastFor = timestamp
            track.manual_run = True
            track.asc_rate = self.config['track_settings']['balloon_asc_rate']
            track.desc_rate = self.config['track_settings']['balloon_desc_rate']
            track.burst_height = self.config['track_settings']['balloon_burst_alt']
        except Exception as e:
            logger.error('Could not start manual run: %s' % str(e))
            return

        t = CalculationWorker(tracks=[track], parent=self)
        t.start()
        self.threads.append(t)
        return

    def refreshTree(self, *args, **kwargs):
        self.removeThreads()
        h = Helper()
        try:
            focus = self.FindFocus()
            self.predictionsTree.Disable()
            state = {}
            firstVisible = self.predictionsTree.GetFirstVisibleItem()
            root_item = self.predictionsTree.GetRootItem()
            if root_item.IsOk():
                parent, pcookie = self.predictionsTree.GetFirstChild(root_item)
                while parent.IsOk():
                    parentTxt = self.predictionsTree.GetItemText(parent)
                    selected = self.predictionsTree.IsSelected(parent)
                    expanded = self.predictionsTree.IsExpanded(parent)
                    state[parentTxt] = {'selected': selected, 'expanded': expanded, 'children': {}, 'firstVisible': False}
                    if parent == firstVisible:
                        state[parentTxt]['firstVisible'] = True
                    if self.predictionsTree.ItemHasChildren(parent):
                        child, cookie = self.predictionsTree.GetFirstChild(parent)
                        while child.IsOk():
                            id = self.predictionsTree.GetPyData(child)
                            state[parentTxt]['children'][id] = {'selected': False, 'firstVisible': False}
                            if child == firstVisible:
                                state[parentTxt]['children'][id]['firstVisible'] = True
                            if self.predictionsTree.IsSelected(child):
                                state[parentTxt]['children'][id]['selected'] = True
                            child = self.predictionsTree.GetNextSibling(child)

                    parent, pcookie = self.predictionsTree.GetNextChild(parent, pcookie)

            self.predictionsTree.DeleteAllItems()
            treeRoot = self.predictionsTree.AddRoot('Root')
            data = self.db.get(wayPoints=False)
            parents = {}
            for item in data:
                parentTitle = h.epochToReadableShort(item.forecastFor)
                if parentTitle not in parents:
                    parent = self.predictionsTree.AppendItem(treeRoot, parentTitle)
                    parents[parentTitle] = parent
                else:
                    parent = parents[parentTitle]
                title = str(item.siteLoc) + ' ' + h.epochToHour(item.forecastFor) + 'h'
                new = self.predictionsTree.AppendItem(parent, title)
                self.predictionsTree.SetPyData(new, item.id)
                try:
                    for location in self.config['locations']:
                        if self.config['locations'][location]['name'] == item.siteLoc:
                            warning = int(self.config['locations'][location]['warning'])

                except Exception as e:
                    logger.info('Error retriving warning distance: %s' % str(e))
                    warning = None

                if warning != None:
                    if item.distance <= warning:
                        self.predictionsTree.SetItemBold(new, True)
                        self.predictionsTree.SetItemBold(parent, True)

            ensureVisible = None
            if root_item.IsOk():
                parent, pcookie = self.predictionsTree.GetFirstChild(root_item)
                while parent.IsOk():
                    parentTxt = self.predictionsTree.GetItemText(parent)
                    if parentTxt in state:
                        if state[parentTxt]['expanded']:
                            self.predictionsTree.Expand(parent)
                        if state[parentTxt]['selected']:
                            self.predictionsTree.SelectItem(parent, select=True)
                        if state[parentTxt]['firstVisible']:
                            ensureVisible = parent
                    if self.predictionsTree.ItemHasChildren(parent):
                        child, cookie = self.predictionsTree.GetFirstChild(parent)
                        while child.IsOk():
                            if parentTxt in state:
                                id = self.predictionsTree.GetPyData(child)
                                if id in state[parentTxt]['children']:
                                    if state[parentTxt]['children'][id]['selected']:
                                        self.predictionsTree.SelectItem(child, select=True)
                                    if state[parentTxt]['children'][id]['firstVisible']:
                                        ensureVisible = child
                            child = self.predictionsTree.GetNextSibling(child)

                    parent, pcookie = self.predictionsTree.GetNextChild(parent, pcookie)

            if ensureVisible:
                self.predictionsTree.EnsureVisible(ensureVisible)
            self.predictionsTree.Enable()
            if focus:
                focus.SetFocus()
        except Exception as e:
            logger.error('refreshTree: %s' % str())

        return

    def onTreeChange(self, *args, **kwargs):
        try:
            selections = self.predictionsTree.GetSelections()
            if len(selections) == 1:
                pyData = self.predictionsTree.GetPyData(selections[0])
                if pyData != None:
                    id = pyData
                    track = self.db.get(id=id, wayPoints=False)[0]
                    txt = Helper().makeDetailsTxt(track)
                    self.predictionDetailsTxt.Clear()
                    self.predictionDetailsTxt.AppendText(txt)
                    self.predictionDetailsTxt.Refresh()
                    return
            self.predictionDetailsTxt.Clear()
            self.predictionDetailsTxt.Refresh()
        except Exception as e:
            logger.error('onTreeChange error: %s' % str(e))
            self.predictionDetailsTxt.Clear()
            self.predictionDetailsTxt.AppendText('error')
            self.predictionDetailsTxt.Refresh()

        return

    def getTreeSelected(self):
        """Return the currently selected track ids in a list"""
        selections = self.predictionsTree.GetSelections()
        ids = []
        for item in selections:
            if self.predictionsTree.ItemHasChildren(item):
                child = self.predictionsTree.GetFirstChild(item)[0]
                id = self.predictionsTree.GetPyData(child)
                if id not in ids:
                    ids.append(id)
                while True:
                    child = self.predictionsTree.GetNextSibling(child)
                    if not child.IsOk():
                        break
                    id = self.predictionsTree.GetPyData(child)
                    if id not in ids:
                        ids.append(id)

            else:
                id = self.predictionsTree.GetPyData(item)
                if id not in ids:
                    ids.append(id)

        return ids

    def onDelete(self, *args, **kwargs):
        self.predictionsTree.Disable()
        ids = self.getTreeSelected()
        if ids:
            self.db.delete(ids)
            self.predictionDetailsTxt.Clear()
            self.predictionDetailsTxt.Refresh()
            self.refreshTree()
        else:
            self.predictionsTree.Enable()
        return

    def onGoogleEarth(self, *args, **kwargs):
        ids = self.getTreeSelected()
        if ids:
            tracks = []
            for id in ids:
                track = self.db.get(id=id)[0]
                if track:
                    tracks.append(track)

            if tracks:
                self.enableGeButton(False)
                geFile = self.config['general']['google_earth']
                CreateKml(tracks, parent=self, geFile=geFile)
        return

    def enableGeButton(self, enable):
        if enable:
            self.m_button5.Enable(True)
            self.m_button5.SetLabel('GoogleEarth')
        else:
            self.m_button5.Enable(False)
            self.m_button5.SetLabel('Working...')
        return

    def refreshNameBoxes(self):
        self.manualYourLoc.Clear()
        self.manualLaunchLoc.Clear()
        first = False
        if 'locations' in self.config:
            urSelected = self.manualYourLoc.GetValue()
            siteSelected = self.manualLaunchLoc.GetValue()
            if self.config['locations']:
                for pos in self.config['locations']:
                    if not first:
                        first = pos
                    name = self.config['locations'][pos]['name']
                    self.manualYourLoc.Append(name, clientData=str(pos))
                    self.manualLaunchLoc.Append(name, clientData=str(pos))

                if urSelected == '':
                    if first:
                        try:
                            self.manualYourLoc.SetStringSelection(self.config['locations'][first]['name'])
                        except:
                            pass

                else:
                    self.manualYourLoc.SetStringSelection(urSelected)
                if siteSelected == '':
                    if first:
                        try:
                            self.manualLaunchLoc.SetStringSelection(self.config['locations'][first]['name'])
                        except:
                            pass

                else:
                    self.manualLaunchLoc.SetStringSelection(siteSelected)
                self.manualYourLoc.Refresh()
                self.manualLaunchLoc.Refresh()
        return

    def setCalendarRange(self, lower, upper):
        self.manualDatepicker.SetDateRange(lower, upper)
        return

    def setClock(self, string):
        self.m_statusBar1.SetStatusText(string, 3)
        return

    def onAbout(self, *args, **kwds):
        description = '      Some description      \n\n\n\n'
        info = wx.AboutDialogInfo()
        info.SetName('Sonde Predict')
        ver = str(settings.version)
        logger.debug('Version: %s' % ver)
        info.SetVersion('v %s.%s.%s' % (ver[0], ver[1], ver[2:]))
        info.SetDescription('First release 2012')
        info.SetCopyright('(C) 2022')
        info.SetWebSite('https://atroniq.com')
        wx.AboutBox(info)
        return

    def onManualRunSettingsChange(self, event):
        logger.debug('onManualRunSettingsChange')
        try:
            t = self.manualTimeBox.GetSelection()
            if t != -1:
                self.config['manual_run']['time'] = t
            launch_loc = self.manualLaunchLoc.GetSelection()
            if launch_loc != -1:
                self.config['manual_run']['launch_loc'] = launch_loc
            your_loc = self.manualYourLoc.GetSelection()
            if your_loc != -1:
                self.config['manual_run']['your_loc'] = your_loc
            self.config.write()
        except Exception as e:
            logger.error('could not save config: %s' % e)

        return

    def setManualRunSettingsChange(self):
        try:
            selection = self.config['manual_run'].as_int('time')
            if selection != -1:
                self.manualTimeBox.SetSelection(selection)
        except Exception as e:
            logger.error('could not set manualTimeBox: %s' % e)

        try:
            selection = self.config['manual_run'].as_int('launch_loc')
            if selection != -1:
                self.manualLaunchLoc.SetSelection(selection)
        except Exception as e:
            logger.error('could not set manualLaunchLoc: %s' % e)

        try:
            selection = self.config['manual_run'].as_int('your_loc')
            if selection != -1:
                self.manualYourLoc.SetSelection(selection)
        except Exception as e:
            logger.error('could not set manualYourLoc: %s' % e)

        return


if __name__ == '__main__':
    app = wx.App(False)
    start_app = Main(None)
    start_app.Show()
    app.MainLoop()
