// 发现（「推荐」下的子页）：首页只放为你推荐的，其余在这里，枢轴分
// 「排行榜 | 新歌 | 新碟 | 精品歌单 | MV | 播客」。新歌与播客节目是曲目行（点了就放），其余是磁贴网格。
// 排行榜一屏放完，不滚动。两条带，全部按 56（48 行 + 8 间距）的节距对齐：
//   上带：官方榜（榜单卡：封面 + 前三首，信息气味）| 更多榜单（104 封面磁贴）
//   下带：热歌榜，名次按列优先排（先往下再换列），多列时阅读顺序仍与名次一致；放不下的行数直接不显示
// 窄窗：官方榜卡改单列、更多榜单改 2 列，两者都是 4 行卡高；下带没空间就整条省略（入口仍在热歌榜卡上）。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ScreenFrame {
    id: page
    kept: ["pane", "category"]
    scroller: page.pane === "newSongs" || page.pane === "programs" ? songList
        : page.pane === "charts" ? null : grid

    property real currentId   // 正在播放的曲目 id（新歌的 id 超出 32 位整数，不能用 int）
    property string pane: "charts"      // charts | newSongs | newAlbums | highquality | mvs | programs
    property string category: "全部"    // 精品歌单的分类

    signal toplistOpened(int index)
    signal trackActivated(int index)

    heading: "发现"
    parentText: "推荐"

    // 除排行榜外，切到哪一栏才取哪一栏
    readonly property var songs: page.pane === "newSongs" ? Source.discover("newSongs")
        : page.pane === "programs" ? Source.discover("recommendedPrograms") : null
    readonly property var tiles: page.pane === "newAlbums" ? Source.discover("newAlbums")
        : page.pane === "mvs" ? Source.discover("recommendedMvs")
        : page.pane === "highquality" ? Source.highquality(page.category) : null
    readonly property var tags: page.pane === "highquality" ? Source.discover("highqualityTags").items : []
    readonly property int pivotBlock: Tokens.height10 + Tokens.space3

    Pivot {
        x: Tokens.pageMargin
        items: [{ key: "charts", text: "排行榜" }, { key: "newSongs", text: "新歌" }, { key: "newAlbums", text: "新碟" },
                { key: "highquality", text: "精品歌单" }, { key: "mvs", text: "MV" }, { key: "programs", text: "播客" }]
        current: page.pane
        onActivated: key => page.pane = key
    }

    // 新歌 / 推荐的播客节目：曲目行，点一首从这里开始放整张
    ListView {
        id: songList
        visible: page.songs !== null
        anchors.fill: parent
        anchors.topMargin: page.pivotBlock
        anchors.leftMargin: Tokens.pageMargin
        rightMargin: Tokens.pageMargin
        bottomMargin: Tokens.space6
        spacing: Tokens.space2
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: page.songs ? page.songs.items : []
        ScrollBar.vertical: AppScrollBar {}
        WheelScroll { flickable: songList }

        delegate: TrackTile {
            width: songList.width - songList.rightMargin
            current: modelData.id === page.currentId
            onActivated: index => Actions.play(page.songs.items, index)
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Tokens.pageMargin
            visible: songList.count === 0
            text: page.songs && page.songs.pending ? "正在取回…" : page.songs && page.songs.failed ? "没取到"
                : !Source.live ? "原型数据里没有这一栏" : page.pane === "programs" ? "暂时没有推荐的节目" : "暂时没有新歌"
            helpfulAction: Kirigami.Action {
                enabled: page.songs !== null && page.songs.failed
                text: "重试"
                icon.name: "view-refresh"
                onTriggered: Source.retryKey(page.pane === "programs" ? "discover:programs" : "discover:newSongs")
            }
        }
    }

    // 新碟 / 精品歌单 / MV：磁贴网格；精品歌单上面一排分类（热门的在前）
    ColumnLayout {
        visible: page.tiles !== null
        anchors.fill: parent
        anchors.topMargin: page.pivotBlock
        anchors.leftMargin: Tokens.pageMargin
        spacing: Tokens.space3

        Pivot {
            visible: page.pane === "highquality" && page.tags.length > 0
            items: [{ key: "全部", text: "全部" }].concat(
                page.tags.filter(t => t.hot).concat(page.tags.filter(t => !t.hot)).slice(0, 11)
                    .map(t => ({ key: t.name, text: t.name })))
            current: page.category
            rowHeight: Tokens.height8
            fontSize: Tokens.textBase
            spacing: Tokens.space4
            onActivated: key => page.category = key
        }

        TileGrid {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            list: page.tiles
            captionOf: item => page.pane === "highquality" ? (item.count > 0 ? item.count + " 首" : "")
                : item.artist || ""
            emptyIcon: "media-optical-audio"
            emptyText: !Source.live ? "原型数据里没有这一栏" : "这里暂时是空的"
            onOpened: item => page.pane === "newAlbums" ? Actions.openAlbum(item.id)
                : page.pane === "mvs" ? Actions.openMv(item.id) : Actions.openPlaylist(item.id)
            onMenuRequested: item => { if (page.pane === "highquality") Actions.playlistMenuRequested(item); }
        }
    }

    readonly property var official: Source.toplists.filter(t => t.preview.length > 0)
    readonly property var others: Source.toplists.filter(t => t.preview.length === 0)

    readonly property int pitch: Tokens.trackRow + Tokens.space2           // 56
    readonly property int headerHeight: Tokens.height10 + Tokens.space3      // 分组标题行 + 与内容的间距
    readonly property int moreWidth: 4 * (Tokens.tileSm + Tokens.space2) - Tokens.space2   // 440
    // 宽版要放下两列官方榜卡（每张至少 2 个大磁贴宽）+ 更多榜单
    readonly property bool wide: body.width >= 2 * (2 * Tokens.tileLg) + Tokens.space2 + Tokens.space6 + page.moreWidth
    // 宽：官方榜 2×2 卡、更多榜单 4×2 磁贴，都是 2 张卡高 = 4 个节距；窄：4×1 卡、2×4 磁贴 = 8 个节距
    readonly property int bandHeight: (page.wide ? 4 : 8) * page.pitch - Tokens.space2
    readonly property int hotRows: Math.max(0, Math.floor((body.height - page.headerHeight - page.bandHeight - Tokens.space6 - page.headerHeight + Tokens.space2) / page.pitch))
    readonly property int hotColumns: Math.max(1, Math.min(3, Math.floor((body.width + Tokens.space6) / (Tokens.trackColumn + Tokens.space6))))

    ColumnLayout {
        id: body
        visible: page.pane === "charts"
        anchors.fill: parent
        anchors.topMargin: page.pivotBlock
        anchors.leftMargin: Tokens.pageMargin
        anchors.rightMargin: Tokens.pageMargin
        anchors.bottomMargin: Tokens.space6
        spacing: Tokens.space6

        // 上带
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: page.headerHeight + page.bandHeight
            spacing: Tokens.space6

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 0
                GroupHeader { text: "官方榜" }
                GridLayout {
                    Layout.fillWidth: true
                    columns: page.wide ? 2 : 1
                    columnSpacing: Tokens.space2
                    rowSpacing: Tokens.space2
                    Repeater {
                        model: page.official
                        delegate: ChartCard {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            chart: modelData
                            onClicked: page.toplistOpened(Source.toplists.indexOf(modelData))
                        }
                    }
                }
            }

            ColumnLayout {
                readonly property int gridWidth: (page.wide ? 4 : 2) * (Tokens.tileSm + Tokens.space2) - Tokens.space2
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth: false // 子项 GroupHeader 的 fillWidth 会向上传染，这里必须显式关掉
                Layout.preferredWidth: gridWidth
                Layout.maximumWidth: gridWidth
                spacing: 0
                GroupHeader { text: "更多榜单" }
                Grid {
                    columns: page.wide ? 4 : 2
                    spacing: Tokens.space2
                    Repeater {
                        model: page.others
                        delegate: CoverTile {
                            required property var modelData
                            width: Tokens.tileSm
                            height: Tokens.tileSm
                            cover: modelData.cover
                            name: modelData.name
                            detail: modelData.update
                            onClicked: page.toplistOpened(Source.toplists.indexOf(modelData))
                        }
                    }
                }
            }
        }

        // 下带：热歌榜
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            visible: page.hotRows > 0
            spacing: 0

            GroupHeader {
                text: Source.hotChart.name
                actionText: "全部 " + Source.hotChart.count + " 首"
                // 热歌榜按它自己的 id 打开，不依赖它在榜单列表里排第几
                onActionTriggered: Actions.openPlaylist(Source.d.playlist.id)
            }
            Grid {
                id: hot
                readonly property int tileWidth: Math.floor((body.width - (page.hotColumns - 1) * Tokens.space6) / page.hotColumns)
                rows: page.hotRows
                flow: Grid.TopToBottom
                rowSpacing: Tokens.space2
                columnSpacing: Tokens.space6
                Repeater {
                    model: Source.hotChart.tracks.slice(0, page.hotRows * page.hotColumns)
                    delegate: TrackTile {
                        width: hot.tileWidth
                        ranked: true
                        current: modelData.id === page.currentId
                        onActivated: index => page.trackActivated(index)
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
