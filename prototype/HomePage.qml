// 推荐（首页）：一屏放完。两排磁贴共用同一套列网格：
//   第一排＝为你推荐的入口（每日推荐 · 心动模式 · 私人漫游 · 私人雷达 · 相似歌曲 · 我喜欢的音乐），按常用程度排，
//          每块磁贴下方一句理由（从哪首歌出发），未登录时需要账号的入口显示为锁定磁贴；
//   第二排＝推荐歌单，末尾一块「排行榜」入口（榜单在官方客户端里也只是次要入口）。
// 固定 6 列，磁贴边长由宽度与高度共同约束，保证两排都在首屏。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

ScreenFrame {
    id: page

    signal entryOpened(string key)
    signal playlistOpened(int index)

    currentPivot: "home"

    // 入口内容（封面、理由、缺数据时的退化）由 Source 决定，这里只管排版
    readonly property var entries: Source.homeEntries(page.loggedIn)

    // 列网格：宽度够时磁贴撑满两条页边线之间；高度不够时边长改由高度决定（取 8 的倍数），网格靠左
    readonly property int textBlock: Tokens.space2 + Tokens.height12             // 磁贴下方两行字
    // 固定 6 列：第一排的六个入口是首页的主体，窄窗口下磁贴变小，但一个都不能少（最窄时约 118）
    readonly property int columns: 6
    readonly property int tileByWidth: Math.floor((body.width - (columns - 1) * Tokens.space2) / columns)
    readonly property int tileByHeight: Math.floor((body.height - Tokens.space6 - Tokens.height10 - Tokens.space3 - 2 * textBlock) / 2 / Tokens.space2) * Tokens.space2
    readonly property int tile: Math.min(tileByWidth, tileByHeight)

    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.leftMargin: Tokens.pageMargin
        anchors.rightMargin: Tokens.pageMargin
        anchors.bottomMargin: Tokens.space6
        spacing: Tokens.space6

        Row {
            spacing: Tokens.space2
            Repeater {
                model: page.entries.slice(0, page.columns)
                delegate: MediaTile {
                    required property var modelData
                    width: page.tile
                    title: modelData.title
                    caption: modelData.caption
                    cover: modelData.cover || ""
                    symbol: modelData.symbol || ""
                    locked: modelData.locked || false
                    onClicked: page.entryOpened(modelData.key)
                }
            }
        }

        ColumnLayout {
            spacing: 0
            GroupHeader { text: "推荐歌单" }
            Row {
                spacing: Tokens.space2
                Repeater {
                    model: Source.playlists.slice(0, page.columns - (page.loggedIn ? 1 : 0))
                    delegate: MediaTile {
                        required property var modelData
                        required property int index
                        width: page.tile
                        title: modelData.name
                        // 推荐理由优先；没有理由时写曲目数，动态歌单的 0 首不显示（缺失 ≠ 零）
                        caption: modelData.reason || (modelData.count > 0 ? modelData.count + " 首" : "")
                        cover: modelData.cover
                        onClicked: page.playlistOpened(index)
                    }
                }
                MediaTile {
                    visible: page.loggedIn
                    width: page.tile
                    title: "排行榜"
                    caption: Source.chartsCaption
                    symbol: "office-chart-line-symbolic"
                    onClicked: page.entryOpened("charts")
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
