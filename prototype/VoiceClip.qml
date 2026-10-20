// 评论里的一段语音：一个可点的胶囊，播放键 + 时长，越长越宽；放的时候底色从左往右填满、
// 时长换成已放到哪里（同样是 m:ss，宽度不跳）。点一下放，再点停；放别的那条会把这条停掉。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Format.js" as Format

AbstractButton {
    id: button

    property var voice: ({})   // { url, seconds, text }

    readonly property bool playing: Voice.source !== "" && Voice.source === (button.voice.url || "")
    readonly property real progress: button.playing && button.voice.seconds > 0
                                     ? Math.min(1, Voice.position / button.voice.seconds) : 0

    implicitWidth: Math.round(Tokens.voiceClipMin + (Tokens.voiceClipMax - Tokens.voiceClipMin)
                              * Math.min(button.voice.seconds || 0, 60) / 60)
    implicitHeight: Tokens.height8
    leftPadding: Tokens.space2
    rightPadding: Tokens.space3
    hoverEnabled: true
    Accessible.name: (button.playing ? "停止语音 " : "播放语音 ") + Format.duration(button.voice.seconds)
    onClicked: Voice.toggle(button.voice.url)

    background: Rectangle {
        radius: height / 2
        color: Qt.alpha(Kirigami.Theme.highlightColor, button.hovered ? Tokens.tonalHover : Tokens.tonal)
        border.width: button.visualFocus ? Tokens.lineStrong : 0
        border.color: Kirigami.Theme.highlightColor
        // 已放的部分：同一色调再叠一层。至少一个圆（垫在停止键下），免得开头的细条露出圆角外
        Rectangle {
            visible: button.playing
            width: Math.max(parent.height, parent.width * button.progress)
            height: parent.height
            radius: parent.radius
            color: Qt.alpha(Kirigami.Theme.highlightColor, Tokens.tonal)
        }
    }

    contentItem: RowLayout {
        spacing: Tokens.space2
        Kirigami.Icon {
            implicitWidth: Tokens.iconSm
            implicitHeight: Tokens.iconSm
            source: button.playing ? "media-playback-stop" : "media-playback-start"
            color: Kirigami.Theme.highlightColor
            isMask: true
        }
        Label {
            text: button.playing ? Format.elapsed(Voice.position) : Format.duration(button.voice.seconds)
            font.pixelSize: Tokens.textSm
            font.features: { "tnum": 1 }
        }
        Item { Layout.fillWidth: true }   // 胶囊按时长变宽，内容仍贴着左边
    }
}
