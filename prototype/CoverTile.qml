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
    focusPolicy: Qt.StrongFocus
    Accessible.name: tile.name
    ToolTip.text: tile.name + "\n" + tile.detail
    ToolTip.visible: hovered
    ToolTip.delay: 600

    contentItem: Item {
        Cover {
            anchors.fill: parent
            source: tile.cover
        }
        // 悬停/按下：强调色淡色调；键盘焦点：强调色描边（与 MediaTile 一致）
        Rectangle {
            anchors.fill: parent
            radius: Tokens.radius
            color: Qt.alpha(Kirigami.Theme.highlightColor, tile.down ? Tokens.tintPressed : tile.hovered ? Tokens.tintHover : 0)
            Behavior on color { ColorAnimation { duration: Kirigami.Units.shortDuration } }
        }
        Rectangle {
            anchors.fill: parent
            radius: Tokens.radius
            color: "transparent"
            border.width: Tokens.lineStrong
            border.color: Kirigami.Theme.highlightColor
            visible: tile.visualFocus
        }
    }
}
