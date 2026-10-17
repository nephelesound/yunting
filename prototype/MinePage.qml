// 我的（已登录）：回到自己的歌。一屏放完，与首页共用同一套列网格。
//   第一排：「我的音乐」两块入口（我喜欢的音乐 · 音乐云盘）| 「最近播放」直接列出最近的歌（识别而非回忆），
//          行数取刚好与磁贴（含下方两行字）同高的条数，两组上下边缘对齐；
//   第二排：「歌单」，创建的在前、收藏的在后；放不下的在「全部歌单」页，右键磁贴有歌单菜单。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ScreenFrame {
    id: page

    property real currentId

    signal entryOpened(string key)
    signal playlistOpened(int index)
    signal recentActivated(int index)
    signal allPlaylistsRequested()

    currentPivot: "mine"

    readonly property int textBlock: Tokens.space2 + Tokens.height12
    readonly property int headerBlock: Tokens.height10 + Tokens.space3
    // 磁贴边长先由高度定（两排都要放下），再取「刚好不超过它、又能把整行铺到右页边线」的列数：
    // 高度紧时多放几列（多看到几个歌单），行的右缘始终落在页边线上
    readonly property int tileByHeight: Math.floor((body.height - Tokens.space6 - 2 * headerBlock - 2 * textBlock) / 2 / Tokens.space2) * Tokens.space2
    readonly property int columns: Math.max(6, Math.ceil((body.width + Tokens.space2) / (page.tileByHeight + Tokens.space2)))
    readonly property int tile: Math.floor((body.width - (columns - 1) * Tokens.space2) / columns)
    // 最近播放的行数：与「磁贴 + 两行字」同高
    readonly property int recentRows: Math.max(1, Math.floor((page.tile + page.textBlock + Tokens.space2) / (Tokens.trackRow + Tokens.space2)))

    readonly property var cloud: Source.cloud
    readonly property var entries: [
        { key: "liked", title: "我喜欢的音乐", cover: Source.list("liked").cover, symbol: "love-symbolic", caption: Source.pe.liked.count > 0 ? Source.pe.liked.count + " 首" : "还没有红心歌曲" },
        { key: "cloud", title: "音乐云盘", symbol: "folder-cloud-symbolic",
          caption: page.cloud.count === 0 ? "还没有上传的歌"
                 : page.cloud.maxSize > 0 ? page.cloud.count + " 首  已用 " + Source.gigabytes(page.cloud.size) + " / " + Source.gigabytes(page.cloud.maxSize)
                 : page.cloud.count + " 首" },
    ]

    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.leftMargin: Tokens.pageMargin
        anchors.rightMargin: Tokens.pageMargin
        anchors.bottomMargin: Tokens.space6
        spacing: Tokens.space6

        RowLayout {
            Layout.fillWidth: true
            spacing: Tokens.space2

            // 我的音乐
            ColumnLayout {
                readonly property int gridWidth: page.entries.length * (page.tile + Tokens.space2) - Tokens.space2
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth: false
                Layout.preferredWidth: gridWidth
                Layout.maximumWidth: gridWidth
                spacing: 0

                GroupHeader { text: "我的音乐" }
                Row {
                    spacing: Tokens.space2
                    Repeater {
                        model: page.entries
                        delegate: MediaTile {
                            required property var modelData
                            width: page.tile
                            title: modelData.title
                            caption: modelData.caption
                            cover: modelData.cover || ""
                            symbol: modelData.symbol
                            onClicked: page.entryOpened(modelData.key)
                        }
                    }
                }
            }

            // 最近播放：占剩余列，左缘正好落在第三列上（与下一排第三块磁贴对齐）
            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth: true
                spacing: 0

                GroupHeader {
                    text: "最近播放"
                    actionText: Source.recent.total > 0 ? "全部 " + Source.recent.total + " 首" : ""
                    onActionTriggered: page.entryOpened("recent")
                }
                Repeater {
                    model: Source.recent.tracks.slice(0, page.recentRows)
                    delegate: TrackTile {
                        Layout.fillWidth: true
                        Layout.bottomMargin: Tokens.space2
                        hangingMarker: false   // 左边紧挨磁贴，悬挂竖条会压到磁贴上；靠加粗标出
                        current: modelData.id === page.currentId
                        onActivated: index => page.recentActivated(index)
                    }
                }
                Label {
                    visible: Source.recent.tracks.length === 0
                    text: "还没有播放记录"
                    font.pixelSize: Tokens.textSm
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                }
            }
        }

        // 歌单：创建的在前，收藏的在后
        ColumnLayout {
            spacing: 0
            // 有歌单时进「全部歌单」（那里也能新建）；一个都没有时直接新建
            GroupHeader {
                text: "歌单"
                actionText: Source.myPlaylists.length > 0 ? "全部 " + Source.myPlaylists.length + " 个" : "新建歌单"
                onActionTriggered: Source.myPlaylists.length > 0 ? page.allPlaylistsRequested() : Actions.newPlaylistRequested()
            }
            Row {
                spacing: Tokens.space2
                Repeater {
                    model: Source.myPlaylists.slice(0, page.columns)
                    delegate: MediaTile {
                        required property var modelData
                        required property int index
                        width: page.tile
                        title: modelData.name
                        caption: modelData.owned ? (modelData.count > 0 ? modelData.count + " 首" : "空歌单") : "收藏的歌单"
                        cover: modelData.cover
                        onClicked: page.playlistOpened(index)
                        onMenuRequested: Actions.playlistMenuRequested(modelData)
                    }
                }
            }
            Label {
                visible: Source.myPlaylists.length === 0
                text: "还没有创建或收藏的歌单"
                font.pixelSize: Tokens.textSm
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            }
        }

        Item { Layout.fillHeight: true }
    }
}
