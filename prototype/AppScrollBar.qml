// 云听的滚动条：只有一条圆角把手，没有槽，也没有分隔线。
// Breeze 的原生滚动条在鼠标移到列表上时展开成带槽的样式，并在左边画一条分隔线：放在没有边框、
// 底色随封面变化的页面上，那条线像一道接缝。这里只画把手：平时细，悬停或拖动时变宽、变深；
// 滚动时出现，停下后淡出（悬停时一直在）；内容放得下时不出现。可以拖动，也可以点槽的位置翻页。
import QtQuick
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami

T.ScrollBar {
    id: bar

    readonly property bool emphasized: bar.hovered || bar.pressed

    // 点按区域比把手宽，好点中；把手贴着右（下）缘
    implicitWidth: bar.horizontal ? 0 : Tokens.space3
    implicitHeight: bar.horizontal ? Tokens.space3 : 0
    minimumSize: 0.1
    hoverEnabled: true
    visible: bar.size < 1.0
    policy: T.ScrollBar.AsNeeded

    contentItem: Item {
        implicitWidth: Tokens.space2
        implicitHeight: Tokens.space2
        Rectangle {
            readonly property real thickness: bar.emphasized ? Tokens.space2 : Tokens.space1
            anchors.right: bar.horizontal ? undefined : parent.right
            anchors.bottom: bar.horizontal ? parent.bottom : undefined
            width: bar.horizontal ? parent.width : thickness
            height: bar.horizontal ? thickness : parent.height
            radius: thickness / 2
            color: Qt.alpha(Kirigami.Theme.textColor, bar.emphasized ? Tokens.scrollHandleActive : Tokens.scrollHandle)
            opacity: bar.policy === T.ScrollBar.AlwaysOn || bar.active || bar.emphasized ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Tokens.motionBase; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut } }
            Behavior on width { enabled: !bar.horizontal; NumberAnimation { duration: Tokens.motionFast } }
            Behavior on height { enabled: bar.horizontal; NumberAnimation { duration: Tokens.motionFast } }
            Behavior on color { ColorAnimation { duration: Tokens.motionFast } }
        }
    }
}
