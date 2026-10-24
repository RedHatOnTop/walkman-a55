import QtQuick 2.3
import "qrc:/parts"
import "qrc:/components"

// Player transport in Material 3 Expressive style. Drop-in for
// BasicPlayerControls: same `properties` map, signals and heights per
// dispMode, which is what the window's ViewModel and layout rely on. What it
// shows is WmPlayerControlsBody.qml, which the probe's `reload` swaps in a
// running app.
Item {
    id: basicPlayerControls

    width: 480
    height: 0

    property var properties

    objectName: properties["objectName"]

    signal prevButtonClicked()
    signal playButtonClicked()
    signal nextButtonClicked()
    signal prevButtonLongPressed()
    signal prevButtonReleased()
    signal nextButtonLongPressed()
    signal nextButtonReleased()
    signal shuffleButtonClicked()
    signal repeatButtonClicked()

    QtObject {
        id: shell
        property Item wm: viewstyle.wm
        property int dispMode: properties["dispMode"]
        property bool playState: properties["playState"]
        property int controlHeight: 110
        property int titleHeight: 48
    }

    // The screen's wave seek bar follows the play state, which only the
    // window's ViewModel carries.
    Binding {
        target: shell.wm
        property: "playing"
        value: shell.playState
    }

    WmHot {
        anchors.fill: parent
        file: "WmPlayerControlsBody.qml"
    }

    states: [
        State {
            name: "VisibleMusicTitle"
            when: shell.dispMode === 0
            PropertyChanges { target: basicPlayerControls; height: shell.titleHeight + shell.controlHeight }
        },
        State {
            name: "InVisibleMusicTitle"
            when: shell.dispMode === 1
            PropertyChanges { target: basicPlayerControls; height: shell.controlHeight }
        },
        State {
            name: "InVisibleAll"
            when: shell.dispMode === 9
            PropertyChanges { target: basicPlayerControls; height: 0 }
        },
        State {
            name: "Initial"
            when: shell.dispMode === 10
        }
    ]
}
