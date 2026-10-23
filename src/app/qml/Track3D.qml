// 3D view of predicted tracks over an OpenStreetMap ground image. Data: the C++ object "scene3d".
// Scene units are 100 m; x east, y up, z south. Heights are shown at true scale.
import QtQuick
import QtQuick.Controls
import QtQuick3D
import QtQuick3D.Helpers
import SondePredict

Item {
    id: root
    readonly property real exaggeration: 1 // kept as a knob for the geometry; true scale
    readonly property real r: scene3d.sceneRadius
    readonly property var markerColors: ({
        launch: "#2e7d32", burst: "#ef6c00", landing: "#c62828", home: "#1565c0"
    })

    // Overview from the side of the flight, 25° above the horizon, framing the ground and the
    // full (exaggerated) height even in a short, wide view.
    function resetView() {
        const height = scene3d.maxAltitude * exaggeration;
        orbitOrigin.position = Qt.vector3d(0, height * 0.4, 0);
        orbitOrigin.eulerRotation = Qt.vector3d(-25, scene3d.viewYaw, 0);
        camera.position = Qt.vector3d(0, 0, Math.max(scene3d.groundSize.width, scene3d.groundSize.height, height * 1.6, 20) * 1.25);
        camera.eulerRotation = Qt.vector3d(0, 0, 0);
    }

    Connections {
        target: scene3d
        function onViewReset() { root.resetView() }
    }

    View3D {
        id: view
        anchors.fill: parent
        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Color
            clearColor: "#c9e2f5"
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        Node {
            id: orbitOrigin
            PerspectiveCamera {
                id: camera
                clipNear: 0.5
                clipFar: 200000
                fieldOfView: 45
            }
        }

        DirectionalLight {
            eulerRotation: Qt.vector3d(-55, -35, 0)
            brightness: 1.1
            ambientColor: Qt.rgba(0.45, 0.45, 0.45, 1)
        }

        // Ground: the map image on a plane covering the flight area.
        Model {
            source: "#Rectangle"
            eulerRotation.x: -90
            scale: Qt.vector3d(scene3d.groundSize.width / 100, scene3d.groundSize.height / 100, 1)
            visible: scene3d.hasData
            materials: DefaultMaterial {
                lighting: DefaultMaterial.NoLighting
                diffuseColor: scene3d.groundImage.toString().length > 0 ? "white" : "#dfe6d3" // until the tiles arrive
                diffuseMap: Texture {
                    source: scene3d.groundImage
                    generateMipmaps: true
                    mipFilter: Texture.Linear
                }
            }
        }

        Repeater3D {
            model: scene3d.areas
            delegate: Model {
                required property var modelData
                position.y: 0.3
                opacity: modelData.current ? 0.55 : 0.3
                geometry: AreaGeometry { points: modelData.outline }
                materials: DefaultMaterial {
                    lighting: DefaultMaterial.NoLighting
                    diffuseColor: "#ffb300"
                    cullMode: Material.NoCulling
                }
            }
        }

        Repeater3D {
            model: scene3d.tracks
            delegate: Node {
                id: track
                required property var modelData
                readonly property real tubeRadius: Math.max(root.r / (modelData.current ? 350 : 600), 0.3)

                Model {
                    geometry: TubeGeometry { points: track.modelData.ascent; radius: track.tubeRadius; verticalScale: root.exaggeration }
                    materials: PrincipledMaterial { baseColor: "#1e88e5"; roughness: 0.7 }
                }
                Model {
                    geometry: TubeGeometry { points: track.modelData.descent; radius: track.tubeRadius; verticalScale: root.exaggeration }
                    materials: PrincipledMaterial { baseColor: "#e53935"; roughness: 0.7 }
                }
                // See-through curtains down to the ground show the shape of the flight.
                Model {
                    opacity: track.modelData.current ? 0.22 : 0.1
                    geometry: CurtainGeometry { points: track.modelData.ascent; verticalScale: root.exaggeration }
                    materials: DefaultMaterial { lighting: DefaultMaterial.NoLighting; diffuseColor: "#1e88e5"; cullMode: Material.NoCulling }
                }
                Model {
                    opacity: track.modelData.current ? 0.22 : 0.1
                    geometry: CurtainGeometry { points: track.modelData.descent; verticalScale: root.exaggeration }
                    materials: DefaultMaterial { lighting: DefaultMaterial.NoLighting; diffuseColor: "#e53935"; cullMode: Material.NoCulling }
                }
            }
        }

        Repeater3D {
            model: scene3d.markers
            delegate: Model {
                required property var modelData
                readonly property real size: Math.max(root.r / (modelData.current ? 45 : 70), 1.5)
                source: modelData.kind === "home" ? "#Cube" : "#Sphere"
                position: Qt.vector3d(modelData.position.x, modelData.position.y * root.exaggeration + size / 2, modelData.position.z)
                scale: Qt.vector3d(size / 100, size / 100, size / 100)
                materials: PrincipledMaterial { baseColor: root.markerColors[modelData.kind] || "black"; roughness: 0.5 }
            }
        }
    }

    OrbitCameraController {
        anchors.fill: parent
        origin: orbitOrigin
        camera: camera
        panEnabled: true
    }

    // Legend and controls.
    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 8
        width: controls.implicitWidth + 16
        height: controls.implicitHeight + 12
        radius: 4
        color: "#d9ffffff"
        visible: scene3d.hasData

        Column {
            id: controls
            anchors.centerIn: parent
            spacing: 4
            Row {
                spacing: 10
                Repeater {
                    model: [["#1e88e5", qsTr("Ascent")], ["#e53935", qsTr("Descent")], ["#ffb300", qsTr("Landing area")],
                            ["#ef6c00", qsTr("Burst")], ["#c62828", qsTr("Landing")], ["#1565c0", qsTr("Your location")]]
                    delegate: Row {
                        required property var modelData
                        spacing: 4
                        Rectangle { width: 10; height: 10; radius: 2; color: modelData[0]; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: modelData[1]; font.pixelSize: 11; color: "#202020" }
                    }
                }
            }
            Row {
                spacing: 8
                Button { text: qsTr("Reset view"); onClicked: root.resetView() }
                Text {
                    text: qsTr("Drag to turn, scroll to zoom, right-drag to move")
                    font.pixelSize: 10
                    color: "#505050"
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: !scene3d.hasData
        text: qsTr("Select a prediction to see it in 3D.")
        color: "#404040"
        font.pixelSize: 14
    }

    Text {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 4
        text: "© OpenStreetMap contributors"
        font.pixelSize: 10
        color: "#303030"
        visible: scene3d.hasData
    }

    Component.onCompleted: resetView()
}
