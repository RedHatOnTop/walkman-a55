import QtQuick 2.0

// The harness's copy of the mods' components/WmHot.qml.
Loader {
    property int wmReloadToken: 0
    property url file
    source: wmReloadToken < 0 ? "" : file
}
