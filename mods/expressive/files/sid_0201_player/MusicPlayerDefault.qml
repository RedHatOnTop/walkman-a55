import QtQuick 2.3
import "qrc:/"
import "qrc:/parts"
import "qrc:/components"

// Now playing, Material 3 Expressive style. This file is what the ViewModel
// binds by name: the stock screen's properties, signals and functions, and
// the child the app finds by objectName. What it shows is
// WmMusicPlayerBody.qml, which the probe's `reload` swaps in a running app.
ScreenBase {
    id: musicPlayerDefault

    property real total_playback_time: 0
    property real currently_playing_time: 0
    property int currentry_playtrack_num: 0
    property int total_playtrack_num: 0
    property var entry_id
    property var update_db_counter
    property bool is_activated: false
    property var meta_data

    signal pressed()
    signal released(int time)
    signal playEnded()

    width: 480
    height: parent.height

    Item {
        id: dialog_p
        property int dialog_index: 0
    }

    QtObject {
        id: shell
        property Item wm: viewstyle.wm
        property bool shown: false
        property string contentsId: (musicPlayerDefault.is_activated && musicPlayerDefault.entry_id !== undefined)
                                    ? String(musicPlayerDefault.entry_id) : ""
    }

    function onNotifySeekError()
    {
        if (body.item)
            body.item.resetSeek()
    }

    // Follows the playing track even while another screen is current, so
    // the transport keeps the track's colours there.
    Binding {
        target: shell.wm
        property: "coverSource"
        value: shell.contentsId === "" ? ""
             : "image://coverart/" + shell.contentsId + "?32?" + musicPlayerDefault.update_db_counter
    }
    Binding {
        target: shell.wm
        property: "tint"
        value: shell.shown
    }

    WmHot {
        id: body
        anchors.fill: parent
        file: "WmMusicPlayerBody.qml"
    }

    SwipeSelectorViewJudge {
        id: swipeSelectorTimeScrubBar
        objectName: "swipeSelectorTimeScrubBar"
        property Item area: body.item ? body.item.scrubArea : null
        anchors.fill: undefined
        x: area ? area.x : 0
        y: area ? area.y : 0
        width: area ? area.width : 0
        height: area ? area.height : 0
        target: area
    }

    Connections {
        target: controller
        onAvailable: {
            viewstyle.mode = "normal"
            shell.shown = true
        }
        onUnavailable: shell.shown = false
    }
}
