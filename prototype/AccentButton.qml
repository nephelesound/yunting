// 页面唯一的主动作：强调色实底。其余动作一律用 IconButton / 平按钮。
import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Button {
    id: button

    implicitHeight: Tokens.height8
    leftPadding: Tokens.space4
    rightPadding: Tokens.space4
    hoverEnabled: true

    background: Rectangle {
        radius: Tokens.radius
        color: button.down ? Qt.darker(Kirigami.Theme.highlightColor, 1.15)
             : button.hovered ? Qt.lighter(Kirigami.Theme.highlightColor, 1.08)
             : Kirigami.Theme.highlightColor
        Behavior on color { ColorAnimation { duration: Kirigami.Units.shortDuration } }
    }

    contentItem: Row {
        spacing: Tokens.space2
        Kirigami.Icon {
            anchors.verticalCenter: parent.verticalCenter
            width: Tokens.iconSm
            height: Tokens.iconSm
            source: button.icon.name
            color: Kirigami.Theme.highlightedTextColor
            isMask: true
        }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: button.text
            font.pixelSize: Tokens.textBase
            font.weight: Font.Medium
            color: Kirigami.Theme.highlightedTextColor
        }
    }
}
