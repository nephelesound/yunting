// 全部歌单：「我的」页只放得下一排，其余在这里。枢轴分「创建的 | 收藏的」，
// 右侧是「新建歌单」；下面是铺满到页边线的歌单磁贴网格（右键有歌单菜单）。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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

        TileGrid {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            list: ({ items: page.shown, more: false, pending: false, failed: false, moreFailed: false, key: "" })
            captionOf: item => item.count > 0 ? item.count + " 首" : "空歌单"
            emptyIcon: "view-media-playlist"
            emptyText: page.pane === "owned" ? "还没有创建歌单" : "还没有收藏歌单"
            emptyExplanation: page.pane === "owned" ? "用右上角的「新建歌单」，或在任意歌曲的菜单里「添加到歌单 › 新建歌单」" : "在歌单页的「更多」里可以收藏"
            onOpened: item => Actions.openPlaylist(item.id)
            onMenuRequested: item => Actions.playlistMenuRequested(item)
        }
    }
}
