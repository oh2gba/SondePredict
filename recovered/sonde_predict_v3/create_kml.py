# uncompyle6 version 3.9.3
# Python bytecode version base 2.7 (62211)
# Decompiled from: Python 3.13.5 (main, Aug 10 2026, 12:06:59) [GCC 14.2.0]
# Embedded file name: sonde_predict_v3\create_kml.py
import simplekml, os, settings, helpers, threading, wx, subprocess, logging
logger = logging.getLogger('logger.create_kml')
logger.setLevel(logging.DEBUG)

class CreateKml(threading.Thread):

    def __init__(self, tracks, parent, geFile):
        threading.Thread.__init__(self)
        self.tracks = tracks
        self.parent = parent
        self.geFile = geFile
        self.start()
        return

    def run(self):
        helper = helpers.Helper()
        locations = {}
        kml = simplekml.Kml(name='Sonde Predict')
        doc = kml.newdocument(name='Tracks')
        schema = kml.newschema()
        schema.newgxsimplearrayfield(name='t', type=simplekml.Types.string, displayname='Timestamp')
        schema.newgxsimplearrayfield(name='fcT', type=simplekml.Types.string, displayname='Forecast created')
        schema.newgxsimplearrayfield(name='validT', type=simplekml.Types.string, displayname='Fc valid for')
        schema.newgxsimplearrayfield(name='velocity', type=simplekml.Types.string, displayname='Speed')
        schema.newgxsimplearrayfield(name='heading', type=simplekml.Types.string, displayname='Heading')
        schema.newgxsimplearrayfield(name='level', type=simplekml.Types.string, displayname='Level')
        schema.newgxsimplearrayfield(name='altitude', type=simplekml.Types.string, displayname='Altitude')
        for track in self.tracks:
            trackName = helper.epochToReadableShort(track.forecastFor) + ' ' + track.siteLoc + '(' + helper.epochToHour(track.forecastFor) + 'h)'
            trk = doc.newgxtrack(name=trackName)
            trk.extendeddata.schemadata.schemaurl = schema.id
            trk.newgxcoord([(wp.lon, wp.lat, wp.altitude) for wp in track.wayPoints])
            trk.extendeddata.schemadata.newgxsimplearraydata('t', [helper.epochToReadableLong(wp.t) for wp in track.wayPoints])
            trk.extendeddata.schemadata.newgxsimplearraydata('fcT', [helper.epochToReadableLong(wp.fcT) for wp in track.wayPoints])
            trk.extendeddata.schemadata.newgxsimplearraydata('validT', [wp.validT for wp in track.wayPoints])
            trk.extendeddata.schemadata.newgxsimplearraydata('velocity', [round(wp.velocity, 1) for wp in track.wayPoints])
            trk.extendeddata.schemadata.newgxsimplearraydata('heading', [round(wp.heading, 1) for wp in track.wayPoints])
            trk.extendeddata.schemadata.newgxsimplearraydata('level', [wp.level for wp in track.wayPoints])
            trk.extendeddata.schemadata.newgxsimplearraydata('altitude', [wp.altitude for wp in track.wayPoints])
            trk.altitudemode = simplekml.AltitudeMode.relativetoground
            trk.stylemap.normalstyle.linestyle.color = '99ffac59'
            trk.stylemap.normalstyle.linestyle.width = 6
            trk.stylemap.highlightstyle.linestyle.color = '99ffac59'
            trk.stylemap.highlightstyle.linestyle.width = 8
            if track.urLoc not in locations:
                locations[track.urLoc] = (
                 track.urLon, track.urLat)
            if track.siteLoc not in locations:
                locations[track.siteLoc] = (
                 track.siteLon, track.siteLat)

        for location in locations:
            kml.newpoint(name=location, coords=[locations[location]])

        fileName = os.path.join(settings.kmlDir, 'latest.kml')
        try:
            try:
                kml.save(fileName)
                self.startGoogleEarth(fileName)
            except Exception as e:
                wx.CallAfter(self.parent.showErrorMessage, 'Error in generating the kml file: \n%s' % str(e), fatal=False)

        finally:
            wx.CallAfter(self.parent.enableGeButton, True)

        return

    def startGoogleEarth(self, kmlFile):
        if os.path.isfile(self.geFile):
            if os.path.isfile(kmlFile):
                try:
                    subprocess.Popen([self.geFile, kmlFile])
                except Exception as e:
                    logger.error('Error opening GoogleEarth: %s' % str(e))
                    wx.CallAfter(self.parent.showErrorMessage, 'Error opening Google Earth: \n%s' % str(e), fatal=False)

            else:
                logger.error('This file does not exist: %s' % kmlFile)
                wx.CallAfter(self.parent.showErrorMessage, 'This file does not exist: %s' % kmlFile, fatal=False)
        else:
            logger.error('This file does not exist: %s' % self.geFile)
            wx.CallAfter(self.parent.showErrorMessage, 'This file does not exist: %s' % self.geFile, fatal=False)
        return
