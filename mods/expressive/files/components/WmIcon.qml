import QtQuick 2.3

// A Material icon in one colour, from its 24-unit SVG path. Only the M, L,
// H, V and Z commands (absolute and relative) are understood, which covers
// the straight-edged media icons; Context2D's own SVG path setter is not
// relied on because the device runs Qt 5.3. `rounding` strokes the outline
// with round joins, softening the corners by that many units.
Canvas {
    id: icon

    property string path: ""
    property color color: "white"
    property real rounding: 0

    width: 24
    height: 24

    onPathChanged: requestPaint()
    onColorChanged: requestPaint()
    onRoundingChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        ctx.scale(width / 24, height / 24)
        ctx.beginPath()
        var tokens = path.match(/[MLHVZmlhvz]|-?[0-9]*\.?[0-9]+/g) || []
        var cmd = "M", x = 0, y = 0, startX = 0, startY = 0
        for (var i = 0; i < tokens.length;) {
            if (/[A-Za-z]/.test(tokens[i]))
                cmd = tokens[i++]
            var rel = cmd === cmd.toLowerCase()
            switch (cmd.toUpperCase()) {
            case "M":
                x = (rel ? x : 0) + Number(tokens[i++])
                y = (rel ? y : 0) + Number(tokens[i++])
                ctx.moveTo(x, y)
                startX = x; startY = y
                // Pairs after a moveto are linetos.
                cmd = rel ? "l" : "L"
                break
            case "L":
                x = (rel ? x : 0) + Number(tokens[i++])
                y = (rel ? y : 0) + Number(tokens[i++])
                ctx.lineTo(x, y)
                break
            case "H":
                x = (rel ? x : 0) + Number(tokens[i++])
                ctx.lineTo(x, y)
                break
            case "V":
                y = (rel ? y : 0) + Number(tokens[i++])
                ctx.lineTo(x, y)
                break
            case "Z":
                ctx.closePath()
                x = startX; y = startY
                // A number after Z is malformed; skip it rather than spin.
                if (i < tokens.length && !/[A-Za-z]/.test(tokens[i]))
                    i++
                break
            default:
                i++
            }
        }
        ctx.fillStyle = icon.color
        ctx.fill()
        if (rounding > 0) {
            ctx.lineJoin = "round"
            ctx.lineWidth = rounding
            ctx.strokeStyle = icon.color
            ctx.stroke()
        }
    }
}
