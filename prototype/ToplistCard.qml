// 榜单封面卡：小、方形、真封面图。卡片只用在这里（独立可浏览的对象）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ItemDelegate {
    id: control

    required property int index
    required property var modelData

    signal activated(int index)

    width: Kirigami.Units.gridUnit * 7
    hoverEnabled: true

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: width
            radius: Kirigami.Units.smallSpacing
            color: Kirigami.Theme.alternateBackgroundColor
            clip: true

            Image {
                anchors.fill: parent
                source: control.modelData.cover
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: 300
                sourceSize.height: 300
            }
        }

        Label {
            Layout.fillWidth: true
            text: control.modelData.name
            font.weight: Font.Medium
            elide: Text.ElideRight
        }

        Label {
            Layout.fillWidth: true
            text: control.modelData.update
            color: Kirigami.Theme.disabledTextColor
            font: Kirigami.Theme.smallFont
            elide: Text.ElideRight
        }
    }

    onClicked: control.activated(control.index)
}
