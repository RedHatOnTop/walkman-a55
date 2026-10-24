import QtQuick 2.0
import "parts"

Rectangle {
    objectName: "root"
    width: 800
    height: 480
    color: "black"

    Column {
        objectName: "column"
        Text {
            objectName: "titleText"
            color: "white"
            text: playerVM.title
        }
        Text {
            objectName: "volumeText"
            color: "white"
            text: "vol " + playerVM.volume
        }
        Badge {
            objectName: "badge"
        }
        Hot {
            objectName: "hot"
            width: 100
            height: 12
            file: "Body.qml"
        }
        Image {
            objectName: "outsideDot"
            source: "dot.png"
        }
        // Its attached objects have an open meta-object, which makes up any
        // property name looked up with indexOfProperty().
        PathView {
            objectName: "pathView"
            width: 100
            height: 8
            model: 3
            delegate: Rectangle {
                width: 4
                height: 4
                color: PathView.isCurrentItem ? "white" : "gray"
            }
            path: Path {
                startX: 0
                startY: 4
                PathLine { x: 100; y: 4 }
            }
        }
        Btn {
            objectName: "liveBtn"
        }
        Loader {
            objectName: "slowLoader"
            property string url
            source: url
        }
        Loader {
            objectName: "lateLoader"
            active: false
            source: "Win.qml"
        }
        Rectangle {
            objectName: "hiddenRect"
            visible: false
            width: 10
            height: 10
        }
    }
}
