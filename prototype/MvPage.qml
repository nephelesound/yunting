// MV：黑色画面区 + 底部一条播放控制。数据来自 Store 的 mv:<id>（详情 + 播放地址）。
// 画面之外只保留标题、歌手与播放量；这是观看模式，不做弹幕/评论。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import org.kde.kirigami as Kirigami
import "Format.js" as Format

ScreenFrame {
    id: page

    property real mvId
    readonly property var mv: Source.mv(page.mvId)
    readonly property bool ready: page.mv.url !== ""
    // 正在出画面：main.qml 据此让屏幕常亮（backend.power.keepScreenOn）
    readonly property bool videoPlaying: video.playbackState === MediaPlayer.PlayingState

    heading: page.mv.name || "MV"
    Component.onDestruction: video.stop()

    Item {
        anchors.fill: parent
        anchors.leftMargin: Tokens.pageMargin
        anchors.rightMargin: Tokens.pageMargin
        anchors.bottomMargin: Tokens.space6

        Rectangle {
            id: stage
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: info.top
            anchors.bottomMargin: Tokens.space4
            color: "black"
            radius: Tokens.radius

            Video {
                id: video
                anchors.fill: parent
                fillMode: VideoOutput.PreserveAspectFit
                source: page.mv.url
                autoPlay: true
                volume: 0.8
            }

            // 解码失败时给出原因，而不是一直黑屏
            Label {
                anchors.centerIn: parent
                visible: video.errorString.length > 0
                text: video.errorString
                color: "white"
                font.pixelSize: Tokens.textSm
                wrapMode: Text.Wrap
                width: parent.width - 2 * Tokens.pageMargin
                horizontalAlignment: Text.AlignHCenter
            }

            // 没有地址：先说明在取 / 取不到，不要留一块纯黑
            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width - 2 * Tokens.pageMargin
                visible: !page.ready
                spacing: Tokens.space2
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: page.mv.name || "MV"
                    font.pixelSize: Tokens.text2xl
                    color: "white"
                    elide: Text.ElideRight
                    Layout.maximumWidth: parent.width
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: Source.live ? (page.mv.id > 0 ? "正在取回 MV…" : "没有可播放的 MV") : "原型数据里没有 MV 播放地址"
                    font.pixelSize: Tokens.textSm
                    color: Qt.alpha("white", Tokens.muted)
                }
            }

            // 底部控制条压在画面上：半透明黑底，白字，不跟系统主题
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: Tokens.height12
                radius: Tokens.radius
                color: Qt.alpha("black", Tokens.desktopBacking)
                visible: page.ready

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Tokens.space2
                    anchors.rightMargin: Tokens.space3
                    spacing: Tokens.space2

                    IconButton {
                        icon.color: "white"
                        icon.name: video.playbackState === MediaPlayer.PlayingState
                            ? "media-playback-pause" : "media-playback-start"
                        text: video.playbackState === MediaPlayer.PlayingState ? "暂停" : "播放"
                        onClicked: video.playbackState === MediaPlayer.PlayingState ? video.pause() : video.play()
                    }
                    Label {
                        text: Format.elapsed(video.position / 1000)
                        color: "white"
                        font.pixelSize: Tokens.textSm
                        font.features: { "tnum": 1 }
                    }
                    Slider {
                        id: seek
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(1, video.duration)
                        onMoved: video.position = value
                        Binding {
                            target: seek
                            property: "value"
                            value: video.position
                            when: !seek.pressed
                        }
                    }
                    Label {
                        text: Format.duration(video.duration / 1000)
                        color: "white"
                        font.pixelSize: Tokens.textSm
                        font.features: { "tnum": 1 }
                    }
                }
            }
        }

        ColumnLayout {
            id: info
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            spacing: Tokens.space1

            Label {
                Layout.fillWidth: true
                text: page.mv.name || "MV"
                font.pixelSize: Tokens.text2xl
                font.weight: Font.Light
                elide: Text.ElideRight
            }
            // 歌手可点（去歌手页），播放次数只是文字
            LinkText {
                Layout.fillWidth: true
                visible: parts.length > 0
                separator: " · "
                parts: [{ text: page.mv.artist || "", artistId: page.mv.artistId || 0 },
                        { text: page.mv.playCount > 0 ? page.formatCount(page.mv.playCount) + " 次播放" : "" }].filter(p => p.text.length > 0)
                font.pixelSize: Tokens.textSm
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                elide: Text.ElideRight
            }
        }
    }

    function formatCount(n) {
        return n >= 10000 ? (n / 10000).toFixed(n >= 100000 ? 0 : 1) + " 万" : String(n);
    }
}
