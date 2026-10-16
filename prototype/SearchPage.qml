// 搜索：找一首特定的歌。一屏放完，内容分左右两半（左缘、中缝都在同一条线上）：
//   没输入时：搜索框 + 热门搜索（20 个词，按名次两列、列优先排）；
//   有结果时：左＝单曲（单列，带专辑）；右＝歌手（圆形头像，人物用圆形是通行约定）与歌单。
// 搜索框宽度 = 左半宽，和下方左列对齐。原型快照只有一个查询词的结果。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ScreenFrame {
    id: page

    property string query
    property int currentId

    signal trackActivated(int index)

    currentPivot: "search"

    readonly property var result: Source.searchResult
    readonly property int half: Math.floor((body.width - Tokens.space6) / 2)
    readonly property int headerBlock: Tokens.height10 + Tokens.space3
    readonly property int pitch: Tokens.trackRow + Tokens.space2
    readonly property int listHeight: body.height - Tokens.height10 - Tokens.space6 - page.headerBlock
    readonly property int songRows: Math.max(1, Math.floor((page.listHeight + Tokens.space2) / page.pitch))
    readonly property int hotRows: Math.ceil(Math.min(20, Source.hotSearches.length) / 2)

    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.leftMargin: Tokens.pageMargin
        anchors.rightMargin: Tokens.pageMargin
        anchors.bottomMargin: Tokens.space6
        spacing: Tokens.space6

        Kirigami.SearchField {
            Layout.preferredWidth: page.half
            implicitHeight: Tokens.height10
            placeholderText: "搜索歌曲、歌手、歌单"
            text: page.query
            focus: true
            onAccepted: page.query = text
        }

        // ── 没有输入：热门搜索 ──
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

        // ── 有结果 ──
        RowLayout {
            visible: page.query.length > 0
            Layout.fillWidth: true
            spacing: Tokens.space6

            // 左：单曲
            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: page.half
                Layout.maximumWidth: page.half
                Layout.fillWidth: false
                spacing: 0
                GroupHeader { text: "单曲" }
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
                    visible: page.result.songs.length === 0
                    text: "没有找到相关单曲"
                    font.pixelSize: Tokens.textSm
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                }
            }

            // 右：歌手 + 歌单
            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth: true
                spacing: 0

                GroupHeader { text: "歌手" }
                Row {
                    spacing: Tokens.space2
                    Repeater {
                        model: page.result.artists.slice(0, Math.floor((page.half + Tokens.space2) / (Tokens.tileSm + Tokens.space2)))
                        delegate: MediaTile {
                            required property var modelData
                            width: Tokens.tileSm
                            title: modelData.name
                            caption: modelData.albums > 0 ? modelData.albums + " 张专辑" : ""
                            cover: modelData.cover
                            round: true
                        }
                    }
                }

                GroupHeader {
                    Layout.topMargin: Tokens.space6
                    text: "歌单"
                }
                Row {
                    spacing: Tokens.space2
                    Repeater {
                        model: page.result.playlists.slice(0, Math.floor((page.half + Tokens.space2) / (Tokens.tileSm + Tokens.space2)))
                        delegate: MediaTile {
                            required property var modelData
                            width: Tokens.tileSm
                            title: modelData.name
                            caption: modelData.count > 0 ? modelData.count + " 首" : ""
                            cover: modelData.cover
                        }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
