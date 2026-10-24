import QtQuick 2.3

// Material 3 Expressive wavy progress track: a sine wave up to the handle, a
// flat track after it with a stop dot at the end, and a vertical bar handle.
// The wave flattens out while paused.
ShaderEffect {
    id: wave

    property real value: 0
    property bool playing: false
    property real amplitude: playing ? 3.2 : 0
    Behavior on amplitude { NumberAnimation { duration: 450; easing.type: Easing.OutCubic } }
    property real phase: 0
    property real wavelength: 32
    property real thickness: 4
    property real gap: 5
    property real handleWidth: 4
    property real handleHeight: 24
    Behavior on handleHeight { NumberAnimation { duration: 150 } }
    property color activeColor: "white"
    property color trackColor: "gray"
    property size frame: Qt.size(width, height)

    // A step every 50 ms is smooth enough at this speed, and keeps the
    // scene graph idle most of the time on a battery device.
    Timer {
        interval: 50
        repeat: true
        running: wave.visible && wave.amplitude > 0
        onTriggered: wave.phase = (wave.phase + wave.wavelength * 0.04) % wave.wavelength
    }

    // mediump only, see WmCover.qml. Distances are in pixels.
    fragmentShader: "
        varying highp vec2 qt_TexCoord0;
        uniform lowp float qt_Opacity;
        uniform mediump vec2 frame;
        uniform mediump float value;
        uniform mediump float amplitude;
        uniform mediump float phase;
        uniform mediump float wavelength;
        uniform mediump float thickness;
        uniform mediump float gap;
        uniform mediump float handleWidth;
        uniform mediump float handleHeight;
        uniform lowp vec4 activeColor;
        uniform lowp vec4 trackColor;

        mediump float segment(mediump vec2 p, mediump float x0, mediump float x1, mediump float r) {
            mediump float dx = max(max(x0 - p.x, p.x - x1), 0.0);
            return length(vec2(dx, p.y)) - r;
        }

        void main() {
            mediump vec2 p = vec2(qt_TexCoord0.x * frame.x, (qt_TexCoord0.y - 0.5) * frame.y);
            mediump float r = 0.5 * thickness;
            mediump float head = 0.5 * handleWidth + value * (frame.x - handleWidth);
            mediump float waveEnd = head - 0.5 * handleWidth - gap - r;
            mediump float trackStart = head + 0.5 * handleWidth + gap + r;
            mediump float trackEnd = frame.x - r;

            // The amplitude eases in and out over half a wavelength so the
            // round caps at both ends sit on the axis.
            mediump float k = 6.2831853 / wavelength;
            mediump float amp = amplitude * clamp(min(p.x - r, waveEnd - p.x) / (0.5 * wavelength), 0.0, 1.0);
            mediump float t = mod(p.x - phase, wavelength) * k;
            mediump float slope = amp * k * cos(t);
            mediump float dWave = abs(p.y - amp * sin(t)) / sqrt(1.0 + slope * slope) - r;
            if (p.x < r || p.x > waveEnd)
                dWave = segment(p, r, waveEnd, r);
            if (waveEnd < r)
                dWave = 100.0;

            mediump float dTrack = trackStart > trackEnd ? 100.0 : segment(p, trackStart, trackEnd, r);
            mediump float dStop = trackStart > trackEnd - thickness ? 100.0
                                : length(p - vec2(trackEnd, 0.0)) - 0.5 * r;

            mediump vec2 hq = abs(p - vec2(head, 0.0)) - vec2(0.0, 0.5 * (handleHeight - handleWidth));
            mediump float dHandle = length(max(hq, 0.0)) - 0.5 * handleWidth;

            lowp float aActive = clamp(0.5 - min(min(dWave, dHandle), dStop), 0.0, 1.0);
            lowp float aTrack = clamp(0.5 - dTrack, 0.0, 1.0);
            gl_FragColor = mix(trackColor * aTrack, activeColor, aActive) * qt_Opacity;
        }"
}
