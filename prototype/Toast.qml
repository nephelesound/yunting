// 提示条：一句话（失败原因、操作结果），可带一个动作（「扫码登录」）。
// 用 KDE 原生的 Kirigami.InlineMessage（失败是 Error 配色，其余是 Information），浮在页头那一条的正中：
// 眼睛在页面上方，不挡播放条的控制，也不压住页面内容的第一行。
// 新消息直接替换旧的；指针停在上面时不消失，可以点 × 关掉。出现与消失由 InlineMessage 自己的动画负责。
import QtQuick
import org.kde.kirigami as Kirigami

Item {
    id: toast

    property string message
    property string actionText
    property var action: null
    property bool failure: false

    // timeout 是 "short" / "long" 或毫秒（与 Kirigami.ApplicationWindow.showPassiveNotification 相同）；失败默认长一些
    function show(message, timeout, actionText, action, failure) {
        toast.message = message;
        toast.actionText = actionText || "";
        toast.action = action || null;
        toast.failure = failure === true;
        hide.interval = typeof timeout === "number" ? timeout
            : timeout === "long" || toast.failure ? 6000 : 3500;
        bar.visible = true;
        hide.restart();
    }

    Timer {
        id: hide
        running: bar.visible && !hover.hovered
        onTriggered: bar.visible = false
    }

    // InlineMessage 的 implicitWidth 不含一行文字的宽度（它按给定宽度折行），宽度按文字量出来：
    // 文字 + 类型图标、关闭按钮与内边距 + 动作按钮
    TextMetrics {
        id: messageWidth
        font: Kirigami.Theme.defaultFont
        text: toast.message
    }
    TextMetrics {
        id: actionWidth
        font: Kirigami.Theme.defaultFont
        text: toast.actionText
    }

    Kirigami.InlineMessage {
        id: bar
        // 页头一行（ScreenFrame：上边距 space4，高 height12）的正中
        anchors.horizontalCenter: parent.horizontalCenter
        y: Tokens.space4 + Math.max(0, (Tokens.height12 - height) / 2)
        width: Math.min(Math.ceil(messageWidth.advanceWidth) + Tokens.iconMd + Tokens.height8 + 4 * Tokens.space3
                        + (toast.actionText ? Math.ceil(actionWidth.advanceWidth) + 2 * Tokens.space4 + Tokens.space2 : 0),
                        parent.width - 2 * Tokens.pageMargin)
        visible: false
        type: toast.failure ? Kirigami.MessageType.Error : Kirigami.MessageType.Information
        text: toast.message
        showCloseButton: true
        actions: Kirigami.Action {
            visible: toast.actionText.length > 0
            text: toast.actionText
            onTriggered: {
                bar.visible = false;
                if (toast.action)
                    toast.action();
            }
        }

        HoverHandler { id: hover }
    }
}
