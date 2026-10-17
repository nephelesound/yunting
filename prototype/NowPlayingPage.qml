// 正在播放：密度最低的一屏。版面按 48 的行距排：封面边长、封面顶边、右栏各行都是 48 的整数倍。
// 左半屏居中放封面与歌手/专辑；右半屏第一行是「歌词 评论」枢轴，下面是所选的内容：
//   歌词：唯一的视觉焦点是当前句（36，其余 24 弱化），当前句与封面中线同高；有译文的句子占两行，译文在下
//   评论：网易云的热门评论，阅读模式，不对齐歌词网格
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Format.js" as Format

ScreenFrame {
    id: page

    property var track
    property real elapsed
    property string pane: "lyrics"   // lyrics | comments

    heading: page.track ? page.track.name : ""

    // 只铺页面区域（含页头），播放条保持原底色：进度线仍是唯一的分界
    background: CoverAmbience {
        cover: page.track ? page.track.cover : ""
    }

    readonly property int pitch: Tokens.height12
    readonly property int rows: Math.floor((areaHeight - Tokens.space6) / pitch)
    readonly property int areaHeight: right.parent ? right.parent.height : 0
    // 封面占奇数行，中线正好穿过某一行的中心，当前句就停在那一行；留两行给封面下方的事实
    readonly property int coverRows: { const n = Math.min(7, page.rows - 2); return n % 2 ? n : n - 1; }
    readonly property int coverTopRow: Math.max(1, Math.floor((page.rows - page.coverRows - 1) / 2))
    readonly property int focusRow: page.coverTopRow + Math.floor(page.coverRows / 2)

    readonly property var lines: page.track ? Source.lyrics(page.track.id) : []
    readonly property var comments: page.track && page.pane === "comments" ? Source.comments(page.track.id) : []
    // 最新评论（分页累积）：只认这首歌的；打开评论或换歌时从第一页开始取
    readonly property var latest: page.track && page.pane === "comments" && Source.latestComments.owner === page.track.id
        ? Source.latestComments : null
    // 精彩评论也会出现在按时间排的分页里：同一条（用户 + 时间 + 内容）只在精彩评论里出现一次
    function commentKey(c) { return c.user + "\n" + c.time + "\n" + c.content; }
    // 精彩评论只放 3 条（接口按点赞排好序），下面接按时间排的全部评论
    readonly property var hotShown: page.comments.slice(0, 3)
    readonly property var commentRows: {
        const hotKeys = {};
        for (const c of page.hotShown)
            hotKeys[page.commentKey(c)] = true;
        const latest = (page.latest ? page.latest.items : []).filter(c => !hotKeys[page.commentKey(c)]);
        return page.hotShown.map(c => Object.assign({ group: "hot" }, c))
            .concat(latest.map(c => Object.assign({ group: "latest" }, c)));
    }
    function startComments() {
        if (page.track && page.pane === "comments" && Source.latestComments.owner !== page.track.id)
            Source.moreComments(page.track.id);
    }
    onPaneChanged: page.startComments()
    onTrackChanged: page.startComments()
    Component.onCompleted: page.startComments()
    readonly property int lyricIndex: {
        let i = 0;
        while (i + 1 < page.lines.length && page.lines[i + 1].t <= page.elapsed)
            i++;
        return i;
    }

    function count(n) {
        return n >= 10000 ? (n / 10000).toFixed(n >= 100000 ? 0 : 1) + "万" : String(n);
    }

    // 左半：封面与事实。这一屏是「左封面、右歌词」的两栏构图：封面在左半栏水平居中是有意的构图轴
    //（不贴页边线），事实行与封面左缘对齐；右栏从中线开始
    ColumnLayout {
        x: Math.round((parent.width / 2 - width) / 2)
        y: page.coverTopRow * page.pitch
        width: page.coverRows * page.pitch
        spacing: Tokens.space4

        Cover {
            Layout.preferredWidth: page.coverRows * page.pitch
            Layout.preferredHeight: Layout.preferredWidth
            source: page.track ? Source.sized(page.track.cover, 800) : ""
            pixelSize: 800
        }
        Facts {
            Layout.fillWidth: true
            // 歌手、专辑都可点：去歌手页 / 专辑页
            items: page.track ? [{ label: page.track.artist, artistId: page.track.artistId || 0 },
                                 { label: page.track.album, albumId: page.track.albumId || 0 }] : []
        }
    }

    // 右半
    Item {
        id: right
        x: parent.width / 2
        width: parent.width / 2 - Tokens.pageMargin
        height: page.rows * page.pitch

        // 第 0 行：枢轴，与页头枢轴同一种语言（淡出的可点）
        Pivot {
            rowHeight: page.pitch
            items: [{ key: "lyrics", text: "歌词" }, { key: "comments", text: "评论" }]
            current: page.pane
            onActivated: key => page.pane = key
        }

        // 歌词
        ListView {
            id: lyrics
            visible: page.pane === "lyrics"
            y: page.pitch
            width: parent.width
            height: (page.rows - 1) * page.pitch
            clip: true
            model: page.lines
            currentIndex: page.lyricIndex
            highlightRangeMode: ListView.StrictlyEnforceRange
            preferredHighlightBegin: (page.focusRow - 1) * page.pitch
            preferredHighlightEnd: page.focusRow * page.pitch
            // 换句时列表平滑上移（屏上移动 → ease-in-out）；歌词是被动观看的环境运动，所以用最慢的一档
            highlightMoveDuration: Tokens.motionSlow
            highlightMoveVelocity: -1

            delegate: Item {
                id: line
                required property int index
                required property var modelData
                readonly property bool current: index === page.lyricIndex
                readonly property string translation: modelData.tr || ""
                width: ListView.view.width
                // 有译文的句子占两行网格：原文贴第一行底边，译文贴第二行顶边，读起来是一组
                height: line.translation.length > 0 ? 2 * page.pitch : page.pitch

                // 每句都按展示字号排版，非当前句用 scale 缩到 24/36：换句时只动 transform 与颜色，不重排文字
                Label {
                    id: original
                    readonly property real rest: Tokens.text2xl / Tokens.text4xl
                    width: parent.width / original.scale   // 缩小后仍占满列宽，省略号落在列的右缘
                    height: page.pitch
                    verticalAlignment: line.translation.length > 0 ? Text.AlignBottom : Text.AlignVCenter
                    transformOrigin: line.translation.length > 0 ? Item.BottomLeft : Item.Left
                    scale: line.current ? 1 : original.rest
                    text: line.modelData.text
                    font.pixelSize: Tokens.text4xl
                    font.weight: Font.Medium
                    color: line.current ? Kirigami.Theme.textColor : Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    elide: Text.ElideRight
                    Behavior on scale { NumberAnimation { duration: Tokens.motionBase; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut } }
                    Behavior on color { ColorAnimation { duration: Tokens.motionBase } }
                }
                Label {
                    visible: line.translation.length > 0
                    y: page.pitch
                    width: parent.width
                    topPadding: Tokens.space1
                    text: line.translation
                    font.pixelSize: Tokens.textBase
                    color: Qt.alpha(Kirigami.Theme.textColor, line.current ? 1 : Tokens.muted)
                    elide: Text.ElideRight
                    Behavior on color { ColorAnimation { duration: Tokens.motionBase } }
                }
            }
        }

        Label {
            y: page.focusRow * page.pitch
            height: page.pitch
            visible: page.pane === "lyrics" && page.lines.length === 0
            verticalAlignment: Text.AlignVCenter
            text: page.track ? "暂无歌词" : "还没有在播放的歌"
            font.pixelSize: Tokens.text2xl
            color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
        }

        // 评论：阅读模式，头像、名字、内容、赞数。先精彩评论，再最新评论（滚到底取下一页）
        ListView {
            id: commentList
            visible: page.pane === "comments"
            y: page.pitch
            width: parent.width
            height: (page.rows - 1) * page.pitch
            clip: true
            spacing: Tokens.space6   // 评论之间 24：每条是一段独立的话，间距比行内大一档才分得开
            topMargin: Tokens.space3
            bottomMargin: Tokens.space6
            boundsBehavior: Flickable.StopAtBounds
            model: page.commentRows
            ScrollBar.vertical: ScrollBar {}
            onAtYEndChanged: if (atYEnd && page.track && page.pane === "comments") Source.moreComments(page.track.id)

            footer: PagingFooter {
                width: ListView.view.width - Tokens.space4
                paging: page.latest
                loadingText: "正在取回更多评论…"
                doneText: "没有更多评论了"
                onRetryRequested: Source.moreComments(page.track.id)
            }

            delegate: ColumnLayout {
                id: comment
                required property var modelData
                required property int index
                width: ListView.view.width - Tokens.space4
                spacing: Tokens.space3

                // 分组标题：每组第一条之前，与队列的「正在播放 / 接下来播放」同一种小节标题
                Label {
                    visible: comment.index === 0 || page.commentRows[comment.index - 1].group !== comment.modelData.group
                    Layout.topMargin: comment.index === 0 ? 0 : Tokens.space6
                    text: comment.modelData.group === "hot" ? "精彩评论"
                        : "最新评论" + (page.latest && page.latest.total > 0 ? " " + page.count(page.latest.total) : "")
                    font.pixelSize: Tokens.textSm
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                }

                // 一条评论：头像 | 作者与时间挨在一起（都是关于「谁、何时」），赞数在右；正文放宽行距便于阅读
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Tokens.space3

                    PersonAvatar {
                        Layout.alignment: Qt.AlignTop
                        implicitWidth: Tokens.height8
                        implicitHeight: Tokens.height8
                        name: comment.modelData.user
                        source: comment.modelData.avatar || ""
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Tokens.space1
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Tokens.space2
                            Label {
                                Layout.maximumWidth: implicitWidth
                                Layout.fillWidth: true
                                text: comment.modelData.user
                                font.pixelSize: Tokens.textSm
                                elide: Text.ElideRight
                            }
                            Label {
                                text: Format.commentTime(comment.modelData.time)
                                font.pixelSize: Tokens.textSm
                                font.features: { "tnum": 1 }
                                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: page.count(comment.modelData.likes) + " 赞"
                                font.pixelSize: Tokens.textSm
                                font.features: { "tnum": 1 }
                                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: comment.modelData.content
                            font.pixelSize: Tokens.textBase
                            lineHeight: 1.4
                            wrapMode: Text.Wrap
                            maximumLineCount: 6
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        // 评论空着时：在取（10 秒内）/ 没取到（可点重试）/ 原型里就是没有。Store 失败不回调，只能靠超时判断
        Label {
            id: commentsEmpty
            readonly property bool waiting: page.pane === "comments" && page.commentRows.length === 0 && Source.live && page.track !== null
            property bool timedOut: false
            onWaitingChanged: {
                commentsEmpty.timedOut = false;
                if (commentsEmpty.waiting)
                    commentsTimeout.restart();
            }
            Timer { id: commentsTimeout; interval: 10000; onTriggered: commentsEmpty.timedOut = commentsEmpty.waiting }

            y: page.focusRow * page.pitch
            height: page.pitch
            visible: page.pane === "comments" && page.commentRows.length === 0
            verticalAlignment: Text.AlignVCenter
            text: !page.track ? "还没有在播放的歌" : !Source.live ? "暂无评论"
                : commentsEmpty.timedOut ? "没取到评论，点此重试" : "正在取回评论…"
            font.pixelSize: Tokens.text2xl
            font.underline: commentsEmpty.timedOut && retryHover.hovered
            color: commentsEmpty.timedOut ? Kirigami.Theme.textColor : Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            HoverHandler { id: retryHover; enabled: commentsEmpty.timedOut; cursorShape: Qt.PointingHandCursor }
            TapHandler {
                enabled: commentsEmpty.timedOut
                onTapped: {
                    commentsEmpty.timedOut = false;
                    commentsTimeout.restart();
                    Source.need("comments:" + page.track.id);
                    Source.moreComments(page.track.id);
                }
            }
        }
    }
}
