# uncompyle6 version 3.9.3
# Python bytecode version base 2.7 (62211)
# Decompiled from: Python 3.13.5 (main, Aug 10 2026, 12:06:59) [GCC 14.2.0]
# Embedded file name: sonde_predict_v3\gui\spUI.py
import wx, wx.xrc, wx.calendar

class MainFrame(wx.Frame):

    def __init__(self, parent):
        wx.Frame.__init__(self, parent, id=wx.ID_ANY, title=u'Sonde Predict', pos=wx.DefaultPosition, size=wx.Size(-1, -1), style=wx.DEFAULT_FRAME_STYLE | wx.TAB_TRAVERSAL)
        self.SetSizeHintsSz(wx.Size(700, 500), wx.DefaultSize)
        bSizer1 = wx.BoxSizer(wx.VERTICAL)
        self.m_panel1 = wx.Panel(self, wx.ID_ANY, wx.DefaultPosition, wx.DefaultSize, wx.TAB_TRAVERSAL)
        fgSizer1 = wx.FlexGridSizer(1, 2, 0, 0)
        fgSizer1.AddGrowableCol(0)
        fgSizer1.AddGrowableCol(1)
        fgSizer1.AddGrowableRow(0)
        fgSizer1.SetFlexibleDirection(wx.BOTH)
        fgSizer1.SetNonFlexibleGrowMode(wx.FLEX_GROWMODE_SPECIFIED)
        sbSizer2 = wx.StaticBoxSizer(wx.StaticBox(self.m_panel1, wx.ID_ANY, u'Predictions'), wx.VERTICAL)
        fgSizer71 = wx.FlexGridSizer(2, 1, 0, 0)
        fgSizer71.AddGrowableCol(0)
        fgSizer71.AddGrowableRow(0)
        fgSizer71.SetFlexibleDirection(wx.BOTH)
        fgSizer71.SetNonFlexibleGrowMode(wx.FLEX_GROWMODE_SPECIFIED)
        self.predictionsTree = wx.TreeCtrl(sbSizer2.GetStaticBox(), wx.ID_ANY, wx.DefaultPosition, wx.DefaultSize, wx.TR_DEFAULT_STYLE | wx.TR_HIDE_ROOT | wx.TR_MULTIPLE)
        fgSizer71.Add(self.predictionsTree, 1, wx.EXPAND, 5)
        fgSizer91 = wx.FlexGridSizer(0, 3, 0, 0)
        fgSizer91.AddGrowableCol(0)
        fgSizer91.SetFlexibleDirection(wx.BOTH)
        fgSizer91.SetNonFlexibleGrowMode(wx.FLEX_GROWMODE_SPECIFIED)
        self.m_panel6 = wx.Panel(sbSizer2.GetStaticBox(), wx.ID_ANY, wx.DefaultPosition, wx.DefaultSize, wx.TAB_TRAVERSAL)
        fgSizer91.Add(self.m_panel6, 1, wx.EXPAND | wx.ALL, 5)
        self.m_button4 = wx.Button(sbSizer2.GetStaticBox(), wx.ID_ANY, u'&Delete', wx.DefaultPosition, wx.DefaultSize, 0)
        fgSizer91.Add(self.m_button4, 0, wx.ALL, 5)
        self.m_button5 = wx.Button(sbSizer2.GetStaticBox(), wx.ID_ANY, u'&Google Earth', wx.DefaultPosition, wx.DefaultSize, 0)
        fgSizer91.Add(self.m_button5, 0, wx.ALL, 5)
        fgSizer71.Add(fgSizer91, 1, wx.EXPAND, 5)
        sbSizer2.Add(fgSizer71, 1, wx.EXPAND, 5)
        fgSizer1.Add(sbSizer2, 1, wx.EXPAND | wx.RIGHT | wx.LEFT, 5)
        fgSizer3 = wx.FlexGridSizer(3, 1, 0, 0)
        fgSizer3.AddGrowableCol(0)
        fgSizer3.AddGrowableRow(0)
        fgSizer3.SetFlexibleDirection(wx.BOTH)
        fgSizer3.SetNonFlexibleGrowMode(wx.FLEX_GROWMODE_SPECIFIED)
        sbSizer4 = wx.StaticBoxSizer(wx.StaticBox(self.m_panel1, wx.ID_ANY, u'Details'), wx.VERTICAL)
        self.predictionDetailsTxt = wx.TextCtrl(sbSizer4.GetStaticBox(), wx.ID_ANY, wx.EmptyString, wx.DefaultPosition, wx.DefaultSize, wx.TE_AUTO_URL | wx.TE_MULTILINE | wx.TE_READONLY)
        self.predictionDetailsTxt.SetMaxLength(0)
        sbSizer4.Add(self.predictionDetailsTxt, 1, wx.EXPAND | wx.BOTTOM | wx.RIGHT | wx.LEFT, 5)
        fgSizer3.Add(sbSizer4, 1, wx.EXPAND, 5)
        self.m_checkBox2 = wx.CheckBox(self.m_panel1, wx.ID_ANY, u'Run scheduled batch tasks', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_checkBox2.Hide()
        fgSizer3.Add(self.m_checkBox2, 1, wx.ALL, 5)
        fgSizer7 = wx.FlexGridSizer(1, 2, 0, 0)
        fgSizer7.AddGrowableCol(1)
        fgSizer7.AddGrowableRow(0)
        fgSizer7.SetFlexibleDirection(wx.BOTH)
        fgSizer7.SetNonFlexibleGrowMode(wx.FLEX_GROWMODE_SPECIFIED)
        sbSizer51 = wx.StaticBoxSizer(wx.StaticBox(self.m_panel1, wx.ID_ANY, u'Choose a date'), wx.VERTICAL)
        self.manualDatepicker = wx.calendar.CalendarCtrl(sbSizer51.GetStaticBox(), wx.ID_ANY, wx.DefaultDateTime, wx.DefaultPosition, wx.DefaultSize, wx.calendar.CAL_NO_MONTH_CHANGE | wx.calendar.CAL_NO_YEAR_CHANGE | wx.calendar.CAL_SHOW_HOLIDAYS | wx.calendar.CAL_SHOW_SURROUNDING_WEEKS)
        sbSizer51.Add(self.manualDatepicker, 0, wx.ALL, 5)
        fgSizer7.Add(sbSizer51, 1, wx.EXPAND, 5)
        fgSizer9 = wx.FlexGridSizer(2, 1, 0, 0)
        fgSizer9.AddGrowableCol(0)
        fgSizer9.AddGrowableRow(0)
        fgSizer9.SetFlexibleDirection(wx.BOTH)
        fgSizer9.SetNonFlexibleGrowMode(wx.FLEX_GROWMODE_SPECIFIED)
        fgSizer6 = wx.FlexGridSizer(4, 2, 0, 0)
        fgSizer6.AddGrowableCol(1)
        fgSizer6.AddGrowableRow(3)
        fgSizer6.SetFlexibleDirection(wx.BOTH)
        fgSizer6.SetNonFlexibleGrowMode(wx.FLEX_GROWMODE_SPECIFIED)
        self.m_staticText3 = wx.StaticText(self.m_panel1, wx.ID_ANY, u'Time', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_staticText3.Wrap(-1)
        fgSizer6.Add(self.m_staticText3, 0, wx.ALL, 5)
        manualTimeBoxChoices = [
         23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 
         34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46]
        self.manualTimeBox = wx.ComboBox(self.m_panel1, wx.ID_ANY, u'0', wx.DefaultPosition, wx.DefaultSize, manualTimeBoxChoices, wx.CB_READONLY)
        fgSizer6.Add(self.manualTimeBox, 1, wx.ALL | wx.EXPAND, 5)
        self.m_staticText1 = wx.StaticText(self.m_panel1, wx.ID_ANY, u'Launch location', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_staticText1.Wrap(-1)
        fgSizer6.Add(self.m_staticText1, 0, wx.ALL, 5)
        manualLaunchLocChoices = []
        self.manualLaunchLoc = wx.ComboBox(self.m_panel1, wx.ID_ANY, wx.EmptyString, wx.DefaultPosition, wx.DefaultSize, manualLaunchLocChoices, wx.CB_READONLY)
        fgSizer6.Add(self.manualLaunchLoc, 1, wx.ALL | wx.EXPAND, 5)
        self.m_staticText2 = wx.StaticText(self.m_panel1, wx.ID_ANY, u'Your location', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_staticText2.Wrap(-1)
        fgSizer6.Add(self.m_staticText2, 0, wx.ALL, 5)
        manualYourLocChoices = []
        self.manualYourLoc = wx.ComboBox(self.m_panel1, wx.ID_ANY, wx.EmptyString, wx.DefaultPosition, wx.DefaultSize, manualYourLocChoices, wx.CB_READONLY)
        fgSizer6.Add(self.manualYourLoc, 1, wx.ALL | wx.EXPAND, 5)
        self.m_panel31 = wx.Panel(self.m_panel1, wx.ID_ANY, wx.DefaultPosition, wx.DefaultSize, wx.TAB_TRAVERSAL)
        fgSizer6.Add(self.m_panel31, 0, wx.EXPAND | wx.ALL, 5)
        self.m_panel4 = wx.Panel(self.m_panel1, wx.ID_ANY, wx.DefaultPosition, wx.DefaultSize, wx.TAB_TRAVERSAL)
        fgSizer6.Add(self.m_panel4, 1, wx.EXPAND | wx.ALL, 5)
        fgSizer9.Add(fgSizer6, 1, wx.EXPAND, 5)
        fgSizer10 = wx.FlexGridSizer(0, 3, 0, 0)
        fgSizer10.AddGrowableCol(0)
        fgSizer10.SetFlexibleDirection(wx.BOTH)
        fgSizer10.SetNonFlexibleGrowMode(wx.FLEX_GROWMODE_SPECIFIED)
        self.m_panel3 = wx.Panel(self.m_panel1, wx.ID_ANY, wx.DefaultPosition, wx.DefaultSize, wx.TAB_TRAVERSAL)
        fgSizer10.Add(self.m_panel3, 1, wx.EXPAND | wx.ALL, 5)
        self.m_button8 = wx.Button(self.m_panel1, wx.ID_ANY, u'Start a batch', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_button8.SetToolTipString(u'Manually start a task defined as a scheduled task')
        fgSizer10.Add(self.m_button8, 0, wx.ALL, 5)
        self.m_button1 = wx.Button(self.m_panel1, wx.ID_ANY, u'Start', wx.DefaultPosition, wx.DefaultSize, 0)
        fgSizer10.Add(self.m_button1, 0, wx.ALL, 5)
        fgSizer9.Add(fgSizer10, 1, wx.EXPAND, 5)
        fgSizer7.Add(fgSizer9, 1, wx.EXPAND, 5)
        fgSizer3.Add(fgSizer7, 1, wx.EXPAND, 5)
        fgSizer1.Add(fgSizer3, 1, wx.EXPAND, 5)
        self.m_panel1.SetSizer(fgSizer1)
        self.m_panel1.Layout()
        fgSizer1.Fit(self.m_panel1)
        bSizer1.Add(self.m_panel1, 1, wx.EXPAND, 5)
        self.SetSizer(bSizer1)
        self.Layout()
        bSizer1.Fit(self)
        self.m_statusBar1 = self.CreateStatusBar(4, wx.ST_SIZEGRIP, wx.ID_ANY)
        self.m_toolBar1 = self.CreateToolBar(wx.TB_HORIZONTAL, wx.ID_ANY)
        self.m_toolBar1.Realize()
        self.Centre(wx.BOTH)
        self.Bind(wx.EVT_CLOSE, self.shutdown)
        self.predictionsTree.Bind(wx.EVT_TREE_SEL_CHANGED, self.onTreeChange)
        self.m_button4.Bind(wx.EVT_BUTTON, self.onDelete)
        self.m_button5.Bind(wx.EVT_BUTTON, self.onGoogleEarth)
        self.m_checkBox2.Bind(wx.EVT_CHECKBOX, self.onEnableScheduler)
        self.manualTimeBox.Bind(wx.EVT_COMBOBOX, self.onManualRunSettingsChange)
        self.manualLaunchLoc.Bind(wx.EVT_COMBOBOX, self.onManualRunSettingsChange)
        self.manualYourLoc.Bind(wx.EVT_COMBOBOX, self.onManualRunSettingsChange)
        self.m_button8.Bind(wx.EVT_BUTTON, self.onStartManualScheduledTask)
        self.m_button1.Bind(wx.EVT_BUTTON, self.onManualRun)
        return

    def __del__(self):
        return

    def shutdown(self, event):
        event.Skip()
        return

    def onTreeChange(self, event):
        event.Skip()
        return

    def onDelete(self, event):
        event.Skip()
        return

    def onGoogleEarth(self, event):
        event.Skip()
        return

    def onEnableScheduler(self, event):
        event.Skip()
        return

    def onManualRunSettingsChange(self, event):
        event.Skip()
        return

    def onStartManualScheduledTask(self, event):
        event.Skip()
        return

    def onManualRun(self, event):
        event.Skip()
        return


class SettingsDialog(wx.Dialog):

    def __init__(self, parent):
        wx.Dialog.__init__(self, parent, id=wx.ID_ANY, title=u'Settings', pos=wx.DefaultPosition, size=wx.Size(559, 459), style=wx.DEFAULT_DIALOG_STYLE | wx.RESIZE_BORDER)
        self.SetSizeHintsSz(wx.Size(500, 400), wx.DefaultSize)
        fgSizer12 = wx.FlexGridSizer(2, 1, 0, 0)
        fgSizer12.AddGrowableCol(0)
        fgSizer12.AddGrowableRow(0)
        fgSizer12.SetFlexibleDirection(wx.BOTH)
        fgSizer12.SetNonFlexibleGrowMode(wx.FLEX_GROWMODE_SPECIFIED)
        self.m_notebook4 = wx.Notebook(self, wx.ID_ANY, wx.DefaultPosition, wx.Size(-1, -1), 0)
        self.m_panel8 = wx.Panel(self.m_notebook4, wx.ID_ANY, wx.DefaultPosition, wx.DefaultSize, wx.TAB_TRAVERSAL)
        fgSizer14 = wx.FlexGridSizer(0, 1, 0, 0)
        fgSizer14.AddGrowableCol(0)
        fgSizer14.AddGrowableRow(0)
        fgSizer14.AddGrowableRow(1)
        fgSizer14.SetFlexibleDirection(wx.BOTH)
        fgSizer14.SetNonFlexibleGrowMode(wx.FLEX_GROWMODE_SPECIFIED)
        sbSizer5 = wx.StaticBoxSizer(wx.StaticBox(self.m_panel8, wx.ID_ANY, u'3rd party applications'), wx.VERTICAL)
        self.m_staticText5 = wx.StaticText(sbSizer5.GetStaticBox(), wx.ID_ANY, u'GoogleEarth', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_staticText5.Wrap(-1)
        sbSizer5.Add(self.m_staticText5, 0, wx.TOP | wx.RIGHT | wx.LEFT, 5)
        self.m_filePicker1 = wx.FilePickerCtrl(sbSizer5.GetStaticBox(), wx.ID_ANY, wx.EmptyString, u'Select a file', u'googleearth.exe', wx.DefaultPosition, wx.DefaultSize, wx.FLP_DEFAULT_STYLE)
        sbSizer5.Add(self.m_filePicker1, 0, wx.ALL | wx.EXPAND, 5)
        self.m_staticline2 = wx.StaticLine(sbSizer5.GetStaticBox(), wx.ID_ANY, wx.DefaultPosition, wx.DefaultSize, wx.LI_HORIZONTAL)
        self.m_staticline2.Hide()
        sbSizer5.Add(self.m_staticline2, 0, wx.EXPAND | wx.ALL, 5)
        self.m_staticText25 = wx.StaticText(sbSizer5.GetStaticBox(), wx.ID_ANY, u'Internet browser', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_staticText25.Wrap(-1)
        self.m_staticText25.Enable(False)
        self.m_staticText25.Hide()
        sbSizer5.Add(self.m_staticText25, 0, wx.TOP | wx.RIGHT | wx.LEFT, 5)
        self.m_filePicker2 = wx.FilePickerCtrl(sbSizer5.GetStaticBox(), wx.ID_ANY, u'C:\Program Files\Mozilla Firefox\firefox.exe', u'Select a file', u'firefox.exe; iexplore.exe', wx.DefaultPosition, wx.DefaultSize, wx.FLP_DEFAULT_STYLE)
        self.m_filePicker2.Enable(False)
        self.m_filePicker2.Hide()
        sbSizer5.Add(self.m_filePicker2, 0, wx.ALL | wx.EXPAND, 5)
        fgSizer14.Add(sbSizer5, 0, wx.ALL | wx.EXPAND, 5)
        sbSizer6 = wx.StaticBoxSizer(wx.StaticBox(self.m_panel8, wx.ID_ANY, u'Network'), wx.VERTICAL)
        self.m_checkBox4 = wx.CheckBox(sbSizer6.GetStaticBox(), wx.ID_ANY, u'Use a netowrk proxy', wx.DefaultPosition, wx.DefaultSize, 0)
        sbSizer6.Add(self.m_checkBox4, 0, wx.ALL, 5)
        fgSizer211 = wx.FlexGridSizer(0, 1, 0, 0)
        fgSizer211.AddGrowableCol(0)
        fgSizer211.SetFlexibleDirection(wx.BOTH)
        fgSizer211.SetNonFlexibleGrowMode(wx.FLEX_GROWMODE_SPECIFIED)
        self.m_staticText22 = wx.StaticText(sbSizer6.GetStaticBox(), wx.ID_ANY, u'Address', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_staticText22.Wrap(-1)
        fgSizer211.Add(self.m_staticText22, 0, wx.TOP | wx.RIGHT | wx.LEFT, 5)
        self.m_textCtrl71 = wx.TextCtrl(sbSizer6.GetStaticBox(), wx.ID_ANY, u'http://proxy.example.com:8080/', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_textCtrl71.SetMaxLength(0)
        fgSizer211.Add(self.m_textCtrl71, 0, wx.ALL | wx.EXPAND, 5)
        sbSizer6.Add(fgSizer211, 1, wx.EXPAND, 5)
        fgSizer14.Add(sbSizer6, 1, wx.EXPAND, 5)
        self.m_checkBox2 = wx.CheckBox(self.m_panel8, wx.ID_ANY, u'Overwrite duplicate predictions', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_checkBox2.SetValue(True)
        self.m_checkBox2.Hide()
        fgSizer14.Add(self.m_checkBox2, 0, wx.ALL, 5)
        self.m_checkBox3 = wx.CheckBox(self.m_panel8, wx.ID_ANY, u'Use UTC times', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_checkBox3.SetValue(True)
        self.m_checkBox3.Hide()
        fgSizer14.Add(self.m_checkBox3, 0, wx.ALL, 5)
        self.m_panel8.SetSizer(fgSizer14)
        self.m_panel8.Layout()
        fgSizer14.Fit(self.m_panel8)
        self.m_notebook4.AddPage(self.m_panel8, u'General', True)
        self.m_panel9 = wx.Panel(self.m_notebook4, wx.ID_ANY, wx.DefaultPosition, wx.DefaultSize, wx.TAB_TRAVERSAL)
        fgSizer16 = wx.FlexGridSizer(0, 1, 0, 0)
        fgSizer16.AddGrowableCol(0)
        fgSizer16.AddGrowableRow(2)
        fgSizer16.SetFlexibleDirection(wx.BOTH)
        fgSizer16.SetNonFlexibleGrowMode(wx.FLEX_GROWMODE_SPECIFIED)
        fgSizer17 = wx.FlexGridSizer(0, 2, 0, 0)
        fgSizer17.AddGrowableCol(1)
        fgSizer17.SetFlexibleDirection(wx.BOTH)
        fgSizer17.SetNonFlexibleGrowMode(wx.FLEX_GROWMODE_SPECIFIED)
        self.m_staticText6 = wx.StaticText(self.m_panel9, wx.ID_ANY, u'Interval (h)', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_staticText6.Wrap(-1)
        self.m_staticText6.Hide()
        fgSizer17.Add(self.m_staticText6, 0, wx.ALL | wx.ALIGN_CENTER_VERTICAL, 5)
        self.m_spinCtrl1 = wx.SpinCtrl(self.m_panel9, wx.ID_ANY, wx.EmptyString, wx.DefaultPosition, wx.DefaultSize, wx.SP_ARROW_KEYS, 1, 999, 3)
        self.m_spinCtrl1.Hide()
        fgSizer17.Add(self.m_spinCtrl1, 0, wx.ALL | wx.EXPAND, 5)
        self.m_staticText10 = wx.StaticText(self.m_panel9, wx.ID_ANY, u'Launch location', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_staticText10.Wrap(-1)
        fgSizer17.Add(self.m_staticText10, 0, wx.ALL | wx.ALIGN_CENTER_VERTICAL, 5)
        m_comboBox5Choices = []
        self.m_comboBox5 = wx.ComboBox(self.m_panel9, wx.ID_ANY, u'Combo!', wx.DefaultPosition, wx.DefaultSize, m_comboBox5Choices, wx.CB_READONLY)
        fgSizer17.Add(self.m_comboBox5, 0, wx.ALL | wx.EXPAND, 5)
        self.m_staticText12 = wx.StaticText(self.m_panel9, wx.ID_ANY, u'Your location', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_staticText12.Wrap(-1)
        fgSizer17.Add(self.m_staticText12, 0, wx.ALL | wx.ALIGN_CENTER_VERTICAL, 5)
        m_comboBox6Choices = []
        self.m_comboBox6 = wx.ComboBox(self.m_panel9, wx.ID_ANY, u'Combo!', wx.DefaultPosition, wx.DefaultSize, m_comboBox6Choices, wx.CB_READONLY)
        fgSizer17.Add(self.m_comboBox6, 0, wx.ALL | wx.EXPAND, 5)
        self.m_panel13 = wx.Panel(self.m_panel9, wx.ID_ANY, wx.DefaultPosition, wx.DefaultSize, wx.TAB_TRAVERSAL)
        fgSizer17.Add(self.m_panel13, 1, wx.EXPAND | wx.ALL, 5)
        fgSizer171 = wx.FlexGridSizer(0, 4, 0, 0)
        fgSizer171.SetFlexibleDirection(wx.BOTH)
        fgSizer171.SetNonFlexibleGrowMode(wx.FLEX_GROWMODE_SPECIFIED)
        self.m_staticText14 = wx.StaticText(self.m_panel9, wx.ID_ANY, u'Run a batch 
for the following
days', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_staticText14.Wrap(-1)
        fgSizer171.Add(self.m_staticText14, 0, wx.ALL | wx.ALIGN_CENTER_VERTICAL, 5)
        m_checkList2Choices = [
         38, 39, 40, 41, 42, 43, 44, 45]
        self.m_checkList2 = wx.CheckListBox(self.m_panel9, wx.ID_ANY, wx.DefaultPosition, wx.Size(60, 60), m_checkList2Choices, 0)
        self.m_checkList2.SetMaxSize(wx.Size(20, 20))
        fgSizer171.Add(self.m_checkList2, 0, wx.ALL | wx.EXPAND, 5)
        self.m_staticText13 = wx.StaticText(self.m_panel9, wx.ID_ANY, u'Hours (UTC)', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_staticText13.Wrap(-1)
        fgSizer171.Add(self.m_staticText13, 0, wx.ALL | wx.ALIGN_CENTER_VERTICAL, 5)
        m_checkList1Choices = [
         38, 39, 40, 41, 42, 43, 44, 45, 49, 50, 51, 
         52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64]
        self.m_checkList1 = wx.CheckListBox(self.m_panel9, wx.ID_ANY, wx.DefaultPosition, wx.Size(60, 60), m_checkList1Choices, wx.LB_EXTENDED)
        fgSizer171.Add(self.m_checkList1, 0, wx.ALL | wx.EXPAND, 5)
        fgSizer17.Add(fgSizer171, 1, wx.EXPAND, 5)
        fgSizer16.Add(fgSizer17, 1, wx.EXPAND, 5)
        self.m_staticText141 = wx.StaticText(self.m_panel9, wx.ID_ANY, u'Current Schedule:', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_staticText141.Wrap(-1)
        fgSizer16.Add(self.m_staticText141, 0, wx.TOP | wx.RIGHT | wx.LEFT, 5)
        self.m_textCtrl7 = wx.TextCtrl(self.m_panel9, wx.ID_ANY, wx.EmptyString, wx.DefaultPosition, wx.DefaultSize, wx.TE_MULTILINE | wx.TE_READONLY)
        self.m_textCtrl7.SetMaxLength(0)
        fgSizer16.Add(self.m_textCtrl7, 1, wx.ALL | wx.EXPAND, 5)
        fgSizer21 = wx.FlexGridSizer(0, 2, 0, 0)
        fgSizer21.AddGrowableCol(0)
        fgSizer21.SetFlexibleDirection(wx.BOTH)
        fgSizer21.SetNonFlexibleGrowMode(wx.FLEX_GROWMODE_SPECIFIED)
        self.m_panel151 = wx.Panel(self.m_panel9, wx.ID_ANY, wx.DefaultPosition, wx.DefaultSize, wx.TAB_TRAVERSAL)
        fgSizer21.Add(self.m_panel151, 1, wx.EXPAND | wx.ALL, 5)
        self.m_button10 = wx.Button(self.m_panel9, wx.ID_ANY, u'Save', wx.DefaultPosition, wx.DefaultSize, 0)
        fgSizer21.Add(self.m_button10, 0, wx.ALL, 5)
        fgSizer16.Add(fgSizer21, 1, wx.EXPAND, 5)
        self.m_panel9.SetSizer(fgSizer16)
        self.m_panel9.Layout()
        fgSizer16.Fit(self.m_panel9)
        self.m_notebook4.AddPage(self.m_panel9, u'Batch', False)
        self.m_panel10 = wx.Panel(self.m_notebook4, wx.ID_ANY, wx.DefaultPosition, wx.DefaultSize, wx.TAB_TRAVERSAL)
        fgSizer141 = wx.FlexGridSizer(0, 2, 0, 0)
        fgSizer141.AddGrowableCol(1)
        fgSizer141.AddGrowableRow(0)
        fgSizer141.SetFlexibleDirection(wx.BOTH)
        fgSizer141.SetNonFlexibleGrowMode(wx.FLEX_GROWMODE_SPECIFIED)
        locBoxChoices = []
        self.locBox = wx.ListBox(self.m_panel10, wx.ID_ANY, wx.DefaultPosition, wx.DefaultSize, locBoxChoices, 0)
        fgSizer141.Add(self.locBox, 0, wx.ALL | wx.EXPAND, 5)
        fgSizer15 = wx.FlexGridSizer(0, 2, 0, 0)
        fgSizer15.AddGrowableCol(1)
        fgSizer15.SetFlexibleDirection(wx.BOTH)
        fgSizer15.SetNonFlexibleGrowMode(wx.FLEX_GROWMODE_SPECIFIED)
        self.m_panel17 = wx.Panel(self.m_panel10, wx.ID_ANY, wx.DefaultPosition, wx.DefaultSize, wx.TAB_TRAVERSAL)
        fgSizer15.Add(self.m_panel17, 1, wx.EXPAND | wx.ALL, 5)
        self.m_staticText21 = wx.StaticText(self.m_panel10, wx.ID_ANY, wx.EmptyString, wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_staticText21.Wrap(-1)
        fgSizer15.Add(self.m_staticText21, 0, wx.ALL, 5)
        self.m_staticText61 = wx.StaticText(self.m_panel10, wx.ID_ANY, u'Name', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_staticText61.Wrap(-1)
        fgSizer15.Add(self.m_staticText61, 0, wx.ALL | wx.ALIGN_CENTER_VERTICAL, 5)
        self.m_textCtrl2 = wx.TextCtrl(self.m_panel10, wx.ID_ANY, wx.EmptyString, wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_textCtrl2.SetMaxLength(0)
        fgSizer15.Add(self.m_textCtrl2, 0, wx.ALL | wx.EXPAND, 5)
        self.m_staticText7 = wx.StaticText(self.m_panel10, wx.ID_ANY, u'Latitude', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_staticText7.Wrap(-1)
        fgSizer15.Add(self.m_staticText7, 0, wx.ALL | wx.ALIGN_CENTER_VERTICAL, 5)
        self.m_textCtrl3 = wx.TextCtrl(self.m_panel10, wx.ID_ANY, wx.EmptyString, wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_textCtrl3.SetMaxLength(0)
        fgSizer15.Add(self.m_textCtrl3, 0, wx.ALL | wx.EXPAND, 5)
        self.m_staticText8 = wx.StaticText(self.m_panel10, wx.ID_ANY, u'Longitude', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_staticText8.Wrap(-1)
        fgSizer15.Add(self.m_staticText8, 0, wx.ALL | wx.ALIGN_CENTER_VERTICAL, 5)
        self.m_textCtrl4 = wx.TextCtrl(self.m_panel10, wx.ID_ANY, wx.EmptyString, wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_textCtrl4.SetMaxLength(0)
        fgSizer15.Add(self.m_textCtrl4, 0, wx.ALL | wx.EXPAND, 5)
        self.m_staticText9 = wx.StaticText(self.m_panel10, wx.ID_ANY, u'Altitude (m)', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_staticText9.Wrap(-1)
        fgSizer15.Add(self.m_staticText9, 0, wx.ALL | wx.ALIGN_CENTER_VERTICAL, 5)
        self.m_textCtrl5 = wx.TextCtrl(self.m_panel10, wx.ID_ANY, wx.EmptyString, wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_textCtrl5.SetMaxLength(0)
        fgSizer15.Add(self.m_textCtrl5, 0, wx.ALL | wx.EXPAND, 5)
        self.m_staticText24 = wx.StaticText(self.m_panel10, wx.ID_ANY, u'Warning (km)', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_staticText24.Wrap(-1)
        fgSizer15.Add(self.m_staticText24, 0, wx.ALL, 5)
        self.warning_spin = wx.SpinCtrl(self.m_panel10, wx.ID_ANY, wx.EmptyString, wx.DefaultPosition, wx.DefaultSize, wx.SP_ARROW_KEYS, 0, 1000, 30)
        fgSizer15.Add(self.warning_spin, 1, wx.ALL | wx.EXPAND, 5)
        self.m_panel24 = wx.Panel(self.m_panel10, wx.ID_ANY, wx.DefaultPosition, wx.DefaultSize, wx.TAB_TRAVERSAL)
        fgSizer15.Add(self.m_panel24, 1, wx.EXPAND | wx.ALL, 5)
        fgSizer19 = wx.FlexGridSizer(0, 3, 0, 0)
        fgSizer19.AddGrowableCol(0)
        fgSizer19.SetFlexibleDirection(wx.BOTH)
        fgSizer19.SetNonFlexibleGrowMode(wx.FLEX_GROWMODE_SPECIFIED)
        self.m_panel16 = wx.Panel(self.m_panel10, wx.ID_ANY, wx.DefaultPosition, wx.DefaultSize, wx.TAB_TRAVERSAL)
        fgSizer19.Add(self.m_panel16, 1, wx.EXPAND | wx.ALL, 5)
        self.m_button9 = wx.Button(self.m_panel10, wx.ID_ANY, u'Add', wx.DefaultPosition, wx.DefaultSize, 0)
        fgSizer19.Add(self.m_button9, 0, wx.ALL, 5)
        self.m_button11 = wx.Button(self.m_panel10, wx.ID_ANY, u'Remove', wx.DefaultPosition, wx.DefaultSize, 0)
        fgSizer19.Add(self.m_button11, 0, wx.ALL, 5)
        fgSizer15.Add(fgSizer19, 1, wx.EXPAND, 5)
        fgSizer141.Add(fgSizer15, 1, wx.EXPAND, 5)
        self.m_panel10.SetSizer(fgSizer141)
        self.m_panel10.Layout()
        fgSizer141.Fit(self.m_panel10)
        self.m_notebook4.AddPage(self.m_panel10, u'Locations', False)
        self.m_panel101 = wx.Panel(self.m_notebook4, wx.ID_ANY, wx.DefaultPosition, wx.DefaultSize, wx.TAB_TRAVERSAL)
        fgSizer1411 = wx.FlexGridSizer(0, 3, 0, 0)
        fgSizer1411.AddGrowableCol(0)
        fgSizer1411.SetFlexibleDirection(wx.BOTH)
        fgSizer1411.SetNonFlexibleGrowMode(wx.FLEX_GROWMODE_SPECIFIED)
        self.asc_rate_slider = wx.Slider(self.m_panel101, wx.ID_ANY, 50, 10, 1000, wx.DefaultPosition, wx.DefaultSize, wx.SL_BOTH | wx.SL_HORIZONTAL)
        fgSizer1411.Add(self.asc_rate_slider, 1, wx.ALL | wx.EXPAND, 5)
        self.asc_rate_label = wx.StaticText(self.m_panel101, wx.ID_ANY, wx.EmptyString, wx.DefaultPosition, wx.DefaultSize, 0)
        self.asc_rate_label.Wrap(-1)
        self.asc_rate_label.SetMinSize(wx.Size(100, -1))
        fgSizer1411.Add(self.asc_rate_label, 1, wx.ALL | wx.EXPAND, 5)
        self.m_staticText2511 = wx.StaticText(self.m_panel101, wx.ID_ANY, u'Ascent rate (m/s)', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_staticText2511.Wrap(-1)
        fgSizer1411.Add(self.m_staticText2511, 0, wx.ALL, 5)
        self.desc_rate_slider = wx.Slider(self.m_panel101, wx.ID_ANY, 400, 10, 1000, wx.DefaultPosition, wx.DefaultSize, wx.SL_BOTH | wx.SL_HORIZONTAL)
        fgSizer1411.Add(self.desc_rate_slider, 1, wx.ALL | wx.EXPAND, 5)
        self.desc_rate_label = wx.StaticText(self.m_panel101, wx.ID_ANY, wx.EmptyString, wx.DefaultPosition, wx.DefaultSize, 0)
        self.desc_rate_label.Wrap(-1)
        self.desc_rate_label.SetMinSize(wx.Size(100, -1))
        fgSizer1411.Add(self.desc_rate_label, 1, wx.ALL | wx.EXPAND, 5)
        self.m_staticText261 = wx.StaticText(self.m_panel101, wx.ID_ANY, u'Descent rate (m/s)', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_staticText261.Wrap(-1)
        fgSizer1411.Add(self.m_staticText261, 0, wx.ALL, 5)
        self.burst_slider = wx.Slider(self.m_panel101, wx.ID_ANY, 24000, 200, 35000, wx.DefaultPosition, wx.DefaultSize, wx.SL_BOTH | wx.SL_HORIZONTAL)
        fgSizer1411.Add(self.burst_slider, 1, wx.ALL | wx.EXPAND, 5)
        self.burst_label = wx.StaticText(self.m_panel101, wx.ID_ANY, wx.EmptyString, wx.DefaultPosition, wx.DefaultSize, 0)
        self.burst_label.Wrap(-1)
        self.burst_label.SetMinSize(wx.Size(100, -1))
        fgSizer1411.Add(self.burst_label, 1, wx.ALL | wx.EXPAND, 5)
        self.m_staticText271 = wx.StaticText(self.m_panel101, wx.ID_ANY, u'Burst altitude (m)', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_staticText271.Wrap(-1)
        fgSizer1411.Add(self.m_staticText271, 0, wx.ALL, 5)
        self.gain_slider = wx.Slider(self.m_panel101, wx.ID_ANY, 100, 50, 150, wx.DefaultPosition, wx.DefaultSize, wx.SL_BOTH | wx.SL_HORIZONTAL)
        fgSizer1411.Add(self.gain_slider, 1, wx.ALL | wx.EXPAND, 5)
        self.gain_label = wx.StaticText(self.m_panel101, wx.ID_ANY, wx.EmptyString, wx.DefaultPosition, wx.DefaultSize, 0)
        self.gain_label.Wrap(-1)
        self.gain_label.SetMinSize(wx.Size(100, -1))
        fgSizer1411.Add(self.gain_label, 1, wx.ALL | wx.EXPAND, 5)
        self.m_staticText2711 = wx.StaticText(self.m_panel101, wx.ID_ANY, u'Gain', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_staticText2711.Wrap(-1)
        fgSizer1411.Add(self.m_staticText2711, 0, wx.ALL, 5)
        self.m_staticline21 = wx.StaticLine(self.m_panel101, wx.ID_ANY, wx.DefaultPosition, wx.DefaultSize, wx.LI_HORIZONTAL)
        fgSizer1411.Add(self.m_staticline21, 0, wx.EXPAND | wx.ALL, 5)
        self.m_panel171 = wx.Panel(self.m_panel101, wx.ID_ANY, wx.DefaultPosition, wx.DefaultSize, wx.TAB_TRAVERSAL)
        fgSizer1411.Add(self.m_panel171, 1, wx.EXPAND | wx.ALL, 5)
        self.m_panel18 = wx.Panel(self.m_panel101, wx.ID_ANY, wx.DefaultPosition, wx.DefaultSize, wx.TAB_TRAVERSAL)
        fgSizer1411.Add(self.m_panel18, 1, wx.EXPAND | wx.ALL, 5)
        fgSizer212 = wx.FlexGridSizer(0, 2, 0, 0)
        fgSizer212.SetFlexibleDirection(wx.BOTH)
        fgSizer212.SetNonFlexibleGrowMode(wx.FLEX_GROWMODE_SPECIFIED)
        self.m_staticText27 = wx.StaticText(self.m_panel101, wx.ID_ANY, u'Grid size', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_staticText27.Wrap(-1)
        fgSizer212.Add(self.m_staticText27, 0, wx.ALL, 5)
        grid_sizeChoices = [
         u'0p25', u'0p50', u'1p00']
        self.grid_size = wx.Choice(self.m_panel101, wx.ID_ANY, wx.DefaultPosition, wx.DefaultSize, grid_sizeChoices, 0)
        self.grid_size.SetSelection(1)
        fgSizer212.Add(self.grid_size, 0, wx.ALL, 5)
        fgSizer1411.Add(fgSizer212, 1, wx.EXPAND, 5)
        self.m_panel101.SetSizer(fgSizer1411)
        self.m_panel101.Layout()
        fgSizer1411.Fit(self.m_panel101)
        self.m_notebook4.AddPage(self.m_panel101, u'Tweaks', False)
        fgSizer12.Add(self.m_notebook4, 1, wx.EXPAND | wx.ALL, 5)
        fgSizer13 = wx.FlexGridSizer(0, 0, 0, 0)
        fgSizer13.AddGrowableCol(0)
        fgSizer13.SetFlexibleDirection(wx.BOTH)
        fgSizer13.SetNonFlexibleGrowMode(wx.FLEX_GROWMODE_SPECIFIED)
        self.m_panel7 = wx.Panel(self, wx.ID_ANY, wx.DefaultPosition, wx.DefaultSize, wx.TAB_TRAVERSAL)
        fgSizer13.Add(self.m_panel7, 1, wx.EXPAND | wx.ALL, 5)
        self.m_button101 = wx.Button(self, wx.ID_ANY, u'Apply', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_button101.SetToolTipString(u'Apply settings')
        fgSizer13.Add(self.m_button101, 0, wx.ALL, 5)
        self.m_button111 = wx.Button(self, wx.ID_ANY, u'Cancel', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_button111.SetToolTipString(u'Close and do not save settings')
        fgSizer13.Add(self.m_button111, 0, wx.ALL, 5)
        self.m_button6 = wx.Button(self, wx.ID_ANY, u'Close', wx.DefaultPosition, wx.DefaultSize, 0)
        self.m_button6.SetToolTipString(u'Save and close')
        fgSizer13.Add(self.m_button6, 0, wx.ALL, 5)
        fgSizer12.Add(fgSizer13, 1, wx.EXPAND, 5)
        self.SetSizer(fgSizer12)
        self.Layout()
        self.Centre(wx.BOTH)
        self.Bind(wx.EVT_CLOSE, self.onCancel)
        self.m_filePicker1.Bind(wx.EVT_FILEPICKER_CHANGED, self.onGoogleEarthFileChange)
        self.m_checkBox2.Bind(wx.EVT_CHECKBOX, self.onOverWriteDupes)
        self.m_checkBox3.Bind(wx.EVT_CHECKBOX, self.onUseUtcTimes)
        self.m_button10.Bind(wx.EVT_BUTTON, self.onScheduleSave)
        self.locBox.Bind(wx.EVT_LISTBOX, self.onLocListClick)
        self.m_textCtrl2.Bind(wx.EVT_TEXT, self.onLocInput)
        self.m_textCtrl3.Bind(wx.EVT_TEXT, self.onLocInput)
        self.m_textCtrl4.Bind(wx.EVT_TEXT, self.onLocInput)
        self.m_textCtrl5.Bind(wx.EVT_TEXT, self.onLocInput)
        self.m_button9.Bind(wx.EVT_BUTTON, self.onLocSave)
        self.m_button11.Bind(wx.EVT_BUTTON, self.onLocRemove)
        self.asc_rate_slider.Bind(wx.EVT_SCROLL, self.onAscRateScroll)
        self.desc_rate_slider.Bind(wx.EVT_SCROLL, self.onDescRateScroll)
        self.burst_slider.Bind(wx.EVT_SCROLL, self.onBurstScroll)
        self.gain_slider.Bind(wx.EVT_SCROLL, self.onGainScroll)
        self.grid_size.Bind(wx.EVT_CHOICE, self.onGridSet)
        self.m_button101.Bind(wx.EVT_BUTTON, self.onApply)
        self.m_button111.Bind(wx.EVT_BUTTON, self.onCancel)
        self.m_button6.Bind(wx.EVT_BUTTON, self.onClose)
        return

    def __del__(self):
        return

    def onCancel(self, event):
        event.Skip()
        return

    def onGoogleEarthFileChange(self, event):
        event.Skip()
        return

    def onOverWriteDupes(self, event):
        event.Skip()
        return

    def onUseUtcTimes(self, event):
        event.Skip()
        return

    def onScheduleSave(self, event):
        event.Skip()
        return

    def onLocListClick(self, event):
        event.Skip()
        return

    def onLocInput(self, event):
        event.Skip()
        return

    def onLocSave(self, event):
        event.Skip()
        return

    def onLocRemove(self, event):
        event.Skip()
        return

    def onAscRateScroll(self, event):
        event.Skip()
        return

    def onDescRateScroll(self, event):
        event.Skip()
        return

    def onBurstScroll(self, event):
        event.Skip()
        return

    def onGainScroll(self, event):
        event.Skip()
        return

    def onGridSet(self, event):
        event.Skip()
        return

    def onApply(self, event):
        event.Skip()
        return

    def onClose(self, event):
        event.Skip()
        return
