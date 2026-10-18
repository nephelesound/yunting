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
    scroller: page.pane === "comments" ? commentList : page.pane === "wiki" ? wikiPane : null
    kept: ["pane"]

    property var track
    property real elapsed
    property bool playing
    property string pane: "lyrics"   // lyrics | comments | wiki

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
    // 百科只在打开这一栏时取
    readonly property var wiki: page.track && page.pane === "wiki" ? Source.wiki(page.track.id) : null
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

            // 动态封面（登录后，少数歌有的循环小视频）：盖在封面上，不出声。只在这一页、正在放歌时才转，
            // 暂停、换歌或离开就把播放器拆掉（不在后台解码）；没有动态封面时就是普通封面。
            // 与静态封面交叉淡化：第一帧出来之前渲染面是黑的，等 hasFrame 再淡入；不要了先淡出再拆。
            // 换歌时旧画面先淡出到新歌的封面，再加载新地址（就地换地址，mpv 会先停成黑帧）
            Loader {
                id: dynamicCover
                readonly property string url: page.track && Source.live ? Source.dynamicCover(page.track.id) : ""
                readonly property bool wanted: dynamicCover.url.length > 0 && page.visible && page.playing && !page.coverHidden
                property string loaded: ""   // 播放器正在放的地址：只在完全淡出时换
                readonly property var player: dynamicCover.item   // MvVideo 只在应用里注册（与 MvPage 同一写法）
                readonly property bool shown: dynamicCover.wanted && dynamicCover.loaded === dynamicCover.url
                    && dynamicCover.player !== null && dynamicCover.player.hasFrame === true
                function swap() { if (dynamicCover.opacity === 0) dynamicCover.loaded = dynamicCover.url; }
                onUrlChanged: dynamicCover.swap()
                onOpacityChanged: dynamicCover.swap()
                Component.onCompleted: dynamicCover.swap()
                anchors.fill: parent
                active: dynamicCover.wanted || dynamicCover.opacity > 0
                opacity: dynamicCover.shown ? 1 : 0
                Behavior on opacity {
                    NumberAnimation { duration: Tokens.motionBase; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut }
                }
                sourceComponent: Qt.createComponent("org.nephelesound.yunting", "MvVideo")
                Binding {
                    when: dynamicCover.item !== null
                    target: dynamicCover.item
                    property: "ambient"
                    value: true
                }
                Binding {
                    when: dynamicCover.item !== null
                    target: dynamicCover.item
                    property: "source"
                    value: dynamicCover.loaded
                }
            }
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
            items: [{ key: "lyrics", text: "歌词" }, { key: "comments", text: "评论" }, { key: "wiki", text: "百科" }]
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
                if (lyrics.browsing || lyrics.count === 0 || !lyrics.visible || page.lyricIndex < 0)
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

                // 可点：悬停时整行淡色调 + 右侧这句的时间；点击跳到这句，右键出菜单。
                // 没有时间轴的歌词（t < 0）无处可跳：不响应悬停与点击
                readonly property bool seekable: line.modelData.t >= 0
                HoverHandler { id: lineHover; enabled: line.seekable; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    enabled: line.seekable
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
                    // 没有时间轴的歌词没有「当前句」可比，整篇按正文色读
                    color: line.current || !line.seekable ? Kirigami.Theme.textColor : Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
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

        // 没有歌词时说清楚是哪种：没取到（可点重试）/ 还在取 / 纯音乐 / 这首真的没有
        LinkText {
            objectName: "lyricsEmpty"
            readonly property bool failed: page.track !== null && Boolean(Source.failure("lyrics:" + page.track.id))
            readonly property bool loading: page.track !== null && Source.live !== null && Source.d.lyricsTrackId !== page.track.id
            readonly property bool pureMusic: page.track !== null
                && Source.d.lyricsTrackId === page.track.id && Source.d.lyricsPureMusic === true
            y: page.focusRow * page.pitch
            height: page.pitch
            visible: page.pane === "lyrics" && page.lines.length === 0
            verticalAlignment: Text.AlignVCenter
            parts: [{ text: !page.track ? "还没有在播放的歌" : failed ? "歌词没取到，点此重试"
                          : loading ? "正在取回歌词…" : pureMusic ? "纯音乐，请欣赏" : "暂无歌词",
                      link: failed }]
            font.pixelSize: Tokens.text2xl
            color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            onActivated: Source.retryKey("lyrics:" + page.track.id)
        }

        // 评论：阅读模式，头像、名字、内容、赞数（与歌单页共用 CommentsPane）
        CommentsPane {
            id: commentList
            visible: page.pane === "comments"
            y: page.pitch
            width: parent.width
            height: (page.rows - 1) * page.pitch
            thread: page.track ? Source.commentThread("song", page.track.id) : null
            active: page.pane === "comments"
            loggedIn: page.loggedIn
            onLoginRequested: page.loginRequested()
        }

        WikiPane {
            id: wikiPane
            visible: page.pane === "wiki" && page.wiki !== null && !page.wiki.empty
            y: page.pitch
            width: parent.width
            height: (page.rows - 1) * page.pitch
            topMargin: Tokens.space3
            wiki: page.wiki || ({})
            onSeekRequested: seconds => page.seekRequested(seconds)
        }

        // 百科空着时：在取 / 没取到（可点重试）/ 这首没有百科 / 原型数据里没有
        Label {
            id: wikiEmpty
            readonly property bool failed: page.wiki !== null && page.wiki.failed
            y: page.focusRow * page.pitch
            height: page.pitch
            visible: page.pane === "wiki" && (page.wiki === null || page.wiki.empty)
            verticalAlignment: Text.AlignVCenter
            text: !page.track ? "还没有在播放的歌" : wikiEmpty.failed ? "百科没取到，点此重试"
                : page.wiki && page.wiki.pending ? "正在取回百科…"
                : Source.live ? "这首歌还没有百科" : "原型数据里没有这首歌的百科"
            font.pixelSize: Tokens.text2xl
            font.underline: wikiEmpty.failed && wikiRetryHover.hovered
            color: wikiEmpty.failed ? Kirigami.Theme.textColor : Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            HoverHandler { id: wikiRetryHover; enabled: wikiEmpty.failed; cursorShape: Qt.PointingHandCursor }
            TapHandler {
                enabled: wikiEmpty.failed
                onTapped: Source.retryKey("wiki:" + page.track.id)
            }
        }

        // 评论空着时：在取（10 秒内）/ 没取到（可点重试）/ 原型里就是没有。Store 失败不回调，只能靠超时判断
        Label {
            id: commentsEmpty
            readonly property bool waiting: page.pane === "comments" && commentList.rows.length === 0 && Source.live && page.track !== null
            property bool timedOut: false
            onWaitingChanged: {
                commentsEmpty.timedOut = false;
                if (commentsEmpty.waiting)
                    commentsTimeout.restart();
            }
            Timer { id: commentsTimeout; interval: 10000; onTriggered: commentsEmpty.timedOut = commentsEmpty.waiting }

            y: page.focusRow * page.pitch
            height: page.pitch
            visible: page.pane === "comments" && commentList.rows.length === 0
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
                    // 等热评落地后由 commentsReady 触发最新页，不能在这里抢先排序。
                }
            }
        }
    }
}
