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
    scroller: page.pane === "comments" && page.commentThread !== null ? comments : tracks
    background: PageAmbience { cover: page.list.cover }

    // { heading, cover, facts: [{ value, label }], description, tracks, ranked }
    property var list
    property real currentId   // 正在播放的曲目 id；不在本列表里就没有高亮
    property bool vip
    property string listKey   // 失败重试用：root.listKey
    // 自己的歌单可以进入排序模式：每行固定显示上移 / 下移，提交一次后退出
    property bool reordering: false
    property string pane: "tracks"   // tracks | comments（只有歌单有评论）
    kept: ["pane"]
    // 歌单（含榜单、红心、雷达这些本身就是歌单的）才有评论；专辑、电台、每日推荐没有
    readonly property var commentThread: (page.list.listId || 0) > 0 && !page.list.isAlbum && !page.list.paging
        ? Source.commentThread("playlist", page.list.listId) : null
    readonly property int commentCount: page.commentThread ? Source.commentTotal(page.commentThread) : 0

    signal activated(int index)
    signal playAllRequested()
    signal shuffleRequested()
    signal renameRequested(real id, string name)
    signal describeRequested(real id, string description)
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
                // 本地下载：文件就在这个文件夹里，交给文件管理器（还没下过时文件夹可能还不存在）
                Button {
                    visible: (page.list.folder || "") !== "" && page.list.tracks.length > 0
                    implicitHeight: Tokens.height8
                    flat: true
                    icon.name: "folder-open"
                    icon.width: Tokens.iconSm
                    icon.height: Tokens.iconSm
                    text: "打开文件夹"
                    onClicked: Qt.openUrlExternally("file://" + page.list.folder)
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
                        icon.name: "document-edit"
                        text: "编辑简介…"
                        onTriggered: page.describeRequested(page.list.listId, page.list.description || "")
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

        // 右：歌单有评论时上面一排枢轴「曲目 | 评论」；专辑、电台、每日推荐只有曲目
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Tokens.space3

            Pivot {
                visible: page.commentThread !== null
                items: [{ key: "tracks", text: "曲目" },
                        { key: "comments", text: page.commentCount > 0 ? "评论 " + Format.count(page.commentCount) : "评论" }]
                current: page.pane
                onActivated: key => page.pane = key
            }

            ListView {
                id: tracks
                visible: page.pane === "tracks" || page.commentThread === null
                Layout.fillWidth: true
                Layout.fillHeight: true
                // 列表右边缘 = 页边线；滚动条落在页边距里，不占内容宽度
                rightMargin: Tokens.pageMargin
                bottomMargin: Tokens.space6
                spacing: Tokens.space2
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                // 与歌手页 / 评论区同一种模型：列表更新只增量追加，滚回顶部不会发生
                model: GrowingModel { values: page.list.tracks }
                currentIndex: -1
                ScrollBar.vertical: ScrollBar {}
                WheelScroll { flickable: tracks }
                // 分页的列表（电台的节目）：滚到底接着取
                onAtYEndChanged: if (atYEnd && page.list.paging && count > 0) Source.morePaged(page.list.paging)
                footer: PagingFooter {
                    width: tracks.width - tracks.rightMargin
                    paging: page.list.paging ? { items: page.list.tracks, more: page.list.paging.more, loading: false,
                                                 failed: page.list.paging.moreFailed } : null
                    doneText: "没有更早的了"
                    onRetryRequested: Source.morePaged(page.list.paging)
                }
                LocateCurrent {
                    parent: tracks
                    view: tracks
                    index: page.list.tracks.findIndex(t => t.id === page.currentId)
                }

                delegate: TrackTile {
                    modelData: page.list.tracks[index] || ({ id: 0, name: "", artist: "", dur: 0, fee: 0, cover: "" })
                    width: tracks.width - tracks.rightMargin
                    ranked: page.list.ranked || false
                    current: modelData.id === page.currentId
                    showReason: page.list.showReason || false
                    showAlbum: !(page.list.isAlbum || false) && page.list.showAlbum !== false
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

            // 歌单评论：与正在播放页同一个评论区；歌单评论接口只读，不能点赞
            CommentsPane {
                id: comments
                visible: page.pane === "comments" && page.commentThread !== null
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.rightMargin: Tokens.pageMargin
                thread: page.commentThread
                active: comments.visible
                loggedIn: page.loggedIn
                onLoginRequested: page.loginRequested()

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Tokens.pageMargin
                    visible: comments.count === 0
                    icon.name: "dialog-messages"
                    text: !Source.live ? "原型数据里没有这个歌单的评论"
                        : comments.hot.loaded && comments.latest && !comments.latest.more ? "还没有评论" : "正在取回评论…"
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
