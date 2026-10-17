// 可点的小封面（播放条、迷你播放器）：点了去另一个视图。
// 悬停 / 聚焦时封面上浮出一层系统底色淡底和方向图标，说明点了去哪；提示文字给出去处的名字。
import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

AbstractButton {
    id: button

    property string source
    property string hintIcon   // 去向：go-up-symbolic 展开、go-down-symbolic 收起……
    property string hint       // 提示文字

    implicitWidth: Tokens.height12
    implicitHeight: Tokens.height12
    hoverEnabled: true
    Accessible.name: button.hint
    ToolTip.text: button.hint
    ToolTip.visible: button.hovered && button.hint.length > 0
    ToolTip.delay: 600

    contentItem: Cover {
        source: button.source
        pixelSize: 96

        Rectangle {
            anchors.fill: parent
            radius: Tokens.radius
            color: Qt.alpha(Kirigami.Theme.backgroundColor, Tokens.coverScrim)
            opacity: button.enabled && (button.hovered || button.visualFocus) ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Tokens.motionFast } }
            Kirigami.Icon {
                anchors.centerIn: parent
                width: Tokens.iconMd
                height: Tokens.iconMd
                source: button.hintIcon
                color: Kirigami.Theme.textColor
                isMask: true
            }
        }
    }
}
