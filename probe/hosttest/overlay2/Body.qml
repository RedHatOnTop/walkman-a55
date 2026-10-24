import QtQuick 2.0
import "parts"

// Leaf is used by nothing else, so the reload compiles it again too.
Row {
    objectName: "bodyV2"
    Image {
        objectName: "bodyDot"
        source: "bodyDot.png"
    }
    Leaf {}
}
