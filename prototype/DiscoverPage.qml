// 发现：一屏放完，不滚动。左＝榜单封面网格（3 行），右＝热歌榜前 n 首（单列，填满剩余宽度）。
// 磁贴边长由 Tokens.tileFor 取档，3 块磁贴的总高恰好等于 n 条曲目行，两组上下边缘对齐。
// 窗口变窄时减少榜单列数（多出来的榜单收进「全部」），而不是改成横向滚动。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Data.js" as Data

ScreenFrame {
    id: page

    property int currentIndex

    signal toplistOpened(int index)
    signal trackActivated(int index)

    currentPivot: "discover"

    readonly property int tile: Tokens.tileFor(body.height - Tokens.height10 - Tokens.space3)
    readonly property int chartColumns: Math.max(2, Math.min(4,
        Math.floor((body.width - Tokens.space6 - Tokens.trackColumn + Tokens.space2) / (tile + Tokens.space2))))
    readonly property int hotRows: Tokens.rowsFor(page.tile)

    // 分组标题行：固定 40 高，左标题、右侧可选的「全部」，两组共用，保证内容顶边对齐
    component GroupHeader: Item {
        id: header
        property string text
        property string actionText
        signal actionTriggered()

        Layout.fillWidth: true
        Layout.bottomMargin: Tokens.space3
        implicitHeight: Tokens.height10

        Label {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: header.text
            font.pixelSize: Tokens.text2xl
            font.weight: Font.Light
        }
        Button {
            anchors.right: parent.right
            anchors.rightMargin: -Tokens.space2
            anchors.verticalCenter: parent.verticalCenter
            visible: header.actionText.length > 0
            implicitHeight: Tokens.height8
            flat: true
            text: header.actionText
            font.pixelSize: Tokens.textSm
            onClicked: header.actionTriggered()
        }
    }

    RowLayout {
        id: body
        anchors.fill: parent
        anchors.leftMargin: Tokens.pageMargin
        anchors.rightMargin: Tokens.pageMargin
        anchors.bottomMargin: Tokens.space6
        spacing: Tokens.space6

        ColumnLayout {
            readonly property int gridWidth: page.chartColumns * (page.tile + Tokens.space2) - Tokens.space2
            Layout.alignment: Qt.AlignTop
            Layout.fillWidth: false // 子项 GroupHeader 的 fillWidth 会向上传染，这里必须显式关掉
            Layout.preferredWidth: gridWidth
            Layout.maximumWidth: gridWidth
            spacing: 0

            GroupHeader {
                text: "排行榜"
                actionText: page.chartColumns * 3 < Data.toplists.length ? "全部 " + Data.toplists.length + " 个" : ""
            }
            Grid {
                columns: page.chartColumns
                spacing: Tokens.space2
                Repeater {
                    model: Data.toplists.slice(0, page.chartColumns * 3)
                    delegate: CoverTile {
                        required property var modelData
                        required property int index
                        width: page.tile
                        height: page.tile
                        cover: modelData.cover
                        name: modelData.name
                        detail: modelData.update
                        onClicked: page.toplistOpened(index)
                    }
                }
            }
        }

        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            Layout.fillWidth: true
            spacing: 0

            GroupHeader {
                text: Data.playlist.name
                actionText: "全部 " + Data.playlist.count + " 首"
                onActionTriggered: page.toplistOpened(3)
            }
            Repeater {
                model: Data.tracks.slice(0, page.hotRows)
                delegate: TrackTile {
                    Layout.fillWidth: true
                    Layout.bottomMargin: index < page.hotRows - 1 ? Tokens.space2 : 0
                    current: index === page.currentIndex
                    onActivated: index => page.trackActivated(index)
                }
            }
        }
    }
}
