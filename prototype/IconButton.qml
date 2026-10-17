// 图标按钮：固定 32×32、图标 16。图标比按钮边缘内缩 space2，
// 所以贴页边的图标按钮用 pageMargin − space2 定位，让图形本身落在对齐线上。
// 图标自己画：主题图标交给主题着色；补充图标集的文件图标（icon.source）按蒙版用 icon.color 着色，
// 深浅配色下都对（样式自带的按钮不给文件图标上色，深色配色下会是黑的）。
// glyph：用一个字代替图标（桌面歌词的「词」：网易云的习惯，比任何图形都直白）。
// corner：图标右上角的小数字（评论数，网易云的写法）：不是徽章，没有底色，按钮按数字的宽度变宽，不压到旁边。
import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

ToolButton {
    id: button

    property string glyph
    property string corner

    implicitWidth: leftPadding + Tokens.iconSm + rightPadding
    implicitHeight: Tokens.height8
    padding: Tokens.space2
    rightPadding: button.corner ? Math.ceil(cornerLabel.implicitWidth) + Tokens.space1 : Tokens.space2
    icon.width: Tokens.iconSm
    icon.height: Tokens.iconSm
    display: AbstractButton.IconOnly

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
        // 上标：贴着图标右上角，基线略高于图标顶部
        Label {
            id: cornerLabel
            visible: button.corner !== ""
            x: parent.width - 1
            y: -Tokens.space1
            text: button.corner
            font.pixelSize: Tokens.textCorner
            font.features: { "tnum": 1 }
            color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
        }
    }

    ToolTip.text: text
    ToolTip.visible: hovered
    ToolTip.delay: 600
}
