import QtQuick 2.0

// A leaf with a property of its own: Qt 5.3 names an instance's class after
// the file only when the root declares a property, signal or function.
Rectangle {
    property bool lit: false
    objectName: "pill"
    width: 40
    height: 12
    radius: 6
    color: "orange"
}
