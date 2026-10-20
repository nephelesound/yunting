// 图标按钮：固定 32×32、图标 16。图标比按钮边缘内缩 space2，
// 所以贴页边的图标按钮用 pageMargin − space2 定位，让图形本身落在对齐线上。
// 图标自己画：主题图标交给主题着色；补充图标集的文件图标（icon.source）按蒙版用 icon.color 着色，
// 深浅配色下都对（样式自带的按钮不给文件图标上色，深色配色下会是黑的）。
// glyph：用一个字代替图标（桌面歌词的「词」：网易云的习惯，比任何图形都直白）。
// 底也自己画（与资源行、账号按钮同一种悬停底）：Breeze 的按钮底会按 icon 再画一遍原生图标，
// 在 1.15 这类小数缩放下两份图标对不齐像素，看起来像重影
import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

ToolButton {
    id: button

    property string glyph

    implicitWidth: Tokens.height8
    implicitHeight: Tokens.height8
    padding: Tokens.space2
    icon.width: Tokens.iconSm
    icon.height: Tokens.iconSm
    display: AbstractButton.IconOnly

    background: Rectangle {
        radius: Tokens.radius
        color: Qt.alpha(Kirigami.Theme.highlightColor,
                        button.down ? Tokens.tintPressed : button.hovered ? Tokens.tintHover : 0)
        border.width: button.visualFocus ? Tokens.lineStrong : 0
        border.color: Kirigami.Theme.highlightColor
        Behavior on color { ColorAnimation { duration: Tokens.motionFast } }
    }

    contentItem: Item {
        implicitWidth: Tokens.iconSm
        implicitHeight: Tokens.iconSm
        Kirigami.Icon {
            anchors.fill: parent
            visible: button.glyph === ""
            source: button.icon.name !== "" ? button.icon.name : button.icon.source
            isMask: button.icon.name === ""
            color: button.icon.color.a > 0 ? button.icon.color : Kirigami.Theme.textColor
        }
        Label {
            anchors.centerIn: parent
            visible: button.glyph !== ""
            text: button.glyph
            font.pixelSize: Tokens.textSm
            font.weight: Font.DemiBold
            color: button.icon.color.a > 0 ? button.icon.color : Kirigami.Theme.textColor
        }
    }

    ToolTip.text: text
    ToolTip.visible: hovered
    ToolTip.delay: 600
}
