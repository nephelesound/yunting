// 歌手页：与列表页同一个辅助窗格布局，左缘、窗格宽度、主动作位置都对齐。
// 左＝固定窗格（圆形照片 280，人物用圆形；事实、简介、主动作）；
// 右＝枢轴「歌曲 | 专辑」，下面是对应内容：曲目列（同列表页，按热度、滚到底续取）或专辑磁贴网格。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Format.js" as Format

ScreenFrame {
    id: page
    scroller: page.pane === "songs" ? tracks : albums
    kept: ["pane"]
    background: PageAmbience { cover: page.artist.cover || "" }

    property real artistId
    property real currentId
    property bool vip
    property string pane: "songs"   // songs | albums

    signal activated(int index)
    signal playAllRequested()
    signal shuffleRequested()
    signal followToggled(real id, bool followed)

    readonly property var artist: Source.artist(page.artistId)
    readonly property bool followed: page.artist.followed === true
    // 歌曲列：先是热门 50 首，滚到底按热度接着往后取（与热门不重复）；点播、播放都用这份
    readonly property var songs: Source.artistSongs.owner === page.artistId ? Source.artistSongs.items : page.artist.tracks
    readonly property var paging: Source.artistSongs.owner === page.artistId ? Source.artistSongs : null
    heading: page.artist.name

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
                source: page.artist.cover
                pixelSize: 560
                radius: Tokens.trackColumn / 2
            }

            Facts {
                Layout.fillWidth: true
                Layout.topMargin: Tokens.space4
                items: {
                    const f = [];
                    if (page.artist.musicSize > 0)
                        f.push({ value: String(page.artist.musicSize), label: "首单曲" });
                    if (page.artist.albumSize > 0)
                        f.push({ value: String(page.artist.albumSize), label: "张专辑" });
                    return f;
                }
            }

            Blurb {
                Layout.fillWidth: true
                Layout.topMargin: Tokens.space2
                // 别名放在简介前；简介里已经写了别名就不重复
                text: page.artist.alias && !page.artist.description.includes(page.artist.alias)
                    ? page.artist.alias + "。" + page.artist.description : page.artist.description
                title: page.artist.name
            }

            RowLayout {
                Layout.topMargin: Tokens.space4
                spacing: Tokens.space2

                // 与列表页同一套主动作：放的就是右边列出来的这些（含滚动续取到的）
                AccentButton {
                    enabled: page.songs.length > 0
                    icon.name: "media-playback-start"
                    text: "播放"
                    onClicked: page.playAllRequested()
                }
                // 窗格只有 280 宽：随机播放只留图标（悬停有提示），把位置让给有状态的「关注」
                IconButton {
                    enabled: page.songs.length > 1
                    icon.name: "media-playlist-shuffle"
                    text: "随机播放"
                    onClicked: page.shuffleRequested()
                }
                Button {
                    enabled: page.artistId > 0
                    implicitHeight: Tokens.height8
                    flat: true
                    icon.name: page.followed ? "bookmark-remove" : "bookmark-new"
                    icon.width: Tokens.iconSm
                    icon.height: Tokens.iconSm
                    text: page.followed ? "已关注" : "关注"
                    onClicked: page.followToggled(page.artistId, !page.followed)
                }
            }
        }

        // 右：枢轴 + 内容
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Tokens.space3

            Pivot {
                items: [{ key: "songs", text: page.artist.musicSize > 0 ? "歌曲 " + page.artist.musicSize : "歌曲" },
                        { key: "albums", text: page.artist.albums.length > 0 ? "专辑 " + page.artist.albums.length : "专辑" }]
                current: page.pane
                onActivated: key => page.pane = key
            }

            ListView {
                id: tracks
                visible: page.pane === "songs"
                Layout.fillWidth: true
                Layout.fillHeight: true
                rightMargin: Tokens.pageMargin
                bottomMargin: Tokens.space6
                spacing: Tokens.space2
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                // 续取只追加新的行，滚动位置不变（见 GrowingModel）
                model: GrowingModel { values: page.songs }
                currentIndex: -1
                ScrollBar.vertical: AppScrollBar {}
                WheelScroll { flickable: tracks }
                LocateCurrent {
                    parent: tracks
                    view: tracks
                    index: page.songs.findIndex(t => t.id === page.currentId)
                }
                onAtYEndChanged: if (atYEnd && page.artist.tracks.length > 0) Source.moreArtistSongs(page.artistId, page.artist.tracks)

                footer: PagingFooter {
                    width: ListView.view.width - ListView.view.rightMargin
                    paging: page.paging
                    loadingText: "正在取回更多歌曲…"
                    doneText: "已经是全部 " + page.songs.length + " 首"
                    onRetryRequested: Source.moreArtistSongs(page.artistId, page.artist.tracks)
                }

                delegate: TrackTile {
                    modelData: page.songs[index] || ({ id: 0, name: "", artist: "", dur: 0, fee: 0, cover: "" })
                    width: tracks.width - tracks.rightMargin
                    ranked: false   // 名次只给榜单；歌手的歌按热度排，但不是榜单名次
                    current: modelData.id === page.currentId
                    locked: Format.blockedBy(modelData, page.vip) !== ""
                    onActivated: index => page.activated(index)
                }

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - 2 * Tokens.pageMargin
                    visible: tracks.count === 0
                    icon.name: "view-media-artist"
                    text: page.artist.failed ? "歌手的歌取不到，点此重试"
                        : Source.live ? "正在取回歌手的歌…" : "这位歌手的歌还没取到"
                    HoverHandler { enabled: page.artist.failed === true; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        enabled: page.artist.failed === true
                        onTapped: Source.retryKey("artist:" + page.artistId)
                    }
                }
            }

            // 专辑网格：列数由宽度决定，磁贴等宽铺满到页边线；视图本身延伸到窗口右缘，
            // 滚动条落在页边距里（与曲目列表一致）
            GridView {
                id: albums
                visible: page.pane === "albums"
                Layout.fillWidth: true
                Layout.fillHeight: true
                readonly property int usable: width - Tokens.pageMargin
                readonly property int columns: Math.max(1, Math.floor((usable + Tokens.space4) / (Tokens.tileLg + Tokens.space4)))
                cellWidth: (usable + Tokens.space4) / columns
                cellHeight: cellWidth - Tokens.space4 + Tokens.space2 + Tokens.height12 + Tokens.space4
                bottomMargin: Tokens.space6
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: page.artist.albums
                ScrollBar.vertical: AppScrollBar {}
                WheelScroll { flickable: albums }

                delegate: MediaTile {
                    required property var modelData
                    width: albums.cellWidth - Tokens.space4
                    title: modelData.name
                    caption: [modelData.year > 0 ? modelData.year : "", modelData.count > 0 ? modelData.count + " 首" : ""].filter(s => s !== "").join("  ")
                    cover: modelData.cover
                    onClicked: Actions.openAlbum(modelData.id)
                }

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Tokens.pageMargin
                    visible: albums.count === 0
                    icon.name: "view-media-album-cover"
                    // 歌手数据还没回来时不说「没有」
                    text: page.artist.name === "" && Source.live ? "正在取回专辑…" : "还没有专辑"
                }
            }
        }
    }
}
