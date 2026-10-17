// 列表页（每日推荐、心动模式、私人雷达、相似歌曲、我喜欢的音乐、榜单共用）：辅助窗格布局。
// 左＝固定窗格（封面 280、事实、说明、主动作），不滚动；右＝单列曲目列表，宽度够时显示专辑列。
// 只有榜单显示名次（ranked）；页名只出现在页头面包屑里。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Format.js" as Format

ScreenFrame {
    id: page

    // { heading, cover, facts: [{ value, label }], description, tracks, ranked }
    property var list
    property real currentId   // 正在播放的曲目 id；不在本列表里就没有高亮
    property bool vip
    property string listKey   // 失败重试用：root.listKey
    // 自己的歌单可以进入排序模式：每行固定显示上移 / 下移，提交一次后退出
    property bool reordering: false

    signal activated(int index)
    signal playAllRequested()
    signal shuffleRequested()
    signal renameRequested(real id, string name)
    signal deleteRequested(real id, string name)
    signal subscribeToggled(real id, string name, bool subscribed)
    signal albumSubscriptionToggled(real id, bool subscribed)
    signal shareRequested(string kind, real id, string title, string cover)

    // 歌单才有操作：自己建的（「我喜欢的音乐」除外）改名/删除，别人的收藏/取消收藏
    readonly property bool hasListActions: (page.list.listId || 0) > 0 && page.loggedIn
    readonly property bool canReorder: page.loggedIn && (page.list.owned || false)
        && (page.list.listId || 0) > 0 && (page.list.tracks || []).length > 1

    heading: page.list.heading

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Tokens.pageMargin
        spacing: Tokens.space6

        // 左：固定窗格
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: Tokens.trackColumn
            Layout.maximumWidth: Tokens.trackColumn
            spacing: 0

            Cover {
                Layout.preferredWidth: Tokens.trackColumn
                Layout.preferredHeight: Tokens.trackColumn
                source: page.list.cover
                pixelSize: 560
            }

            Facts {
                Layout.fillWidth: true
                Layout.topMargin: Tokens.space4
                items: page.list.facts
            }

            Label {
                Layout.fillWidth: true
                Layout.topMargin: Tokens.space2
                visible: text.length > 0
                text: page.list.description || ""
                font.pixelSize: Tokens.textSm
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }

            RowLayout {
                Layout.topMargin: Tokens.space4
                spacing: Tokens.space2

                AccentButton {
                    enabled: page.list.tracks.length > 0
                    icon.name: "media-playback-start"
                    text: "播放全部"
                    onClicked: page.playAllRequested()
                }
                // 与歌手页同一排：随机播放只留图标（悬停有提示），窗格 280 宽放得下其余带状态的动作
                IconButton {
                    enabled: page.list.tracks.length > 1
                    icon.name: "media-playlist-shuffle"
                    text: "随机播放"
                    onClicked: page.shuffleRequested()
                }
                Button {
                    visible: page.list.isAlbum || false
                    implicitHeight: Tokens.height8
                    flat: true
                    icon.name: page.list.subscribed ? "bookmark-remove" : "bookmark-new"
                    icon.width: Tokens.iconSm
                    icon.height: Tokens.iconSm
                    text: page.list.subscribed ? "已收藏" : "收藏专辑"
                    onClicked: page.albumSubscriptionToggled(page.list.albumId, !page.list.subscribed)
                }
                Button {
                    visible: page.canReorder
                    implicitHeight: Tokens.height8
                    flat: true
                    icon.name: page.reordering ? "dialog-ok" : "view-sort-ascending"
                    icon.width: Tokens.iconSm
                    icon.height: Tokens.iconSm
                    text: page.reordering ? "完成" : "排序"
                    onClicked: page.reordering = !page.reordering
                }
                IconButton {
                    visible: page.hasListActions
                    icon.name: "overflow-menu"
                    text: "更多"
                    onClicked: listMenu.popup(this, 0, height)
                }
                Menu {
                    id: listMenu
                    MenuItem {
                        visible: page.list.owned || false
                        height: visible ? implicitHeight : 0
                        icon.name: "edit-rename"
                        text: "重命名…"
                        onTriggered: page.renameRequested(page.list.listId, page.list.heading)
                    }
                    MenuItem {
                        visible: page.list.owned || false
                        height: visible ? implicitHeight : 0
                        icon.name: "edit-delete"
                        text: "删除歌单…"
                        onTriggered: page.deleteRequested(page.list.listId, page.list.heading)
                    }
                    MenuItem {
                        visible: !(page.list.owned || false)
                        height: visible ? implicitHeight : 0
                        icon.name: page.list.subscribed ? "bookmark-remove" : "bookmark-new"
                        text: page.list.subscribed ? "取消收藏" : "收藏歌单"
                        onTriggered: page.subscribeToggled(page.list.listId, page.list.heading, !page.list.subscribed)
                    }
                    MenuItem {
                        icon.name: "document-share"
                        text: "分享…"
                        onTriggered: page.shareRequested(page.list.isAlbum ? "album" : "playlist",
                                                        page.list.isAlbum ? page.list.albumId : page.list.listId,
                                                        page.list.heading, page.list.cover)
                    }
                }
            }
        }

        // 右：曲目列表
        ListView {
            id: tracks
            Layout.fillWidth: true
            Layout.fillHeight: true
            // 列表右边缘 = 页边线；滚动条落在页边距里，不占内容宽度
            rightMargin: Tokens.pageMargin
            bottomMargin: Tokens.space6
            spacing: Tokens.space2
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: page.list.tracks
            currentIndex: -1
            ScrollBar.vertical: ScrollBar {}
            WheelScroll { flickable: tracks }
            LocateCurrent {
                parent: tracks
                view: tracks
                index: page.list.tracks.findIndex(t => t.id === page.currentId)
            }

            delegate: TrackTile {
                width: tracks.width - tracks.rightMargin
                ranked: page.list.ranked || false
                current: modelData.id === page.currentId
                showReason: page.list.showReason || false
                showAlbum: !(page.list.isAlbum || false)
                locked: Format.blockedBy(modelData, page.vip) !== ""
                menuContext: ({ playlistId: page.list.playlistId || 0 })
                reordering: page.reordering
                canMoveDown: index < (page.list.tracks || []).length - 1
                onActivated: index => page.activated(index)
                onMoveRequested: (index, delta) => page.moveTrack(index, delta)
            }

            // 空列表：说明为什么空，而不是留一片空白
            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - 2 * Tokens.pageMargin
                visible: tracks.count === 0
                icon.name: "view-media-playlist"
                text: page.list.failed ? "曲目取不到，点此重试" : (page.list.emptyText || "这里还没有歌")
                HoverHandler { enabled: page.list.failed === true; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    enabled: page.list.failed === true
                    onTapped: Source.retryKey(page.listKey || "")
                }
            }
        }
    }

    // 上移 / 下移一次就提交完整顺序；Store 成功后会重建列表，所以一次只挪一步
    function moveTrack(index, delta) {
        const target = index + delta;
        const source = page.list.tracks || [];
        if (target < 0 || target >= source.length)
            return;
        const ordered = source.slice();
        const moved = ordered.splice(index, 1)[0];
        ordered.splice(target, 0, moved);
        page.reordering = false;
        Source.reorderPlaylistTracks(page.list.listId, ordered.map(t => t.id));
    }
}
