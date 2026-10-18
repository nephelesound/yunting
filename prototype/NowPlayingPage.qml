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
    scroller: page.pane === "comments" ? commentList : null
    kept: ["pane"]

    property var track
    property real elapsed
    property bool playing
    property string pane: "lyrics"   // lyrics | comments

    // 逐字填色要逐帧走（引擎的进度更新得没那么勤）：播放时每帧前进，与引擎偏差大或暂停时对齐（与桌面歌词同一做法）
    property real clock: 0
    onElapsedChanged: if (!page.playing || Math.abs(page.elapsed - page.clock) > 0.35) page.clock = page.elapsed
    onPlayingChanged: page.clock = page.elapsed
    FrameAnimation {
        running: page.visible && page.playing && page.pane === "lyrics" && page.timed
        onTriggered: page.clock += frameTime
    }
    // 这首有逐字时间吗；有才给当前句填色（只按时间比例填是猜的，在应用里不画）
    readonly property bool timed: page.lines.length > 0 && page.lines[0].words !== undefined
    readonly property real fill: page.timed
        ? Source.lineFill(page.lines[page.lyricIndex], page.lines[page.lyricIndex + 1] || null, page.track ? page.track.dur || 0 : 0, page.clock)
        : 0

    signal seekRequested(real seconds)   // 点了某句歌词：跳到这句开始
    // 共享封面转场的一端：main 读它的位置，飞行期间 coverHidden
    property bool coverHidden: false
    readonly property Item coverItem: pageCover

    heading: page.track ? page.track.name : ""

    // 只铺页面区域（含页头），播放条保持原底色：进度线仍是唯一的分界
    background: CoverAmbience {
        cover: page.track ? page.track.cover : ""
    }

    readonly property int pitch: Tokens.height12
    readonly property int rows: Math.floor((areaHeight - Tokens.space6) / pitch)
    readonly property int areaHeight: rightHalf.parent ? rightHalf.parent.height : 0
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
    readonly property bool commentsReady: page.track !== null && Source.commentsLoaded(page.track.id)
    function startComments() {
        if (!(page.track && page.pane === "comments") || Source.latestComments.owner === page.track.id)
            return;
        // 热评是列表头。等它落地后再拉最新页，避免热评晚到后把整张表重建、滚回顶部。
        if (!page.commentsReady) {
            Source.need("comments:" + page.track.id);
            return;
        }
        Source.moreComments(page.track.id);
    }
    onCommentsReadyChanged: if (page.commentsReady) page.startComments()
    onPaneChanged: page.startComments()
    onTrackChanged: {
        page.openThread = 0;
        page.startComments();
    }
    Component.onCompleted: page.startComments()

    // 展开着回复的那条评论（0 = 都没展开）。展开时要第一页回复，再点收起
    property real openThread: 0
    function toggleThread(commentId) {
        if (page.openThread === commentId) {
            page.openThread = 0;
            return;
        }
        page.openThread = commentId;
        if (Source.replies(page.track.id, commentId).pending)
            Source.need("commentReplies:" + page.track.id + ":" + commentId);
    }
    readonly property int lyricIndex: Source.lyricIndex(page.lines, page.elapsed)


    // 左半：封面与事实。这一屏是「左封面、右歌词」的两栏构图：封面在左半栏水平居中是有意的构图轴
    //（不贴页边线），事实行与封面左缘对齐；右栏从中线开始
    ColumnLayout {
        x: Math.round((parent.width / 2 - width) / 2)
        y: page.coverTopRow * page.pitch
        width: page.coverRows * page.pitch
        spacing: Tokens.space4

        Cover {
            id: pageCover
            Layout.preferredWidth: page.coverRows * page.pitch
            Layout.preferredHeight: Layout.preferredWidth
            source: page.track ? Source.sized(page.track.cover, 800) : ""
            pixelSize: 800
            // 共享封面转场飞行时隐藏，落地后再显示：屏上永远只有一张封面
            opacity: page.coverHidden ? 0 : 1
        }
        Facts {
            Layout.fillWidth: true
            // 歌手、专辑都可点：去歌手页 / 专辑页
            items: page.track ? [{ artists: Format.artistLinks(page.track) },
                                 { label: page.track.album, albumId: page.track.albumId || 0 }] : []
        }
    }

    // 右半
    Item {
        id: rightHalf
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

        // 歌词：自动跟随当前句（当前句停在封面中线那一行）；用户滚动时暂停跟随，3 秒不动后平滑回到当前句。
        // 点一句跳到那里播放；右键可复制这句 / 全部歌词。
        // 跟随不用 StrictlyEnforceRange（它会把当前句钉死，用户滚不动），而是自己算目标位置再缓动过去
        ListView {
            id: lyrics
            objectName: "lyrics"
            visible: page.pane === "lyrics"
            y: page.pitch
            width: parent.width
            height: (page.rows - 1) * page.pitch
            clip: true
            model: page.lines
            // 上下留出余量，第一句和最后一句也能停在中线那一行
            readonly property real focusOffset: (page.focusRow - 1) * page.pitch
            topMargin: lyrics.focusOffset
            bottomMargin: lyrics.height - lyrics.focusOffset - page.pitch
            boundsBehavior: Flickable.StopAtBounds

            property bool browsing: false      // 用户在手动看别处

            // 目标位置：让第 i 句的顶边落在中线那一行（借 positionViewAtIndex 量，再还原，交给动画）
            function follow(animated) {
                if (lyrics.browsing || lyrics.count === 0 || !lyrics.visible)
                    return;
                const from = lyrics.contentY;
                lyrics.positionViewAtIndex(page.lyricIndex, ListView.Beginning);
                const to = lyrics.contentY - lyrics.focusOffset;
                lyrics.contentY = from;
                followAnimation.stop();
                if (!animated) {
                    lyrics.contentY = to;
                    return;
                }
                followAnimation.to = to;
                followAnimation.start();
            }
            NumberAnimation {
                id: followAnimation
                target: lyrics
                property: "contentY"
                // 换句时列表平滑上移（屏上移动 → ease-in-out）；歌词是被动观看的环境运动，所以用最慢的一档
                duration: Tokens.motionSlow
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Tokens.easeInOut
            }
            // 只认真实的输入算「用户在看别处」：滚轮 / 触控板（下面 WheelScroll 的 wheel）、触屏拖动（moving）。
            // 不能用 contentY 变化来判断：留白、排版、窗口尺寸变化都会挪动它
            function userScrolled() {
                followAnimation.stop();
                lyrics.browsing = true;
                resumeTimer.restart();
            }
            onMovingChanged: if (moving) lyrics.userScrolled()
            Timer {
                id: resumeTimer
                interval: 3000
                onTriggered: lyrics.resume()
            }
            function resume() {
                resumeTimer.stop();
                lyrics.browsing = false;
                lyrics.follow(true);
            }
            Connections {
                target: page
                function onLyricIndexChanged() { lyrics.follow(true); }
                function onLinesChanged() {
                    lyrics.browsing = false;
                    Qt.callLater(() => lyrics.follow(false));
                }
            }
            onVisibleChanged: if (visible) Qt.callLater(() => lyrics.follow(false))
            onHeightChanged: Qt.callLater(() => lyrics.follow(false))
            Component.onCompleted: Qt.callLater(() => lyrics.follow(false))

            WheelScroll {
                flickable: lyrics
                onWheel: lyrics.userScrolled()
            }

            // 右键菜单：一个实例，打开时记下是哪一句
            Menu {
                id: lyricMenu
                property var line: null
                MenuItem {
                    icon.name: "media-playback-start"
                    text: "从这句开始播放"
                    onTriggered: page.seekRequested(lyricMenu.line.t)
                }
                MenuSeparator {}
                MenuItem {
                    objectName: "copyLine"
                    icon.name: "edit-copy"
                    text: "复制这句"
                    onTriggered: Clipboard.copy(lyricMenu.line.tr ? lyricMenu.line.text + "\n" + lyricMenu.line.tr : lyricMenu.line.text)
                }
                MenuItem {
                    icon.name: "edit-copy"
                    text: "复制全部歌词"
                    onTriggered: Clipboard.copy(page.lines.map(l => l.tr ? l.text + "\n" + l.tr : l.text).join("\n"))
                }
            }

            delegate: Item {
                id: line
                required property int index
                required property var modelData
                readonly property bool current: index === page.lyricIndex
                readonly property string translation: modelData.tr || ""
                width: ListView.view.width
                // 有译文的句子占两行网格：原文贴第一行底边，译文贴第二行顶边，读起来是一组
                height: line.translation.length > 0 ? 2 * page.pitch : page.pitch

                // 可点：悬停时整行淡色调 + 右侧这句的时间；点击跳到这句，右键出菜单
                HoverHandler { id: lineHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: {
                        page.seekRequested(line.modelData.t);
                        lyrics.resume();
                    }
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: {
                        lyricMenu.line = line.modelData;
                        lyricMenu.popup();
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: -Tokens.space2
                    radius: Tokens.radius
                    color: Qt.alpha(Kirigami.Theme.highlightColor, lineHover.hovered ? Tokens.tintHover : 0)
                    Behavior on color { ColorAnimation { duration: Tokens.motionFast } }
                }
                Label {
                    anchors.right: parent.right
                    anchors.rightMargin: Tokens.space2
                    y: (page.pitch - height) / 2
                    visible: lineHover.hovered
                    text: Format.duration(Math.max(1, line.modelData.t))
                    font.pixelSize: Tokens.textSm
                    font.features: { "tnum": 1 }
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                }

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
                    // 常规字重：当前句靠字号与颜色突出。Medium 在各家字体里差别最大（HarmonyOS Sans 的 Medium 很轻，
                    // Noto Sans CJK 的 Medium 接近粗体），换台机器整页歌词就显得发黑
                    font.weight: Font.Normal
                    color: line.current ? Kirigami.Theme.textColor : Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    elide: Text.ElideRight
                    Behavior on scale { NumberAnimation { duration: Tokens.motionBase; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut } }
                    Behavior on color { ColorAnimation { duration: Tokens.motionBase } }

                    // 逐字歌词：同样的字用强调色、按唱到的位置从左裁出一段，盖在当前句上
                    Item {
                        visible: line.current && page.timed
                        width: original.contentWidth * page.fill
                        height: parent.height
                        clip: true
                        Label {
                            width: original.width
                            height: original.height
                            verticalAlignment: original.verticalAlignment
                            text: original.text
                            font: original.font
                            color: Kirigami.Theme.highlightColor
                            elide: Text.ElideRight
                        }
                    }
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

        // 手动滚动时：立刻回到当前句（否则 3 秒后自动回去）
        Button {
            anchors.right: parent.right
            y: page.rows * page.pitch - height
            visible: page.pane === "lyrics" && lyrics.browsing
            implicitHeight: Tokens.height8
            flat: true
            icon.name: "go-jump"
            icon.width: Tokens.iconSm
            icon.height: Tokens.iconSm
            text: "回到当前句"
            font.pixelSize: Tokens.textSm
            onClicked: lyrics.resume()
        }

        // 没有歌词时说清楚是哪种：没取到（可点重试）/ 还在取 / 这首真的没有
        LinkText {
            readonly property bool failed: page.track !== null && Boolean(Source.failure("lyrics:" + page.track.id))
            readonly property bool loading: page.track !== null && Source.live !== null && Source.d.lyricsTrackId !== page.track.id
            y: page.focusRow * page.pitch
            height: page.pitch
            visible: page.pane === "lyrics" && page.lines.length === 0
            verticalAlignment: Text.AlignVCenter
            parts: [{ text: !page.track ? "还没有在播放的歌" : failed ? "歌词没取到，点此重试"
                          : loading ? "正在取回歌词…" : "暂无歌词",
                      link: failed }]
            font.pixelSize: Tokens.text2xl
            color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            onActivated: Source.retryKey("lyrics:" + page.track.id)
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
            // 续取一页只追加新的行，滚动位置不变（直接用数组当 model 会整个重置、滚回顶部）
            model: GrowingModel {
                values: page.commentRows
                keyOf: c => c.group + "\n" + page.commentKey(c)
            }
            ScrollBar.vertical: ScrollBar {}
            WheelScroll { flickable: commentList }
            onAtYEndChanged: if (atYEnd && page.track && page.pane === "comments") Source.moreComments(page.track.id)

            footer: PagingFooter {
                width: ListView.view.width - Tokens.space4
                paging: page.latest
                loadingText: "正在取回更多评论…"
                doneText: "没有更多评论了"
                onRetryRequested: Source.moreComments(page.track.id)
            }

            delegate: ColumnLayout {
                id: entry
                required property int index
                readonly property var modelData: page.commentRows[entry.index] || ({})
                width: ListView.view.width - Tokens.space4
                spacing: Tokens.space3

                // 分组标题：每组第一条之前，与队列的「正在播放 / 接下来播放」同一种小节标题
                Label {
                    visible: entry.index === 0 || page.commentRows[entry.index - 1].group !== entry.modelData.group
                    Layout.topMargin: entry.index === 0 ? 0 : Tokens.space6
                    text: entry.modelData.group === "hot" ? "精彩评论"
                        : "最新评论" + (page.latest && page.latest.total > 0 ? " " + Format.count(page.latest.total) : "")
                    font.pixelSize: Tokens.textSm
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                }

                CommentRow {
                    Layout.fillWidth: true
                    comment: entry.modelData
                    threadOpen: page.openThread === entry.modelData.id
                    onLikeRequested: liked => {
                        if (!page.loggedIn) {
                            page.loginRequested();
                            return;
                        }
                        Source.setCommentLiked(page.track.id, entry.modelData.id, liked);
                    }
                    onThreadToggled: page.toggleThread(entry.modelData.id)
                }

                // 楼中楼：展开在这条下面，缩进到正文的左缘；一次只展开一条（后端只留最近展开的那一条）
                ColumnLayout {
                    readonly property var thread: Source.replies(page.track ? page.track.id : 0, entry.modelData.id || 0)
                    visible: page.openThread === entry.modelData.id && entry.modelData.id > 0
                    Layout.fillWidth: true
                    Layout.leftMargin: Tokens.height8 + Tokens.space3
                    spacing: Tokens.space4

                    Repeater {
                        model: parent.visible ? parent.thread.items : []
                        delegate: CommentRow {
                            required property var modelData
                            Layout.fillWidth: true
                            comment: modelData
                            likeable: false
                            threadOf: entry.modelData.id || 0
                        }
                    }
                    LinkText {
                        readonly property var thread: parent.thread
                        visible: thread.more || thread.pending || thread.failed
                        parts: [{ text: thread.failed ? "回复没取到，点此重试" : thread.pending ? "正在取回回复…" : "更多回复",
                                  link: !thread.pending || thread.failed }]
                        font.pixelSize: Tokens.textSm
                        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                        onActivated: thread.failed ? Source.retryKey("commentReplies:" + thread.songId + ":" + thread.commentId)
                                                   : Source.moreReplies(thread)
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
