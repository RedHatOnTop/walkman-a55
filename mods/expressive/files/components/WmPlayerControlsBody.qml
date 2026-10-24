import QtQuick 2.3
import "qrc:/parts"
import "qrc:/components"

// What WmPlayerControls.qml shows: a primary play button that morphs
// between a pill (paused) and a rounded square (playing), tonal
// previous/next buttons that squash when pressed, and shuffle/repeat toggles
// with a filled container when on. Loaded through WmHot, so the probe's
// `reload` swaps it in a running app; its signals go out through the
// basicPlayerControls id there.
Item {
    property var properties: basicPlayerControls.properties
    onPropertiesChanged: {
        if (properties)
            playTitleText.setMainText(properties["playTitle"])
    }

    QtObject {
        id: p
        property int dispMode: properties["dispMode"]
        property bool playState: properties["playState"]
        property bool playerButtonEnabled: properties["playerButtonEnabled"]
        property bool shuffleState: properties["shuffleState"]
        property bool shuffleVisible: properties["shuffleVisible"]
        property int repeatModeState: properties["repeatModeState"]
        property bool repeatModeVisible: properties["repeatModeVisible"]

        property Item wm: viewstyle.wm
        property int controlHeight: shell.controlHeight
        property int titleHeight: shell.titleHeight
        property int fadeDurationMsec: 50

        property string iconPrevious: "M6 6h2v12H6zm3.5 6l8.5 6V6z"
        property string iconNext: "M6 18l8.5-6L6 6v12zM16 6v12h2V6h-2z"
        property string iconPlay: "M8.6 6.4v11.2l9-5.6z"
        property string iconShuffle: "M10.59 9.17L5.41 4 4 5.41l5.17 5.17 1.42-1.41zM14.5 4l2.04 2.04L4 18.59 5.41 20 17.96 7.46 20 9.5V4h-5.5zm.33 9.41l-1.41 1.41 3.13 3.13L14.5 20H20v-5.5l-2.04 2.04-3.13-3.13z"
        property string iconRepeat: "M7 7h10v3l4-4-4-4v3H5v6h2V7zm10 10H7v-3l-4 4 4 4v-3h12v-6h-2v4z"
        property string iconRepeatOne: "M7 7h10v3l4-4-4-4v3H5v6h2V7zm10 10H7v-3l-4 4 4 4v-3h12v-6h-2v4zm-4-2V9h-1l-2 1v1h1.5v4H13z"
    }

    MouseArea {
        anchors.fill: parent
    }

    SCmnMarquee {
        id: playTitleText
        anchors { top: parent.top; left: parent.left; right: parent.right; leftMargin: 40; rightMargin: 40 }
        height: p.titleHeight
        clip: true
        pixelSize: 22
        fontColor: p.wm.surfaceInk
        visible: basicPlayerControls.visible && (p.dispMode === 0)
        isUseMarquee: true
        onVisibleChanged: isUseMarquee = visible
    }

    Item {
        id: playControl
        width: parent.width
        height: p.controlHeight
        anchors.bottom: parent.bottom

        Row {
            anchors.centerIn: parent
            spacing: 8

            SwipeDetectionButton {
                id: shuffleButton
                width: 52
                height: p.controlHeight
                visible: p.shuffleVisible
                loadComponent: Item {}
                pressedComponent: Item {}
                disableComponent: Item {}
                onClicked: basicPlayerControls.shuffleButtonClicked()

                Rectangle {
                    anchors.centerIn: parent
                    width: 52
                    height: 52
                    radius: shuffleButton.state === "Pressed" ? 16 : 26
                    Behavior on radius { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }
                    antialiasing: true
                    color: p.shuffleState ? p.wm.primaryContainer : "transparent"
                    WmIcon {
                        anchors.centerIn: parent
                        width: 26
                        height: 26
                        path: p.iconShuffle
                        color: p.shuffleState ? p.wm.primaryContainerInk : p.wm.surfaceInkVariant
                    }
                }
            }

            SwipeDetectionButton {
                id: prevButton
                width: 76
                height: p.controlHeight
                enable: p.playerButtonEnabled
                isEnablePressAndHold: true
                loadComponent: Item {}
                pressedComponent: Item {}
                disableComponent: Item {}
                onClicked: basicPlayerControls.prevButtonClicked()
                onPressAndHold: basicPlayerControls.prevButtonLongPressed()
                onReleased: basicPlayerControls.prevButtonReleased()

                Rectangle {
                    anchors.centerIn: parent
                    property bool down: prevButton.state === "Pressed"
                    width: down ? 86 : 76
                    height: 76
                    radius: down ? 22 : 38
                    Behavior on width { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }
                    Behavior on radius { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }
                    antialiasing: true
                    color: p.wm.surfaceContainerHigh
                    opacity: prevButton.enable ? 1 : 0.38
                    WmIcon {
                        anchors.centerIn: parent
                        width: 34
                        height: 34
                        path: p.iconPrevious
                        rounding: 1.2
                        color: p.wm.surfaceInk
                    }
                }
            }

            SwipeDetectionButton {
                id: playButton
                width: 112
                height: p.controlHeight
                enable: p.playerButtonEnabled
                loadComponent: Item {}
                pressedComponent: Item {}
                disableComponent: Item {}
                onClicked: basicPlayerControls.playButtonClicked()

                Rectangle {
                    anchors.centerIn: parent
                    property bool down: playButton.state === "Pressed"
                    width: down ? 102 : 112
                    height: 88
                    // Pill while paused, rounded square while playing.
                    radius: down ? 20 : (p.playState ? 28 : 44)
                    Behavior on width { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }
                    Behavior on radius { NumberAnimation { duration: 380; easing.type: Easing.OutBack; easing.overshoot: 2.2 } }
                    antialiasing: true
                    color: p.wm.primary
                    opacity: playButton.enable ? 1 : 0.38

                    Row {
                        anchors.centerIn: parent
                        spacing: 9
                        opacity: p.playState ? 1 : 0
                        scale: p.playState ? 1 : 0.5
                        Behavior on opacity { NumberAnimation { duration: 180 } }
                        Behavior on scale { NumberAnimation { duration: 260; easing.type: Easing.OutBack } }
                        Repeater {
                            model: 2
                            Rectangle {
                                width: 10
                                height: 30
                                radius: 3.5
                                antialiasing: true
                                color: p.wm.primaryInk
                            }
                        }
                    }
                    WmIcon {
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: 2
                        width: 44
                        height: 44
                        path: p.iconPlay
                        rounding: 2.6
                        color: p.wm.primaryInk
                        opacity: p.playState ? 0 : 1
                        scale: p.playState ? 0.5 : 1
                        Behavior on opacity { NumberAnimation { duration: 180 } }
                        Behavior on scale { NumberAnimation { duration: 260; easing.type: Easing.OutBack } }
                    }
                }
            }

            SwipeDetectionButton {
                id: nextButton
                width: 76
                height: p.controlHeight
                enable: p.playerButtonEnabled
                isEnablePressAndHold: true
                loadComponent: Item {}
                pressedComponent: Item {}
                disableComponent: Item {}
                onClicked: basicPlayerControls.nextButtonClicked()
                onPressAndHold: basicPlayerControls.nextButtonLongPressed()
                onReleased: basicPlayerControls.nextButtonReleased()

                Rectangle {
                    anchors.centerIn: parent
                    property bool down: nextButton.state === "Pressed"
                    width: down ? 86 : 76
                    height: 76
                    radius: down ? 22 : 38
                    Behavior on width { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }
                    Behavior on radius { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }
                    antialiasing: true
                    color: p.wm.surfaceContainerHigh
                    opacity: nextButton.enable ? 1 : 0.38
                    WmIcon {
                        anchors.centerIn: parent
                        width: 34
                        height: 34
                        path: p.iconNext
                        rounding: 1.2
                        color: p.wm.surfaceInk
                    }
                }
            }

            SwipeDetectionButton {
                id: repeatButton
                width: 52
                height: p.controlHeight
                visible: p.repeatModeVisible
                loadComponent: Item {}
                pressedComponent: Item {}
                disableComponent: Item {}
                onClicked: basicPlayerControls.repeatButtonClicked()

                Rectangle {
                    anchors.centerIn: parent
                    // 0: off, 1: this track, 2: all
                    property bool on: p.repeatModeState !== 0
                    width: 52
                    height: 52
                    radius: repeatButton.state === "Pressed" ? 16 : 26
                    Behavior on radius { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }
                    antialiasing: true
                    color: on ? p.wm.primaryContainer : "transparent"
                    WmIcon {
                        anchors.centerIn: parent
                        width: 26
                        height: 26
                        path: p.repeatModeState === 1 ? p.iconRepeatOne : p.iconRepeat
                        color: parent.on ? p.wm.primaryContainerInk : p.wm.surfaceInkVariant
                    }
                }
            }
        }
    }

    states: [
        State {
            name: "VisibleMusicTitle"
            when: p.dispMode === 0
            PropertyChanges { target: playControl; enabled: true; opacity: 1 }
            PropertyChanges { target: playTitleText; opacity: 1 }
        },
        State {
            name: "InVisibleMusicTitle"
            when: p.dispMode === 1
            PropertyChanges { target: playControl; enabled: true; opacity: 1 }
            PropertyChanges { target: playTitleText; opacity: 0 }
        },
        State {
            name: "InVisibleAll"
            when: p.dispMode === 9
            PropertyChanges { target: playControl; enabled: false; opacity: 0 }
            PropertyChanges { target: playTitleText; opacity: 0 }
        },
        State {
            name: "Initial"
            when: p.dispMode === 10
        }
    ]
    transitions: [
        Transition {
            from: "Initial"
        },
        Transition {
            to: "VisibleMusicTitle"
            NumberAnimation { target: playTitleText; property: "opacity"; duration: p.fadeDurationMsec }
            NumberAnimation { target: playControl; property: "opacity"; duration: p.fadeDurationMsec }
        }
    ]
}
