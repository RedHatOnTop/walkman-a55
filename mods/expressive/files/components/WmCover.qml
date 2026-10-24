import QtQuick 2.3

// Cover art with rounded corners, cut in a shader so the cover can sit on
// any backdrop. Same image://coverart/ request as components/CoverArt.qml.
Item {
    id: base

    property string contents_id: ""
    property var update_db_counter
    property real radius: 40
    property color placeholderColor: "#2b2930"
    property color placeholderInkColor: "#cac4d0"

    readonly property bool ready: art.status === Image.Ready

    Rectangle {
        anchors.fill: parent
        radius: base.radius
        antialiasing: true
        color: base.placeholderColor
        opacity: base.ready ? 0 : 1
        Behavior on opacity { NumberAnimation { duration: 200 } }

        // A disc, for "no art yet".
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.42
            height: width
            radius: width / 2
            antialiasing: true
            color: "transparent"
            border.width: width * 0.14
            border.color: base.placeholderInkColor
            opacity: 0.55
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 0.16
                height: width
                radius: width / 2
                antialiasing: true
                color: base.placeholderInkColor
            }
        }
    }

    Image {
        id: art
        anchors.fill: parent
        visible: false
        asynchronous: true
        fillMode: Image.PreserveAspectCrop
        sourceSize { width: base.width; height: base.height }
        source: (base.height == 0 || base.contents_id == "") ? ""
              : "image://coverart/" + base.contents_id + "?" + base.height + "?" + base.update_db_counter
    }

    ShaderEffect {
        anchors.fill: parent
        visible: base.ready
        opacity: base.ready ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }

        property variant source: art
        property real radius: base.radius
        property size frame: Qt.size(width, height)

        // mediump only: the device's Mali-450 has no highp in fragment
        // shaders, and pixel distances up to a few hundred fit fp16.
        fragmentShader: "
            varying highp vec2 qt_TexCoord0;
            uniform sampler2D source;
            uniform lowp float qt_Opacity;
            uniform mediump float radius;
            uniform mediump vec2 frame;
            void main() {
                mediump vec2 q = abs((qt_TexCoord0 - 0.5) * frame) - (0.5 * frame - radius);
                mediump float d = length(max(q, 0.0)) - radius;
                lowp float a = clamp(0.5 - d, 0.0, 1.0);
                gl_FragColor = texture2D(source, qt_TexCoord0) * (a * qt_Opacity);
            }"
    }
}
