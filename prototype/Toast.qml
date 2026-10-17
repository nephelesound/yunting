// 提示条：一句话（失败原因、操作结果），可带一个动作（「扫码登录」）。
// 浮在播放条上方居中，不挡播放控制；与应用里其它浮层同一种面（系统底色、圆角、1px 边框、投影）。
// 新消息直接替换旧的；指针停在上面时不消失。进出：淡入并略微上移（只用动效档位，系统关掉动画时就是瞬间）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Item {
    id: toast

    property real bottomInset   // 播放条的高度：提示条落在它上面

    property string message
    property string actionText
    property var action: null
    property bool shown: false

    // 与 Kirigami.ApplicationWindow.showPassiveNotification 同样的参数：timeout 是 "short" / "long" 或毫秒
    function show(message, timeout, actionText, action) {
        toast.message = message;
        toast.actionText = actionText || "";
        toast.action = action || null;
        hide.interval = typeof timeout === "number" ? timeout : timeout === "long" ? 6000 : 3500;
        toast.shown = true;
        hide.restart();
    }

    Timer {
        id: hide
        running: toast.shown && !hover.hovered
        onTriggered: toast.shown = false
    }

    Kirigami.ShadowedRectangle {
        id: surface
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height - toast.bottomInset - height - Tokens.space4 + (toast.shown ? 0 : Tokens.space2)
        width: Math.min(row.implicitWidth + 2 * Tokens.space4, parent.width - 2 * Tokens.pageMargin)
        height: Tokens.height10
        radius: Tokens.radiusLg
        color: Kirigami.Theme.backgroundColor
        border.width: Tokens.line
        border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
        shadow.size: Tokens.space2
        shadow.yOffset: Tokens.space1
        shadow.color: Qt.rgba(0, 0, 0, 0.3)
        opacity: toast.shown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Tokens.motionBase; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut } }
        Behavior on y { NumberAnimation { duration: Tokens.motionBase; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut } }

        Kirigami.Theme.colorSet: Kirigami.Theme.View
        Kirigami.Theme.inherit: false

        HoverHandler { id: hover }

        RowLayout {
            id: row
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Tokens.space4
            anchors.rightMargin: toast.actionText ? Tokens.space1 : Tokens.space4
            spacing: Tokens.space3
            Label {
                Layout.fillWidth: true
                text: toast.message
                font.pixelSize: Tokens.textBase
                elide: Text.ElideRight
            }
            Button {
                visible: toast.actionText.length > 0
                flat: true
                implicitHeight: Tokens.height8
                text: toast.actionText
                onClicked: {
                    toast.shown = false;
                    if (toast.action)
                        toast.action();
                }
            }
        }
    }
}
