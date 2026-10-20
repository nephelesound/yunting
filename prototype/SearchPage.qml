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
    scroller: collections
    kept: ["query", "collection"]

    property string query
    onQueryChanged: Source.search(page.query)
    Component.onCompleted: Source.search(page.query)
    property real currentId

    signal trackActivated(int index)
    signal allSongsRequested()

    // 截图 harness（startOverlay=suggest）：像刚打过字那样亮出建议
    function showSuggestions() {
        Source.suggest(field.text.trim());
        suggestions.dismissed = false;
    }

    currentPivot: "search"

    // 只用这个词的结果；旧词的响应后到时不会冒充新词的结果
    readonly property var result: Source.searchFor(page.query)
    // 搜索建议：按接口给的顺序分组，每组最多 3 条，合起来不超过 10 条
    readonly property var suggestionRows: {
        const s = Source.suggestions(field.text.trim());
        if (!s)
            return [];
        const groups = { songs: "单曲", artists: "歌手", albums: "专辑", playlists: "歌单" };
        const rows = [];
        for (const kind of s.order.filter(k => groups[k] !== undefined)) {
            for (const item of (s[kind] || []).slice(0, 3))
                rows.push({ group: groups[kind], kind: kind, id: item.id, name: item.name,
                            detail: kind === "songs" || kind === "albums" ? item.artist || "" : "" });
        }
        return rows.slice(0, 10);
    }
    // 打字时新词的建议还没到：先留着上一份，不让列表一闪一闪；清空输入才清掉
    property var shownSuggestions: []
    onSuggestionRowsChanged: {
        if (page.suggestionRows.length > 0 || field.text.trim().length === 0)
            page.shownSuggestions = page.suggestionRows;
    }
    readonly property int half: Math.floor((body.width - Tokens.space6) / 2)
    readonly property int headerBlock: Tokens.height10 + Tokens.space3
    readonly property int pitch: Tokens.trackRow + Tokens.space2
    readonly property int listHeight: body.height - Tokens.height10 - Tokens.space6 - page.headerBlock
    readonly property int songRows: Math.max(1, Math.floor((page.listHeight + Tokens.space2) / page.pitch))
    readonly property int hotRows: Math.ceil(Math.min(20, Source.hotSearches.length) / 2)

    // 右栏：一类结果铺满可用的行列，不滚动
    property string collection: "artists"
    // 电台、用户另外搜（切到那一类时才要）
    readonly property var separate: page.collection === "radios" ? Source.searchSeparate("searchRadios", page.query)
        : page.collection === "users" ? Source.searchSeparate("searchUsers", page.query) : null
    readonly property var collectionItems: page.separate ? page.separate.items : page.result[page.collection] || []
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
        if (page.collection === "radios")
            return item.host || "";
        if (page.collection === "users")
            return item.fans > 0 ? Format.count(item.fans) + " 粉丝" : "";
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
            // 空着时显示网易推荐的那句；空着回车就搜它（与网易云客户端一样）
            readonly property var suggested: Source.defaultSearch()
            readonly property string suggestedQuery: field.suggested ? field.suggested.realKeyword || "" : ""
            Layout.preferredWidth: page.half
            implicitHeight: Tokens.height10
            placeholderText: field.suggested && field.suggested.showKeyword ? field.suggested.showKeyword : "搜索歌曲、歌手、专辑、歌单"
            text: page.query
            focus: true
            autoAccept: false
            onTextEdited: {
                typing.restart();
                suggesting.restart();
                suggestions.dismissed = false;
            }
            onAccepted: {
                typing.stop();
                if (suggestions.opened && suggestionList.currentIndex >= 0) {
                    suggestions.pick(page.shownSuggestions[suggestionList.currentIndex]);
                    return;
                }
                suggestions.dismissed = true;
                // 清除按钮先清空输入再发 accepted()：此时 page.query 还留着被清掉的词。
                // 空着回车时 page.query 本来就是空的——只有那种情况才搜推荐词。
                const cleared = field.text.trim() === "" && page.query.length > 0;
                if (!cleared && field.text.trim() === "" && field.suggestedQuery.length > 0)
                    field.text = field.suggestedQuery;
                page.query = field.text.trim();
            }
            // 建议列表里上下选（焦点留在输入框，照常打字）；Esc 先收起建议
            Keys.onDownPressed: if (suggestions.opened) suggestionList.incrementCurrentIndex()
            Keys.onUpPressed: if (suggestions.opened) suggestionList.decrementCurrentIndex()
            Keys.onEscapePressed: event => {
                event.accepted = suggestions.opened;
                suggestions.dismissed = true;
            }
            Timer {
                id: typing
                interval: 500
                onTriggered: page.query = field.text.trim()
            }
            // 建议比搜索轻得多，停顿 200 毫秒就取
            Timer {
                id: suggesting
                interval: 200
                onTriggered: Source.suggest(field.text.trim())
            }

            // 搜索建议：边打字边给出可以直接去的歌手、专辑、歌单（歌曲换成搜它）。
            // 结果照常在下面边输入边出现，建议只是跳过去的捷径：选了、回车、Esc 或点别处就收起
            Popup {
                id: suggestions
                // 只有打字才打开：回到搜索页时（输入框里还留着上次的词）不自己弹出来
                property bool dismissed: true
                function pick(row) {
                    suggestions.dismissed = true;
                    if (row.kind === "artists")
                        Actions.openArtist(row.id);
                    else if (row.kind === "albums")
                        Actions.openAlbum(row.id);
                    else if (row.kind === "playlists")
                        Actions.openPlaylist(row.id);
                    else {
                        field.text = row.name;
                        typing.stop();
                        page.query = row.name;
                    }
                }
                // 开关由这几样决定；点别处由弹层自己关，关了就算收起（下一次打字再出来）
                readonly property bool wanted: field.activeFocus && !suggestions.dismissed && page.shownSuggestions.length > 0
                onWantedChanged: suggestions.wanted ? suggestions.open() : suggestions.close()
                onClosed: suggestions.dismissed = true
                y: field.height + Tokens.space1
                width: field.width
                padding: Tokens.space1
                closePolicy: Popup.CloseOnPressOutsideParent

                contentItem: ListView {
                    id: suggestionList
                    implicitHeight: contentHeight
                    interactive: false
                    currentIndex: -1
                    model: page.shownSuggestions
                    onModelChanged: currentIndex = -1
                    section.property: "group"
                    section.delegate: Label {
                        required property string section
                        width: ListView.view.width
                        leftPadding: Tokens.space2
                        topPadding: Tokens.space1
                        bottomPadding: Tokens.space1
                        text: section
                        font.pixelSize: Tokens.textSm
                        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    }
                    delegate: ItemDelegate {
                        id: suggestion
                        required property var modelData
                        required property int index
                        width: ListView.view.width
                        leftPadding: Tokens.space2   // 与组标题同一条左缘
                        highlighted: ListView.isCurrentItem
                        text: suggestion.modelData.name
                        onClicked: suggestions.pick(suggestion.modelData)
                        contentItem: RowLayout {
                            spacing: Tokens.space2
                            Label {
                                Layout.fillWidth: true
                                Layout.maximumWidth: Math.ceil(implicitWidth)
                                text: suggestion.modelData.name
                                font.pixelSize: Tokens.textBase
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: text.length > 0
                                text: suggestion.modelData.detail
                                font.pixelSize: Tokens.textSm
                                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
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
                            { key: "playlists", text: "歌单" }, { key: "mvs", text: "MV" }, { key: "radios", text: "电台" },
                            { key: "users", text: "用户" }]
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
                    ScrollBar.vertical: AppScrollBar {}
                    WheelScroll { flickable: collections }
                    delegate: MediaTile {
                        required property var modelData
                        width: page.tileWidth
                        title: modelData.name
                        caption: page.caption(modelData)
                        cover: modelData.cover
                        round: page.collection === "artists" || page.collection === "users"
                        pendant: page.collection === "users" ? modelData.avatarPendant || "" : ""
                        onClicked: page.collection === "artists" ? Actions.openArtist(modelData.id)
                                 : page.collection === "users" ? Actions.openUser(modelData.id)
                                 : page.collection === "albums" ? Actions.openAlbum(modelData.id)
                                 : page.collection === "mvs" ? Actions.openMv(modelData.id)
                                 : page.collection === "radios" ? Actions.openRadio(modelData.id)
                                 : Actions.openPlaylist(modelData.id)
                        // 歌单在哪里出现都是同一个菜单（这里能收藏）；歌手、专辑没有菜单
                        onMenuRequested: if (page.collection === "playlists") Actions.playlistMenuRequested(modelData)
                    }
                }
                Label {
                    visible: page.collectionItems.length === 0
                    text: (page.separate ? page.separate.pending : page.result.pending) ? "正在搜索…"
                        : page.collection === "artists" ? "没有找到相关歌手"
                        : page.collection === "albums" ? "没有找到相关专辑"
                        : page.collection === "mvs" ? "没有找到相关 MV"
                        : page.collection === "radios" ? "没有找到相关电台"
                        : page.collection === "users" ? "没有找到相关用户" : "没有找到相关歌单"
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
