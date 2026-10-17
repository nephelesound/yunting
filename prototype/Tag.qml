// 曲目标题后的小标签（VIP / 付费 / Hi-Res / 无损）：淡色调底 + 12px 字。
// muted：锁定的曲目（匿名下的 VIP）用中性色，其余用强调色。
import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Rectangle {
    id: tag

    property string text
    property bool muted

    visible: tag.text.length > 0
    implicitWidth: label.implicitWidth + 2 * Tokens.space1
    implicitHeight: label.implicitHeight
    radius: Tokens.radiusSm
    color: Qt.alpha(tag.muted ? Kirigami.Theme.textColor : Kirigami.Theme.highlightColor, Tokens.tonal)

    Label {
        id: label
        anchors.centerIn: parent
        text: tag.text
        font.pixelSize: Tokens.textXs
        font.weight: Font.Medium
        color: tag.muted ? Kirigami.Theme.disabledTextColor : Kirigami.Theme.highlightColor
    }
}
