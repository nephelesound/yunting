// 用户主页。与歌手页共用辅助窗格布局：
// 左＝固定窗格（头像、名字与会员等级、签名、事实）。自己的主页多听歌时长、升级还差多少与退出登录；
// 右＝枢轴「歌单 | 听歌排行 | 关注 | 粉丝」（听歌排行只有自己的），下面是对应内容。
// 关注、粉丝、歌单三条事实的说明也是链接，点了切到那一栏。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Format.js" as Format

ScreenFrame {
    id: page
    // 关注 / 粉丝的网格在 Repeater 里，显示着的那个自己报上来
    property Flickable peopleView: null
    scroller: page.pane === "playlists" ? playlists : page.pane === "rank" ? rankList : page.peopleView
    kept: ["pane"]
    background: PageAmbience { cover: page.user.avatar || "" }

    property real userId
    property real currentId
    property string pane: "playlists"   // playlists | rank | follows | fans
    property string rankType: "week"    // week | month

    readonly property var user: Source.user(page.userId)
    // 自己的主页：账号的去处都在这里（页头的头像与名字直接跳过来），关注的位置换成退出登录
    readonly property bool own: page.userId > 0 && Source.account !== null && Source.account.id === page.userId
    readonly property var rank: Source.listenRank(page.rankType)
    readonly property var follows: Source.people("userFollows", page.userId)
    readonly property var fans: Source.people("userFolloweds", page.userId)
    readonly property var level: page.own ? Source.level : null
    heading: page.user.name || "用户"

    // 听歌排行、关注、粉丝后端不缓存：切到那一栏（或换周 / 月）时要一次，已经有了就不再要
    function load() {
        if (page.pane === "rank" && page.own && page.rank.pending)
            Source.need("listenRank:" + page.rankType);
        else if (page.pane === "follows" && page.follows.pending)
            Source.need("userFollows:" + page.userId);
        else if (page.pane === "fans" && page.fans.pending)
            Source.need("userFolloweds:" + page.userId);
    }
    function loadOwn() {
        if (!page.own)
            return;
        Source.need("listen:total");
        Source.need("userLevel");
        Source.need("notifications");
    }
    onPaneChanged: page.load()
    onRankTypeChanged: page.load()
    onOwnChanged: page.loadOwn()
    // 从一个人的主页点进另一个人：还是这一页，只换了人。回到歌单一栏，再按当前栏要数据
    onUserIdChanged: {
        page.pane = "playlists";
        page.load();
    }
    Component.onCompleted: {
        page.load();
        page.loadOwn();
    }

    // 一栏空着时说什么：在取 / 取不到（可点重试）/ 真的没有
    component PaneMessage: Kirigami.PlaceholderMessage {
        id: message
        property bool pending
        property bool failed
        property bool restricted
        property string restrictedText
        property string retryKey
        property string emptyText
        anchors.centerIn: parent
        width: parent.width - Tokens.pageMargin
        text: message.restricted ? message.restrictedText
              : message.failed ? "没取到，点此重试"
              : message.pending && Source.live ? "正在取回…" : message.emptyText
        HoverHandler { enabled: message.failed && !message.restricted; cursorShape: Qt.PointingHandCursor }
        TapHandler {
            enabled: message.failed && !message.restricted
            onTapped: Source.retryKey(message.retryKey)
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Tokens.pageMargin
        spacing: Tokens.space6

        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: Tokens.trackColumn
            Layout.maximumWidth: Tokens.trackColumn
            spacing: 0

            PersonAvatar {
                Layout.preferredWidth: Tokens.trackColumn
                Layout.preferredHeight: Tokens.trackColumn
                name: page.user.name
                source: page.user.avatar
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Tokens.space4
                spacing: Tokens.space2
                Label {
                    Layout.fillWidth: true
                    text: page.user.name || (Source.live ? "正在取回用户…" : "用户")
                    font.pixelSize: Tokens.text2xl
                    font.weight: Font.Light
                    elide: Text.ElideRight
                }
                Tag {
                    visible: (page.user.vipType || 0) > 0
                    text: "VIP"
                    Layout.alignment: Qt.AlignVCenter
                }
                Tag {
                    visible: (page.user.level || 0) > 0
                    text: "Lv." + page.user.level
                    muted: true
                    Layout.alignment: Qt.AlignVCenter
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.topMargin: Tokens.space2
                visible: text.length > 0
                text: page.user.signature
                font.pixelSize: Tokens.textSm
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }

            // 两组事实：社交（可切到对应一栏）与听歌
            Facts {
                Layout.fillWidth: true
                Layout.topMargin: Tokens.space4
                items: [
                    { value: Format.count(page.user.follows || 0), label: "关注", key: "follows" },
                    { value: Format.count(page.user.followeds || 0), label: "粉丝", key: "fans" },
                    { value: String(page.user.playlistCount || 0), label: "歌单", key: "playlists" },
                ]
                onActivated: key => page.pane = key
            }
            Facts {
                Layout.fillWidth: true
                Layout.topMargin: Tokens.space2
                visible: items.length > 0
                items: ((page.user.listenSongs || 0) > 0 ? [{ value: Format.count(page.user.listenSongs), label: "首累计听歌" }] : [])
                    .concat(page.own && Source.listenSeconds >= 3600
                        ? [{ value: Format.count(Math.round(Source.listenSeconds / 3600)), label: "小时听歌时长" }] : [])
            }

            // 升级还差多少：等级接口给出下一级要求的听歌数与登录天数（满级时两者都不再增长，不显示）
            Label {
                Layout.fillWidth: true
                Layout.topMargin: Tokens.space2
                readonly property int songs: page.level ? page.level.nextPlayCount - page.level.nowPlayCount : 0
                readonly property int days: page.level ? page.level.nextLoginCount - page.level.nowLoginCount : 0
                visible: page.level !== null && (songs > 0 || days > 0)
                text: "再听 " + Math.max(0, songs) + " 首歌、登录 " + Math.max(0, days) + " 天升到 Lv." + ((page.level ? page.level.level : 0) + 1)
                font.pixelSize: Tokens.textSm
                font.features: { "tnum": 1 }
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                wrapMode: Text.Wrap
            }

            // 私信：自己的东西放在自己的主页上。未读数弱化地跟在后面——大多是官方号与音乐人的推送，
            // 常年 99+，放在全局页头用强调色标出来只会一直催人
            RowLayout {
                Layout.topMargin: Tokens.space4
                visible: page.own
                spacing: Tokens.space2
                LinkText {
                    parts: [{ text: "私信", link: true }]
                    font.pixelSize: Tokens.textBase
                    color: Kirigami.Theme.textColor
                    onActivated: Actions.openInbox()
                }
                Label {
                    visible: Source.unreadMessages > 0
                    text: Format.count(Source.unreadMessages) + " 条未读"
                    font.pixelSize: Tokens.textSm
                    font.features: { "tnum": 1 }
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                }
            }

            // 破坏性操作，确认在对话框里。次要操作用文字链接：字的左缘与上面的事实对齐（按钮会把字往里缩）
            LinkText {
                Layout.topMargin: Tokens.space2
                visible: page.own
                parts: [{ text: "退出登录…", link: true }]
                font.pixelSize: Tokens.textBase
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                onActivated: Actions.logoutRequested()
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Tokens.space3

            Pivot {
                items: [{ key: "playlists", text: (page.user.playlistCount || 0) > 0 ? "歌单 " + page.user.playlistCount : "歌单" }]
                    .concat(page.own ? [{ key: "rank", text: "听歌排行" }] : [])
                    .concat([{ key: "follows", text: (page.user.follows || 0) > 0 ? "关注 " + Format.count(page.user.follows) : "关注" },
                             { key: "fans", text: (page.user.followeds || 0) > 0 ? "粉丝 " + Format.count(page.user.followeds) : "粉丝" }])
                current: page.pane
                onActivated: key => page.pane = key
            }

            // 歌单：磁贴等宽铺满到页边线，视图延伸到窗口右缘，滚动条落在页边距里（与歌手页的专辑网格一致）
            GridView {
                id: playlists
                visible: page.pane === "playlists"
                Layout.fillWidth: true
                Layout.fillHeight: true
                readonly property int usable: width - Tokens.pageMargin
                readonly property int columns: Math.max(1, Math.floor((usable + Tokens.space4) / (Tokens.tileLg + Tokens.space4)))
                cellWidth: (usable + Tokens.space4) / columns
                cellHeight: cellWidth - Tokens.space4 + Tokens.space2 + Tokens.height12 + Tokens.space4
                bottomMargin: Tokens.space6
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: page.user.playlists || []
                ScrollBar.vertical: ScrollBar {}
                WheelScroll { flickable: playlists }

                delegate: MediaTile {
                    required property var modelData
                    width: playlists.cellWidth - Tokens.space4
                    title: modelData.name
                    caption: modelData.count > 0 ? modelData.count + " 首" : "空歌单"
                    cover: modelData.cover
                    onClicked: Actions.openPlaylist(modelData.id)
                }

                PaneMessage {
                    visible: playlists.count === 0
                    icon.name: "view-media-playlist"
                    pending: page.user.id <= 0 || page.user.name === ""
                    failed: page.user.failed === true
                    retryKey: "user:" + page.userId
                    emptyText: "还没有公开歌单"
                }
            }

            // 听歌排行：这段时间里播放最多的歌，按次数排；点一首从这里开始放整张排行
            ColumnLayout {
                visible: page.pane === "rank"
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Tokens.space3

                RowLayout {
                    Layout.rightMargin: Tokens.pageMargin
                    spacing: Tokens.space1
                    ButtonGroup { id: rankRange }
                    Repeater {
                        model: [{ key: "week", text: "最近一周" }, { key: "month", text: "最近一月" }]
                        delegate: Button {
                            required property var modelData
                            ButtonGroup.group: rankRange
                            implicitHeight: Tokens.height8
                            flat: true
                            checkable: true
                            checked: page.rankType === modelData.key
                            text: modelData.text
                            onClicked: page.rankType = modelData.key
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        visible: page.rank.songCount > 0
                        text: "听了 " + Format.count(page.rank.songCount) + " 首歌"
                        font.pixelSize: Tokens.textSm
                        font.features: { "tnum": 1 }
                        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    }
                }

                ListView {
                    id: rankList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    rightMargin: Tokens.pageMargin
                    bottomMargin: Tokens.space6
                    spacing: Tokens.space2
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: page.rank.tracks
                    ScrollBar.vertical: ScrollBar {}
                    WheelScroll { flickable: rankList }

                    delegate: TrackTile {
                        width: rankList.width - rankList.rightMargin
                        ranked: true
                        current: modelData.id === page.currentId
                        endText: modelData.plays > 0 ? Format.count(modelData.plays) + " 次" : ""
                        onActivated: index => Actions.play(page.rank.tracks, index)
                    }

                    PaneMessage {
                        visible: rankList.count === 0
                        icon.name: "view-media-chart"
                        pending: page.rank.pending
                        failed: page.rank.failed
                        retryKey: "listenRank:" + page.rankType
                        emptyText: page.rankType === "week" ? "最近一周还没有听歌记录" : "最近一月还没有听歌记录"
                    }
                }
            }

            // 关注 / 粉丝：人物行按列宽铺成几列；滚到底接着取下一页
            Repeater {
                model: [{ key: "follows", kind: "userFollows", empty: "还没有关注任何人" },
                        { key: "fans", kind: "userFolloweds", empty: "还没有粉丝" }]
                delegate: GridView {
                    id: people
                    required property var modelData
                    readonly property var list: people.modelData.key === "follows" ? page.follows : page.fans
                    visible: page.pane === people.modelData.key
                    onVisibleChanged: if (visible) page.peopleView = people
                    Component.onCompleted: if (visible) page.peopleView = people
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    readonly property int usable: width - Tokens.pageMargin
                    readonly property int columns: Math.max(1, Math.floor((usable + Tokens.space4) / (Tokens.measureMax + Tokens.space4)))
                    cellWidth: (usable + Tokens.space4) / columns
                    cellHeight: Tokens.trackRow + Tokens.space2
                    bottomMargin: Tokens.space6
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: people.list.items
                    ScrollBar.vertical: ScrollBar {}
                    WheelScroll { flickable: people }
                    onAtYEndChanged: if (atYEnd && visible) Source.morePeople(people.modelData.kind, people.list)
                    footer: LinkText {
                        visible: people.list.moreFailed === true
                        height: visible ? Tokens.height10 : 0
                        verticalAlignment: Text.AlignVCenter
                        parts: [{ text: "后面的没取到，点此重试", link: true }]
                        font.pixelSize: Tokens.textSm
                        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                        onActivated: Source.morePeople(people.modelData.kind, people.list)
                    }

                    delegate: PersonRow {
                        required property var modelData
                        width: people.cellWidth - Tokens.space4
                        avatar: modelData.avatar
                        name: modelData.name
                        detail: modelData.signature
                        tags: ((modelData.vipType || 0) > 0 ? [{ text: "VIP" }] : [])
                            .concat(modelData.mutual ? [{ text: "互相关注", muted: true }] : [])
                        onClicked: Actions.openUser(modelData.id)
                    }

                    PaneMessage {
                        objectName: "peopleMessage:" + people.modelData.key
                        visible: people.count === 0
                        icon.name: "system-users"
                        pending: people.list.pending === true
                        failed: people.list.failed === true
                        restricted: people.list.restricted === true
                        restrictedText: people.modelData.key === "follows"
                                        ? "对方设置了隐私，无法查看关注列表"
                                        : "对方设置了隐私，无法查看粉丝列表"
                        retryKey: people.modelData.kind + ":" + page.userId
                        emptyText: people.modelData.empty
                    }
                }
            }
        }
    }
}
