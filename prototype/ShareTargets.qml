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

    Label {
        text: "发送到"
        font.pixelSize: Tokens.textSm
        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
    }

    Repeater {
        model: [
            { key: "telegram", title: "通过 Telegram 发送", icon: "org.telegram.desktop" },
            { key: "email", title: "通过邮件发送", icon: "mail-send-symbolic" },
        ]
        delegate: AbstractButton {
            id: row
            required property var modelData
            Layout.fillWidth: true
            implicitHeight: Tokens.height10
            hoverEnabled: true
            leftPadding: Tokens.space2
            rightPadding: Tokens.space2
            Accessible.name: row.modelData.title
            onClicked: targets.launch(row.modelData.key)
            background: Rectangle {
                radius: Tokens.radius
                color: Qt.alpha(Kirigami.Theme.highlightColor,
                                row.down ? Tokens.tintPressed : row.hovered ? Tokens.tintHover : 0)
                border.width: row.visualFocus ? Tokens.lineStrong : 0
                border.color: Kirigami.Theme.highlightColor
                Behavior on color { ColorAnimation { duration: Tokens.motionFast } }
            }
            contentItem: RowLayout {
                spacing: Tokens.space3
                Kirigami.Icon {
                    implicitWidth: Tokens.iconMd
                    implicitHeight: Tokens.iconMd
                    source: row.modelData.icon
                }
                Label {
                    Layout.fillWidth: true
                    text: row.modelData.title
                    font.pixelSize: Tokens.textBase
                    elide: Text.ElideRight
                }
                Kirigami.Icon {
                    implicitWidth: Tokens.iconSm
                    implicitHeight: Tokens.iconSm
                    source: "go-next-symbolic"
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    isMask: true
                }
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
