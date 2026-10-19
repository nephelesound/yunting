// 全部歌单：「我的」页只放得下一排，其余在这里。枢轴分「创建的 | 收藏的」，
// 右侧是「排序」（创建的）与「新建歌单」；下面是铺满到页边线的歌单磁贴网格（右键有歌单菜单）。
// 排序时网格换成一列行，每行上移 / 下移（与歌单里给曲目排序同一种做法），每挪一下就保存；
// 「我喜欢的音乐」固定在最前，不参与排序
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ScreenFrame {
    id: page
    scroller: page.sorting ? sortList : grid
    kept: ["pane"]

    property string pane: "owned"   // owned | subscribed
    property bool reordering: false
    readonly property bool sorting: page.reordering && page.pane === "owned"
    onPaneChanged: page.reordering = false

    heading: "全部歌单"

    readonly property var owned: Source.myPlaylists.filter(pl => pl.owned)
    readonly property var subscribed: Source.myPlaylists.filter(pl => !pl.owned)
    readonly property var shown: page.pane === "owned" ? page.owned : page.subscribed
    readonly property real likedId: Source.pe.liked.id || 0
    function move(index, delta) {
        const target = index + delta;
        const ordered = page.owned.slice();
        if (target < 0 || target >= ordered.length || ordered[target].id === page.likedId)
            return;
        ordered.splice(target, 0, ordered.splice(index, 1)[0]);
        Source.reorderPlaylists(ordered.map(pl => pl.id));
    }

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
                visible: page.pane === "owned" && page.owned.length > 2
                implicitHeight: Tokens.height8
                flat: true
                icon.name: page.reordering ? "dialog-ok" : "view-sort"
                icon.width: Tokens.iconSm
                icon.height: Tokens.iconSm
                text: page.reordering ? "完成" : "排序"
                onClicked: page.reordering = !page.reordering
            }
            Button {
                implicitHeight: Tokens.height8
                flat: true
                icon.name: "document-new"
                icon.width: Tokens.iconSm
                icon.height: Tokens.iconSm
                text: "新建歌单"
                onClicked: Actions.newPlaylistRequested()
            }
            Button {
                implicitHeight: Tokens.height8
                flat: true
                icon.name: "document-import"
                icon.width: Tokens.iconSm
                icon.height: Tokens.iconSm
                text: "导入歌单"
                onClicked: Actions.importPlaylistRequested()
            }
        }

        ListView {
            id: sortList
            visible: page.sorting
            Layout.fillWidth: true
            Layout.maximumWidth: Tokens.measureMax + Tokens.space6 + Tokens.wideRow
            Layout.fillHeight: true
            bottomMargin: Tokens.space6
            spacing: Tokens.space2
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: page.sorting ? page.owned : []
            ScrollBar.vertical: ScrollBar {}
            WheelScroll { flickable: sortList }

            delegate: RowLayout {
                id: sortRow
                required property var modelData
                required property int index
                readonly property bool fixed: sortRow.modelData.id === page.likedId
                width: sortList.width - Tokens.space3
                spacing: Tokens.space3
                Cover {
                    Layout.preferredWidth: Tokens.height10
                    Layout.preferredHeight: Tokens.height10
                    source: Source.sized(sortRow.modelData.cover, 160)
                    pixelSize: 160
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    spacing: 0
                    Label {
                        Layout.fillWidth: true
                        text: sortRow.modelData.name
                        font.pixelSize: Tokens.textBase
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        text: sortRow.fixed ? "固定在最前" : sortRow.modelData.count > 0 ? sortRow.modelData.count + " 首" : "空歌单"
                        font.pixelSize: Tokens.textSm
                        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    }
                }
                IconButton {
                    enabled: !sortRow.fixed && sortRow.index > 0 && page.owned[sortRow.index - 1].id !== page.likedId
                    icon.name: "go-up-symbolic"
                    text: "上移"
                    onClicked: page.move(sortRow.index, -1)
                }
                IconButton {
                    enabled: !sortRow.fixed && sortRow.index < page.owned.length - 1
                    icon.name: "go-down-symbolic"
                    text: "下移"
                    onClicked: page.move(sortRow.index, 1)
                }
            }
        }

        TileGrid {
            id: grid
            visible: !page.sorting
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
