// 分享链接的去处。这里刻意不走 Purpose 的 Export 插件：Export 的 inputData 语义是
// 本地文件列表（例如 Telegram 会用 -sendpath 打开），把 https URL 塞进 urls 会被
// 当成一个未知文件。链接分享改用各服务自己的 URL scheme。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: targets

    property string url
    property string title
    property string text
    property string resultText
    property bool resultOk: true

    spacing: Tokens.space2

    function telegramUrl() {
        return "https://t.me/share/url?url=" + encodeURIComponent(targets.url)
            + "&text=" + encodeURIComponent(targets.text || targets.title);
    }
    function mailUrl() {
        return "mailto:?subject=" + encodeURIComponent(targets.title)
            + "&body=" + encodeURIComponent((targets.text || targets.title) + "\n" + targets.url);
    }
    function launch(kind) {
        const target = kind === "telegram" ? targets.telegramUrl() : targets.mailUrl();
        targets.resultText = "";
        if (!Qt.openUrlExternally(target)) {
            targets.resultOk = false;
            targets.resultText = kind === "telegram" ? "没有可用的 Telegram 处理器" : "没有可用的邮件程序";
        }
    }

    // 一行：「发送到」和两个去处，与上面复制链接的那排按钮同一种样子
    RowLayout {
        spacing: Tokens.space2
        Label {
            Layout.rightMargin: Tokens.space2
            text: "发送到"
            font.pixelSize: Tokens.textSm
            color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
        }
        Repeater {
            model: [
                { key: "telegram", title: "Telegram", icon: "org.telegram.desktop" },
                { key: "email", title: "邮件", icon: "mail-send-symbolic" },
            ]
            delegate: Button {
                id: target
                required property var modelData
                implicitHeight: Tokens.height8
                icon.name: target.modelData.icon
                icon.width: Tokens.iconSm
                icon.height: Tokens.iconSm
                text: target.modelData.title
                font.pixelSize: Tokens.textSm
                onClicked: targets.launch(target.modelData.key)
            }
        }
    }

    Label {
        visible: text.length > 0
        text: targets.resultText
        font.pixelSize: Tokens.textSm
        color: targets.resultOk ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor
    }
}
