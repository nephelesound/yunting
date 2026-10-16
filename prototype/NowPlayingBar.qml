// 底部播放条：图标控件 + 进度 + 音量，全部用 Units/Theme 令牌，无硬编码像素。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Rectangle {
    id: bar
    color: Kirigami.Theme.backgroundColor

    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: 1
        color: Kirigami.Theme.textColor
        opacity: 0.12
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Kirigami.Units.largeSpacing
        anchors.rightMargin: Kirigami.Units.largeSpacing
        anchors.topMargin: Kirigami.Units.smallSpacing
        anchors.bottomMargin: Kirigami.Units.smallSpacing
        spacing: Kirigami.Units.largeSpacing

        // 当前曲目
        RowLayout {
            spacing: Kirigami.Units.smallSpacing

            Rectangle {
                Layout.preferredWidth: Kirigami.Units.iconSizes.large
                Layout.preferredHeight: Kirigami.Units.iconSizes.large
                radius: Kirigami.Units.smallSpacing
                color: Kirigami.Theme.alternateBackgroundColor
                clip: true

                Image {
                    anchors.fill: parent
                    source: bar.trackCover
                    asynchronous: true
                    cache: true
                    fillMode: Image.PreserveAspectCrop
                    sourceSize.width: 120
                    sourceSize.height: 120
                }
            }

            ColumnLayout {
                spacing: 0
                Layout.preferredWidth: Kirigami.Units.gridUnit * 11

                Label {
                    Layout.fillWidth: true
                    text: bar.trackTitle
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: bar.trackArtist
                    elide: Text.ElideRight
                    color: Kirigami.Theme.disabledTextColor
                    font: Kirigami.Theme.smallFont
                }
            }
        }

        // 传输控制
        RowLayout {
            spacing: 0

            ToolButton {
                icon.name: "media-skip-backward-symbolic"
                display: AbstractButton.IconOnly
                onClicked: bar.previousRequested()
            }
            ToolButton {
                icon.name: bar.playing ? "media-playback-pause-symbolic" : "media-playback-start-symbolic"
                display: AbstractButton.IconOnly
                onClicked: bar.playPauseRequested()
            }
            ToolButton {
                icon.name: "media-skip-forward-symbolic"
                display: AbstractButton.IconOnly
                onClicked: bar.nextRequested()
            }
        }

        // 进度
        Label {
            text: bar.positionText
            color: Kirigami.Theme.disabledTextColor
            font: Kirigami.Theme.smallFont
        }
        Slider {
            Layout.fillWidth: true
            from: 0
            to: Math.max(1, bar.duration)
            value: bar.position
            onMoved: bar.seekRequested(value)
        }
        Label {
            text: bar.durationText
            color: Kirigami.Theme.disabledTextColor
            font: Kirigami.Theme.smallFont
        }

        // 音量
        RowLayout {
            spacing: 0
            Kirigami.Icon {
                source: "audio-volume-high-symbolic"
                implicitWidth: Kirigami.Units.iconSizes.smallMedium
                implicitHeight: Kirigami.Units.iconSizes.smallMedium
                color: Kirigami.Theme.textColor
            }
            Slider {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                from: 0
                to: 1
                value: bar.volume
                onMoved: bar.volume = value
            }
        }
    }

    // ── 对外接口 ──
    property string trackTitle: ""
    property string trackArtist: ""
    property string trackCover: ""
    property bool playing: false
    property real position: 0
    property real duration: 1
    property real volume: 0.7

    readonly property string positionText: bar.formatTime(bar.position)
    readonly property string durationText: bar.formatTime(bar.duration)

    signal playPauseRequested()
    signal nextRequested()
    signal previousRequested()
    signal seekRequested(real seconds)

    function formatTime(seconds) {
        var total = Math.max(0, Math.floor(seconds));
        var m = Math.floor(total / 60);
        var s = total % 60;
        return m + ":" + (s < 10 ? "0" : "") + s;
    }
}
