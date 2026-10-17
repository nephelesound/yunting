// 曲目标题后的小标签（VIP / 付费 / Hi-Res / 无损）：强调色淡色调底 + 12px 正文色字。
// 字不用强调色：12px 的强调色字在 Breeze 亮色下对比度不够（与正在播放的标题同一条规则）。
// muted：中性底与弱化字。锁定的曲目（匿名下的 VIP），以及不是曲目属性的推荐理由。
import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Rectangle {
    id: tag

    property string text
    property bool muted
    property real maximumWidth: Infinity   // 超出时文字省略

    visible: tag.text.length > 0
    implicitWidth: Math.min(label.implicitWidth + 2 * Tokens.space1, tag.maximumWidth)
    implicitHeight: label.implicitHeight
    radius: Tokens.radiusSm
    color: Qt.alpha(tag.muted ? Kirigami.Theme.textColor : Kirigami.Theme.highlightColor, Tokens.tonal)

    Label {
        id: label
        anchors.centerIn: parent
        width: Math.min(implicitWidth, tag.width - 2 * Tokens.space1)
        elide: Text.ElideRight
        text: tag.text
        font.pixelSize: Tokens.textXs
        font.weight: Font.Medium
        color: tag.muted ? Qt.alpha(Kirigami.Theme.textColor, Tokens.muted) : Kirigami.Theme.textColor
    }
}
