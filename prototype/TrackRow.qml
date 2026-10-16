// 歌曲行：同质、大量 → 行式 delegate（分隔线 + hover + 当前播放高亮）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ItemDelegate {
    id: control

    required property int index
    required property var modelData

    property bool current: false
    property bool playing: false

    signal activated(int index)

    hoverEnabled: true
    highlighted: control.current

    contentItem: RowLayout {
        spacing: Kirigami.Units.largeSpacing

        // 序号 / 当前播放指示（真实音乐应用的位置语义）
        Item {
            Layout.preferredWidth: Kirigami.Units.gridUnit * 2
            implicitHeight: Kirigami.Units.iconSizes.smallMedium

            Label {
                anchors.centerIn: parent
                visible: !control.current
                text: control.index + 1
                color: Kirigami.Theme.disabledTextColor
                horizontalAlignment: Text.AlignRight
                font: Kirigami.Theme.smallFont
            }

            Kirigami.Icon {
                anchors.centerIn: parent
                visible: control.current
                source: control.playing ? "media-playback-start-symbolic" : "media-playback-pause-symbolic"
                color: Kirigami.Theme.highlightColor
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: Kirigami.Units.iconSizes.small
            }
        }

        Label {
            Layout.fillWidth: true
            text: control.modelData.name
            elide: Text.ElideRight
            color: control.current ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
        }

        Label {
            Layout.preferredWidth: Kirigami.Units.gridUnit * 10
            visible: control.width > Kirigami.Units.gridUnit * 34
            text: control.modelData.artist
            color: Kirigami.Theme.disabledTextColor
            elide: Text.ElideRight
        }

        Label {
            Layout.preferredWidth: Kirigami.Units.gridUnit * 12
            visible: control.width > Kirigami.Units.gridUnit * 46
            text: control.modelData.album
            color: Kirigami.Theme.disabledTextColor
            elide: Text.ElideRight
        }

        Label {
            Layout.preferredWidth: Kirigami.Units.gridUnit * 3
            horizontalAlignment: Text.AlignRight
            text: {
                var m = Math.floor(control.modelData.dur / 60);
                var s = control.modelData.dur % 60;
                return m + ":" + (s < 10 ? "0" : "") + s;
            }
            color: Kirigami.Theme.disabledTextColor
            font: Kirigami.Theme.smallFont
        }
    }

    onClicked: control.activated(control.index)
}
