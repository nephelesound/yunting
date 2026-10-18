// 私信（只读：网易云的私信接口只开放读取）。主从两栏：
// 左＝会话（谁、最后一条说了什么、什么时候、几条没读），默认打开最近的一段；
// 右＝这段对话，按时间从上到下，最新的在底部，往上滚接着取更早的。
// 对方分享的歌、歌单、专辑、MV 照原样可用：歌点了就放，其余点了打开。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Format.js" as Format

ScreenFrame {
    id: page
    scroller: page.pane === "messages" ? list : noticeList
    kept: ["peer", "pane"]

    property real currentId
    property real peer: 0   // 打开的那段对话的对方 uid
    property string pane: "messages"   // messages | comments | forwards | notices

    // 未读数（通知计数接口）；评论 / @我 / 通知只在切到那一栏时取
    readonly property var counts: Source.d.notifications || ({})
    function labelled(text, n) { return n > 0 ? text + " " + Format.count(n) : text; }
    readonly property var notices: page.pane === "messages" || !page.loggedIn ? null : Source.notices(page.pane)

    readonly property var conversations: Source.inbox
    readonly property var thread: Source.thread(page.peer)
    readonly property var peerItem: page.conversations.items.find(c => c.userId === page.peer) || null

    heading: "消息"

    Component.onCompleted: if (page.loggedIn) Source.need("inbox")
    // 读过的私信服务器会记成已读：离开时重取页头的未读数
    Component.onDestruction: if (page.loggedIn) Source.need("notifications")
    onPeerChanged: if (page.peer > 0 && page.thread.pending) Source.need("inboxThread:" + page.peer)
    // 会话到了还没选：打开最近的一段
    readonly property real firstPeer: page.conversations.items.length > 0 ? page.conversations.items[0].userId : 0
    onFirstPeerChanged: if (page.peer === 0) page.peer = page.firstPeer


    // 分享来的专辑 / 歌单 / MV：封面 + 名字 + 是什么，块状可点（与曲目行同一套）
    component SharedItem: AbstractButton {
        id: shared
        property string cover
        property string title
        property string caption
        implicitHeight: Tokens.trackRow
        rightPadding: Tokens.space3
        hoverEnabled: true
        background: Rectangle {
            radius: Tokens.radius
            color: Qt.alpha(Kirigami.Theme.highlightColor, shared.down ? Tokens.tintPressed : shared.hovered ? Tokens.tintHover : 0)
            Behavior on color { ColorAnimation { duration: Tokens.motionFast } }
        }
        contentItem: RowLayout {
            spacing: Tokens.space3
            Cover {
                Layout.preferredWidth: Tokens.trackRow
                Layout.preferredHeight: Tokens.trackRow
                source: shared.cover
                pixelSize: 96
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    text: shared.title
                    font.pixelSize: Tokens.textBase
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: shared.caption
                    font.pixelSize: Tokens.textSm
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    elide: Text.ElideRight
                }
            }
        }
    }

    // 四类消息，未读数跟在后面
    Pivot {
        x: Tokens.pageMargin
        visible: page.loggedIn
        items: [{ key: "messages", text: page.labelled("私信", page.counts.msg) },
                { key: "comments", text: page.labelled("评论", page.counts.comment) },
                { key: "forwards", text: page.labelled("@我", page.counts.forward) },
                { key: "notices", text: page.labelled("通知", page.counts.notice) }]
        current: page.pane
        onActivated: key => page.pane = key
    }

    RowLayout {
        anchors.fill: parent
        anchors.topMargin: Tokens.height10 + Tokens.space3
        anchors.leftMargin: Tokens.pageMargin
        anchors.bottomMargin: Tokens.space6
        spacing: Tokens.space6
        visible: page.loggedIn && page.pane === "messages"

        // 左：会话
        ListView {
            id: list
            // 会话列宽一栏（400）：名字与最后一句都要看得出；右边留出滚动条的位置，不压在时间与未读数上
            Layout.preferredWidth: Tokens.measureMax
            Layout.fillHeight: true
            rightMargin: Tokens.space3
            spacing: Tokens.space1
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: GrowingModel {
                values: page.conversations.items
                keyOf: c => c.userId
            }
            ScrollBar.vertical: ScrollBar {}
            WheelScroll { flickable: list }
            onAtYEndChanged: if (atYEnd) Source.moreInbox()
            footer: LinkText {
                visible: page.conversations.moreFailed
                height: visible ? Tokens.height10 : 0
                verticalAlignment: Text.AlignVCenter
                parts: [{ text: "后面的没取到，点此重试", link: true }]
                font.pixelSize: Tokens.textSm
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                onActivated: Source.moreInbox()
            }

            delegate: PersonRow {
                required property int index
                readonly property var conversation: page.conversations.items[index] || ({})
                width: ListView.view.width - ListView.view.rightMargin
                avatar: conversation.avatar
                name: conversation.name
                // 后端给的最后一句：对方说的话，没说话时是分享物的名字（再包一层「分享了…」会重复书名号）
                detail: conversation.lastText
                meta: Format.commentTime(conversation.lastTime)
                unread: conversation.unread
                current: conversation.userId === page.peer
                onClicked: page.peer = conversation.userId
            }

            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width
                visible: list.count === 0
                icon.name: "dialog-messages"
                text: Source.failure("inbox") ? "私信取不到，点此重试"
                    : page.conversations.pending && Source.live ? "正在取回私信…" : "还没有私信"
                HoverHandler { enabled: Boolean(Source.failure("inbox")); cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    enabled: Boolean(Source.failure("inbox"))
                    onTapped: Source.retryKey("inbox")
                }
            }
        }

        // 右：对话
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Tokens.space3
            visible: page.peer > 0

            // 对方：名字是链接，打开他的主页
            LinkText {
                Layout.rightMargin: Tokens.pageMargin
                parts: page.peerItem ? [{ text: page.peerItem.name, link: true }] : []
                font.pixelSize: Tokens.text2xl
                font.weight: Font.Light
                color: Kirigami.Theme.textColor
                onActivated: Actions.openUser(page.peer)
            }

            ListView {
                id: messages
                Layout.fillWidth: true
                Layout.fillHeight: true
                rightMargin: Tokens.pageMargin
                spacing: Tokens.space6
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                // 接口给的是新到旧：从下往上排，最新的贴着底部；往上滚到头接着取更早的
                verticalLayoutDirection: ListView.BottomToTop
                model: GrowingModel {
                    values: page.thread.items
                    keyOf: m => m.id > 0 ? String(m.id) : m.fromUserId + ":" + m.time + ":" + m.batchId
                }
                ScrollBar.vertical: ScrollBar {}
                WheelScroll { flickable: messages }
                onAtYBeginningChanged: if (atYBeginning && count > 0) Source.moreThread(page.thread)
                // 从下往上排时 footer 在最上面：更早的消息没取到时在那里重试
                footer: LinkText {
                    visible: page.thread.moreFailed
                    height: visible ? Tokens.height10 : 0
                    verticalAlignment: Text.AlignVCenter
                    parts: [{ text: "更早的消息没取到，点此重试", link: true }]
                    font.pixelSize: Tokens.textSm
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    onActivated: Source.moreThread(page.thread)
                }

                delegate: RowLayout {
                    id: message
                    required property int index
                    readonly property var messageData: page.thread.items[index] || ({})
                    width: ListView.view.width - ListView.view.rightMargin
                    spacing: Tokens.space3

                    PersonAvatar {
                        Layout.alignment: Qt.AlignTop
                        implicitWidth: Tokens.height8
                        implicitHeight: Tokens.height8
                        name: message.messageData.fromName
                        source: message.messageData.fromAvatar
                    }
                    // 贴左上：只有分享物（固定宽）时列比行窄，不能被居中
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignLeft | Qt.AlignTop
                        spacing: Tokens.space1
                        RowLayout {
                            spacing: Tokens.space2
                            Label {
                                text: message.messageData.mine ? "我" : message.messageData.fromName
                                font.pixelSize: Tokens.textSm
                            }
                            Label {
                                text: Format.commentTime(message.messageData.time)
                                font.pixelSize: Tokens.textSm
                                font.features: { "tnum": 1 }
                                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                            }
                        }
                        // 文字：可选中复制（私信里常有链接、口令）
                        TextEdit {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            readOnly: true
                            selectByMouse: true
                            // 分享时没写话，接口用分享物的名字顶上：那就不重复显示
                            readonly property var shared: message.messageData.message[message.messageData.message.kind] || null
                            text: message.messageData.message.text !== (shared && shared.name ? shared.name : null)
                                ? message.messageData.message.text : ""
                            font.pixelSize: Tokens.textBase
                            color: Kirigami.Theme.textColor
                            selectionColor: Kirigami.Theme.highlightColor
                            selectedTextColor: Kirigami.Theme.highlightedTextColor
                            wrapMode: Text.Wrap
                        }
                        // 分享的歌：与列表里的曲目行一样，点了就放
                        // 分享物宽度固定为一栏（窄于 wideRow 时曲目行自己收起专辑列，成为紧凑的一张「歌」）。
                        // 外面套一层占满列宽的容器：定宽的子项不能反过来把整列压窄、被布局居中
                        Item {
                            readonly property var song: message.messageData.message.song || null
                            Layout.fillWidth: true
                            implicitHeight: songTile.height
                            visible: song !== null
                            TrackTile {
                                id: songTile
                                width: Math.min(parent.width, Tokens.measureMax)
                                index: 0
                                modelData: parent.song || ({ id: 0, name: "", artist: "", dur: 0, fee: 0, cover: "" })
                                hangingMarker: false
                                current: parent.song !== null && parent.song.id === page.currentId
                                onActivated: Actions.play([parent.song], 0)
                            }
                        }
                        Item {
                            readonly property string kind: message.messageData.message.kind
                            readonly property var item: kind === "album" || kind === "playlist" || kind === "mv"
                                ? message.messageData.message[kind] || null : null
                            Layout.fillWidth: true
                            implicitHeight: sharedTile.height
                            visible: item !== null
                            SharedItem {
                                id: sharedTile
                                readonly property var item: parent.item
                                readonly property string kind: parent.kind
                                width: Math.min(parent.width, Tokens.measureMax)
                                cover: item ? item.cover : ""
                                title: item ? item.name : ""
                                caption: !item ? ""
                                    : kind === "playlist" ? "歌单" + (item.count > 0 ? "，" + item.count + " 首" : "")
                                    : kind === "album" ? "专辑" + (item.artist ? "，" + item.artist : "")
                                    : "MV" + (item.artist ? "，" + item.artist : "")
                                onClicked: kind === "album" ? Actions.openAlbum(item.id)
                                         : kind === "playlist" ? Actions.openPlaylist(item.id) : Actions.openMv(item.id)
                            }
                        }
                    }
                }

                // 空着时：没取到（可点重试）/ 在取 / 真的没有
                LinkText {
                    anchors.centerIn: parent
                    visible: messages.count === 0
                    parts: [{ text: page.thread.failed ? "对话没取到，点此重试"
                                  : page.thread.pending && Source.live ? "正在取回对话…" : "这段对话还没有消息",
                              link: page.thread.failed }]
                    font.pixelSize: Tokens.textSm
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    onActivated: Source.retryKey("inboxThread:" + page.peer)
                }
            }

            // 能力边界说清楚：这里只能看，回复要去官方客户端
            Label {
                Layout.rightMargin: Tokens.pageMargin
                text: "云听只能查看私信；回复请在网易云音乐 App 里进行"
                font.pixelSize: Tokens.textSm
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            }
        }
    }

    // 评论回复 / @我 / 通知：一列，按时间从新到旧，滚到底接着取。
    // 左缘与私信两栏一致，止于可读的宽度（一段话不拉到整屏宽）
    ListView {
        id: noticeList
        visible: page.loggedIn && page.pane !== "messages"
        x: Tokens.pageMargin
        y: Tokens.height10 + Tokens.space3
        width: Math.min(parent.width - 2 * Tokens.pageMargin, Tokens.measureMax + Tokens.space6 + Tokens.wideRow)
        height: parent.height - y - Tokens.space6
        rightMargin: Tokens.space3
        spacing: Tokens.space6
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: GrowingModel {
            values: page.notices ? page.notices.items : []
            keyOf: n => String(n.id)
        }
        ScrollBar.vertical: ScrollBar {}
        WheelScroll { flickable: noticeList }
        onAtYEndChanged: if (atYEnd && page.notices && count > 0) Source.morePaged(page.notices)

        delegate: NoticeRow {
            id: notice
            required property int index
            readonly property var entry: page.notices ? page.notices.items[notice.index] || ({}) : ({})
            width: noticeList.width - noticeList.rightMargin
            user: notice.entry.user || ({})
            time: notice.entry.time || 0
            text: notice.entry.content || notice.entry.message || notice.entry.text || ""
            quote: notice.entry.repliedContent || ""
            resource: notice.entry.resource || null
        }

        footer: PagingFooter {
            width: noticeList.width - noticeList.rightMargin
            paging: page.notices ? { items: page.notices.items, more: page.notices.more, loading: false, failed: page.notices.moreFailed } : null
            doneText: "没有更早的了"
            onRetryRequested: Source.morePaged(page.notices)
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width
            visible: noticeList.count === 0
            icon.name: page.notices && (page.notices.pending || page.notices.failed) ? "" : "notifications"
            text: page.notices && page.notices.pending ? "正在取回…" : page.notices && page.notices.failed ? "没取到"
                : ({ comments: "还没有人回复你的评论", forwards: "还没有人 @ 你", notices: "还没有通知" })[page.pane] || ""
            helpfulAction: Kirigami.Action {
                enabled: page.notices !== null && page.notices.failed
                text: "重试"
                icon.name: "view-refresh"
                onTriggered: Source.retryKey(page.notices.key)
            }
        }
    }

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        visible: !page.loggedIn
        icon.name: "dialog-messages"
        text: "登录后可以查看消息"
    }
}
