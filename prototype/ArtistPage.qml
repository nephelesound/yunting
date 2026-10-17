// 歌手页：与列表页同一个辅助窗格布局，左缘、窗格宽度、主动作位置都对齐。
// 左＝固定窗格（圆形照片 280，人物用圆形；事实、简介、主动作）；
// 右＝枢轴「歌曲 | 专辑」，下面是对应内容：曲目列（同列表页，按热度、滚到底续取）或专辑磁贴网格。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ScreenFrame {
    id: page

    property real artistId
    property int currentId
    property bool vip
    property string pane: "songs"   // songs | albums

    signal activated(int index)
    signal playAllRequested()
    signal shuffleRequested()

    readonly property var artist: Source.artist(page.artistId)
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

            Label {
                Layout.fillWidth: true
                Layout.topMargin: Tokens.space2
                visible: text.length > 0
                // 别名放在简介前；简介里已经写了别名就不重复
                text: page.artist.alias && !page.artist.description.includes(page.artist.alias)
                    ? page.artist.alias + "。" + page.artist.description : page.artist.description
                font.pixelSize: Tokens.textSm
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                wrapMode: Text.Wrap
                maximumLineCount: 3   // 与列表页的说明同一个截断
                elide: Text.ElideRight
            }

            RowLayout {
                Layout.topMargin: Tokens.space4
                spacing: Tokens.space2

                // 与列表页同一套主动作：放的就是右边列出来的这些（含滚动续取到的）
                AccentButton {
                    enabled: page.songs.length > 0
                    icon.name: "media-playback-start"
                    text: "播放全部"
                    onClicked: page.playAllRequested()
                }
                Button {
                    enabled: page.songs.length > 1
                    implicitHeight: Tokens.height8
                    flat: true
                    icon.name: "media-playlist-shuffle"
                    icon.width: Tokens.iconSm
                    icon.height: Tokens.iconSm
                    text: "随机播放"
                    onClicked: page.shuffleRequested()
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
                model: page.songs
                currentIndex: -1
                ScrollBar.vertical: ScrollBar {}
                onAtYEndChanged: if (atYEnd && page.artist.tracks.length > 0) Source.moreArtistSongs(page.artistId, page.artist.tracks)

                footer: PagingFooter {
                    width: ListView.view.width - ListView.view.rightMargin
                    paging: page.paging
                    loadingText: "正在取回更多歌曲…"
                    doneText: "已经是全部 " + page.songs.length + " 首"
                    onRetryRequested: Source.moreArtistSongs(page.artistId, page.artist.tracks)
                }

                delegate: TrackTile {
                    width: tracks.width - tracks.rightMargin
                    ranked: false   // 名次只给榜单；歌手的歌按热度排，但不是榜单名次
                    current: modelData.id === page.currentId
                    locked: modelData.fee === 4 || (!page.vip && modelData.fee === 1)
                    onActivated: index => page.activated(index)
                }

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - 2 * Tokens.pageMargin
                    visible: tracks.count === 0
                    icon.name: "view-media-artist"
                    text: Source.live ? "正在取回歌手的歌…" : "这位歌手的歌还没取到"
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
                ScrollBar.vertical: ScrollBar {}

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
