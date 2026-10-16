// 曲目行：高 48 = 封面边长。静止时无底色（重复元素保持 Level 0），悬停才出现淡色调。
// 正在播放：左侧悬挂的强调色竖条 + 强调色标题。VIP 是标题后的淡色调小标签。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

AbstractButton {
    id: tile

    required property int index
    required property var modelData

    property bool current
    property bool locked // 匿名会话下的 VIP 曲：可见，点击时提示登录

    signal activated(int index)

    implicitHeight: Tokens.trackRow
    hoverEnabled: true
    onClicked: tile.activated(tile.index)

    background: Rectangle {
        radius: Tokens.radius
        color: Qt.alpha(Kirigami.Theme.highlightColor, tile.down ? 0.16 : tile.hovered ? 0.1 : 0)
        Behavior on color { ColorAnimation { duration: Kirigami.Units.shortDuration } }

        // 悬挂在对齐线外侧，不推动内容
        Rectangle {
            visible: tile.current
            width: Tokens.space1
            height: Tokens.space6
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.left
            anchors.rightMargin: Tokens.space2
            radius: Tokens.radiusSm
            color: Kirigami.Theme.highlightColor
        }
    }

    contentItem: RowLayout {
        spacing: Tokens.space3

        Cover {
            Layout.preferredWidth: Tokens.trackRow
            Layout.preferredHeight: Tokens.trackRow
            source: tile.modelData.cover
            pixelSize: 96
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 0 // 宽度只由剩余空间决定，长标题不得挤出时长列
            spacing: 0

            // 标题 + VIP 标签：标题先省略，标签始终紧跟其后
            Item {
                id: titleRow
                Layout.fillWidth: true
                implicitHeight: title.implicitHeight

                Label {
                    id: title
                    width: Math.min(implicitWidth, titleRow.width - (tag.visible ? tag.width + Tokens.space2 : 0))
                    text: tile.modelData.name
                    font.pixelSize: Tokens.textBase
                    font.weight: tile.current ? Font.Medium : Font.Normal
                    color: tile.current ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
                    elide: Text.ElideRight
                }
                Rectangle {
                    id: tag
                    visible: tile.modelData.fee === 1
                    x: title.width + Tokens.space2
                    anchors.verticalCenter: title.verticalCenter
                    width: vip.implicitWidth + 2 * Tokens.space1
                    height: vip.implicitHeight
                    radius: Tokens.radiusSm
                    color: Qt.alpha(tile.locked ? Kirigami.Theme.textColor : Kirigami.Theme.highlightColor, 0.1)
                    Label {
                        id: vip
                        anchors.centerIn: parent
                        text: "VIP"
                        font.pixelSize: Tokens.textXs
                        font.weight: Font.Medium
                        color: tile.locked ? Kirigami.Theme.disabledTextColor : Kirigami.Theme.highlightColor
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                text: tile.modelData.artist
                font.pixelSize: Tokens.textSm
                color: Kirigami.Theme.disabledTextColor
                elide: Text.ElideRight
            }
        }

        Label {
            Layout.preferredWidth: Tokens.albumColumn
            visible: tile.width >= Tokens.wideRow
            text: tile.modelData.album
            font.pixelSize: Tokens.textSm
            color: Kirigami.Theme.disabledTextColor
            elide: Text.ElideRight
        }

        Label {
            Layout.rightMargin: Tokens.space3
            text: {
                const s = tile.modelData.dur % 60;
                return Math.floor(tile.modelData.dur / 60) + ":" + (s < 10 ? "0" : "") + s;
            }
            font.pixelSize: Tokens.textSm
            font.features: { "tnum": 1 }
            color: Kirigami.Theme.disabledTextColor
        }
    }
}
