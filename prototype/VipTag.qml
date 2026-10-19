// 会员标：名字后面。level 是 Store 给的 vipLevel："svip" | "vip" | ""（不是会员不占位置）。
// VIP 用与其他标签同一种样子（强调色淡底）；SVIP 是浓一档、斜着渐变的强调色淡底，字加粗：
// 跟着系统配色走，一眼分得出两档。字一律用正文色——实心强调色配白字在 Breeze 的亮蓝上只有 2.5:1，
// 12px 的字读不清。只做静止的渐变，不做流光动画：评论区一屏几十个，动起来就是噪音
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
    color: Qt.alpha(Kirigami.Theme.highlightColor, Tokens.tonal)
    gradient: badge.svip ? sheen : null

    Gradient {
        id: sheen
        orientation: Gradient.Horizontal
        GradientStop { position: 0; color: Qt.alpha(Kirigami.Theme.highlightColor, Tokens.tonalStrong) }
        GradientStop { position: 1; color: Qt.alpha(Kirigami.Theme.highlightColor, Tokens.tonal) }
    }

    Accessible.role: Accessible.StaticText
    Accessible.name: badge.svip ? "黑胶 SVIP" : "黑胶 VIP"

    Label {
        id: label
        anchors.centerIn: parent
        text: badge.svip ? "SVIP" : "VIP"
        font.pixelSize: Tokens.textXs
        font.weight: badge.svip ? Font.DemiBold : Font.Medium
        color: Kirigami.Theme.textColor
    }
}
