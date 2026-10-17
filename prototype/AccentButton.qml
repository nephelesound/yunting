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
    opacity: enabled ? 1 : Tokens.fadedHover

    // 悬停 / 按下与曲目行同一套档位：在强调色上叠一层前景色的 tintHover / tintPressed；
    // 键盘焦点：外圈 2px 强调色描边，与实底之间隔 2px 背景色，才看得出来
    background: Rectangle {
        radius: Tokens.radius
        color: Qt.tint(Kirigami.Theme.highlightColor, Qt.alpha(Kirigami.Theme.highlightedTextColor,
                       button.down ? Tokens.tintPressed : button.hovered ? Tokens.tintHover : 0))
        Behavior on color { ColorAnimation { duration: Tokens.motionFast } }
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2 * Tokens.lineStrong
            visible: button.visualFocus
            radius: Tokens.radiusMd
            color: "transparent"
            border.width: Tokens.lineStrong
            border.color: Kirigami.Theme.highlightColor
        }
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
