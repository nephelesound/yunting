// 推荐（首页）：一屏放完。两排磁贴共用同一套列网格：
//   第一排＝为你推荐的入口（每日推荐 · 心动模式 · 私人漫游 · 私人雷达 · 相似歌曲 · 我喜欢的音乐），按常用程度排，
//          每块磁贴下方一句理由（从哪首歌出发），未登录时需要账号的入口显示为锁定磁贴；
//   第二排＝推荐歌单，末尾一块「排行榜」入口（榜单在官方客户端里也只是次要入口）。
// 列数由宽度决定（3–6 列），磁贴边长同时受高度约束，保证两排都在首屏。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import "Data.js" as Data
import "Personal.js" as Personal

ScreenFrame {
    id: page

    signal entryOpened(string key)
    signal playlistOpened(int index)

    currentPivot: "home"

    function large(url) { return url.replace("120y120", "400y400"); }
    function quoted(name) { return "「" + name + "」"; }

    readonly property var entries: page.loggedIn ? [
        { key: "daily", title: "每日推荐", cover: page.large(Personal.daily.tracks[0].cover), caption: "从" + page.quoted(Personal.daily.tracks[0].name) + "听起" },
        { key: "heart", title: "心动模式", cover: page.large(Personal.heart.tracks.find(t => t.id !== Personal.heart.seed.id).cover), caption: "从" + page.quoted(Personal.heart.seed.name) + "出发" },
        { key: "fm", title: "私人漫游", cover: page.large(Personal.fm.tracks[0].cover), caption: "从" + page.quoted(Personal.fm.tracks[0].name) + "开启漫游" },
        { key: "radar", title: "私人雷达", cover: Personal.radar.cover, caption: Personal.radar.description.split("|")[0] },
        { key: "similar", title: "相似歌曲", cover: page.large(Personal.similar.tracks[0].cover), caption: "和" + page.quoted(Personal.similar.seed.name) + "相似" },
        { key: "liked", title: "我喜欢的音乐", cover: page.large(Personal.liked.tracks[0].cover), caption: Personal.liked.count + " 首" },
    ] : [
        { key: "daily", title: "每日推荐", locked: true, caption: "登录后每天 6:00 更新" },
        { key: "heart", title: "心动模式", locked: true, caption: "登录后从红心歌曲出发" },
        { key: "fm", title: "私人漫游", cover: page.large(Personal.fm.tracks[0].cover), caption: "从" + page.quoted(Personal.fm.tracks[0].name) + "开启漫游" },
        { key: "similar", title: "相似歌曲", cover: page.large(Personal.similar.tracks[0].cover), caption: "和" + page.quoted(Personal.similar.seed.name) + "相似" },
        { key: "charts", title: "排行榜", symbol: "office-chart-line-symbolic", caption: Data.toplists.length + " 个官方与特色榜单" },
        { key: "liked", title: "我喜欢的音乐", locked: true, caption: "登录后同步红心歌曲" },
    ]

    // 列网格：最小磁贴 160。宽度够时磁贴撑满两条页边线之间；高度不够时边长改由高度决定（取 8 的倍数），网格靠左
    readonly property int textBlock: Tokens.space2 + Tokens.height12             // 磁贴下方两行字
    readonly property int columns: Math.max(3, Math.min(6, Math.floor((body.width + Tokens.space2) / (Tokens.tileLg + Tokens.space2))))
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
                    model: Personal.playlists.slice(0, page.columns - (page.loggedIn ? 1 : 0))
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
                    caption: Data.toplists.length + " 个官方与特色榜单"
                    symbol: "office-chart-line-symbolic"
                    onClicked: page.entryOpened("charts")
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
