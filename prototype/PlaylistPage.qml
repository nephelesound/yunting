// 歌单/榜单：展示标题 + 事实行 + 一排动作，下方曲目网格（GridView 原生虚拟化）。
// 列间距 space6 大于行间距 space2，靠留白分列，不画分隔线。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Data.js" as Data

ScreenFrame {
    id: page

    property int currentIndex

    signal activated(int index)
    signal playAllRequested()
    signal shuffleRequested()

    heading: Data.playlist.name
    backVisible: true
    facts: [
        { label: "官方榜" },
        { value: String(Data.playlist.count), label: "首" },
        { value: (Data.playlist.plays / 1e8).toFixed(1) + " 亿", label: "次播放" },
        { label: "每天更新" },
    ]

    RowLayout {
        id: actions
        anchors.left: parent.left
        anchors.leftMargin: Tokens.pageMargin
        spacing: Tokens.space2

        AccentButton {
            icon.name: "media-playback-start"
            text: "播放全部"
            onClicked: page.playAllRequested()
        }
        IconButton {
            icon.name: "media-playlist-shuffle"
            text: "随机播放"
            onClicked: page.shuffleRequested()
        }
        IconButton {
            icon.name: "love-symbolic"
            text: "收藏歌单"
        }
        IconButton {
            icon.name: "overflow-menu"
            text: "更多"
        }
    }

    GridView {
        id: grid
        anchors.top: actions.bottom
        anchors.topMargin: Tokens.space4
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        // 每格右侧与下方各留一道间距，所以右边距少一个列间距，最后一列正好落在页边线上
        readonly property int columnGap: Tokens.space6
        readonly property int columns: Math.max(1, Math.floor((width - 2 * Tokens.pageMargin + columnGap) / (Tokens.trackColumn + columnGap)))
        leftMargin: Tokens.pageMargin
        rightMargin: Tokens.pageMargin - columnGap
        topMargin: Tokens.space2
        bottomMargin: Tokens.space6
        cellWidth: Math.floor((width - leftMargin - rightMargin) / columns)
        cellHeight: Tokens.trackRow + Tokens.space2
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: Data.tracks
        ScrollBar.vertical: ScrollBar {}

        delegate: Item {
            id: cell
            required property int index
            required property var modelData
            width: grid.cellWidth
            height: grid.cellHeight

            TrackTile {
                width: cell.width - grid.columnGap
                index: cell.index
                modelData: cell.modelData
                current: cell.index === page.currentIndex
                locked: !page.loggedIn && cell.modelData.fee === 1
                onActivated: index => page.activated(index)
            }
        }
    }
}
