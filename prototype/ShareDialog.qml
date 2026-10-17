// 分享：一首歌 / 一张专辑 / 一个歌单。三件事，按常用程度从上到下：
//   复制：链接一点即复制（链接本身就是按钮）；「复制歌曲信息」是「歌手《歌名》+ 链接」，贴进聊天里一眼看懂。
//         反馈就在点的地方：按钮短暂变成「已复制 ✓」，对话框不关（复制完还可以接着发送）。
//   扫码：手机扫二维码直接打开（C++ 的 image://qrcode 生成；没有 libqrencode 的构建里不显示）。
//   发送到：KDE 的原生分享（Purpose：KDE Connect 发到手机、邮件、Telegram、蓝牙……），
//         放在 ShareTargets.qml 里按需加载，系统没装 Purpose 时这一节不出现，其余照常。
// 真正浮在页面之上的对话框：阴影与外框由 Kirigami 给出。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.Dialog {
    id: share

    property string shareTitle
    property string shareSubtitle
    property string shareUrl
    property string shareText     // 「分享歌曲《晴天》 - 周杰伦」：复制歌曲信息时放在链接前
    property bool qrAvailable: false
    property string cover

    title: "分享"
    padding: Tokens.space6
    preferredWidth: Tokens.measureMax + Tokens.trackColumn - Tokens.space6 * 2   // 640
    standardButtons: Kirigami.Dialog.NoButton

    onOpened: copied.which = ""

    // 刚复制了哪一项：对应按钮显示「已复制」，1.5 秒后复原
    QtObject {
        id: copied
        property string which
    }
    Timer {
        id: copiedReset
        interval: 1500
        onTriggered: copied.which = ""
    }
    function copy(which, text) {
        Clipboard.copy(text);
        copied.which = which;
        copiedReset.restart();
    }

    ColumnLayout {
        spacing: Tokens.space6

        // 分享的是什么
        RowLayout {
            Layout.fillWidth: true
            spacing: Tokens.space4
            Cover {
                Layout.preferredWidth: 2 * Tokens.height12
                Layout.preferredHeight: 2 * Tokens.height12
                source: share.cover ? Source.sized(share.cover, 192) : ""
                pixelSize: 192
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Tokens.space1
                Label {
                    Layout.fillWidth: true
                    text: share.shareTitle
                    font.pixelSize: Tokens.text2xl
                    font.weight: Font.Light
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: share.shareSubtitle
                    font.pixelSize: Tokens.textBase
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    elide: Text.ElideRight
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Tokens.space6

            // 复制：链接本身可点；下面两个按钮
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: Tokens.space3

                // 链接：整条是按钮，点一下复制（悬停淡色调，右侧图标说明动作）
                AbstractButton {
                    id: linkButton
                    Layout.fillWidth: true
                    implicitHeight: Tokens.height10
                    hoverEnabled: true
                    leftPadding: Tokens.space3
                    rightPadding: Tokens.space3
                    Accessible.name: "复制链接 " + share.shareUrl
                    ToolTip.text: "点击复制链接"
                    ToolTip.visible: hovered && copied.which !== "link"
                    ToolTip.delay: 600
                    onClicked: share.copy("link", share.shareUrl)
                    background: Rectangle {
                        radius: Tokens.radius
                        color: Qt.alpha(Kirigami.Theme.highlightColor,
                                        linkButton.down ? Tokens.tintPressed : linkButton.hovered ? Tokens.tintHover : Tokens.placeholder)
                        border.width: linkButton.visualFocus ? Tokens.lineStrong : 0
                        border.color: Kirigami.Theme.highlightColor
                        Behavior on color { ColorAnimation { duration: Tokens.motionFast } }
                    }
                    contentItem: RowLayout {
                        spacing: Tokens.space2
                        Label {
                            Layout.fillWidth: true
                            text: share.shareUrl
                            font.pixelSize: Tokens.textSm
                            elide: Text.ElideMiddle
                        }
                        Kirigami.Icon {
                            implicitWidth: Tokens.iconSm
                            implicitHeight: Tokens.iconSm
                            source: copied.which === "link" ? "checkmark" : "edit-copy"
                            color: copied.which === "link" ? Kirigami.Theme.positiveTextColor : Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                            isMask: true
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Tokens.space2
                    AccentButton {
                        icon.name: copied.which === "link" ? "checkmark" : "edit-copy"
                        text: copied.which === "link" ? "已复制" : "复制链接"
                        onClicked: share.copy("link", share.shareUrl)
                    }
                    Button {
                        implicitHeight: Tokens.height8
                        flat: true
                        icon.name: copied.which === "info" ? "checkmark" : "edit-copy"
                        icon.width: Tokens.iconSm
                        icon.height: Tokens.iconSm
                        text: copied.which === "info" ? "已复制" : "复制歌曲信息"
                        onClicked: share.copy("info", share.shareText + "\n" + share.shareUrl)
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        implicitHeight: Tokens.height8
                        flat: true
                        icon.name: "internet-web-browser"
                        icon.width: Tokens.iconSm
                        icon.height: Tokens.iconSm
                        text: "在浏览器打开"
                        onClicked: Qt.openUrlExternally(share.shareUrl)
                    }
                }
            }

            // 扫码：白底（二维码要高对比，深色主题下也是白底）
            ColumnLayout {
                visible: share.qrAvailable
                Layout.alignment: Qt.AlignTop
                spacing: Tokens.space1
                Rectangle {
                    Layout.preferredWidth: 3 * Tokens.height10   // 120
                    Layout.preferredHeight: 3 * Tokens.height10
                    radius: Tokens.radius
                    color: "white"
                    Image {
                        anchors.fill: parent
                        anchors.margins: Tokens.space2
                        source: share.qrAvailable && share.shareUrl.length > 0
                            ? "image://qrcode/" + encodeURIComponent(share.shareUrl) : ""
                        sourceSize.width: 240
                        sourceSize.height: 240
                        smooth: false            // 模块边缘保持锐利，扫得快
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                    }
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: "手机扫码打开"
                    font.pixelSize: Tokens.textXs
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                }
            }
        }

        // 发送到：KDE 原生分享（没有 Purpose 时整节不出现）
        Loader {
            id: targets
            Layout.fillWidth: true
            active: share.visible && share.shareUrl.length > 0
            visible: status === Loader.Ready
            source: "ShareTargets.qml"
        }
        Binding { target: targets.item; property: "url"; value: share.shareUrl; when: targets.status === Loader.Ready }
        Binding { target: targets.item; property: "title"; value: share.shareTitle; when: targets.status === Loader.Ready }
    }
}
