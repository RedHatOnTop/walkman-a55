import QtQuick 2.0
import "qrc:/parts"

// Replaces qrc:/Badge.qml. Pill comes from the app's resources, Dot from the
// overlay's replacement of a nested app file, Extra only from the overlay.
Row {
    property string origin: "overlay"
    spacing: 8
    Text {
        color: "#00ccff"
        text: "overlay badge"
    }
    Pill {}
    Dot {}
    Extra {}
}
