import QtQuick 2.0
import "qrc:/parts"

// The second version of overlay/Badge.qml, outside any reload token.
Row {
    property string origin: "overlay2"
    spacing: 8
    Text {
        color: "#00ccff"
        text: "overlay2 badge"
    }
    Pill {}
    Dot {}
    Extra {}
}
