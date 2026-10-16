// 榜单卡：封面 + 榜名 + 前三首。网易的官方榜封面只印榜名，前三首才告诉用户「点进去会听到什么」。
// 高 104 = 两条曲目行，和曲目网格同一节距。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

AbstractButton {
    id: card

    property var chart

    implicitHeight: Tokens.tileSm
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: card.chart ? card.chart.name : ""

    background: Rectangle {
        radius: Tokens.radius
        color: Qt.alpha(Kirigami.Theme.highlightColor, card.down ? Tokens.tintPressed : card.hovered ? Tokens.tintHover : 0)
        border.width: card.visualFocus ? Tokens.lineStrong : 0
        border.color: Kirigami.Theme.highlightColor
        Behavior on color { ColorAnimation { duration: Kirigami.Units.shortDuration } }
    }

    contentItem: RowLayout {
        spacing: Tokens.space3

        Cover {
            Layout.preferredWidth: Tokens.tileSm
            Layout.preferredHeight: Tokens.tileSm
            source: card.chart ? card.chart.cover : ""
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            Layout.rightMargin: Tokens.space3
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: Tokens.space6
                spacing: Tokens.space2
                Label {
                    text: card.chart ? card.chart.name : ""
                    font.pixelSize: Tokens.textBase
                    font.weight: Font.Medium
                }
                Label {
                    Layout.fillWidth: true
                    text: card.chart ? card.chart.update : ""
                    font.pixelSize: Tokens.textXs
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    elide: Text.ElideRight
                }
            }

            Repeater {
                model: card.chart ? card.chart.preview : []
                delegate: RowLayout {
                    id: line
                    required property int index
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: Tokens.space6
                    spacing: Tokens.space2

                    Label {
                        Layout.preferredWidth: Tokens.space3
                        text: line.index + 1
                        font.pixelSize: Tokens.textSm
                        font.features: { "tnum": 1 }
                        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.maximumWidth: implicitWidth
                        text: line.modelData.name
                        font.pixelSize: Tokens.textSm
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        text: line.modelData.artist
                        font.pixelSize: Tokens.textSm
                        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
