// OpenStreetMap view of predicted tracks. Data comes from the C++ object "trackModel".
import QtQuick
import QtLocation
import QtPositioning

Item {
    id: root

    readonly property var markerColors: ({
        launch: "#2e7d32", burst: "#ef6c00", landing: "#c62828", home: "#1565c0", site: "#2e7d32"
    })

    Plugin {
        id: osm
        name: "osm"
        PluginParameter { name: "osm.useragent"; value: trackModel.userAgent }
        // Use the OSM tile server directly instead of Qt's provider redirect service.
        PluginParameter { name: "osm.mapping.providersrepository.disabled"; value: true }
        PluginParameter { name: "osm.mapping.custom.host"; value: "https://tile.openstreetmap.org/" }
        PluginParameter { name: "osm.mapping.copyright"; value: "© OpenStreetMap contributors" }
        PluginParameter { name: "osm.mapping.cache.directory"; value: trackModel.cacheDirectory }
    }

    Map {
        id: map
        anchors.fill: parent
        plugin: osm
        center: QtPositioning.coordinate(60.8, 23.5)
        zoomLevel: 7
        copyrightsVisible: true

        Component.onCompleted: {
            for (let i = 0; i < supportedMapTypes.length; ++i) {
                if (supportedMapTypes[i].style === MapType.CustomMap) {
                    activeMapType = supportedMapTypes[i];
                    break;
                }
            }
        }

        property geoCoordinate startCentroid

        PinchHandler {
            id: pinch
            target: null
            grabPermissions: PointerHandler.TakeOverForbidden
            onActiveChanged: if (active) map.startCentroid = map.toCoordinate(pinch.centroid.position, false)
            onScaleChanged: (delta) => {
                map.zoomLevel += Math.log2(delta);
                map.alignCoordinateToPoint(map.startCentroid, pinch.centroid.position);
            }
        }
        WheelHandler {
            acceptedDevices: Qt.platform.pluginName === "cocoa" || Qt.platform.pluginName === "wayland"
                             ? PointerDevice.Mouse | PointerDevice.TouchPad : PointerDevice.Mouse
            rotationScale: 1 / 120
            property: "zoomLevel"
        }
        DragHandler {
            target: null
            onTranslationChanged: (delta) => map.pan(-delta.x, -delta.y)
        }

        MapItemView {
            model: trackModel.areas
            delegate: MapPolygon {
                required property var modelData
                path: modelData.path
                // Amber, so the area stands apart from the red descent line.
                color: modelData.current ? "#55ffb300" : "#30ffb300"
                border.color: "#ff8f00"
                border.width: modelData.current ? 2 : 1
            }
        }

        MapItemView {
            model: trackModel.lines
            delegate: MapPolyline {
                required property var modelData
                path: modelData.path
                line.width: modelData.current ? 4 : 2
                line.color: modelData.color
                opacity: modelData.current ? 1.0 : 0.6
            }
        }

        MapItemView {
            model: trackModel.markers
            delegate: MapQuickItem {
                required property var modelData
                coordinate: modelData.coordinate
                anchorPoint.x: dot.width / 2
                anchorPoint.y: dot.height / 2
                z: modelData.current ? 2 : 1
                sourceItem: Item {
                    width: dot.width
                    height: dot.height
                    Rectangle {
                        id: dot
                        width: modelData.kind === "landing" ? 16 : 12
                        height: width
                        radius: modelData.kind === "home" ? 2 : width / 2
                        color: root.markerColors[modelData.kind] || "black"
                        border.color: "white"
                        border.width: 2
                        opacity: modelData.current ? 1.0 : 0.7
                    }
                    Text {
                        anchors.left: dot.right
                        anchors.leftMargin: 4
                        anchors.verticalCenter: dot.verticalCenter
                        text: modelData.label
                        visible: text.length > 0
                        font.pixelSize: 12
                        font.bold: modelData.kind === "landing"
                        color: "#202020"
                        style: Text.Outline
                        styleColor: "white"
                    }
                }
            }
        }
    }

    // Zoom to the latest requested area once the map can do it (it ignores requests made
    // before it has a size), but don't fight the user's panning on later resizes.
    property var pendingFit: null
    function applyFit() {
        if (pendingFit && map.mapReady && map.width > 0 && map.height > 0) {
            map.fitViewportToGeoShape(pendingFit, 60);
            pendingFit = null;
        }
    }
    Connections {
        target: trackModel
        function onFitRequested(shape) { root.pendingFit = shape; root.applyFit(); }
    }
    Connections {
        target: map
        function onMapReadyChanged() { root.applyFit() }
        function onWidthChanged() { Qt.callLater(root.applyFit) }
        function onHeightChanged() { Qt.callLater(root.applyFit) }
    }
}
