// 会员标：名字后面。level 是 Store 给的 vipLevel："svip" | "vip" | ""（不是会员不占位置）。
// VIP 用与其他标签同一种样子；黑胶 SVIP 用网易的黑金（Tokens.svip*），一眼分得出两档。
// 只做静止的光泽，不做流光动画：评论区一屏几十个，动起来就是噪音
import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Rectangle {
    id: badge

    property string level

    readonly property bool svip: badge.level === "svip"
    readonly property bool darkTheme: Kirigami.ColorUtils.brightnessForColor(Kirigami.Theme.backgroundColor) === Kirigami.ColorUtils.Dark

    visible: badge.level === "svip" || badge.level === "vip"
    implicitWidth: label.implicitWidth + 2 * Tokens.space1
    implicitHeight: label.implicitHeight
    radius: Tokens.radiusSm
    color: badge.svip ? Tokens.svipDeep : Qt.alpha(Kirigami.Theme.highlightColor, Tokens.tonal)
    gradient: badge.svip ? sheen : null
    border.width: badge.svip && badge.darkTheme ? Tokens.line : 0
    border.color: Qt.alpha(Tokens.svipGold, Tokens.fadedHover)

    Gradient {
        id: sheen
        orientation: Gradient.Horizontal
        GradientStop { position: 0; color: Tokens.svipDeep }
        GradientStop { position: 0.45; color: Tokens.svipSheen }
        GradientStop { position: 1; color: Tokens.svipDeep }
    }

    Accessible.role: Accessible.StaticText
    Accessible.name: badge.svip ? "黑胶 SVIP" : "黑胶 VIP"

    Label {
        id: label
        anchors.centerIn: parent
        text: badge.svip ? "SVIP" : "VIP"
        font.pixelSize: Tokens.textXs
        font.weight: badge.svip ? Font.DemiBold : Font.Medium
        color: badge.svip ? Tokens.svipGold : Kirigami.Theme.textColor
    }
}
