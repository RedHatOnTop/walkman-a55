import QtQuick 2.12
import QtQuick.Window 2.2

// The root the Sony views run in, in place of framework/FrameWork.qml. The
// views reach `viewstyle`, `screen_info`, and their Loader's `controller` by
// dynamic scope, so those keep the stock names and shapes. Everything that
// decides where to go lives in ScreenController; this file only draws it.
Window {
    id: root
    visible: true
    color: viewstyle.bgcolor.D1
    title: "Walkman"

    property QtObject viewstyle: QtObject {
        property string mode: "normal"
        property string empty_string: appFont.emptyString
        property string font_family: appFont.fontFamily
        property string language_name: appFont.language_name
        property var textsize: screenController.styleProperties[mode]["textsize"]
        property var textcolor: screenController.styleProperties[mode]["textcolor"]
        property var bgcolor: screenController.styleProperties[mode]["bgcolor"]
    }

    property QtObject screen_info: QtObject {
        property real window_width: screenController.windowWidth
        property real window_height: screenController.windowHeight
        property real width: screenController.width
        property real height: screenController.height
        property real x: screenController.x
        property real y: screenController.y
    }

    Item {
        id: stage
        objectName: "stage"
        width: screenController.windowWidth
        height: screenController.windowHeight
        scale: root.width / screenController.windowWidth
        transformOrigin: Item.TopLeft

        // Builds the early-load views one after another while nothing moves,
        // so the first swipe to one never waits for it to be created.
        Timer {
            id: warmUp
            property int next: 0
            property var loading: null
            interval: 100
            repeat: true
            onTriggered: {
                if (screenController.busy || (loading && loading.status === Loader.Loading))
                    return
                var list = screenController.earlyLoad
                while (next < list.length) {
                    var loader = frame.screens[list[next++]]
                    if (loader && !loader.active) {
                        loading = loader
                        frame.load(loader, true)
                        return
                    }
                }
                loading = null
                stop()
            }
        }

        QtObject {
            id: frame
            property var screens: ({})
            property var windows: ({})
            property string from: ""
            property string to: ""
            property string fromWindow: ""
            property string toWindow: ""
            property string effect: ""
            property int direction: 0
            property bool complete: false
            property int running: 0

            function screen(id) { return screens[id] || null }
            function windowLoader(id) { return windows[id] || null }

            function load(loader, asynchronous) {
                if (!loader || loader.active)
                    return
                loader.asynchronous = asynchronous
                loader.active = true
            }

            function present(screenId, windowId) {
                track.x = 0
                track.y = 0
                for (var id in screens) {
                    var loader = screens[id]
                    var current = id === screenId
                    if (current)
                        load(loader, false)
                    if (loader.visible !== current)
                        loader.visible = current
                    loader.x = 0
                    loader.y = 0
                    loader.opacity = 1
                }
                for (var w in windows) {
                    var window = windows[w]
                    var shown = w === windowId
                    if (shown)
                        load(window, false)
                    window.visible = shown
                    window.opacity = 1
                }
                var preload = screenController.preload
                for (var i = 0; i < preload.length; i++)
                    load(screens[preload[i]], true)
                if (!warmUp.running && warmUp.next === 0)
                    warmUp.start()
                var current = windows[windowId]
                if (current && current.item)
                    screenController.ReconfigureScreenByWindow(current.item)
            }

            function begin(fromId, toId, fromWindowId, toWindowId, effectName, dir) {
                from = fromId
                to = toId
                fromWindow = fromWindowId
                toWindow = toWindowId
                effect = effectName
                direction = dir
                var incoming = screens[to]
                load(incoming, false)
                if (incoming.status === Loader.Loading)
                    incoming.asynchronous = false
                incoming.visible = true
                incoming.opacity = 1
                if (effect === "swipe") {
                    var sign = direction > 0 ? 1 : -1
                    incoming.x = Math.abs(direction) === 1 ? sign * screenArea.width : 0
                    incoming.y = Math.abs(direction) === 1 ? 0 : sign * screenArea.height
                } else {
                    incoming.x = 0
                    incoming.y = 0
                    incoming.opacity = 0
                    if (toWindow !== fromWindow) {
                        var window = windows[toWindow]
                        load(window, false)
                        window.visible = true
                        window.opacity = 0
                    }
                }
            }

            function drag(offset) {
                if (to === "")
                    return
                if (effect === "swipe") {
                    var sign = direction > 0 ? 1 : -1
                    if (Math.abs(direction) === 1)
                        track.x = -sign * offset
                    else
                        track.y = -sign * offset
                    return
                }
                var size = Math.abs(direction) === 1 ? screenArea.width : screenArea.height
                var progress = Math.min(1, offset / (size / 2))
                screens[from].opacity = 1 - progress
                screens[to].opacity = progress
                if (toWindow !== fromWindow) {
                    windows[fromWindow].opacity = 1 - progress
                    windows[toWindow].opacity = progress
                }
            }

            function settle(completed, duration) {
                complete = completed
                running = 0
                if (effect === "swipe") {
                    var sign = direction > 0 ? 1 : -1
                    var horizontal = Math.abs(direction) === 1
                    var animation = horizontal ? slideX : slideY
                    animation.from = horizontal ? track.x : track.y
                    animation.to = completed ? -sign * (horizontal ? screenArea.width : screenArea.height) : 0
                    animation.duration = Math.max(1, duration)
                    running = 1
                    animation.restart()
                    return
                }
                fade(fadeFrom, screens[from], completed ? 0 : 1, duration)
                fade(fadeTo, screens[to], completed ? 1 : 0, duration)
                if (toWindow !== fromWindow) {
                    fade(fadeFromWindow, windows[fromWindow], completed ? 0 : 1, duration)
                    fade(fadeToWindow, windows[toWindow], completed ? 1 : 0, duration)
                }
            }

            function fade(animation, target, value, duration) {
                animation.target = target
                animation.from = target.opacity
                animation.to = value
                animation.duration = Math.max(1, duration)
                running += 1
                animation.restart()
            }

            function animationDone() {
                running -= 1
                if (running > 0)
                    return
                var completed = complete
                var incoming = screens[to]
                var incomingWindow = windows[toWindow]
                var changedWindow = toWindow !== fromWindow
                to = ""
                screenController.transitionDone(completed)
                if (!completed) {
                    track.x = 0
                    track.y = 0
                    incoming.visible = false
                    incoming.opacity = 1
                    screens[from].opacity = 1
                    if (changedWindow) {
                        incomingWindow.visible = false
                        windows[fromWindow].opacity = 1
                    }
                }
            }
        }

        // Sony's TransitFaderAnimation eases the outgoing screen with OutCirc
        // and brings the incoming one in linearly.
        OpacityAnimator { id: fadeFrom; easing.type: Easing.OutCirc; onStopped: frame.animationDone() }
        OpacityAnimator { id: fadeTo; onStopped: frame.animationDone() }
        OpacityAnimator { id: fadeFromWindow; onStopped: frame.animationDone() }
        OpacityAnimator { id: fadeToWindow; onStopped: frame.animationDone() }

        XAnimator {
            id: slideX
            target: track
            easing.type: Easing.OutQuint
            onStopped: frame.animationDone()
        }

        YAnimator {
            id: slideY
            target: track
            easing.type: Easing.OutQuint
            onStopped: frame.animationDone()
        }

        Item {
            id: screenArea
            objectName: "screenArea"
            x: screen_info.x
            y: screen_info.y
            width: screen_info.width
            height: screen_info.height
            clip: true

            Item {
                id: track
                width: screenArea.width
                height: screenArea.height

                Repeater {
                    model: screenController.allScreenIDList
                    delegate: Loader {
                        id: screenLoader
                        // Screens reach these two by dynamic scope, as under FrameWork.qml.
                        property QtObject controller: screenController.proxy(modelData)
                        property QtObject viewModel: null
                        width: screenArea.width
                        height: screenArea.height
                        visible: false
                        active: false
                        source: screenController.allViewLayouts[modelData]
                        Component.onCompleted: frame.screens[modelData] = screenLoader
                        onLoaded: {
                            item.objectName = modelData
                            screenController.LoadedView(modelData, item)
                        }
                    }
                }
            }
        }

        Repeater {
            model: screenController.allWindowIDList
            delegate: Loader {
                id: windowLoader
                property QtObject windowController: screenController.proxy(modelData)
                width: stage.width
                height: stage.height
                visible: false
                active: false
                source: screenController.allViewLayouts[modelData] || ""
                Component.onCompleted: frame.windows[modelData] = windowLoader
                onLoaded: {
                    item.objectName = modelData
                    screenController.LoadedWindow(modelData, item)
                }
            }
        }

        Repeater {
            model: screenController.popups
            delegate: Loader {
                property QtObject controller: screenController.proxy(modelData)
                width: stage.width
                height: stage.height
                source: screenController.allViewLayouts[modelData] || ""
                onLoaded: {
                    item.objectName = modelData
                    screenController.LoadedPopup(modelData, item)
                }
            }
        }

        Loader {
            objectName: "dialogLayer"
            anchors.fill: parent
            source: "qrc:/framework/Dialog.qml"
        }

        Loader {
            anchors.fill: parent
            source: "qrc:/framework/Overlay.qml"
        }

        MouseArea {
            objectName: "transitionGuard"
            anchors.fill: parent
            enabled: screenController.busy
        }
    }

    Connections {
        target: screenController
        function onPresentScreen(screen, window) { frame.present(screen, window) }
        function onBeginTransition(from, to, fromWindow, toWindow, effect, direction, interactive) {
            frame.begin(from, to, fromWindow, toWindow, effect, direction)
        }
        function onDragOffsetChanged() { frame.drag(screenController.dragOffset) }
        function onSettleTransition(complete, duration) { frame.settle(complete, duration) }
    }
}
