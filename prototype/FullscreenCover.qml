// 全屏封面：只留封面、曲名、歌手和最少控制。压在整窗之上，F11 或播放条「更多」打开。
// 背景复用正在播放页的封面氛围（预模糊 32px + 系统底色遮罩），不新增实时模糊。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Format.js" as Format

Item {
    id: coverPage

    property var track
    property bool playing
    property real elapsed

    signal closeRequested()
    signal toggleRequested()
    signal previousRequested()
    signal nextRequested()

    visible: false
    z: 100

    CoverAmbience {
        anchors.fill: parent
        cover: coverPage.track ? coverPage.track.cover : ""
    }

    // 右上角关闭：不抢封面的视觉焦点
    IconButton {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Tokens.space4
        icon.name: "window-close-symbolic"
        text: "退出全屏封面"
        onClicked: coverPage.closeRequested()
    }

    // 底部一条进度与时间：全屏仍然知道播到哪儿
    Item {
        id: progressArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Tokens.space6 * 2
        anchors.rightMargin: Tokens.space6 * 2
        anchors.bottomMargin: Tokens.space4
        height: Tokens.height8

        readonly property real duration: coverPage.track ? coverPage.track.dur : 0

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: Tokens.line
            color: Qt.alpha(Kirigami.Theme.textColor, Tokens.track)
            Rectangle {
                width: progressArea.duration > 0
                    ? parent.width * Math.min(1, coverPage.elapsed / progressArea.duration) : 0
                height: parent.height
                color: Kirigami.Theme.highlightColor
            }
        }
        Label {
            anchors.right: parent.right
            anchors.bottom: parent.top
            anchors.bottomMargin: Tokens.space1
            text: Format.elapsed(coverPage.elapsed) + " / " + Format.duration(progressArea.duration)
            font.pixelSize: Tokens.textXs
            font.features: { "tnum": 1 }
            color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Tokens.space6 * 2
        anchors.bottomMargin: Tokens.space6
        spacing: Tokens.space6 * 2

        Cover {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: Math.min(parent.height - 2 * Tokens.space6, parent.width * 0.55)
            Layout.preferredHeight: Layout.preferredWidth
            source: coverPage.track ? Source.sized(coverPage.track.cover, 800) : ""
            pixelSize: 800
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Tokens.space3

            Label {
                Layout.fillWidth: true
                text: coverPage.track ? coverPage.track.name : "未在播放"
                font.pixelSize: Tokens.text4xl
                font.weight: Font.Light
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: coverPage.track ? coverPage.track.artist : ""
                font.pixelSize: Tokens.text2xl
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                visible: text.length > 0
                text: coverPage.track ? coverPage.track.album : ""
                font.pixelSize: Tokens.textBase
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                elide: Text.ElideRight
            }

            RowLayout {
                Layout.topMargin: Tokens.space4
                spacing: Tokens.space4
                IconButton {
                    icon.name: "media-skip-backward"
                    text: "上一首"
                    onClicked: coverPage.previousRequested()
                }
                RoundButton {
                    implicitWidth: Tokens.height10
                    implicitHeight: Tokens.height10
                    text: coverPage.playing ? "暂停" : "播放"
                    display: AbstractButton.IconOnly
                    icon.name: coverPage.playing ? "media-playback-pause" : "media-playback-start"
                    onClicked: coverPage.toggleRequested()
                    scale: down ? 0.95 : 1
                    Behavior on scale { NumberAnimation { duration: Tokens.motionFast; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut } }
                }
                IconButton {
                    icon.name: "media-skip-forward"
                    text: "下一首"
                    onClicked: coverPage.nextRequested()
                }
            }
        }
    }
}
