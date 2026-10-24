import QtQuick 2.3
import "qrc:/"
import "qrc:/parts"
import "qrc:/components"

// What MusicPlayerDefault.qml shows: a large rounded cover, a bold
// left-aligned title and a wavy seek bar, over the backdrop that the screen
// tints from the cover (components/WmBackdrop.qml). Loaded through WmHot, so
// the probe's `reload` swaps it in a running app; the screen's values come
// from the musicPlayerDefault and shell ids there.
Item {
    // The shell lays swipeSelectorTimeScrubBar over this by its x and y, so
    // the scrub bar stays a direct child.
    property Item scrubArea: scrub

    function resetSeek() {
        scrub.resetValue()
    }

    QtObject {
        id: p
        property Item wm: viewstyle.wm
        property int gutter: 40
        property int coverTop: 16
        property string title: meta("play_title")
        property string subtitle: [meta("artist_name"), meta("album_name")]
                                  .filter(function(s) { return s !== "" }).join("  ·  ")
        onTitleChanged: titleText.setMainText(title)
    }

    function meta(key) {
        var v = musicPlayerDefault.meta_data ? musicPlayerDefault.meta_data[key] : undefined
        return v === undefined || v === null ? "" : String(v)
    }

    WmCover {
        id: cover
        x: p.gutter
        y: p.coverTop
        width: parent.width - 2 * p.gutter
        height: width
        radius: 40
        contents_id: shell.contentsId
        update_db_counter: musicPlayerDefault.update_db_counter
        placeholderColor: p.wm.surfaceContainerHigh
        placeholderInkColor: p.wm.surfaceInkVariant

        Row {
            anchors { left: parent.left; bottom: parent.bottom; margins: 16 }
            spacing: 8

            Rectangle {
                visible: meta("is_high_resolution") === "true"
                width: hiResText.contentWidth + 24
                height: 30
                radius: height / 2
                antialiasing: true
                color: p.wm.primary
                Text {
                    id: hiResText
                    anchors.centerIn: parent
                    text: "Hi-Res"
                    color: p.wm.primaryInk
                    font.family: appFont.fontFamily
                    font.pixelSize: 15
                    font.weight: Font.Bold
                }
            }
            Rectangle {
                visible: meta("lyrics_status") === "2"
                width: 46
                height: 30
                radius: height / 2
                antialiasing: true
                color: p.wm.surfaceContainerHigh
                WmIcon {
                    anchors.centerIn: parent
                    width: 22
                    height: 22
                    path: "M3 18h12v-2H3v2zM3 6v2h18V6H3zm0 7h18v-2H3v2z"
                    color: p.wm.surfaceInk
                }
            }
        }
    }

    SCmnMarquee {
        id: titleText
        anchors { top: cover.bottom; topMargin: 12; left: cover.left; right: cover.right }
        height: 42
        clip: true
        pixelSize: 31
        fontWeight: Font.Bold
        fontColor: p.wm.surfaceInk
        horizontal_alignment: Text.AlignLeft
        isUseMarquee: shell.shown

        // A title that runs past the edge fades out there.
        layer.enabled: titleProbe.implicitWidth > width
        layer.effect: ShaderEffect {
            property real fade: 40 / width
            fragmentShader: "
                varying highp vec2 qt_TexCoord0;
                uniform sampler2D source;
                uniform lowp float qt_Opacity;
                uniform mediump float fade;
                void main() {
                    lowp float a = clamp((1.0 - qt_TexCoord0.x) / fade, 0.0, 1.0);
                    gl_FragColor = texture2D(source, qt_TexCoord0) * (a * qt_Opacity);
                }"
        }
    }
    Text {
        id: titleProbe
        visible: false
        text: p.title
        font.family: appFont.fontFamily
        font.pixelSize: titleText.pixelSize
        font.weight: titleText.fontWeight
    }

    Text {
        id: subtitleText
        anchors { top: titleText.bottom; topMargin: 2; left: cover.left; right: cover.right }
        height: 28
        verticalAlignment: Text.AlignVCenter
        text: p.subtitle
        elide: Text.ElideRight
        color: p.wm.surfaceInkVariant
        font.family: appFont.fontFamily
        font.pixelSize: 20
    }

    WmScrubBar {
        id: scrub
        anchors { top: subtitleText.bottom; topMargin: 12; left: cover.left; right: cover.right }
        totalPlaybackTime: musicPlayerDefault.total_playback_time
        currentlyPlayingTime: musicPlayerDefault.currently_playing_time
        totalPlayTrackNum: musicPlayerDefault.total_playtrack_num
        currentryPlayTrackNum: musicPlayerDefault.currentry_playtrack_num
        playing: p.wm.playing
        activeColor: p.wm.primary
        trackColor: p.wm.surfaceContainerHigh
        timeColor: p.wm.surfaceInkVariant
        trackNoColor: p.wm.outline
        onPressed: musicPlayerDefault.pressed()
        onReleased: musicPlayerDefault.released(time)
        onPlayEnded: musicPlayerDefault.playEnded()
        onReqSwipe: swipeSelectorTimeScrubBar.reqSwipe()
    }
}
