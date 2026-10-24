import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2
import "qrc:/parts"

// The player's seek bar: SCmnSlider's touch handling over a WmWave, with the
// elapsed time, track number and length underneath. Same signals as
// components/ScrubBar.qml.
Item {
    id: base

    property real totalPlaybackTime: 0
    property real currentlyPlayingTime: 0
    property int totalPlayTrackNum: 0
    property int currentryPlayTrackNum: 0
    property bool playing: false
    property color activeColor: "white"
    property color trackColor: "gray"
    property color timeColor: "white"
    property color trackNoColor: "gray"

    signal pressed()
    signal released(int time)
    signal playEnded()
    signal reqSwipe()

    width: 400
    height: p.waveHeight + p.labelTop + p.labelHeight

    QtObject {
        id: p
        property int waveHeight: 32
        property int labelTop: 2
        property int labelHeight: 24
        property int pixelSize: 17
        property bool sliderCancel: false
        property string timeOver: qsTranslate("ScrubBar", "020021") + viewstyle.empty_string
        property string trackNoOver: qsTranslate("ScrubBar", "000074") + viewstyle.empty_string
    }

    function resetValue() {
        slider.value = currentlyPlayingTime
    }

    // mm:ss, with as many minute digits as needed.
    function dispTime(time) {
        var min = Math.floor(time / 60000)
        var sec = Math.floor(time % 60000 / 1000)
        return (min < 10 ? "0" + min : min) + ":" + (sec < 10 ? "0" + sec : sec)
    }

    function dispTrackNo(n) {
        return (n <= 0 || n >= 10000) ? p.trackNoOver : n
    }

    onCurrentlyPlayingTimeChanged: {
        if (!slider.sliderHoldingPressed) {
            slider.value = currentlyPlayingTime
        } else if (Math.floor(currentlyPlayingTime / 1000) >= Math.floor(totalPlaybackTime / 1000)) {
            // The track ended under the finger: drop the drag.
            p.sliderCancel = true
            slider.enabled = false
            slider.enabled = true
            p.sliderCancel = false
            slider.value = currentlyPlayingTime
        }
    }

    WmWave {
        id: wave
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: p.waveHeight
        value: base.totalPlaybackTime > 0 ? Math.max(0, Math.min(1, slider.value / base.totalPlaybackTime)) : 0
        playing: base.playing && !slider.sliderHoldingPressed
        handleHeight: slider.sliderHoldingPressed ? 30 : 22
        activeColor: base.activeColor
        trackColor: base.trackColor
    }

    SCmnSlider {
        id: slider
        anchors.fill: wave
        sliderStep: (base.totalPlaybackTime > 0 && width > 0) ? base.totalPlaybackTime / width : 1
        value: base.currentlyPlayingTime
        maxValue: base.totalPlaybackTime
        tapEnable: true
        pressAndHoldDuration: 50
        expandTapHeightBottom: 30
        expandTapWidth: 40
        onPressed: base.pressed()
        onReleased: {
            if (!p.sliderCancel)
                base.released(slider.value)
        }
        onValueChanged: {
            if (base.totalPlaybackTime <= slider.value)
                base.playEnded()
        }
        slider_style: SliderStyle {
            groove: Item {}
            handle: Item {}
        }
        onStartedSwipe: base.reqSwipe()
    }

    SCmnMonospaceLabel {
        anchors { top: wave.bottom; topMargin: p.labelTop; left: parent.left }
        height: p.labelHeight
        verticalAlignment: Text.AlignVCenter
        text: slider.value >= 1000 * 60000 ? p.timeOver : base.dispTime(slider.value)
        normal_color: base.timeColor
        font.pixelSize: p.pixelSize
    }

    SCmnMonospaceLabel {
        anchors { top: wave.bottom; topMargin: p.labelTop; horizontalCenter: parent.horizontalCenter }
        height: p.labelHeight
        verticalAlignment: Text.AlignVCenter
        text: base.dispTrackNo(base.currentryPlayTrackNum) + " / " + base.dispTrackNo(base.totalPlayTrackNum)
        normal_color: base.trackNoColor
        font.pixelSize: p.pixelSize
    }

    SCmnMonospaceLabel {
        anchors { top: wave.bottom; topMargin: p.labelTop; right: parent.right }
        height: p.labelHeight
        verticalAlignment: Text.AlignVCenter
        text: (base.totalPlaybackTime >= 1000 * 60000 || base.totalPlaybackTime < 0) ? p.timeOver
            : base.dispTime(base.totalPlaybackTime)
        normal_color: base.timeColor
        font.pixelSize: p.pixelSize
    }
}
