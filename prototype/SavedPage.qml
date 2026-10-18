// 全部收藏：「我的」第二排放不下的关注的歌手、收藏的专辑与 MV。枢轴分三类（带总数），
// 下面是铺满到页边线的磁贴网格，滚到底接着取。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScreenFrame {
    id: page
    scroller: grid
    kept: ["kind"]

    property string kind: "artists"   // artists | albums | mvs

    heading: "全部收藏"

    readonly property var list: Source.saved(page.kind)
    // 枢轴上的总数：当前这类用列表自己的，其余用收藏数（专辑没有收藏数，切到时才有）
    Component.onCompleted: Source.need("userSubcount")
    function countText(kind, text) {
        const sub = Source.subcount || {};
        const n = kind === page.kind ? page.list.count || 0
            : kind === "artists" ? sub.artistCount || 0 : kind === "mvs" ? sub.mvCount || 0 : 0;
        return n > 0 ? text + " " + n : text;
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Tokens.pageMargin
        spacing: Tokens.space3

        Pivot {
            items: [{ key: "artists", text: page.countText("artists", "歌手") },
                    { key: "albums", text: page.countText("albums", "专辑") },
                    { key: "mvs", text: page.countText("mvs", "MV") }]
            current: page.kind
            onActivated: key => page.kind = key
        }

        TileGrid {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            // MV 收藏里的视频没有 MV 页可开，只列 MV
            list: page.kind === "mvs" ? Object.assign({}, page.list, { items: page.list.items.filter(m => m.kind === "mv") }) : page.list
            round: page.kind === "artists"
            captionOf: item => page.kind === "artists" ? (item.albums > 0 ? item.albums + " 张专辑" : "") : item.artist || ""
            emptyIcon: page.kind === "artists" ? "im-user" : page.kind === "albums" ? "media-optical-audio" : "media-playback-start"
            emptyText: ({ artists: "还没有关注的歌手", albums: "还没有收藏的专辑", mvs: "还没有收藏的 MV" })[page.kind]
            emptyExplanation: page.kind === "artists" ? "在歌手页点「关注」" : page.kind === "albums" ? "在专辑页点「收藏专辑」" : ""
            onOpened: item => page.kind === "artists" ? Actions.openArtist(item.id)
                : page.kind === "albums" ? Actions.openAlbum(item.id) : Actions.openMv(item.id)
        }
    }
}
