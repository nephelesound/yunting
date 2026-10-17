// 全部歌单：「我的」页只放得下一排，其余在这里。枢轴分「创建的 | 收藏的」，
// 右侧是「新建歌单」；下面是铺满到页边线的歌单磁贴网格（右键有歌单菜单）。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ScreenFrame {
    id: page
    scroller: grid
    kept: ["pane"]

    property string pane: "owned"   // owned | subscribed

    heading: "全部歌单"

    readonly property var owned: Source.myPlaylists.filter(pl => pl.owned)
    readonly property var subscribed: Source.myPlaylists.filter(pl => !pl.owned)
    readonly property var shown: page.pane === "owned" ? page.owned : page.subscribed

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Tokens.pageMargin
        spacing: Tokens.space3

        RowLayout {
            Layout.fillWidth: true
            Layout.rightMargin: Tokens.pageMargin - Tokens.space2
            Pivot {
                items: [{ key: "owned", text: "创建的 " + page.owned.length },
                        { key: "subscribed", text: "收藏的 " + page.subscribed.length }]
                current: page.pane
                onActivated: key => page.pane = key
            }
            Item { Layout.fillWidth: true }
            Button {
                implicitHeight: Tokens.height8
                flat: true
                icon.name: "document-new"
                icon.width: Tokens.iconSm
                icon.height: Tokens.iconSm
                text: "新建歌单"
                onClicked: Actions.newPlaylistRequested()
            }
        }

        // 与歌手页的专辑网格同一种排法：视图延伸到窗口右缘，磁贴止于页边线，滚动条在页边距里
        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            readonly property int usable: width - Tokens.pageMargin
            readonly property int columns: Math.max(1, Math.floor((usable + Tokens.space4) / (Tokens.tileLg + Tokens.space4)))
            cellWidth: (usable + Tokens.space4) / columns
            cellHeight: cellWidth - Tokens.space4 + Tokens.space2 + Tokens.height12 + Tokens.space4
            bottomMargin: Tokens.space6
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: page.shown
            ScrollBar.vertical: ScrollBar {}
            WheelScroll { flickable: grid }

            delegate: MediaTile {
                required property var modelData
                width: grid.cellWidth - Tokens.space4
                title: modelData.name
                caption: modelData.count > 0 ? modelData.count + " 首" : "空歌单"
                cover: modelData.cover
                onClicked: Actions.openPlaylist(modelData.id)
                onMenuRequested: Actions.playlistMenuRequested(modelData)
            }

            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - 2 * Tokens.pageMargin
                visible: grid.count === 0
                icon.name: "view-media-playlist"
                text: page.pane === "owned" ? "还没有创建歌单" : "还没有收藏歌单"
                explanation: page.pane === "owned" ? "用右上角的「新建歌单」，或在任意歌曲的菜单里「添加到歌单 › 新建歌单」" : "在歌单页的「更多」里可以收藏"
            }
        }
    }
}
