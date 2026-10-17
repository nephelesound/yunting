// 设计语言参照 Rune（Zune/Metro 一脉）：展示字做导航，扁平磁贴，极简播放条。
// 当前阶段只做「底板」：版面、令牌、对齐、密度。整窗一块底色，页面与播放条都没有自己的面，
// 内部不画分隔线；装饰（封面取色、滚动边缘渐隐、悬停动效）留到版面定稿之后。
// 三条接缝（见 DESIGN.md「后端接口」），每条都有原型实现与 C++ 实现：
//   数据 Source.live ← backend.store；声音 Playback.engine ← backend.audio；登录 LoginDialog.session ← backend.session。
// backend 只由 C++ 应用注入；用 qml 工具或截图 harness 直接打开时为 null，走原型实现。
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

    property var backend: null      // { store, audio, session }，C++ 应用注入

    readonly property bool loggedIn: root.account !== "anonymous" && Source.account !== null
    readonly property bool vip: root.loggedIn && Source.account.vip
    readonly property var pivots: [{ key: "home", text: "推荐" }, { key: "mine", text: "我的" }, { key: "search", text: "搜索" }]

    readonly property var openList: Source.list(root.listKey)

    // ── 播放：队列与规则在 Playback，声音在 engine。队列为空时 currentTrack 为 null ──
    SimEngine { id: simEngine }
    SimSession { id: simSession }

    Playback {
        id: playback
        engine: root.backend ? root.backend.audio : simEngine
        vip: root.vip
        onBlocked: (track, reason) => {
            if (reason === "paid")
                root.showPassiveNotification("「" + track.name + "」所在专辑需要单独购买");
            else if (root.loggedIn)
                root.showPassiveNotification("「" + track.name + "」是 VIP 曲目，开通会员后可播放");
            else
                root.showPassiveNotification("「" + track.name + "」是 VIP 曲目，登录后可播放", "long", "扫码登录", () => root.requestLogin());
        }
    }
    Connections {
        target: playback.engine
        function onFailed(reason) { root.showPassiveNotification(reason); }
    }
    // 媒体键 / 锁屏 / Plasma 媒体控件 / 托盘菜单的上一首、下一首：同样走 Playback，切歌规则只有一份
    Connections {
        target: root.backend ? root.backend.mpris : null
        ignoreUnknownSignals: true
        function onNextRequested() { playback.next(); }
        function onPreviousRequested() { playback.previous(); }
        function onRaiseRequested() {
            root.show();
            root.raise();
            root.requestActivate();
        }
    }
    Connections {
        target: root.backend && root.backend.tray ? root.backend.tray : null
        ignoreUnknownSignals: true
        function onNextRequested() { playback.next(); }
        function onPreviousRequested() { playback.previous(); }
    }

    // 有托盘时，关窗口 = 隐藏到托盘、继续播放；托盘「退出」或 Ctrl+Q 才真正退出
    property bool quitting: false
    onClosing: close => {
        if (root.backend && root.backend.tray && !root.quitting) {
            close.accepted = false;
            root.hide();
        }
    }
    Shortcut {
        sequence: StandardKey.Quit
        onActivated: {
            root.quitting = true;
            Qt.quit();
        }
    }

    readonly property var currentTrack: playback.current

    // 关掉再打开回到原样（只在应用里；原型与截图不读不写）
    Loader {
        active: root.backend !== null
        sourceComponent: SessionState {
            window: root
            player: playback
        }
    }

    // 截图 harness 用：正在播放页右栏（lyrics | comments）、模拟离线 / 登录过期
    property string nowPlayingPane: "lyrics"
    property bool simulateOffline: false
    property bool simulateExpired: false
    onSimulateOfflineChanged: Source.simulateOffline = root.simulateOffline
    onSimulateExpiredChanged: Source.simulateExpired = root.simulateExpired

    // 换数据源。原型（没有 backend）的「正在播放」从热歌榜第 3 首开始、停在 1:23，截图才有内容可看
    function applyFixture() {
        Source.live = root.backend ? root.backend.store : null;
        Source.fixture = root.fixture;
        if (root.backend)
            return;
        playback.restore(Source.hotChart.tracks, 2);
        simEngine.position = Math.min(83, Math.floor(simEngine.duration / 2));   // 短歌也停在中段，不会一启动就播完跳走
        simEngine.playing = true;
    }

    function play(tracks, i) { playback.play(tracks, i); }
    function requestLogin() { loginDialog.open(); }

    // ── 浮层：播放队列（右侧抽屉）与扫码登录（对话框）──
    // 截图 harness 可用 startOverlay = queue | login，以及 loginState = waiting | scanned | expired
    // （不能叫 overlay：会遮住 ApplicationWindow.overlay，Kirigami 的对话框就挂不上去了）
    property string startOverlay: ""
    property string loginState: "waiting"
    onStartOverlayChanged: root.openOverlay()

    QueuePanel {
        id: queuePanel
        queue: playback.queue
        currentIndex: playback.index
        vip: root.vip
        onActivated: index => playback.play(playback.queue, index)
        onRemoveRequested: index => playback.remove(index)
        onClearRequested: playback.clearUpcoming()
    }

    LoginDialog {
        id: loginDialog
        session: root.backend ? root.backend.session : simSession
    }

    function openOverlay() {
        if (root.startOverlay === "queue")
            queuePanel.open();
        else if (root.startOverlay === "login") {
            loginDialog.open();
            simSession.state = root.loginState;
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
            playback.play(Source.fmTracks, 0);
            root.open("nowplaying");
        } else if (!root.loggedIn && key !== "similar")
            root.requestLogin();
        else
            root.open("list", key);
    }

    // ── 键盘与鼠标侧键：每个常用动作都不必离开键盘 ──
    Shortcut { sequence: "Space"; onActivated: playback.engine.toggle() }
    Shortcut { sequence: "Left"; onActivated: playback.engine.seek(playback.engine.position - 5) }
    Shortcut { sequence: "Right"; onActivated: playback.engine.seek(playback.engine.position + 5) }
    Shortcut { sequence: "Ctrl+Left"; onActivated: playback.previous() }
    Shortcut { sequence: "Ctrl+Right"; onActivated: playback.next() }
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
        playing: playback.engine.playing
        elapsed: playback.engine.position
        mode: playback.mode
        volume: playback.engine.volume
        muted: playback.engine.muted
        onVolumeRequested: v => {
            playback.engine.volume = v;
            playback.engine.muted = false;
        }
        onMuteToggled: playback.engine.muted = !playback.engine.muted
        liked: root.currentTrack !== null && Source.isLiked(root.currentTrack.id)
        nowPlayingShown: root.page === "nowplaying"
        onPlayPauseRequested: playback.engine.toggle()
        onNextRequested: playback.next()
        onPreviousRequested: playback.previous()
        onModeRequested: m => playback.mode = m
        onSeekRequested: seconds => playback.engine.seek(seconds)
        // 红心的写操作（weapi 加密接口）还没接：先如实告知，不做会骗人的本地假状态
        onLikeToggled: root.showPassiveNotification("红心与取消红心即将支持，目前显示的是你在网易云里的红心状态")
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
            onPlaylistOpened: index => root.open("list", "playlist:" + Source.playlists[index].id)
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
            pane: root.nowPlayingPane
            track: root.currentTrack
            elapsed: playback.engine.position
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
            onPlaylistOpened: index => root.open("list", "playlist:" + Source.myPlaylists[index].id)
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
