# uncompyle6 version 3.9.3
# Python bytecode version base 2.7 (62211)
# Decompiled from: Python 3.13.5 (main, Aug 10 2026, 12:06:59) [GCC 14.2.0]
# Embedded file name: sonde_predict_v3\settings_dialog.py
import wx
from gui.spUI import SettingsDialog
import logging, settings, configobj
from geopy.distance import Point
logger = logging.getLogger('logger.settings')
logger.setLevel(logging.DEBUG)

class Main(SettingsDialog):

    def __init__(self, parent):
        self.parent = parent
        SettingsDialog.__init__(self, parent)
        self.readConfig()
        self.populateGeneralTab()
        self.populateSchedulingTab()
        self.populateLocationsTab()
        self.populateTrackTab()
        return

    def setSettings(self):
        settings.utcT = self.config['general'].as_bool('use_utc')
        settings.overwrite_predictions = self.config['general'].as_bool('overwrite_predictions')
        settings.balloon_asc_rate = float(self.asc_rate_slider.GetValue() / 10.0)
        settings.balloon_desc_rate = float(self.desc_rate_slider.GetValue() / 10.0)
        settings.balloon_burst_alt = int(self.burst_slider.GetValue())
        settings.proxy = None
        if 'network_settings' in self.config:
            if 'proxy_enabled' in self.config['network_settings']:
                if self.config['network_settings'].as_bool('proxy_enabled'):
                    if 'proxy_address' in self.config['network_settings']:
                        settings.proxy = {'http': (self.config['network_settings']['proxy_address'])}
        logger.debug('Proxy: %s' % settings.proxy)
        return

    def saveConfig(self):
        self.saveGeneralTab()
        self.saveTrackTab()
        self.config.write()
        self.setSettings()
        if hasattr(self.parent, 'config'):
            try:
                self.parent.config.reload()
            except:
                pass

        if hasattr(self.parent, 'refreshTree'):
            try:
                self.parent.refreshTree()
            except:
                pass

        return

    def readConfig(self):
        self.config = configobj.ConfigObj(settings.configFile, encoding='UTF8')
        return

    def onClose(self, event):
        self.saveConfig()
        self.Destroy()
        return

    def onCancel(self, event):
        self.Destroy()
        return

    def onApply(self, event):
        self.saveConfig()
        return

    def populateGeneralTab(self):
        if 'general' in self.config:
            if 'google_earth' in self.config['general']:
                self.m_filePicker1.SetPath(self.config['general']['google_earth'])
            if 'internet_browser' in self.config['general']:
                self.m_filePicker2.SetPath(self.config['general']['internet_browser'])
            if 'overwrite_predictions' in self.config['general']:
                self.m_checkBox2.SetValue(self.config['general'].as_bool('overwrite_predictions'))
            if 'use_utc' in self.config['general']:
                self.m_checkBox3.SetValue(self.config['general'].as_bool('use_utc'))
        if 'network_settings' in self.config:
            if 'proxy_enabled' in self.config['network_settings']:
                self.m_checkBox4.SetValue(self.config['network_settings'].as_bool('proxy_enabled'))
            if 'proxy_address' in self.config['network_settings']:
                self.m_textCtrl71.SetValue(self.config['network_settings']['proxy_address'])
        return

    def saveGeneralTab(self):
        self.config['general']['google_earth'] = self.m_filePicker1.GetPath()
        self.config['general']['internet_browser'] = self.m_filePicker2.GetPath()
        self.config['general']['overwrite_predictions'] = self.m_checkBox2.GetValue()
        self.config['general']['use_utc'] = self.m_checkBox3.GetValue()
        self.config['network_settings']['proxy_enabled'] = self.m_checkBox4.GetValue()
        self.config['network_settings']['proxy_address'] = self.m_textCtrl71.GetValue()
        return

    def populateSchedulingTab(self):
        try:
            self.m_comboBox5.Clear()
            self.m_comboBox6.Clear()
            for location in self.config['locations']:
                item = self.config['locations'][location]['name']
                pos = self.m_comboBox6.GetCount()
                clientData = str(location)
                self.m_comboBox5.Insert(item=item, pos=pos, clientData=clientData)
                self.m_comboBox6.Insert(item=item, pos=pos, clientData=clientData)

        except:
            pass

        self.populateSchedulingDetails()
        return

    def populateSchedulingDetails(self):
        if 'schedule' in self.config:
            try:
                days = (' ').join(self.config['schedule']['days'])
                hours = (' ').join(self.config['schedule']['hours'])
                txt = ' Launch loc:\t%s\n Your loc:\t\t%s\n Days:\t\t%s\n Hours:\t\t%s' % (
                 self.config['schedule']['siteLoc'],
                 self.config['schedule']['urLoc'],
                 days,
                 hours)
                self.m_textCtrl7.Clear()
                self.m_textCtrl7.AppendText(txt)
            except Exception as e:
                logger.error('Can not set schedule details: %s' % str(e))

        return

    def onScheduleSave(self, event):
        try:
            interval = self.m_spinCtrl1.GetValue()
            siteId = self.m_comboBox5.GetSelection()
            if siteId != -1:
                siteLoc = self.config['locations'][str(siteId)]['name']
                siteLat = self.config['locations'][str(siteId)]['lat']
                siteLon = self.config['locations'][str(siteId)]['lon']
                siteAlt = self.config['locations'][str(siteId)]['alt']
                urId = self.m_comboBox6.GetSelection()
                if urId != -1:
                    urLoc = self.config['locations'][str(urId)]['name']
                    urLat = self.config['locations'][str(urId)]['lat']
                    urLon = self.config['locations'][str(urId)]['lon']
                    urAlt = self.config['locations'][str(urId)]['alt']
                    days = self.m_checkList2.GetCheckedStrings()
                    hours = self.m_checkList1.GetCheckedStrings()
                    self.config['schedule'] = {}
                    self.config['schedule']['siteLoc'] = siteLoc
                    self.config['schedule']['siteLat'] = siteLat
                    self.config['schedule']['siteLon'] = siteLon
                    self.config['schedule']['siteAlt'] = siteAlt
                    self.config['schedule']['urLoc'] = urLoc
                    self.config['schedule']['urLat'] = urLat
                    self.config['schedule']['urLon'] = urLon
                    self.config['schedule']['urAlt'] = urAlt
                    self.config['schedule']['interval'] = interval
                    self.config['schedule']['days'] = days
                    self.config['schedule']['hours'] = hours
                    self.saveConfig()
                    self.populateSchedulingDetails()
                    self.parent.reloadScheduler()
        except Exception as e:
            logger.error('Could not save the Schedule: %s' % str(e))

        return

    def populateLocationsTab(self):
        try:
            self.locBox.Clear()
            self.m_textCtrl2.Clear()
            self.m_textCtrl3.Clear()
            self.m_textCtrl4.Clear()
            self.m_textCtrl5.Clear()
            for location in self.config['locations']:
                item = self.config['locations'][location]['name']
                pos = self.locBox.GetCount()
                clientData = str(location)
                self.locBox.Insert(item=item, pos=pos, clientData=clientData)

        except:
            pass

        return

    def onLocListClick(self, event):
        index = event.GetSelection()
        location = self.locBox.GetClientData(index)
        lat = self.config['locations'][location]['lat']
        lon = self.config['locations'][location]['lon']
        alt = self.config['locations'][location]['alt']
        self.m_textCtrl2.SetValue(self.config['locations'][location]['name'])
        self.m_textCtrl3.SetValue(lat)
        self.m_textCtrl4.SetValue(lon)
        self.m_textCtrl5.SetValue(alt)
        try:
            warning = int(self.config['locations'][location]['warning'])
            self.warning_spin.SetValue(warning)
        except Exception as e:
            logger.error(e)

        return

    def onLocName(self, event):
        name = self.m_textCtrl2.GetValue()
        if name == '':
            self.m_button9.SetLabel('Save')
            return
        for _id in self.config['locations']:
            try:
                if name == self.config['locations'][_id]['name']:
                    self.m_button9.SetLabel('Update')
                    return
            except:
                pass

        self.m_button9.SetLabel('Save')
        return

    def onLocInput(self, event):
        name = self.m_textCtrl2.GetValue()
        lat = self.m_textCtrl3.GetValue()
        lon = self.m_textCtrl4.GetValue()
        try:
            p = Point(lat, lon)
            self.m_staticText21.SetLabel(str(p))
            if name != '':
                self.m_button9.Enable(True)
                for _id in self.config['locations']:
                    try:
                        if name == self.config['locations'][_id]['name']:
                            self.m_button9.SetLabel('Update')
                        else:
                            self.m_button9.SetLabel('Save')
                        return
                    except Exception as e:
                        pass

            else:
                raise Exception('Name is not set')
        except Exception as e:
            self.m_staticText21.SetLabel(str(e))
            self.m_button9.Enable(False)
            self.m_button9.SetLabel('Save')

        return

    def onLocSave(self, event):
        name = self.m_textCtrl2.GetValue()
        lat = self.m_textCtrl3.GetValue()
        lon = self.m_textCtrl4.GetValue()
        alt = self.m_textCtrl5.GetValue()
        warning = self.warning_spin.GetValue()
        match = False
        for _id in self.config['locations']:
            try:
                if name == self.config['locations'][_id]['name']:
                    match = True
                    break
            except:
                pass

        if not match:
            _id = 0
            while True:
                if str(_id) not in self.config['locations']:
                    break
                _id += 1

        self.config['locations'][str(_id)] = {'name': name, 'lon': lon, 'lat': lat, 'alt': alt, 'warning': warning}
        self.saveConfig()
        self.populateLocationsTab()
        self.parent.refreshNameBoxes()
        self.populateSchedulingTab()
        return

    def onLocRemove(self, event):
        index = self.locBox.GetSelection()
        if index != -1:
            location = self.locBox.GetClientData(index)
            del self.config['locations'][location]
            self.saveConfig()
            self.populateLocationsTab()
            self.parent.refreshNameBoxes()
        return

    def onAscRateScroll(self, event):
        value = event.GetPosition() / 10.0
        self.asc_rate_label.SetLabel(str(value) + ' m/s')
        self.config['track_settings']['balloon_asc_rate'] = float(value)
        return

    def onDescRateScroll(self, event):
        value = event.GetPosition() / 10.0
        self.desc_rate_label.SetLabel(str(value) + ' m/s')
        self.config['track_settings']['balloon_desc_rate'] = float(value)
        return

    def onBurstScroll(self, event):
        value = event.GetPosition()
        self.burst_label.SetLabel(str(value) + ' m')
        self.config['track_settings']['balloon_burst_alt'] = int(value)
        return

    def onGainScroll(self, event):
        value = event.GetPosition() / 100.0
        self.gain_label.SetLabel('%.2f' % value + '')
        self.config['track_settings']['balloon_desc_gain'] = float(value)
        return

    def onGridSet(self, event):
        n_grid_size = self.grid_size.GetCurrentSelection()
        grid_size = self.grid_size.GetString(n_grid_size)
        self.config['track_settings']['grid_size'] = grid_size
        return

    def populateTrackTab(self):
        try:
            asc_rate = float(self.config['track_settings']['balloon_asc_rate'])
            desc_rate = float(self.config['track_settings']['balloon_desc_rate'])
            burst_alt = float(self.config['track_settings']['balloon_burst_alt'])
            balloon_desc_gain = float(self.config['track_settings']['balloon_desc_gain'])
            self.asc_rate_slider.SetValue(asc_rate * 10.0)
            self.desc_rate_slider.SetValue(desc_rate * 10.0)
            self.burst_slider.SetValue(burst_alt)
            self.gain_slider.SetValue(balloon_desc_gain * 100.0)
            self.asc_rate_label.SetLabel(str(asc_rate) + ' m/s')
            self.desc_rate_label.SetLabel(str(desc_rate) + ' m/s')
            self.burst_label.SetLabel(str(int(burst_alt)) + ' m')
            self.gain_label.SetLabel('%.2f' % balloon_desc_gain + '')
            grid_size = self.config['track_settings']['grid_size']
            n_grid_size = self.grid_size.FindString(grid_size)
            self.grid_size.SetSelection(n_grid_size)
            self.grid_size.Disable()
        except Exception as e:
            logger.error('Can not set track tab: %s' % str(e))

        return

    def saveTrackTab(self):
        return


if __name__ == '__main__':
    app = wx.App(False)
    start_app = Main(None)
    start_app.Show()
    app.MainLoop()
