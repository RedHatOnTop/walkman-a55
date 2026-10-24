import QtQuick 2.3

// The whole-window backdrop and the dynamic colour scheme. FrameWork.qml
// loads it behind every screen and publishes it as viewstyle.wm. A screen
// that wants the tinted backdrop sets `tint` while it is the current one and
// `coverSource` to a small image://coverart/ URL of the playing track.
//
// The scheme is a dark Material-style tonal palette: one seed hue from the
// cover, each role a fixed OKLab lightness and a chroma scaled by how
// colourful the cover is, so a grey cover gives a grey scheme.
Item {
    id: wm

    property string coverSource: ""
    property bool tint: false
    property bool playing: false

    property color surface: "#141218"
    property color surfaceContainer: "#211f26"
    property color surfaceContainerHigh: "#2b2930"
    property color surfaceInk: "#e6e0e9"
    property color surfaceInkVariant: "#cac4d0"
    property color outline: "#938f99"
    property color outlineVariant: "#49454f"
    property color primary: "#d0bcff"
    property color primaryInk: "#381e72"
    property color primaryContainer: "#4f378b"
    property color primaryContainerInk: "#eaddff"
    property color tertiary: "#efb8c8"
    property color tertiaryContainer: "#633b48"

    Behavior on surface { ColorAnimation { duration: 450 } }
    Behavior on surfaceContainer { ColorAnimation { duration: 450 } }
    Behavior on surfaceContainerHigh { ColorAnimation { duration: 450 } }
    Behavior on surfaceInk { ColorAnimation { duration: 450 } }
    Behavior on surfaceInkVariant { ColorAnimation { duration: 450 } }
    Behavior on outline { ColorAnimation { duration: 450 } }
    Behavior on outlineVariant { ColorAnimation { duration: 450 } }
    Behavior on primary { ColorAnimation { duration: 450 } }
    Behavior on primaryInk { ColorAnimation { duration: 450 } }
    Behavior on primaryContainer { ColorAnimation { duration: 450 } }
    Behavior on primaryContainerInk { ColorAnimation { duration: 450 } }
    Behavior on tertiary { ColorAnimation { duration: 450 } }
    Behavior on tertiaryContainer { ColorAnimation { duration: 450 } }

    // Hue in radians and chroma of the scheme when there is no cover art.
    readonly property real fallbackHue: 5.1
    readonly property real fallbackChroma: 0.10

    function toLinear(c) {
        return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4)
    }
    function toGamma(c) {
        return c <= 0.0031308 ? 12.92 * c : 1.055 * Math.pow(c, 1 / 2.4) - 0.055
    }
    function toOklab(r, g, b) {
        r = toLinear(r); g = toLinear(g); b = toLinear(b)
        var l = Math.pow(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b, 1 / 3)
        var m = Math.pow(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b, 1 / 3)
        var s = Math.pow(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b, 1 / 3)
        return [0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
                1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
                0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s]
    }
    function fromOklch(L, C, h) {
        var a = C * Math.cos(h), b = C * Math.sin(h)
        var l = Math.pow(L + 0.3963377774 * a + 0.2158037573 * b, 3)
        var m = Math.pow(L - 0.1055613458 * a - 0.0638541728 * b, 3)
        var s = Math.pow(L - 0.0894841775 * a - 1.2914855480 * b, 3)
        return [4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
                -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s]
    }
    // Lowers chroma until the colour fits in sRGB, keeping lightness and hue.
    function tone(L, C, h) {
        var rgb = fromOklch(L, C, h)
        for (var i = 0; i < 12; i++) {
            if (Math.min(rgb[0], rgb[1], rgb[2]) >= 0 && Math.max(rgb[0], rgb[1], rgb[2]) <= 1)
                break
            C *= 0.85
            rgb = fromOklch(L, C, h)
        }
        return Qt.rgba(toGamma(Math.max(0, Math.min(1, rgb[0]))),
                       toGamma(Math.max(0, Math.min(1, rgb[1]))),
                       toGamma(Math.max(0, Math.min(1, rgb[2]))), 1)
    }

    function applyScheme(h, c) {
        // 1 for a vivid cover, down to a near-grey scheme for a grey one.
        var k = Math.max(0.12, Math.min(1, c / 0.11))
        surface = tone(0.18, 0.040 * k, h)
        surfaceContainer = tone(0.235, 0.048 * k, h)
        surfaceContainerHigh = tone(0.285, 0.055 * k, h)
        surfaceInk = tone(0.95, 0.012 * k, h)
        surfaceInkVariant = tone(0.80, 0.035 * k, h)
        outline = tone(0.62, 0.040 * k, h)
        outlineVariant = tone(0.38, 0.040 * k, h)
        primary = tone(0.84, 0.13 * k, h)
        primaryInk = tone(0.26, 0.08 * k, h)
        primaryContainer = tone(0.40, 0.11 * k, h)
        primaryContainerInk = tone(0.93, 0.05 * k, h)
        tertiary = tone(0.82, 0.11 * k, h + 1.05)
        tertiaryContainer = tone(0.38, 0.09 * k, h + 1.05)
    }

    // Seed hue: the chroma-weighted circular mean of the cover's hues, so it
    // follows the most colourful region instead of a muddy average. Seed
    // chroma: a high percentile, so one vivid area counts on a dark cover.
    function seedFrom(data, count) {
        var x = 0, y = 0, chromas = []
        for (var i = 0; i < count; i++) {
            var lab = toOklab(data[i * 4] / 255, data[i * 4 + 1] / 255, data[i * 4 + 2] / 255)
            var c = Math.sqrt(lab[1] * lab[1] + lab[2] * lab[2])
            // Near-black and near-white pixels carry unreliable hue.
            var w = c * Math.max(0, 1 - Math.abs(lab[0] - 0.62) * 1.6)
            x += w * lab[1]
            y += w * lab[2]
            chromas.push(c)
        }
        chromas.sort(function(a, b) { return a - b })
        var vivid = chromas[Math.floor(count * 0.85)]
        if (x * x + y * y < 1e-8)
            return [fallbackHue, vivid]
        return [Math.atan2(y, x), vivid]
    }

    Component.onCompleted: applyScheme(fallbackHue, fallbackChroma)

    onCoverSourceChanged: {
        if (coverSource === "") {
            applyScheme(fallbackHue, fallbackChroma)
            return
        }
        if (sampler.isImageError(coverSource))
            applyScheme(fallbackHue, fallbackChroma)
        else if (sampler.isImageLoaded(coverSource))
            sampler.requestPaint()
        else
            sampler.loadImage(coverSource)
    }

    Canvas {
        id: sampler
        width: 16
        height: 16
        // Painted for getImageData only; never shown.
        opacity: 0
        onImageLoaded: requestPaint()
        onPaint: {
            var url = wm.coverSource
            if (url === "" || !isImageLoaded(url)) {
                if (url !== "" && isImageError(url))
                    wm.applyScheme(wm.fallbackHue, wm.fallbackChroma)
                return
            }
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.drawImage(url, 0, 0, width, height)
            var seed = wm.seedFrom(ctx.getImageData(0, 0, width, height).data, width * height)
            wm.applyScheme(seed[0], seed[1])
        }
    }

    // The surface colour with a soft glow of the primary container behind
    // the upper half of the screen, where the cover sits.
    ShaderEffect {
        anchors.fill: parent
        opacity: wm.tint ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 250 } }
        property color surface: wm.surface
        property color glow: wm.primaryContainer
        property real aspect: height > 0 ? height / width : 1
        fragmentShader: "
            varying highp vec2 qt_TexCoord0;
            uniform lowp float qt_Opacity;
            uniform lowp vec4 surface;
            uniform lowp vec4 glow;
            uniform mediump float aspect;
            void main() {
                mediump vec2 d = (qt_TexCoord0 - vec2(0.5, 0.3)) * vec2(1.0, aspect);
                lowp float a = 0.6 * (1.0 - smoothstep(0.0, 0.85, length(d)));
                gl_FragColor = mix(surface, glow, a) * qt_Opacity;
            }"
    }
}
