// 正在播放：密度最低的一屏。版面按 48 的行距排：封面边长、封面顶边、右栏各行都是 48 的整数倍。
// 左半屏居中放封面与歌手/专辑；右半屏第一行是「歌词 评论」枢轴，下面是所选的内容：
//   歌词：唯一的视觉焦点是当前句（36，其余 24 弱化），当前句与封面中线同高；有译文的句子占两行，译文在下
//   评论：网易云的热门评论，阅读模式，不对齐歌词网格
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components

ScreenFrame {
    id: page

    property var track
    property real elapsed
    property string pane: "lyrics"   // lyrics | comments

    heading: page.track ? page.track.name : ""

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
    readonly property var commentRows: page.comments.map(c => Object.assign({ group: "hot" }, c))
        .concat((page.latest ? page.latest.items : []).map(c => Object.assign({ group: "latest" }, c)))
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

    // 左半：封面与事实
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
            text: "暂无歌词"
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
            spacing: Tokens.space4
            topMargin: Tokens.space3
            bottomMargin: Tokens.space6
            boundsBehavior: Flickable.StopAtBounds
            model: page.commentRows
            ScrollBar.vertical: ScrollBar {}
            onAtYEndChanged: if (atYEnd && page.track && page.pane === "comments") Source.moreComments(page.track.id)

            footer: Label {
                width: ListView.view.width
                height: visible ? Tokens.height10 : 0
                visible: page.latest !== null && (page.latest.loading || !page.latest.more) && page.latest.items.length > 0
                verticalAlignment: Text.AlignVCenter
                text: page.latest && page.latest.loading ? "正在取回更多评论…" : "没有更多评论了"
                font.pixelSize: Tokens.textSm
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            }

            delegate: ColumnLayout {
                id: comment
                required property var modelData
                required property int index
                width: ListView.view.width - Tokens.space4
                spacing: Tokens.space3

                // 分组标题：每组第一条之前（精彩评论 / 最新评论 N）
                Label {
                    visible: comment.index === 0 || page.commentRows[comment.index - 1].group !== comment.modelData.group
                    Layout.topMargin: comment.index === 0 ? 0 : Tokens.space4
                    text: comment.modelData.group === "hot" ? "精彩评论"
                        : "最新评论" + (page.latest && page.latest.total > 0 ? " " + page.count(page.latest.total) : "")
                    font.pixelSize: Tokens.textSm
                    font.weight: Font.DemiBold
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                }

                RowLayout {
                Layout.fillWidth: true
                spacing: Tokens.space3

                Components.Avatar {
                    Layout.alignment: Qt.AlignTop
                    implicitWidth: Tokens.height8
                    implicitHeight: Tokens.height8
                    name: comment.modelData.user
                    source: comment.modelData.avatar || ""
                    initialsMode: Components.Avatar.InitialsMode.UseIcon
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Tokens.space1
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: comment.modelData.user
                            font.pixelSize: Tokens.textSm
                            color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                            elide: Text.ElideRight
                        }
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
                        wrapMode: Text.Wrap
                        maximumLineCount: 6
                        elide: Text.ElideRight
                    }
                }
                }
            }
        }

        Label {
            y: page.focusRow * page.pitch
            height: page.pitch
            visible: page.pane === "comments" && page.commentRows.length === 0
            verticalAlignment: Text.AlignVCenter
            text: Source.live ? "正在取回评论…" : "暂无评论"
            font.pixelSize: Tokens.text2xl
            color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
        }
    }
}
