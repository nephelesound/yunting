// 列表页（每日推荐、心动模式、私人雷达、相似歌曲、我喜欢的音乐、榜单共用）：辅助窗格布局。
// 左＝固定窗格（封面 280、事实、说明、主动作），不滚动；右＝单列曲目列表，宽度够时显示专辑列。
// 只有榜单显示名次（ranked）；页名只出现在页头面包屑里。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ScreenFrame {
    id: page

    // { heading, cover, facts: [{ value, label }], description, tracks, ranked }
    property var list
    property int currentId   // 正在播放的曲目 id；不在本列表里就没有高亮
    property bool vip

    signal activated(int index)
    signal playAllRequested()
    signal shuffleRequested()
    signal renameRequested(real id, string name)
    signal deleteRequested(real id, string name)
    signal subscribeToggled(real id, string name, bool subscribed)

    // 歌单才有操作：自己建的（「我喜欢的音乐」除外）改名/删除，别人的收藏/取消收藏
    readonly property bool hasListActions: (page.list.listId || 0) > 0 && page.loggedIn

    heading: page.list.heading

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Tokens.pageMargin
        spacing: Tokens.space6

        // ── 左：固定窗格 ──
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
                Button {
                    enabled: page.list.tracks.length > 1
                    implicitHeight: Tokens.height8
                    flat: true
                    icon.name: "media-playlist-shuffle"
                    icon.width: Tokens.iconSm
                    icon.height: Tokens.iconSm
                    text: "随机播放"
                    onClicked: page.shuffleRequested()
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
                }
            }
        }

        // ── 右：曲目列表 ──
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

            delegate: TrackTile {
                width: tracks.width - tracks.rightMargin
                ranked: page.list.ranked || false
                current: modelData.id === page.currentId
                showReason: page.list.showReason || false
                showAlbum: !(page.list.isAlbum || false)
                locked: modelData.fee === 4 || (!page.vip && modelData.fee === 1)
                menuContext: ({ playlistId: page.list.playlistId || 0 })
                onActivated: index => page.activated(index)
            }

            // 空列表：说明为什么空，而不是留一片空白
            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - 2 * Tokens.pageMargin
                visible: tracks.count === 0
                icon.name: "view-media-playlist"
                text: page.list.emptyText || "这里还没有歌"
            }
        }
    }
}
