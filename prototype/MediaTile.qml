// 媒体磁贴：方形封面 + 下方两行字（名称 / 一句理由）。理由是这块磁贴的信息气味：
// 「从《望》听起」「和《晴天》相似」，告诉用户点进去会听到什么。
// 没有封面（入口、未登录锁定）时，方块是强调色淡色调 + 符号图标。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

AbstractButton {
    id: tile

    property string cover
    property string title
    property string caption
    property string symbol      // 无封面时显示的图标
    property bool locked        // 需要登录：图标换成锁，点击触发登录

    readonly property int size: width

    implicitHeight: tile.width + Tokens.space2 + Tokens.height12
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: tile.title

    contentItem: ColumnLayout {
        spacing: Tokens.space2

        Item {
            Layout.preferredWidth: tile.size
            Layout.preferredHeight: tile.size

            Cover {
                anchors.fill: parent
                visible: tile.cover.length > 0
                source: tile.cover
                pixelSize: 400
            }
            Rectangle {
                anchors.fill: parent
                visible: tile.cover.length === 0
                radius: Tokens.radius
                color: Qt.alpha(Kirigami.Theme.highlightColor, tile.hovered ? Tokens.tonalHover : Tokens.tonal)
                Kirigami.Icon {
                    anchors.centerIn: parent
                    width: Tokens.iconLg
                    height: Tokens.iconLg
                    source: tile.locked ? "object-locked-symbolic" : tile.symbol
                    color: tile.locked ? Kirigami.Theme.disabledTextColor : Kirigami.Theme.highlightColor
                    isMask: true
                }
            }
            // 悬停：封面上叠一层强调色淡色调（与曲目行悬停同一令牌）；按下再深一档
            Rectangle {
                anchors.fill: parent
                visible: tile.cover.length > 0
                radius: Tokens.radius
                color: Qt.alpha(Kirigami.Theme.highlightColor, tile.down ? Tokens.tintPressed : tile.hovered ? Tokens.tintHover : 0)
                Behavior on color { ColorAnimation { duration: Kirigami.Units.shortDuration } }
            }
            // 键盘焦点：强调色描边，只在 Tab 导航时出现
            Rectangle {
                anchors.fill: parent
                radius: Tokens.radius
                color: "transparent"
                border.width: Tokens.lineStrong
                border.color: Kirigami.Theme.highlightColor
                visible: tile.visualFocus
            }
        }

        ColumnLayout {
            Layout.preferredWidth: tile.size
            Layout.preferredHeight: Tokens.height12
            spacing: 0
            Label {
                Layout.fillWidth: true
                text: tile.title
                font.pixelSize: Tokens.textBase
                font.weight: Font.Medium
                color: tile.locked ? Kirigami.Theme.disabledTextColor : Kirigami.Theme.textColor
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: tile.caption
                font.pixelSize: Tokens.textSm
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                elide: Text.ElideRight
            }
            Item { Layout.fillHeight: true }
        }
    }
}
