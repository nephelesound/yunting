// 我的（已登录）：回到自己的歌。一屏放完，与首页共用同一套列网格。
//   第一排：「我的音乐」两块入口（我喜欢的音乐 · 音乐云盘）| 「最近播放」直接列出最近的歌（识别而非回忆），
//          行数取刚好与磁贴（含下方两行字）同高的条数，两组上下边缘对齐；
//   第二排：收藏，枢轴切「歌单 / 歌手 / 专辑 / MV」，同一排磁贴；放不下的在「全部」页，右键歌单磁贴有歌单菜单。
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
    signal allSavedRequested(string kind)   // artists | albums | mvs

    currentPivot: "mine"
    kept: ["collection"]

    // 第二排看哪一类收藏：playlists | artists | albums | mvs（歌手等只在切到时才取）
    property string collection: "playlists"
    readonly property var saved: page.collection === "playlists" ? ({ items: [], count: 0, pending: false, failed: false, key: "" })
        : Source.saved(page.collection)
    // MV 收藏里的视频没有 MV 页可开，只列 MV
    readonly property var collectionItems: page.collection === "playlists" ? Source.myPlaylists
        : page.collection === "mvs" ? page.saved.items.filter(m => m.kind === "mv") : page.saved.items
    readonly property int collectionCount: page.collection === "playlists" ? Source.myPlaylists.length : page.saved.count || 0
    function caption(item) {
        if (page.collection === "playlists")
            return item.owned ? (item.count > 0 ? item.count + " 首" : "空歌单") : "收藏的歌单";
        if (page.collection === "artists")
            return item.albums > 0 ? item.albums + " 张专辑" : "";
        return item.artist || "";
    }

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
                 : page.cloud.maxSize > 0 ? page.cloud.count + " 首  已用 " + Source.fileSize(page.cloud.size) + " / " + Source.fileSize(page.cloud.maxSize)
                 : page.cloud.count + " 首" },
    ].concat(Source.downloads ? [{ key: "downloads", title: "本地下载", symbol: "folder-download-symbolic",
                                  caption: Source.downloaded.length > 0 ? Source.downloaded.length + " 首" : "还没有下载的歌" }] : [])

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

        // 第二排：收藏的东西，一次看一类。歌单（创建的在前，收藏的在后）/ 关注的歌手 / 收藏的专辑 / 收藏的 MV
        ColumnLayout {
            spacing: 0
            // 有内容时进「全部」页；歌单一个都没有时直接新建
            GroupHeader {
                pivots: [{ key: "playlists", text: "歌单" }, { key: "artists", text: "歌手" },
                         { key: "albums", text: "专辑" }, { key: "mvs", text: "MV" }]
                current: page.collection
                onPivotActivated: key => page.collection = key
                actionText: page.collectionCount > 0 ? "全部 " + page.collectionCount + " 个"
                    : page.collection === "playlists" ? "新建歌单" : ""
                onActionTriggered: page.collection !== "playlists" ? page.allSavedRequested(page.collection)
                    : Source.myPlaylists.length > 0 ? page.allPlaylistsRequested() : Actions.newPlaylistRequested()
            }
            Row {
                spacing: Tokens.space2
                Repeater {
                    model: page.collectionItems.slice(0, page.columns)
                    delegate: MediaTile {
                        required property var modelData
                        required property int index
                        width: page.tile
                        title: modelData.name
                        caption: page.caption(modelData)
                        cover: modelData.cover
                        round: page.collection === "artists"
                        onClicked: page.collection === "playlists" ? page.playlistOpened(index)
                            : page.collection === "artists" ? Actions.openArtist(modelData.id)
                            : page.collection === "albums" ? Actions.openAlbum(modelData.id)
                            : Actions.openMv(modelData.id)
                        onMenuRequested: if (page.collection === "playlists") Actions.playlistMenuRequested(modelData)
                    }
                }
            }
            Label {
                visible: page.collectionItems.length === 0
                text: page.collection === "playlists" ? "还没有创建或收藏的歌单"
                    : page.saved.pending ? "正在取回…"
                    : page.saved.failed ? "没取到，点此重试"
                    : { artists: "还没有关注的歌手", albums: "还没有收藏的专辑", mvs: "还没有收藏的 MV" }[page.collection]
                font.pixelSize: Tokens.textSm
                font.underline: page.saved.failed && savedRetry.hovered
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                HoverHandler { id: savedRetry; enabled: page.saved.failed; cursorShape: Qt.PointingHandCursor }
                TapHandler { enabled: page.saved.failed; onTapped: Source.retryKey(page.saved.key) }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
