import QtQuick 2.3

// Stands in for framework/FrameWork.qml: the root properties that every
// screen reads through dynamic scope, one screen and its window chrome.
// A spec may also name "backdrop", a component a mod's FrameWork patch loads
// behind everything as viewstyle.wm; "screenInfo", {x, y, width, height} of
// the screen area when the window's chrome differs from globals.json; and
// "available", to send the controller's available() as the app does when the
// screen becomes the current one.
Rectangle {
    id: previewRoot
    width: screenController.windowWidth
    height: screenController.windowHeight
    color: viewstyle.bgcolor.D1

    property QtObject viewstyle: QtObject {
        property string mode: "normal"
        property string empty_string: appFont.emptyString
        property string font_family: appFont.fontFamily
        property string language_name: appFont.language_name
        property var textsize: screenController.styleProperties[mode]["textsize"]
        property var textcolor: screenController.styleProperties[mode]["textcolor"]
        property var bgcolor: screenController.styleProperties[mode]["bgcolor"]
        property Item wm: wmBackdrop.item
    }
    property QtObject screen_info: QtObject {
        property real window_width: screenController.windowWidth
        property real window_height: screenController.windowHeight
        property real width: area("width")
        property real height: area("height")
        property real x: area("x")
        property real y: area("y")
    }

    function area(name) {
        var override = previewSpec.screenInfo || {}
        return name in override ? override[name] : screenController[name]
    }

    // Like FrameWork.qml's Binding-based binder, which only ever writes the
    // view's own properties.
    function apply(item, values) {
        for (var name in values) {
            if (name in item)
                item[name] = values[name]
            else
                console.warn("uisim: " + item + " has no property " + name)
        }
    }

    Loader {
        id: wmBackdrop
        anchors.fill: parent
        source: previewSpec.backdrop || ""
    }

    Loader {
        objectName: "previewScreen"
        // FrameWork.qml's screen loaders carry these, and screens reach them
        // through dynamic scope.
        property QtObject controller: screenController
        property QtObject viewModel
        x: screen_info.x
        y: screen_info.y
        width: screen_info.width
        height: screen_info.height
        source: previewSpec.screen || ""
        onLoaded: {
            item.objectName = screenController.screenID
            apply(item, previewSpec.values || {})
            item.visible = true
            if (previewSpec.available)
                controller.available(null)
        }
    }

    Loader {
        objectName: "previewWindow"
        anchors.fill: parent
        source: previewSpec.window || ""
        onLoaded: apply(item, previewSpec.windowValues || {})
    }
}
