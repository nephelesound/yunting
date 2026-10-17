// 歌手页：与列表页同一个辅助窗格布局，左缘、窗格宽度、主动作位置都对齐。
// 左＝固定窗格（圆形照片 280——人物用圆形，事实、简介、主动作）；
// 右＝枢轴「热门歌曲 | 专辑」，下面是对应内容：曲目列（同列表页）或专辑磁贴网格。
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
    heading: page.artist.name

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
                text: page.artist.alias ? page.artist.alias + "。" + page.artist.description : page.artist.description
                font.pixelSize: Tokens.textSm
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                wrapMode: Text.Wrap
                maximumLineCount: 4
                elide: Text.ElideRight
            }

            RowLayout {
                Layout.topMargin: Tokens.space4
                spacing: Tokens.space2

                AccentButton {
                    enabled: page.artist.tracks.length > 0
                    icon.name: "media-playback-start"
                    text: "播放热门"
                    onClicked: page.playAllRequested()
                }
                Button {
                    enabled: page.artist.tracks.length > 1
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

        // ── 右：枢轴 + 内容 ──
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Tokens.space3

            Pivot {
                items: [{ key: "songs", text: "热门歌曲" },
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
                model: page.artist.tracks
                currentIndex: -1
                ScrollBar.vertical: ScrollBar {}

                delegate: TrackTile {
                    width: tracks.width - tracks.rightMargin
                    ranked: true
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
                    text: "还没有专辑"
                }
            }
        }
    }
}
