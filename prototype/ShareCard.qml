// 分享图：3:4 竖图，与桌面歌词同一种观感（Apple Music 的 vibrancy）：封面放大成色场、压暗，
// 上面是白字，只分两级——全白（标题、歌词）与半透明白（歌手、译文、落款）。没有阴影、没有描边。
// 没选歌词：大封面 + 标题；选了歌词：小封面与标题在上，歌词大字在中间。底部是「云听」与扫码打开的二维码。
// 预览与导出是同一个元素：导出时按 3 倍尺寸重绘（grabToImage 的 targetSize），所以封面按导出尺寸取图。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: card

    property string title
    property string subtitle
    property string cover
    property string url
    property bool qrAvailable: false
    property var lines: []   // 选中的歌词：[{ text, tr }]，至多 maxLines 句

    // 3 句是 3:4 里放得下的上限：每句至多两行，加译文
    readonly property int maxLines: 3
    readonly property int pad: 2 * Tokens.space4
    readonly property color inkSecondary: Qt.alpha("white", Tokens.desktopInk)

    implicitWidth: Tokens.shareCardWidth
    implicitHeight: Tokens.shareCardWidth * 4 / 3
    clip: true

    // 色场：封面缩到 8px（服务器缩图时取平均）再平滑放大，只剩颜色的渐变，认不出图形。
    // 不用实时模糊：导出走 grabToImage，着色器效果在离屏重绘里不可靠。没有封面时是深灰底
    Rectangle {
        anchors.fill: parent
        color: Tokens.shareBlank
    }
    Image {
        anchors.fill: parent
        source: card.cover ? Source.sized(card.cover, 8) : ""
        sourceSize.width: 8
        sourceSize.height: 8
        fillMode: Image.PreserveAspectCrop
        smooth: true
        asynchronous: true
    }
    Rectangle {
        anchors.fill: parent
        color: Qt.alpha("black", Tokens.shareScrim)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: card.pad
        spacing: Tokens.space4

        // 没选歌词：封面是主角
        Cover {
            visible: card.lines.length === 0
            Layout.preferredWidth: 32 * Tokens.space2   // 256
            Layout.preferredHeight: 32 * Tokens.space2
            source: card.cover ? Source.sized(card.cover, 1024) : ""
            pixelSize: 1024
        }
        ColumnLayout {
            visible: card.lines.length === 0
            Layout.fillWidth: true
            spacing: Tokens.space1
            Label {
                Layout.fillWidth: true
                text: card.title
                color: "white"
                font.pixelSize: Tokens.text2xl
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                visible: text.length > 0
                text: card.subtitle
                color: card.inkSecondary
                font.pixelSize: Tokens.textBase
                elide: Text.ElideRight
            }
        }

        // 选了歌词：小封面与标题在上，歌词是主角
        RowLayout {
            visible: card.lines.length > 0
            Layout.fillWidth: true
            spacing: Tokens.space3
            Cover {
                Layout.preferredWidth: Tokens.height12
                Layout.preferredHeight: Tokens.height12
                source: card.cover ? Source.sized(card.cover, 192) : ""
                pixelSize: 192
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    text: card.title
                    color: "white"
                    font.pixelSize: Tokens.textBase
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: card.subtitle
                    color: card.inkSecondary
                    font.pixelSize: Tokens.textSm
                    elide: Text.ElideRight
                }
            }
        }
        ColumnLayout {
            visible: card.lines.length > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Tokens.space4
            Item { Layout.fillHeight: true }
            Repeater {
                model: card.lines
                ColumnLayout {
                    id: lyric
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: Tokens.space1
                    Label {
                        Layout.fillWidth: true
                        text: lyric.modelData.text
                        color: "white"
                        font.pixelSize: Tokens.text2xl
                        font.weight: Font.DemiBold
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: lyric.modelData.tr || ""
                        color: card.inkSecondary
                        font.pixelSize: Tokens.textSm
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }
            }
            Item { Layout.fillHeight: true }
        }

        Item {
            visible: card.lines.length === 0
            Layout.fillHeight: true
        }

        // 落款：左边是应用名，右边是扫码打开的二维码（白底：二维码要高对比）
        RowLayout {
            Layout.fillWidth: true
            spacing: Tokens.space3
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBottom
                spacing: 0
                Label {
                    text: "云听"
                    color: "white"
                    font.pixelSize: Tokens.textSm
                    font.weight: Font.DemiBold
                }
                Label {
                    visible: qr.visible
                    text: "扫码收听"
                    color: card.inkSecondary
                    font.pixelSize: Tokens.textXs
                }
            }
            Rectangle {
                id: qr
                visible: card.qrAvailable && card.url.length > 0
                Layout.preferredWidth: Tokens.height12
                Layout.preferredHeight: Tokens.height12
                radius: Tokens.radius
                color: "white"
                Image {
                    anchors.fill: parent
                    anchors.margins: Tokens.space1
                    source: qr.visible ? "image://qrcode/" + encodeURIComponent(card.url) : ""
                    sourceSize.width: 240
                    sourceSize.height: 240
                    smooth: false            // 模块边缘保持锐利，扫得快
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                }
            }
        }
    }
}
