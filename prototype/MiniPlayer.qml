// 迷你播放器：主窗口收起时的小窗，置顶、无边框。用来在别的程序里瞄一眼、点一下：
// 封面 · 曲名 / 当前这句歌词（没有歌词时是歌手）与进度 · 喜欢 · 上一首 / 播放 / 下一首 · 回到主窗口。
// 它真的浮在一切之上：与系统的浮层同一种面（圆角、1px 边框、投影），投影画在窗口四周留出的透明边里。
// 拖动任意空白处移动；在上面滚滚轮调音量（与播放条的音量按钮同一手势）；点封面或「展开」回到主窗口。
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
    property real elapsed      // 秒
    property real volume

    signal playPauseRequested()
    signal previousRequested()
    signal nextRequested()
    signal likeToggled()
    signal expandRequested()
    signal volumeRequested(real volume)

    readonly property int shadowRoom: Tokens.space4   // 投影的位置
    readonly property var lines: mini.track ? Source.lyrics(mini.track.id) : []
    readonly property int lineIndex: Source.lyricIndex(mini.lines, mini.elapsed)
    readonly property var line: mini.lineIndex >= 0 ? mini.lines[mini.lineIndex] : null
    // 第二行：在放、有歌词时是当前这句，否则是歌手
    readonly property string second: mini.playing && mini.line && mini.line.text ? mini.line.text
        : mini.track ? mini.track.artist : ""

    title: "云听 · 迷你播放器"
    width: Tokens.wideRow + 2 * mini.shadowRoom      // 面宽 480：五个按钮之外给曲名留约 190 宽
    height: Tokens.hudHeight + 2 * mini.shadowRoom   // 面高 80，与播放条同高
    minimumWidth: width
    maximumWidth: width
    minimumHeight: height
    maximumHeight: height
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Window
    color: "transparent"

    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false

    Kirigami.ShadowedRectangle {
        id: surface
        anchors.fill: parent
        anchors.margins: mini.shadowRoom
        radius: Tokens.radiusLg
        color: Kirigami.Theme.backgroundColor
        border.width: Tokens.line
        border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
        shadow.size: Tokens.space2
        shadow.yOffset: Tokens.space1
        shadow.color: Qt.rgba(0, 0, 0, 0.3)

        DragHandler {
            target: null
            onActiveChanged: if (active) mini.startSystemMove()
        }
        WheelHandler {
            onWheel: event => mini.volumeRequested(Math.max(0, Math.min(1, mini.volume + (event.angleDelta.y > 0 ? 0.05 : -0.05))))
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
                // 换句时淡入淡出：一行字整个换掉会跳
                Item {
                    Layout.fillWidth: true
                    implicitHeight: secondLabel.implicitHeight
                    visible: mini.second.length > 0
                    Label {
                        id: secondLabel
                        width: parent.width
                        text: mini.second
                        font.pixelSize: Tokens.textSm
                        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                        elide: Text.ElideRight
                        Behavior on text {
                            SequentialAnimation {
                                NumberAnimation { target: secondLabel; property: "opacity"; to: 0; duration: Tokens.motionFast; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut }
                                PropertyAction {}
                                NumberAnimation { target: secondLabel; property: "opacity"; to: 1; duration: Tokens.motionFast; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut }
                            }
                        }
                    }
                }
                // 进度：一条细线，与播放条顶边的进度线同一种说法（未播段淡、已播段强调色）
                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: Tokens.space2
                    implicitHeight: Tokens.lineStrong
                    radius: Tokens.lineStrong / 2
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.track)
                    visible: mini.track !== null && (mini.track.dur || 0) > 0
                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(1, mini.elapsed / Math.max(1, mini.track ? mini.track.dur || 1 : 1)))
                        height: parent.height
                        radius: parent.radius
                        color: Kirigami.Theme.highlightColor
                    }
                }
            }

            IconButton {
                enabled: mini.track !== null
                icon.name: mini.liked ? "" : "love-symbolic"
                icon.source: mini.liked ? Tokens.likedIcon : ""
                icon.color: mini.liked ? Tokens.liked : Kirigami.Theme.textColor
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
}
