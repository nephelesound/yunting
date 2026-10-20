// 磁贴网格：铺满到页边线的方形磁贴（全部歌单、全部收藏，以及发现里的新碟、精品歌单、MV 共用）。
// 与歌手页的专辑网格同一种排法：视图延伸到窗口右缘，磁贴止于页边线，滚动条落在页边距里。
// 分页列表给 list（Source.paged 的结果）：滚到底取下一页，底部一行显示没取到 / 已经全部
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

GridView {
    id: grid

    property var list: null                 // 分页时：{ items, more, pending, failed, moreFailed, key }
    property var captionOf: item => ""
    property bool round                     // 歌手用圆形
    property string emptyIcon
    property string emptyText
    property string emptyExplanation
    property string doneText

    signal opened(var item)
    signal menuRequested(var item)

    readonly property int usable: width - Tokens.pageMargin
    readonly property int columns: Math.max(1, Math.floor((usable + Tokens.space4) / (Tokens.tileLg + Tokens.space4)))
    cellWidth: (usable + Tokens.space4) / columns
    cellHeight: cellWidth - Tokens.space4 + Tokens.space2 + Tokens.height12 + Tokens.space4
    bottomMargin: Tokens.space6
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    model: grid.list ? grid.list.items : []
    ScrollBar.vertical: AppScrollBar {}
    WheelScroll { flickable: grid }
    onAtYEndChanged: if (atYEnd && grid.list && grid.count > 0) Source.morePaged(grid.list)

    delegate: MediaTile {
        required property var modelData
        width: grid.cellWidth - Tokens.space4
        title: modelData.name
        caption: grid.captionOf(modelData)
        cover: modelData.cover
        round: grid.round
        onClicked: grid.opened(modelData)
        onMenuRequested: grid.menuRequested(modelData)
    }

    footer: PagingFooter {
        width: grid.usable
        paging: grid.list ? { items: grid.list.items, more: grid.list.more, loading: false, failed: grid.list.moreFailed || false } : null
        doneText: grid.doneText
        onRetryRequested: Source.morePaged(grid.list)
    }

    // 空着时：在取 / 没取到（可点重试）/ 真的没有
    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - 2 * Tokens.pageMargin
        visible: grid.count === 0
        icon.name: grid.list && (grid.list.pending || grid.list.failed) ? "" : grid.emptyIcon
        text: grid.list && grid.list.pending ? "正在取回…" : grid.list && grid.list.failed ? "没取到" : grid.emptyText
        explanation: grid.list && (grid.list.pending || grid.list.failed) ? "" : grid.emptyExplanation
        helpfulAction: Kirigami.Action {
            enabled: grid.list !== null && (grid.list.failed || false)
            text: "重试"
            icon.name: "view-refresh"
            onTriggered: Source.retryKey(grid.list.key)
        }
    }
}
