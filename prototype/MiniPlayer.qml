// 迷你播放器：主窗口收起时的小窗，置顶、无边框，高度与播放条相同（80）。
// 只留「正在放什么 + 最常用的控制」：封面 · 曲名/歌手 · 喜欢 · 上一首/播放/下一首 · 回到主窗口。
// 它是一块独立浮起的面：1px 分隔色描边划出边界（没有窗口装饰时它就是边界本身）。
// 拖动任意空白处移动；点封面或「展开」回到主窗口。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami

Window {
    id: mini

    property var track: null
    property bool playing
    property bool liked

    signal playPauseRequested()
    signal previousRequested()
    signal nextRequested()
    signal likeToggled()
    signal expandRequested()

    title: "云听 · 迷你播放器"
    width: Tokens.wideRow             // 480：五个按钮之外给曲名留约 190 宽
    height: Tokens.hudHeight          // 80，与播放条同高
    minimumWidth: width
    maximumWidth: width
    minimumHeight: height
    maximumHeight: height
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Window
    color: Kirigami.Theme.backgroundColor

    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false

    DragHandler {
        target: null
        onActiveChanged: if (active) mini.startSystemMove()
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.width: Tokens.line
        border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Tokens.track)
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Tokens.space4
        anchors.rightMargin: Tokens.space2
        spacing: Tokens.space3

        CoverButton {
            source: mini.track ? Source.sized(mini.track.cover, 96) : ""
            hintIcon: "view-fullscreen-symbolic"
            hint: "回到主窗口"
            onClicked: mini.expandRequested()
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            spacing: 0
            Label {
                Layout.fillWidth: true
                text: mini.track ? mini.track.name : "未在播放"
                font.pixelSize: Tokens.textBase
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                visible: text.length > 0
                text: mini.track ? mini.track.artist : ""
                font.pixelSize: Tokens.textSm
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                elide: Text.ElideRight
            }
        }

        IconButton {
            enabled: mini.track !== null
            icon.name: mini.liked ? "emblem-favorite-symbolic" : "love"
            icon.color: mini.liked ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor
            text: mini.liked ? "取消喜欢" : "喜欢"
            onClicked: mini.likeToggled()
        }
        IconButton {
            enabled: mini.track !== null
            icon.name: "media-skip-backward"
            text: "上一首"
            onClicked: mini.previousRequested()
        }
        IconButton {
            enabled: mini.track !== null
            icon.name: mini.playing ? "media-playback-pause" : "media-playback-start"
            text: mini.playing ? "暂停" : "播放"
            onClicked: mini.playPauseRequested()
        }
        IconButton {
            enabled: mini.track !== null
            icon.name: "media-skip-forward"
            text: "下一首"
            onClicked: mini.nextRequested()
        }
        IconButton {
            icon.name: "view-restore"
            text: "回到主窗口"
            onClicked: mini.expandRequested()
        }
    }
}
