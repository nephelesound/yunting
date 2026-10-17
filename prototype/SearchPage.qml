// 搜索：找一首特定的歌。一屏放完，内容分左右两半（左缘、中缝都在同一条线上）：
//   没输入时：搜索框 + 热门搜索（20 个词，按名次两列、列优先排）；
//   有结果时：左＝单曲（单列，带专辑）；右＝歌手（圆形头像，人物用圆形是通行约定）与歌单。
// 搜索框宽度 = 左半宽，和下方左列对齐。原型快照只有一个查询词的结果。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Format.js" as Format

ScreenFrame {
    id: page

    property string query
    onQueryChanged: Source.search(page.query)
    Component.onCompleted: Source.search(page.query)
    property real currentId

    signal trackActivated(int index)
    signal allSongsRequested()

    currentPivot: "search"

    // 只用这个词的结果；旧词的响应后到时不会冒充新词的结果
    readonly property var result: Source.searchFor(page.query)
    readonly property int half: Math.floor((body.width - Tokens.space6) / 2)
    readonly property int headerBlock: Tokens.height10 + Tokens.space3
    readonly property int pitch: Tokens.trackRow + Tokens.space2
    readonly property int listHeight: body.height - Tokens.height10 - Tokens.space6 - page.headerBlock
    readonly property int songRows: Math.max(1, Math.floor((page.listHeight + Tokens.space2) / page.pitch))
    readonly property int hotRows: Math.ceil(Math.min(20, Source.hotSearches.length) / 2)

    // 右栏：一类结果铺满可用的行列，不滚动
    property string collection: "artists"
    readonly property var collectionItems: page.result[page.collection] || []
    // 列数按最小磁贴 104 算，磁贴再等分整栏：右缘正好落在页边线上
    readonly property int tileColumns: Math.max(1, Math.floor((page.rightWidth + Tokens.space2) / (Tokens.tileSm + Tokens.space2)))
    readonly property int tileWidth: Math.floor((page.rightWidth - (page.tileColumns - 1) * Tokens.space2) / page.tileColumns)
    readonly property int rightWidth: body.width - page.half - Tokens.space6
    readonly property int tileHeight: page.tileWidth + Tokens.space2 + Tokens.height12
    function caption(item) {
        if (page.collection === "artists")
            return item.albums > 0 ? item.albums + " 张专辑" : "";
        if (page.collection === "albums")
            return item.artist || "";
        if (page.collection === "mvs")
            return item.playCount > 0 ? Format.count(item.playCount) + " 次播放" : "";
        return item.count > 0 ? item.count + " 首" : "";
    }

    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.leftMargin: Tokens.pageMargin
        anchors.rightMargin: Tokens.pageMargin
        anchors.bottomMargin: Tokens.space6
        spacing: Tokens.space6

        // 边输入边搜：停顿 500 毫秒才发请求（同一个词后端缓存 5 分钟）；回车立即搜；清空回到热门搜索。
        // 不用 SearchField 自带的自动提交：默认 100 毫秒几乎每个键都发一次网络请求，延迟模式又要等 2 秒

        Kirigami.SearchField {
            id: field
            Layout.preferredWidth: page.half
            implicitHeight: Tokens.height10
            placeholderText: "搜索歌曲、歌手、专辑、歌单"
            text: page.query
            focus: true
            autoAccept: false
            onTextEdited: typing.restart()
            onAccepted: {
                typing.stop();
                page.query = field.text.trim();
            }
            Timer {
                id: typing
                interval: 500
                onTriggered: page.query = field.text.trim()
            }
        }

        // 没有输入：热门搜索
        ColumnLayout {
            visible: page.query.length === 0
            Layout.fillWidth: true
            spacing: 0

            GroupHeader { text: "热门搜索" }
            Grid {
                rows: page.hotRows
                flow: Grid.TopToBottom
                columnSpacing: Tokens.space6
                Repeater {
                    model: Source.hotSearches.slice(0, 20)
                    delegate: AbstractButton {
                        id: hot
                        required property int index
                        required property var modelData
                        width: page.half
                        height: Tokens.height10
                        hoverEnabled: true
                        onClicked: page.query = modelData.word
                        background: Rectangle {
                            radius: Tokens.radius
                            color: Qt.alpha(Kirigami.Theme.highlightColor, hot.down ? Tokens.tintPressed : hot.hovered ? Tokens.tintHover : 0)
                        }
                        contentItem: RowLayout {
                            spacing: Tokens.space3
                            Label {
                                Layout.preferredWidth: Tokens.height8
                                horizontalAlignment: Text.AlignRight
                                text: hot.index + 1
                                font.pixelSize: Tokens.textBase
                                font.weight: hot.index < 3 ? Font.DemiBold : Font.Normal
                                font.features: { "tnum": 1 }
                                color: hot.index < 3 ? Kirigami.Theme.textColor : Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                            }
                            Label {
                                text: hot.modelData.word
                                font.pixelSize: Tokens.textBase
                                elide: Text.ElideRight
                                Layout.maximumWidth: page.half / 2
                            }
                            Label {
                                Layout.fillWidth: true
                                text: hot.modelData.note
                                font.pixelSize: Tokens.textSm
                                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
            Label {
                visible: Source.hotSearches.length === 0
                text: "暂时没有热门搜索"
                font.pixelSize: Tokens.textSm
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            }
        }

        // 有结果：左列单曲不滚动（放得下多少放多少，其余在「全部 N 首」里）；右栏结果磁贴可滚动
        RowLayout {
            visible: page.query.length > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Tokens.space6

            // 左：单曲
            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: page.half
                Layout.maximumWidth: page.half
                Layout.fillWidth: false
                spacing: 0
                GroupHeader {
                    text: "单曲"
                    actionText: page.result.songs.length > page.songRows ? "全部 " + page.result.songs.length + " 首" : ""
                    onActionTriggered: page.allSongsRequested()
                }
                Repeater {
                    model: page.result.songs.slice(0, page.songRows)
                    delegate: TrackTile {
                        Layout.fillWidth: true
                        Layout.bottomMargin: Tokens.space2
                        current: modelData.id === page.currentId
                        onActivated: index => page.trackActivated(index)
                    }
                }
                Label {
                    id: searchEmpty
                    visible: page.result.songs.length === 0
                    text: page.result.failed ? "搜索失败，点此重试"
                        : page.result.pending ? "正在搜索…" : "没有找到相关单曲"
                    font.pixelSize: Tokens.textSm
                    font.underline: page.result.failed
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    HoverHandler { enabled: page.result.failed === true; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        enabled: page.result.failed === true
                        onTapped: Source.retryKey("search:" + page.query)
                    }
                }
            }

            // 右：歌手 / 专辑 / 歌单，一次看一类（枢轴与左列的分组标题同高同字号），磁贴铺满整栏
            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Pivot {
                    Layout.bottomMargin: Tokens.space3
                    items: [{ key: "artists", text: "歌手" }, { key: "albums", text: "专辑" },
                            { key: "playlists", text: "歌单" }, { key: "mvs", text: "MV" }]
                    current: page.collection
                    onActivated: key => page.collection = key
                }
                // 与歌手页的专辑网格同一种排法：视图伸到窗口右缘，磁贴（列宽按页边线内算）止于页边线，
                // 滚动条落在页边距里，不压磁贴
                GridView {
                    id: collections
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.rightMargin: -Tokens.pageMargin
                    cellWidth: page.tileWidth + Tokens.space2
                    cellHeight: page.tileHeight + Tokens.space4
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: page.collectionItems
                    ScrollBar.vertical: ScrollBar {}
                    WheelScroll { flickable: collections }
                    delegate: MediaTile {
                        required property var modelData
                        width: page.tileWidth
                        title: modelData.name
                        caption: page.caption(modelData)
                        cover: modelData.cover
                        round: page.collection === "artists"
                        onClicked: page.collection === "artists" ? Actions.openArtist(modelData.id)
                                 : page.collection === "albums" ? Actions.openAlbum(modelData.id)
                                 : page.collection === "mvs" ? Actions.openMv(modelData.id)
                                 : Actions.openPlaylist(modelData.id)
                        // 歌单在哪里出现都是同一个菜单（这里能收藏）；歌手、专辑没有菜单
                        onMenuRequested: if (page.collection === "playlists") Actions.playlistMenuRequested(modelData)
                    }
                }
                Label {
                    visible: page.collectionItems.length === 0
                    text: page.result.pending ? "正在搜索…"
                        : page.collection === "artists" ? "没有找到相关歌手"
                        : page.collection === "albums" ? "没有找到相关专辑"
                        : page.collection === "mvs" ? "没有找到相关 MV" : "没有找到相关歌单"
                    font.pixelSize: Tokens.textSm
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                }
            }
        }

        Item {
            visible: page.query.length === 0
            Layout.fillHeight: true
        }
    }
}
