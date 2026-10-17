// 分享：一首歌 / 一张专辑 / 一个歌单。KDE 的表单布局（Kirigami.FormLayout，左边是标签），按常用程度从上到下：
//   链接：一点即复制（链接本身就是按钮）；「复制信息」是「歌手《歌名》+ 链接」，贴进聊天里一眼看懂。
//         反馈就在点的地方：按钮短暂变成「已复制」，对话框不关（复制完还可以接着发送）。
//   扫码：手机扫二维码直接打开（C++ 的 image://qrcode 生成；没有 libqrencode 的构建里不显示）。
//   分享图：一张竖图（ShareCard），左边是缩略图；歌曲可以选一句歌词放进去（连同下一句）。保存进「图片」文件夹或复制。
//   发送到：链接分享用 Telegram / 邮件自己的 URL scheme，放在 ShareTargets.qml；
//         不走 Purpose 的 Export（它把 urls 当本地文件，会把 https://... 当未知文件上传）。
// 真正浮在页面之上的对话框：阴影与外框由 Kirigami 给出。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.kde.kirigami as Kirigami

Kirigami.Dialog {
    id: share

    property string shareTitle
    property string shareSubtitle
    property string shareUrl
    property string shareText     // 「分享歌曲《晴天》 - 周杰伦」：复制歌曲信息时放在链接前
    property bool qrAvailable: false
    property string cover
    property real trackId: 0      // 分享的是歌曲时才有，用来取歌词
    property string copyError
    property string imageStatus
    property bool imageFailed: false

    readonly property var lines: share.trackId > 0 && share.visible
        ? Source.lyrics(share.trackId).filter(line => line.text.length > 0) : []

    title: "分享"
    padding: Tokens.space6
    preferredWidth: Tokens.measureMax + Tokens.trackColumn - Tokens.space6 * 2   // 640
    standardButtons: Kirigami.Dialog.NoButton

    onOpened: {
        copied.which = "";
        copyError = "";
        imageStatus = "";
        lyricChoice.currentIndex = 0;
    }

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
    function flash(which) {
        copied.which = which;
        copiedReset.restart();
    }
    function copy(which, text) {
        if (Clipboard.copy(text)) {
            copyError = "";
            share.flash(which);
        } else {
            copyError = "复制失败，请手动选择链接";
            copied.which = "";
        }
    }

    // 分享图：按导出尺寸（3 倍）重绘一次写到 path，done(ok)
    function localPath(location) {
        return decodeURIComponent(String(StandardPaths.writableLocation(location)).replace(/^file:\/\//, ""));
    }
    function renderTo(path, done) {
        card.grabToImage(result => done(result.saveToFile(path)), Qt.size(3 * card.width, 3 * card.height));
    }
    function saveImage() {
        // 文件名里不能有路径分隔符等字符
        const name = ("云听 " + share.shareTitle).replace(/[\/\\:*?"<>|]/g, " ").trim() + ".png";
        share.renderTo(share.localPath(StandardPaths.PicturesLocation) + "/" + name, ok => {
            share.imageFailed = !ok;
            share.imageStatus = ok ? "已保存到「图片」文件夹" : "保存失败：「图片」文件夹不可写";
            if (ok)
                share.flash("save");
        });
    }
    function copyImage() {
        const path = share.localPath(StandardPaths.CacheLocation) + "/share-card.png";
        share.renderTo(path, ok => {
            const done = ok && Clipboard.copyImage(path);
            share.imageFailed = !done;
            share.imageStatus = done ? "" : "复制图片失败，可以先保存再发送";
            if (done)
                share.flash("image");
        });
    }

    ColumnLayout {
        spacing: Tokens.space6

        // 分享的是什么
        RowLayout {
            Layout.fillWidth: true
            spacing: Tokens.space4
            Cover {
                Layout.preferredWidth: Tokens.height12
                Layout.preferredHeight: Tokens.height12
                source: share.cover ? Source.sized(share.cover, 96) : ""
                pixelSize: 96
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    text: share.shareTitle
                    font.pixelSize: Tokens.textBase
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: share.shareSubtitle
                    font.pixelSize: Tokens.textSm
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    elide: Text.ElideRight
                }
            }
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true

            // 链接：整条是按钮，点一下复制（悬停淡色调，右侧图标说明动作）
            AbstractButton {
                id: linkButton
                Kirigami.FormData.label: "链接："
                Layout.fillWidth: true
                implicitHeight: Tokens.height8
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
                spacing: Tokens.space2
                AccentButton {
                    icon.name: copied.which === "link" ? "checkmark" : "edit-copy"
                    text: copied.which === "link" ? "已复制" : "复制链接"
                    onClicked: share.copy("link", share.shareUrl)
                }
                Button {
                    implicitHeight: Tokens.height8
                    icon.name: copied.which === "info" ? "checkmark" : "edit-copy"
                    icon.width: Tokens.iconSm
                    icon.height: Tokens.iconSm
                    text: copied.which === "info" ? "已复制" : "复制信息"
                    onClicked: share.copy("info", share.shareText + "\n" + share.shareUrl)
                }
                Button {
                    implicitHeight: Tokens.height8
                    icon.name: "internet-web-browser"
                    icon.width: Tokens.iconSm
                    icon.height: Tokens.iconSm
                    text: "在浏览器打开"
                    onClicked: Qt.openUrlExternally(share.shareUrl)
                }
            }
            Label {
                visible: text.length > 0
                text: share.copyError
                font.pixelSize: Tokens.textSm
                color: Kirigami.Theme.negativeTextColor
                wrapMode: Text.Wrap
            }

            // 扫码：白底（二维码要高对比，深色主题下也是白底）
            Rectangle {
                Kirigami.FormData.label: "手机扫码："
                visible: share.qrAvailable
                implicitWidth: 3 * Tokens.height8   // 96
                implicitHeight: implicitWidth
                radius: Tokens.radius
                color: "white"
                Image {
                    anchors.fill: parent
                    anchors.margins: Tokens.space1
                    source: share.visible && share.qrAvailable && share.shareUrl.length > 0
                        ? "image://qrcode/" + encodeURIComponent(share.shareUrl) : ""
                    sourceSize.width: 192
                    sourceSize.height: 192
                    smooth: false            // 模块边缘保持锐利，扫得快
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                }
            }

            // 分享图：缩略图 + 选歌词 + 保存/复制。缩略图就是要导出的那张图缩小显示，导出时按原尺寸重绘
            RowLayout {
                Kirigami.FormData.label: "分享图："
                spacing: Tokens.space4
                Item {
                    Layout.alignment: Qt.AlignTop
                    implicitWidth: card.width / 3
                    implicitHeight: card.height / 3
                    ShareCard {
                        id: card
                        scale: 1 / 3
                        transformOrigin: Item.TopLeft
                        title: share.shareTitle
                        subtitle: share.shareSubtitle
                        cover: share.cover
                        url: share.shareUrl
                        qrAvailable: share.qrAvailable && share.visible
                        // 选一句：放这一句与下一句
                        lines: lyricChoice.currentIndex > 0
                            ? share.lines.slice(lyricChoice.currentIndex - 1, lyricChoice.currentIndex + 1) : []
                    }
                }
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    spacing: Tokens.space2
                    ComboBox {
                        id: lyricChoice
                        visible: share.lines.length > 0
                        Layout.preferredWidth: Tokens.trackColumn
                        model: ["不放歌词"].concat(share.lines.map(line => line.text))
                    }
                    RowLayout {
                        spacing: Tokens.space2
                        Button {
                            implicitHeight: Tokens.height8
                            icon.name: copied.which === "save" ? "checkmark" : "document-save"
                            icon.width: Tokens.iconSm
                            icon.height: Tokens.iconSm
                            text: copied.which === "save" ? "已保存" : "保存图片"
                            onClicked: share.saveImage()
                        }
                        Button {
                            implicitHeight: Tokens.height8
                            icon.name: copied.which === "image" ? "checkmark" : "edit-copy"
                            icon.width: Tokens.iconSm
                            icon.height: Tokens.iconSm
                            text: copied.which === "image" ? "已复制" : "复制图片"
                            onClicked: share.copyImage()
                        }
                    }
                    Label {
                        Layout.preferredWidth: Tokens.trackColumn
                        visible: text.length > 0
                        text: share.imageStatus
                        font.pixelSize: Tokens.textSm
                        color: share.imageFailed ? Kirigami.Theme.negativeTextColor : Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                        wrapMode: Text.Wrap
                    }
                }
            }
        }

        // 发送到：链接分享用服务自己的 URL scheme，不把远程 URL 当本地文件交给 Export 插件
        Loader {
            id: targets
            Layout.fillWidth: true
            active: share.visible && share.shareUrl.length > 0
            visible: status === Loader.Ready
            source: "ShareTargets.qml"
        }
        Binding { target: targets.item; property: "url"; value: share.shareUrl; when: targets.status === Loader.Ready }
        Binding { target: targets.item; property: "title"; value: share.shareTitle; when: targets.status === Loader.Ready }
        Binding { target: targets.item; property: "text"; value: share.shareText; when: targets.status === Loader.Ready }
    }
}
