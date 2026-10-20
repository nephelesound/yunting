// 相似：正在播放页右栏的第四栏，「还想听和这首像的」。先相似歌曲，再相似歌单。
// 点一首歌：插到「下一首」并立刻放，队列不动（跟着一首首听下去，这一栏随之换成新歌的相似）；
// 点一个歌单：打开歌单页。与百科同一种排法：组之间 24 的间距与小号组标题，不加框、不加分隔线
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: pane

    property var similar   // Source.similarTo(songId)
    property real currentId

    // 磁贴按栏宽排：每行能放几块就放几块，块宽一致、行尾不留半块
    readonly property int columns: Math.max(1, Math.floor((column.width + Tokens.space2) / (Tokens.tileSm + Tokens.space2)))
    readonly property real tile: (column.width - (pane.columns - 1) * Tokens.space2) / pane.columns

    contentHeight: column.implicitHeight + Tokens.space6
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: AppScrollBar {}
    WheelScroll { flickable: pane }

    component GroupLabel: Label {
        Layout.fillWidth: true
        Layout.bottomMargin: Tokens.space2
        font.pixelSize: Tokens.textSm
        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
    }

    ColumnLayout {
        id: column
        width: pane.width - Tokens.space4   // 右边留出滚动条
        spacing: Tokens.space6

        ColumnLayout {
            visible: pane.similar.songs.length > 0
            Layout.fillWidth: true
            spacing: 0
            GroupLabel { text: "相似歌曲" }
            Repeater {
                model: pane.similar.songs
                delegate: TrackTile {
                    Layout.fillWidth: true
                    Layout.bottomMargin: Tokens.space2   // 与其他曲目列表同一行距
                    current: modelData.id === pane.currentId
                    showAlbum: false   // 栏窄：专辑列让给标题
                    onActivated: Actions.playNowRequested(modelData)
                }
            }
        }

        ColumnLayout {
            visible: pane.similar.playlists.length > 0
            Layout.fillWidth: true
            spacing: 0
            GroupLabel { text: "相似歌单" }
            Flow {
                Layout.fillWidth: true
                spacing: Tokens.space2
                Repeater {
                    model: pane.similar.playlists
                    delegate: MediaTile {
                        required property var modelData
                        width: pane.tile
                        title: modelData.name
                        caption: modelData.count > 0 ? modelData.count + " 首" : ""
                        cover: modelData.cover
                        onClicked: Actions.playlistRequested(modelData.id)
                    }
                }
            }
        }
    }
}
