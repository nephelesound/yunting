// 封面磁贴：只有封面。网易的榜单封面自带榜名，不再叠字；名称给无障碍和悬停提示。
import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

AbstractButton {
    id: tile

    property string cover
    property string name
    property string detail

    hoverEnabled: true
    Accessible.name: tile.name
    ToolTip.text: tile.name + "\n" + tile.detail
    ToolTip.visible: hovered
    ToolTip.delay: 600

    contentItem: Item {
        Cover {
            anchors.fill: parent
            source: tile.cover
        }
        Rectangle {
            anchors.fill: parent
            radius: Tokens.radius
            color: "transparent"
            border.width: 2
            border.color: Kirigami.Theme.highlightColor
            visible: tile.hovered || tile.visualFocus
        }
    }
}
