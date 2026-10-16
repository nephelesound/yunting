// 设计语言参照 Rune（Zune/Metro 一脉）：展示字做导航，扁平磁贴，极简播放条。
// 当前阶段只做「底板」：版面、令牌、对齐、密度。整窗一块底色，页面与播放条都没有自己的面，
// 内部不画分隔线；装饰（封面取色、滚动边缘渐隐、悬停动效）留到版面定稿之后。
// 数据：所有组件经 Source.qml 单例读快照（fixture 可换 Fixtures.js 压力数据）。
pragma ComponentBehavior: Bound

import QtQuick
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root

    visible: true
    title: root.currentTrack ? root.currentTrack.name + " — " + root.currentTrack.artist : "NCM Player"
    width: 1280
    height: 800
    minimumWidth: 800
    minimumHeight: 560

    // 截图 harness 可从命令行覆盖（先给 listKey 再给 page）：
    //   page = home | charts | list | nowplaying | mine | search；listKey = daily | heart | radar | similar | liked | hot
    property string page: "home"
    property string listKey: "daily"
    property string account: "personal"   // anonymous：即使有个人快照也按未登录显示
    property string fixture: ""           // worst | empty | one：Fixtures.js 压力测试数据（截图用，必须在 page 之前设置）
    onFixtureChanged: root.applyFixture()

    readonly property bool loggedIn: root.account !== "anonymous" && Source.account !== null
    readonly property bool vip: root.loggedIn && Source.account.vip
    readonly property var pivots: [{ key: "home", text: "推荐" }, { key: "mine", text: "我的" }, { key: "search", text: "搜索" }]

    readonly property var openList: Source.list(root.listKey)

    // ── 播放状态（原型内模拟，之后由 Player 后端驱动）。队列为空时 currentTrack 为 null ──
    property var queue: []
    property int trackIndex: -1
    property bool playing: true
    property real positionSec: 83
    property bool liked: false

    readonly property var currentTrack: root.trackIndex >= 0 && root.trackIndex < root.queue.length ? root.queue[root.trackIndex] : null

    // 换数据源后，原型的「正在播放」从热歌榜第 3 首开始（数据不够就取最后一首）
    function applyFixture() {
        Source.fixture = root.fixture;
        root.queue = Source.hotChart.tracks;
        root.trackIndex = Math.min(2, root.queue.length - 1);
    }

    Timer {
        interval: 1000
        repeat: true
        running: root.playing
        onTriggered: root.positionSec = Math.min(root.positionSec + 1, root.currentTrack ? root.currentTrack.dur : 0)
    }

    // 付费专辑（fee=4）要单独购买，VIP 也不能播；VIP 曲（fee=1）要会员
    function canPlay(track) {
        return track.fee !== 4 && (root.vip || track.fee !== 1);
    }
    function play(tracks, i) {
        if (i < 0 || i >= tracks.length)
            return;
        const track = tracks[i];
        if (track.fee === 4) {
            root.showPassiveNotification("「" + track.name + "」所在专辑需要单独购买");
            return;
        }
        if (!root.canPlay(track)) {
            if (root.loggedIn)
                root.showPassiveNotification("「" + track.name + "」是 VIP 曲目，开通会员后可播放");
            else
                root.showPassiveNotification("「" + track.name + "」是 VIP 曲目，登录后可播放", "long", "扫码登录", () => root.requestLogin());
            return;
        }
        root.queue = tracks;
        root.trackIndex = i;
        root.positionSec = 0;
        root.playing = true;
    }
    function step(delta) {
        if (root.queue.length === 0)
            return;
        let i = root.trackIndex;
        do {
            i = (i + delta + root.queue.length) % root.queue.length;
        } while (!root.canPlay(root.queue[i]) && i !== root.trackIndex);
        root.play(root.queue, i);
    }
    function requestLogin() {
        loginDialog.state = "waiting";
        loginDialog.open();
    }

    // ── 浮层：播放队列（右侧抽屉）与扫码登录（对话框）──
    // 截图 harness 可用 startOverlay = queue | login，以及 loginState = waiting | scanned | expired
    // （不能叫 overlay：会遮住 ApplicationWindow.overlay，Kirigami 的对话框就挂不上去了）
    property string startOverlay: ""
    property string loginState: "waiting"
    onStartOverlayChanged: root.openOverlay()

    QueuePanel {
        id: queuePanel
        queue: root.queue
        currentIndex: root.trackIndex
        vip: root.vip
        onActivated: index => root.play(root.queue, index)
        onRemoveRequested: index => {
            const q = root.queue.slice();
            q.splice(index, 1);
            root.queue = q;
        }
        onClearRequested: root.queue = root.queue.slice(0, root.trackIndex + 1)
    }

    LoginDialog {
        id: loginDialog
        onRefreshRequested: loginDialog.state = "waiting"
    }

    function openOverlay() {
        if (root.startOverlay === "queue")
            queuePanel.open();
        else if (root.startOverlay === "login") {
            loginDialog.state = root.loginState;
            loginDialog.open();
        }
    }

    // ── 导航：推荐/我的/搜索 是顶层枢轴；排行榜、列表、正在播放压在其上，history 记录来路 ──
    property var history: []
    readonly property var pageNames: ({ home: "推荐", mine: "我的", search: "搜索", charts: "排行榜" })
    readonly property string parentName: {
        const prev = root.history.length > 0 ? root.history[root.history.length - 1] : { page: "home" };
        return prev.page === "list" ? Source.list(prev.listKey).heading : root.pageNames[prev.page] || "推荐";
    }

    function open(page, listKey) {
        root.history = root.history.concat([{ page: root.page, listKey: root.listKey }]);
        if (listKey)
            root.listKey = listKey;
        root.page = page;
    }
    function goBack() {
        if (root.history.length === 0)
            return;
        const prev = root.history[root.history.length - 1];
        root.history = root.history.slice(0, -1);
        root.listKey = prev.listKey;
        root.page = prev.page;
    }
    function switchPivot(key) {
        root.history = [];
        root.page = key;
    }
    function toggleNowPlaying() {
        if (root.page === "nowplaying")
            root.goBack();
        else
            root.open("nowplaying");
    }
    function openEntry(key) {
        if (key === "charts")
            root.open("charts");
        else if (key === "fm") {
            if (Source.fmTracks.length === 0)
                return;
            root.play(Source.fmTracks, 0);
            root.open("nowplaying");
        } else if (!root.loggedIn && key !== "similar")
            root.requestLogin();
        else
            root.open("list", key);
    }

    // ── 键盘与鼠标侧键：每个常用动作都不必离开键盘 ──
    Shortcut { sequence: "Space"; onActivated: root.playing = !root.playing }
    Shortcut { sequence: "Left"; onActivated: root.positionSec = Math.max(0, root.positionSec - 5) }
    Shortcut { sequence: "Right"; onActivated: root.positionSec = Math.min(root.currentTrack ? root.currentTrack.dur : 0, root.positionSec + 5) }
    Shortcut { sequence: "Ctrl+Left"; onActivated: root.step(-1) }
    Shortcut { sequence: "Ctrl+Right"; onActivated: root.step(1) }
    Shortcut { sequence: StandardKey.Find; onActivated: root.switchPivot("search") }
    Shortcut { sequences: ["Esc", StandardKey.Back]; onActivated: root.goBack() }
    Shortcut { sequence: "Ctrl+L"; onActivated: root.toggleNowPlaying() }
    TapHandler {
        acceptedButtons: Qt.BackButton
        onTapped: root.goBack()
    }

    // 「我的」未登录时是登录引导；搜索词由 harness 的 searchQuery 预填（空 = 热门搜索）
    property string searchQuery: ""
    readonly property var pages: ({ home: homePage, mine: root.loggedIn ? minePage : placeholderPage, search: searchPage,
                                    charts: chartsPage, list: listPage, nowplaying: nowPlayingPage })
    onPageChanged: {
        pageStack.clear();
        pageStack.push(root.pages[root.page]);
    }
    Component.onCompleted: {
        // 先换好数据源与队列，再推首页
        root.applyFixture();
        root.pageChanged();
    }

    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false
    background: Rectangle { color: Kirigami.Theme.backgroundColor }

    pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.None
    pageStack.columnView.columnResizeMode: Kirigami.ColumnView.SingleColumn

    footer: PlayerBar {
        track: root.currentTrack
        playing: root.playing
        elapsed: root.positionSec
        liked: root.liked
        nowPlayingShown: root.page === "nowplaying"
        onPlayPauseRequested: root.playing = !root.playing
        onNextRequested: root.step(1)
        onPreviousRequested: root.step(-1)
        onSeekRequested: seconds => root.positionSec = seconds
        onLikeToggled: root.liked = !root.liked
        onNowPlayingToggled: root.toggleNowPlaying()
        onQueueRequested: queuePanel.drawerOpen ? queuePanel.close() : queuePanel.open()
    }

    Component {
        id: homePage
        HomePage {
            pivots: root.pivots
            loggedIn: root.loggedIn
            onPivotActivated: key => root.switchPivot(key)
            onLoginRequested: root.requestLogin()
            onEntryOpened: key => root.openEntry(key)
            onPlaylistOpened: index => root.open("list", "playlist:" + index)
        }
    }

    Component {
        id: chartsPage
        ChartsPage {
            loggedIn: root.loggedIn
            parentText: root.parentName
            currentId: root.currentTrack ? root.currentTrack.id : 0
            onLeaveRequested: root.goBack()
            onLoginRequested: root.requestLogin()
            onToplistOpened: index => root.open("list", "hot")
            onTrackActivated: index => root.play(Source.hotChart.tracks, index)
        }
    }

    Component {
        id: listPage
        PlaylistPage {
            list: root.openList
            loggedIn: root.loggedIn
            vip: root.vip
            parentText: root.parentName
            currentId: root.currentTrack ? root.currentTrack.id : 0
            onActivated: index => root.play(root.openList.tracks, index)
            onPlayAllRequested: root.play(root.openList.tracks, 0)
            onShuffleRequested: root.play(root.openList.tracks, Math.floor(Math.random() * root.openList.tracks.length))
            onLeaveRequested: root.goBack()
            onLoginRequested: root.requestLogin()
        }
    }

    Component {
        id: nowPlayingPage
        NowPlayingPage {
            track: root.currentTrack
            elapsed: root.positionSec
            loggedIn: root.loggedIn
            parentText: root.parentName
            onLeaveRequested: root.goBack()
            onLoginRequested: root.requestLogin()
        }
    }

    Component {
        id: minePage
        MinePage {
            pivots: root.pivots
            loggedIn: root.loggedIn
            currentId: root.currentTrack ? root.currentTrack.id : 0
            onPivotActivated: key => root.switchPivot(key)
            onLoginRequested: root.requestLogin()
            onEntryOpened: key => root.open("list", key)
            onPlaylistOpened: index => root.showPassiveNotification("歌单详情：待接 NcmClient.playlist()")
            onRecentActivated: index => root.play(Source.recent.tracks, index)
        }
    }

    Component {
        id: searchPage
        SearchPage {
            pivots: root.pivots
            loggedIn: root.loggedIn
            query: root.searchQuery
            currentId: root.currentTrack ? root.currentTrack.id : 0
            onPivotActivated: key => root.switchPivot(key)
            onLoginRequested: root.requestLogin()
            onTrackActivated: index => root.play(Source.searchResult.songs, index)
        }
    }

    Component {
        id: placeholderPage
        ScreenFrame {
            pivots: root.pivots
            currentPivot: root.page
            loggedIn: root.loggedIn
            onPivotActivated: key => root.switchPivot(key)
            onLoginRequested: root.requestLogin()

            // 只用于未登录的「我的」
            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - 2 * Tokens.pageMargin
                icon.name: "love-symbolic"
                text: "登录后显示你的音乐"
                explanation: "我喜欢的音乐、最近播放、云盘和收藏的歌单都跟随网易云账号。"
                helpfulAction: loginAction
            }
            Kirigami.Action {
                id: loginAction
                icon.name: "view-barcode-qr"
                text: "扫码登录"
                onTriggered: root.requestLogin()
            }
        }
    }
}
