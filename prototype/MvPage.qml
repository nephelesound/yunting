// MV：黑色画面区 + 底部一条播放控制。数据来自 Store 的 mv:<id>（详情 + 播放地址）。
// 画面之外只保留标题、歌手与播放量；这是观看模式，不做弹幕/评论。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Format.js" as Format

ScreenFrame {
    id: page

    property real mvId
    readonly property var mv: Source.mv(page.mvId)
    readonly property bool ready: page.mv.url !== ""
    // 播放器是应用注册的 MvVideo（mpv 画进 QML）。原型没有它，也没有播放地址，画面区只显示说明
    readonly property var player: video.item
    // 正在出画面：main.qml 据此让屏幕常亮（backend.power.keepScreenOn）
    readonly property bool videoPlaying: page.player !== null && page.player.playing

    heading: page.mv.name || "MV"
    // 播放地址是签名的、会过期：每次打开都向后端要一次（详情仍走缓存，地址重新取）
    Component.onCompleted: Source.need("mv:" + page.mvId)
    Component.onDestruction: if (page.player) page.player.stop()

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

            Loader {
                id: video
                anchors.fill: parent
                sourceComponent: Source.live ? Qt.createComponent("org.nephelesound.yunting", "MvVideo") : null
            }
            Binding {
                when: page.player !== null
                target: page.player
                property: "source"
                value: page.mv.url
            }

            // 解码失败时给出原因，而不是一直黑屏
            Label {
                anchors.centerIn: parent
                visible: text.length > 0
                text: page.player ? page.player.errorString : ""
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
                // 没取到（可点重试）/ 详情到了但没有可播放的地址（会员、版权、下架）/ 还在取
                LinkText {
                    Layout.alignment: Qt.AlignHCenter
                    readonly property bool failed: Boolean(Source.failure("mv:" + page.mvId))
                    readonly property bool unplayable: page.mv.name !== "" && page.mv.urlPending === false
                    parts: [{ text: !Source.live ? "原型数据里没有 MV 播放地址"
                                  : failed ? "MV 没取到，点此重试"
                                  : unplayable ? "这个 MV 暂时不能播放（可能需要会员，或已下架）"
                                  : "正在取回 MV…",
                              link: failed }]
                    font.pixelSize: Tokens.textSm
                    color: Qt.alpha("white", Tokens.muted)
                    onActivated: Source.retryKey("mv:" + page.mvId)
                }
            }

            // 底部控制条压在画面上：半透明黑底，白字，不跟系统主题
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: Tokens.height12
                radius: Tokens.radius
                color: Qt.alpha("black", Tokens.videoScrim)
                visible: page.ready

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Tokens.space2
                    anchors.rightMargin: Tokens.space3
                    spacing: Tokens.space2

                    IconButton {
                        icon.color: "white"
                        icon.name: page.videoPlaying ? "media-playback-pause" : "media-playback-start"
                        text: page.videoPlaying ? "暂停" : "播放"
                        onClicked: page.videoPlaying ? page.player.pause() : page.player.play()
                    }
                    Label {
                        text: Format.elapsed(seek.value / 1000)
                        color: "white"
                        font.pixelSize: Tokens.textSm
                        font.features: { "tnum": 1 }
                    }
                    Slider {
                        id: seek
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(1, seek.duration)
                        readonly property real duration: page.player ? page.player.duration : 0
                        onMoved: page.player.position = value
                        Binding {
                            target: seek
                            property: "value"
                            value: page.player ? page.player.position : 0
                            when: !seek.pressed
                        }
                    }
                    Label {
                        text: Format.duration(seek.duration / 1000)
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
                        { text: page.mv.playCount > 0 ? Format.count(page.mv.playCount) + " 次播放" : "" }].filter(p => p.text.length > 0)
                font.pixelSize: Tokens.textSm
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                elide: Text.ElideRight
            }
        }
    }
}
