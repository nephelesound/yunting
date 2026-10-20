// 云听的滚动条：Breeze 原生的形（8 像素宽的圆角把手，内容放得下时才出现、出现了就一直在，
// 悬停或拖动时换成强调色），但不要它的槽和左边那条分隔线：放在没有边框、底色随封面变化的页面上，
// 那条线像一道接缝。可以拖动，也可以点槽的位置翻页。
import QtQuick
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami

T.ScrollBar {
    id: bar

    readonly property bool emphasized: bar.hovered || bar.pressed

    // 点按区域比把手宽，好点中；把手在其中居中
    implicitWidth: bar.horizontal ? 0 : Tokens.space2 + 2 * Tokens.space1
    implicitHeight: bar.horizontal ? Tokens.space2 + 2 * Tokens.space1 : 0
    padding: Tokens.space1
    minimumSize: 0.1
    hoverEnabled: true
    visible: bar.size < 1.0
    policy: T.ScrollBar.AlwaysOn

    contentItem: Rectangle {
        implicitWidth: Tokens.space2
        implicitHeight: Tokens.space2
        radius: Tokens.radius
        color: bar.emphasized ? Qt.alpha(Kirigami.Theme.highlightColor, Tokens.scrollHandleActive)
                              : Qt.alpha(Kirigami.Theme.textColor, Tokens.scrollHandle)
        Behavior on color { ColorAnimation { duration: Tokens.motionFast } }
    }
}
