import QtQuick 2.0
import "parts"

// Never loaded before a reload, like the app's window/MSCWindow.qml when
// the device crashed: compiled after the reload, out of types that already
// have live instances.
Row {
    objectName: "lateWindow"
    Btn { objectName: "lateBtn" }
    Btn {}
}
