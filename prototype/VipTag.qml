// 会员标：名字后面。level 是 Store 给的 vipLevel："svip" | "vip" | ""（不是会员不占位置）。
// VIP 用与其他标签同一种样子；SVIP 用强调色的渐变底配强调色上的字：跟着系统配色走，一眼分得出两档。
// 只做静止的渐变，不做流光动画：评论区一屏几十个，动起来就是噪音
import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Rectangle {
    id: badge

    property string level

    readonly property bool svip: badge.level === "svip"

    visible: badge.level === "svip" || badge.level === "vip"
    implicitWidth: label.implicitWidth + 2 * Tokens.space1
    implicitHeight: label.implicitHeight
    radius: Tokens.radiusSm
    color: badge.svip ? Kirigami.Theme.highlightColor : Qt.alpha(Kirigami.Theme.highlightColor, Tokens.tonal)
    gradient: badge.svip ? sheen : null

    // 从强调色的亮一档斜到它本身：有一点光泽，又仍是同一种颜色
    Gradient {
        id: sheen
        orientation: Gradient.Horizontal
        GradientStop { position: 0; color: Qt.lighter(Kirigami.Theme.highlightColor, 1.35) }
        GradientStop { position: 1; color: Kirigami.Theme.highlightColor }
    }

    Accessible.role: Accessible.StaticText
    Accessible.name: badge.svip ? "黑胶 SVIP" : "黑胶 VIP"

    Label {
        id: label
        anchors.centerIn: parent
        text: badge.svip ? "SVIP" : "VIP"
        font.pixelSize: Tokens.textXs
        font.weight: badge.svip ? Font.DemiBold : Font.Medium
        color: badge.svip ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
    }
}
