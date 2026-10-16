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
                color: Kirigami.Theme.disabledTextColor
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }

            RowLayout {
                Layout.topMargin: Tokens.space4
                spacing: Tokens.space2

                AccentButton {
                    icon.name: "media-playback-start"
                    text: "播放全部"
                    onClicked: page.playAllRequested()
                }
                IconButton {
                    icon.name: "media-playlist-shuffle"
                    text: "随机播放"
                    onClicked: page.shuffleRequested()
                }
                IconButton {
                    icon.name: "overflow-menu"
                    text: "更多"
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
                locked: !page.vip && modelData.fee === 1
                onActivated: index => page.activated(index)
            }
        }
    }
}
