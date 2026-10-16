// 图标按钮：固定 32×32、图标 16。图标比按钮边缘内缩 space2，
// 所以贴页边的图标按钮用 pageMargin − space2 定位，让图形本身落在对齐线上。
import QtQuick
import QtQuick.Controls

ToolButton {
    implicitWidth: Tokens.height8
    implicitHeight: Tokens.height8
    padding: Tokens.space2
    icon.width: Tokens.iconSm
    icon.height: Tokens.iconSm
    display: AbstractButton.IconOnly

    ToolTip.text: text
    ToolTip.visible: hovered
    ToolTip.delay: 600
}
