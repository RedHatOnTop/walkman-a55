import QtQuick 2.0

// Shaped like the app's components/StateButton.qml: handlers for Area's
// signals, a function, and states whose PropertyChanges hold a binding.
// Qt 5.3 builds such a binding when the state applies, from the compiled
// file it looks up again by URL (QQmlBinding::createBinding).
Area {
    id: btn
    property bool pressing: false
    width: 10
    height: 10
    state: "Up"

    onPressed: {
        pressing = true
        changeState()
    }
    onReleased: {
        pressing = false
        changeState()
    }

    function changeState() {
        state = pressing ? "Down" : "Up"
    }

    states: [
        State {
            name: "Up"
            PropertyChanges { target: btn; opacity: 1 }
        },
        State {
            name: "Down"
            PropertyChanges { target: btn; opacity: pressing ? 0.5 : 0.25 }
        }
    ]
}
