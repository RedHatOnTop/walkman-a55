import QtQuick 2.0

// Shaped like the app's components/ClickArea.qml: signals with a parameter.
Item {
    signal pressed(QtObject mouse)
    signal released(QtObject mouse)

    MouseArea {
        anchors.fill: parent
        onPressed: parent.pressed(mouse)
        onReleased: parent.released(mouse)
    }
}
