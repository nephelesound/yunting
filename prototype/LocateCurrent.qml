// 「定位到正在播放」：浮在列表右下角的小按钮。正在播放的那一行完全滚出视野时才出现，
// 点一下把那一行放到列表中间。长距离直接跳过去（几千像素的滚动动画只会让人晕），按钮本身淡入淡出。
// 用法：在 ListView 里声明，parent 设为这个 ListView（不进内容区，不随列表滚动）。
import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

AbstractButton {
    id: locate

    required property ListView view
    property int index: -1          // 正在播放的那一行；-1 = 不在这个列表里

    // 那一行是否有一部分在视野里（没实例化的行肯定不在）
    readonly property bool inView: {
        void locate.view.contentY;
        void locate.view.height;
        const row = locate.index >= 0 ? locate.view.itemAtIndex(locate.index) : null;
        return row !== null && row.y + row.height > locate.view.contentY
            && row.y < locate.view.contentY + locate.view.height;
    }

    anchors.right: parent.right
    anchors.bottom: parent.bottom
    anchors.rightMargin: locate.view.rightMargin + Tokens.space2
    anchors.bottomMargin: Tokens.space4
    width: Tokens.height10
    height: Tokens.height10
    hoverEnabled: true
    visible: opacity > 0
    opacity: locate.index >= 0 && !locate.inView ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: Tokens.motionBase; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut } }
    Accessible.name: "定位到正在播放"
    ToolTip.text: "定位到正在播放"
    ToolTip.visible: hovered
    ToolTip.delay: 600
    onClicked: locate.view.positionViewAtIndex(locate.index, ListView.Center)

    // 真正浮在列表之上：圆形底 + 系统阴影
    background: Kirigami.ShadowedRectangle {
        radius: width / 2
        color: Qt.tint(Kirigami.Theme.backgroundColor,
                       Qt.alpha(Kirigami.Theme.highlightColor, locate.down ? Tokens.tintPressed : locate.hovered ? Tokens.tintHover : 0))
        border.width: locate.visualFocus ? Tokens.lineStrong : Tokens.line
        border.color: locate.visualFocus ? Kirigami.Theme.highlightColor : Qt.alpha(Kirigami.Theme.textColor, Tokens.track)
        shadow.size: Tokens.space2
        shadow.color: Qt.alpha("black", Tokens.track)
        shadow.yOffset: Tokens.line
    }
    contentItem: Item {
        Kirigami.Icon {
            anchors.centerIn: parent
            width: Tokens.iconSm
            height: Tokens.iconSm
            source: "find-location-symbolic"
            color: Kirigami.Theme.highlightColor
            isMask: true
        }
    }
}
