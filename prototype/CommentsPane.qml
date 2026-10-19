// 评论区（阅读模式）：歌曲的评论（正在播放页）与歌单的评论（歌单页）共用。
// 先精彩评论（3 条），再按时间排的全部评论（滚到底取下一页）；楼中楼展开在那条下面，一次只展开一条。
// 精彩评论也会出现在按时间排的分页里：同一条（用户 + 时间 + 内容）只在精彩评论里出现一次。
// 热评是列表头：等它落地后才拉最新页，免得热评晚到把整张表重建、滚回顶部
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Format.js" as Format

ListView {
    id: pane

    property var thread: null        // Source.commentThread(kind, id)；null 时什么也不取
    property bool active: true       // 这一栏显示着才取
    property bool loggedIn
    readonly property bool likeable: pane.thread !== null && pane.thread.kind === "song"   // 歌单评论接口只读

    signal loginRequested()

    readonly property var hot: pane.thread && pane.active ? Source.hotComments(pane.thread) : ({ loaded: false, items: [], total: 0 })
    readonly property var latest: pane.thread && pane.active ? Source.latestFor(pane.thread) : null
    function key(c) { return c.user + "\n" + c.time + "\n" + c.content; }
    readonly property var hotShown: pane.hot.items.slice(0, 3)
    readonly property var rows: {
        const hotKeys = {};
        for (const c of pane.hotShown)
            hotKeys[pane.key(c)] = true;
        const latest = (pane.latest ? pane.latest.items : []).filter(c => !hotKeys[pane.key(c)]);
        return pane.hotShown.map(c => Object.assign({ group: "hot" }, c))
            .concat(latest.map(c => Object.assign({ group: "latest" }, c)));
    }

    // 建立时 thread 先到、hot 还没求值：hot 没到就等它（hotLoaded 变了再来）
    function start() {
        if (!pane.thread || !pane.active || pane.latest !== null || !pane.hotLoaded)
            return;
        Source.moreComments(pane.thread);
    }
    onActiveChanged: pane.start()
    onThreadChanged: {
        pane.openThread = 0;
        pane.start();
    }
    readonly property bool hotLoaded: Boolean(pane.hot) && pane.hot.loaded
    onHotLoadedChanged: pane.start()
    // 换账号时 Source 把最新评论清空（里面有上一个账号的点赞状态）：开着的评论区要重新取，
    // 不然「最新评论」整段消失。放到下一轮再取：在 latest 的变化通知里改 latest 的来源是绑定环，
    // latest 会停在旧值。start() 放进一个在取的条目后 latest 不再为空，不会反复触发
    onLatestChanged: if (pane.latest === null) Qt.callLater(pane.start)
    Component.onCompleted: pane.start()

    // 展开着回复的那条评论（0 = 都没展开）。展开时要第一页回复，再点收起
    property real openThread: 0
    function toggleThread(commentId) {
        if (pane.openThread === commentId) {
            pane.openThread = 0;
            return;
        }
        pane.openThread = commentId;
        const replies = Source.replies(pane.thread, commentId);
        if (replies.pending)
            Source.need(replies.key);
    }

    clip: true
    spacing: Tokens.space6   // 评论之间 24：每条是一段独立的话，间距比行内大一档才分得开
    topMargin: Tokens.space3
    bottomMargin: Tokens.space6
    boundsBehavior: Flickable.StopAtBounds
    // 续取一页只追加新的行，滚动位置不变（直接用数组当 model 会整个重置、滚回顶部）
    model: GrowingModel {
        values: pane.rows
        keyOf: c => c.group + "\n" + pane.key(c)
    }
    ScrollBar.vertical: ScrollBar {}
    WheelScroll { flickable: pane }
    onAtYEndChanged: if (atYEnd && pane.thread && pane.active) Source.moreComments(pane.thread)

    footer: PagingFooter {
        width: ListView.view.width - Tokens.space4
        paging: pane.latest
        loadingText: "正在取回更多评论…"
        doneText: "没有更多评论了"
        onRetryRequested: Source.moreComments(pane.thread)
    }

    delegate: ColumnLayout {
        id: entry
        required property int index
        readonly property var modelData: pane.rows[entry.index] || ({})
        width: ListView.view.width - Tokens.space4
        spacing: Tokens.space3

        // 分组标题：每组第一条之前，与队列的「正在播放 / 接下来播放」同一种小节标题
        Label {
            // 列表变短时旧的委托还在、前一行可能已经没了
            visible: entry.index === 0 || pane.rows[entry.index - 1]?.group !== entry.modelData.group
            Layout.topMargin: entry.index === 0 ? 0 : Tokens.space6
            text: entry.modelData.group === "hot" ? "精彩评论"
                : "最新评论" + (pane.latest && pane.latest.total > 0 ? " " + Format.count(pane.latest.total) : "")
            font.pixelSize: Tokens.textSm
            color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
        }

        CommentRow {
            Layout.fillWidth: true
            comment: entry.modelData
            likeable: pane.likeable
            threadOpen: pane.openThread === entry.modelData.id
            onLikeRequested: liked => {
                if (!pane.loggedIn) {
                    pane.loginRequested();
                    return;
                }
                Source.setCommentLiked(pane.thread.id, entry.modelData.id, liked);
            }
            onThreadToggled: pane.toggleThread(entry.modelData.id)
        }

        // 楼中楼：展开在这条下面，缩进到正文的左缘；一次只展开一条（后端只留最近展开的那一条）
        ColumnLayout {
            id: replies
            readonly property var thread: pane.thread ? Source.replies(pane.thread, entry.modelData.id || 0) : ({ items: [], more: false, pending: false, failed: false })
            visible: pane.openThread === entry.modelData.id && entry.modelData.id > 0
            Layout.fillWidth: true
            Layout.leftMargin: Tokens.height8 + Tokens.space3
            spacing: Tokens.space4

            Repeater {
                model: replies.visible ? replies.thread.items : []
                delegate: CommentRow {
                    required property var modelData
                    Layout.fillWidth: true
                    comment: modelData
                    likeable: false
                    threadOf: entry.modelData.id || 0
                }
            }
            LinkText {
                visible: replies.thread.more || replies.thread.pending || replies.thread.failed
                parts: [{ text: replies.thread.failed ? "回复没取到，点此重试" : replies.thread.pending ? "正在取回回复…" : "更多回复",
                          link: !replies.thread.pending || replies.thread.failed }]
                font.pixelSize: Tokens.textSm
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                onActivated: replies.thread.failed ? Source.retryKey(replies.thread.key) : Source.moreReplies(replies.thread)
            }
        }
    }
}
