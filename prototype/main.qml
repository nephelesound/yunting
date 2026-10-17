// 设计语言参照 Rune（Zune/Metro 一脉）：展示字做导航，扁平磁贴，极简播放条。
// 当前阶段只做「底板」：版面、令牌、对齐、密度。整窗一块底色，页面与播放条都没有自己的面，
// 内部不画分隔线；装饰（封面取色、滚动边缘渐隐、悬停动效）留到版面定稿之后。
// 三条接缝（见 DESIGN.md「后端接口」），每条都有原型实现与 C++ 实现：
//   数据 Source.live ← backend.store；声音 Playback.engine ← backend.audio；登录 LoginDialog.session ← backend.session。
// backend 只由 C++ 应用注入；用 qml 工具或截图 harness 直接打开时为 null，走原型实现。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root

    visible: true
    title: root.currentTrack ? root.currentTrack.name + " — " + root.currentTrack.artist : "云听"
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

    // 界面层的日志分类：失败与异常写 warning（与 C++ 的 yunting.net / yunting.vault 同一套 Qt 日志）
    LoggingCategory {
        id: logUi
        name: "yunting.ui"
        defaultLogLevel: LoggingCategory.Warning
    }

    // 账号字段在后端回话前可能是 undefined（不只是 null）：两种都算未登录
    readonly property bool loggedIn: root.account !== "anonymous" && Source.account != null
    readonly property bool vip: root.loggedIn && Source.account.vip
    readonly property var pivots: [{ key: "home", text: "推荐" }, { key: "mine", text: "我的" }, { key: "search", text: "搜索" }]

    readonly property var openList: Source.list(root.listKey)

    // 播放：队列与规则在 Playback，声音在 engine。队列为空时 currentTrack 为 null
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
        function onFailed(reason) {
            console.warn(logUi, "playback failed:", root.currentTrack ? root.currentTrack.id : 0, reason);
            root.showPassiveNotification(reason);
        }
    }
    // 私人漫游是一个流：放到倒数第二首时续取一批，取回来的接在队尾，不会放完一轮又从头循环
    Connections {
        target: playback
        function onIndexChanged() {
            if (playback.origin === "fm" && playback.index >= playback.queue.length - 2)
                Source.moreFm();
        }
    }
    Connections {
        target: Source
        function onFmTracksChanged() {
            if (playback.origin === "fm")
                playback.append(Source.fmTracks);
        }
    }

    // 红心是乐观更新：失败时 Store 已经改回，这里只说明原因
    Connections {
        target: root.backend ? root.backend.store : null
        ignoreUnknownSignals: true
        function onLikeFailed(songId, liked, message) {
            console.warn(logUi, "like failed:", songId, liked ? "on" : "off", message);
            root.showPassiveNotification((liked ? "没能加上红心：" : "没能取消红心：") + message);
        }
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

    // 截图 harness 用：正在播放页右栏（lyrics | comments）、搜索右栏（artists | albums | playlists）、
    // 歌手页右栏（songs | albums）、模拟离线 / 登录过期
    property string nowPlayingPane: "lyrics"
    property string searchCollection: "artists"
    property string artistPane: "songs"
    property bool simulateOffline: false
    property bool simulateExpired: false
    onSimulateOfflineChanged: Source.simulateOffline = root.simulateOffline
    onSimulateExpiredChanged: Source.simulateExpired = root.simulateExpired

    // 换数据源。原型（没有 backend）的「正在播放」从热歌榜第 3 首开始、停在 1:23，截图才有内容可看
    function applyFixture() {
        Source.live = root.backend ? root.backend.store : null;
        if (root.backend)
            return;
        // 按路径创建：Snapshot 不在 qmldir 里，打包的应用不带它与快照数据
        if (!Source.snapshot)
            Source.snapshot = Qt.createComponent("Snapshot.qml").createObject(root);
        Source.snapshot.fixture = root.fixture;
        playback.restore(Source.hotChart.tracks, 2);
        simEngine.position = Math.min(83, Math.floor(simEngine.duration / 2));   // 短歌也停在中段，不会一启动就播完跳走
        simEngine.playing = true;
    }

    function play(tracks, i) { playback.play(tracks, i); }
    function requestLogin() { loginDialog.open(); }

    // 浮层：播放队列（右侧抽屉）与扫码登录（对话框）
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
        onMoveRequested: (from, to) => playback.move(from, to)
    }

    LoginDialog {
        id: loginDialog
        session: root.backend ? root.backend.session : simSession
    }

    // 曲目菜单与歌单写操作
    TrackMenu {
        id: trackMenu
        player: playback
        loggedIn: root.loggedIn
        onLoginRequested: root.requestLogin()
        onNewPlaylistRequested: track => playlistNameDialog.ask(0, "", track)
        onAddRequested: (playlist, track) => {
            root.noteOnSuccess(playlist.id, "添加歌曲", "已添加到「" + playlist.name + "」");
            Source.addToPlaylist(playlist.id, track.id);
        }
        onRemoveRequested: (playlistId, track) => {
            root.noteOnSuccess(playlistId, "移出歌单", "已从歌单移除「" + track.name + "」");
            Source.removeFromPlaylist(playlistId, track.id);
        }
        onCopied: root.showPassiveNotification("已复制歌曲链接")
    }

    // 歌单写操作成功时 Store 只发 playlistChanged(id)、失败只发 playlistActionFailed(操作名, 原因)：
    // 发请求时按歌单 id 排队记下 { action, text }（同一歌单连续几次操作各有各的话），
    // 成功时取该歌单最早的一条说出来，失败时按操作名撤掉最早的那条
    property var successNotes: ({})
    function noteOnSuccess(id, action, text) {
        const notes = Object.assign({}, root.successNotes);
        notes[id] = (notes[id] || []).concat([{ action: action, text: text }]);
        root.successNotes = notes;
    }
    function takeNote(match) {
        const notes = Object.assign({}, root.successNotes);
        for (const id in notes) {
            const i = notes[id].findIndex(match);
            if (i < 0)
                continue;
            const note = notes[id][i];
            notes[id] = notes[id].filter((_, k) => k !== i);
            if (notes[id].length === 0)
                delete notes[id];
            root.successNotes = notes;
            return note;
        }
        return null;
    }

    // 新建（id = 0，可带一首要加进去的歌）或重命名（id > 0）共用一个对话框
    Kirigami.PromptDialog {
        id: playlistNameDialog
        property real playlistId
        property var pendingTrack: null
        function ask(id, name, track) {
            playlistNameDialog.playlistId = id;
            playlistNameDialog.pendingTrack = track;
            nameField.text = name;
            playlistNameDialog.open();
            nameField.forceActiveFocus();
            nameField.selectAll();
        }
        title: playlistNameDialog.playlistId > 0 ? "重命名歌单" : "新建歌单"
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: playlistNameDialog.playlistId > 0 ? "重命名" : "创建"
                icon.name: "dialog-ok"
                enabled: nameField.text.trim().length > 0
                onTriggered: playlistNameDialog.accept()
            },
            Kirigami.Action {
                text: "取消"
                icon.name: "dialog-cancel"
                onTriggered: playlistNameDialog.reject()
            }
        ]
        onAccepted: {
            const name = nameField.text.trim();
            if (playlistNameDialog.playlistId > 0) {
                root.noteOnSuccess(playlistNameDialog.playlistId, "重命名歌单", "已重命名为「" + name + "」");
                Source.renamePlaylist(playlistNameDialog.playlistId, name);
            } else {
                root.trackForNewPlaylist = playlistNameDialog.pendingTrack;
                Source.createPlaylist(name);
            }
        }
        TextField {
            id: nameField
            placeholderText: "歌单名称"
            maximumLength: 40
            onAccepted: if (nameField.text.trim().length > 0) playlistNameDialog.accept()
        }
    }

    // 删除歌单不可撤销：明确说出删的是哪个，确认按钮用否定色
    Kirigami.PromptDialog {
        id: deleteDialog
        property real playlistId
        property string playlistName
        function ask(id, name) {
            deleteDialog.playlistId = id;
            deleteDialog.playlistName = name;
            deleteDialog.open();
        }
        title: "删除歌单"
        subtitle: "删除「" + deleteDialog.playlistName + "」？歌单里的歌不会从网易云消失，但这个歌单无法恢复。"
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: "删除"
                icon.name: "edit-delete"
                onTriggered: deleteDialog.accept()
            },
            Kirigami.Action {
                text: "取消"
                icon.name: "dialog-cancel"
                onTriggered: deleteDialog.reject()
            }
        ]
        onAccepted: {
            root.noteOnSuccess(deleteDialog.playlistId, "删除歌单", "已删除歌单「" + deleteDialog.playlistName + "」");
            Source.deletePlaylist(deleteDialog.playlistId);
            if (root.page === "list" && root.listKey === "playlist:" + deleteDialog.playlistId)
                root.goBack();
        }
    }

    // 退出登录要重新扫码：确认一次
    Kirigami.PromptDialog {
        id: logoutDialog
        title: "退出登录"
        subtitle: "退出后个人推荐、红心与歌单都会隐藏，下次需要重新扫码登录。"
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: "退出登录"
                icon.name: "system-log-out"
                onTriggered: logoutDialog.accept()
            },
            Kirigami.Action {
                text: "取消"
                icon.name: "dialog-cancel"
                onTriggered: logoutDialog.reject()
            }
        ]
        onAccepted: {
            Source.logout();
            root.switchPivot("home");
        }
    }

    // Store 的写操作结果：新建歌单后把待加的歌放进去；失败一律用被动通知说明原因
    property var trackForNewPlaylist: null
    Connections {
        target: root.backend ? root.backend.store : null
        ignoreUnknownSignals: true
        function onPlaylistCreated(id, name) {
            if (root.trackForNewPlaylist) {
                root.noteOnSuccess(id, "添加歌曲", "已创建歌单「" + name + "」，并加入「" + root.trackForNewPlaylist.name + "」");
                Source.addToPlaylist(id, root.trackForNewPlaylist.id);
                root.trackForNewPlaylist = null;
            } else {
                root.showPassiveNotification("已创建歌单「" + name + "」");
            }
        }
        function onPlaylistChanged(id) {
            const queued = root.successNotes[id];
            if (!queued)
                return;
            const notes = Object.assign({}, root.successNotes);
            notes[id] = queued.slice(1);
            if (notes[id].length === 0)
                delete notes[id];
            root.successNotes = notes;
            root.showPassiveNotification(queued[0].text);
        }
        function onPlaylistActionFailed(action, message) {
            console.warn(logUi, "playlist action failed:", action, message);
            // 只撤掉这次失败的那条；新建失败时才放弃「建好后要加入的歌」
            root.takeNote(note => note.action === action);
            if (action === "创建歌单")
                root.trackForNewPlaylist = null;
            root.showPassiveNotification(message);
        }
    }

    function openOverlay() {
        if (root.startOverlay === "trackmenu") {
            // 截图：在列表第一行右侧打开曲目菜单（与点行尾「更多」的位置相同）
            trackMenu.track = root.openList.tracks[0];
            trackMenu.context = {};
            trackMenu.popup(root.width - Tokens.pageMargin - trackMenu.implicitWidth, Tokens.height12 * 3);
        } else if (root.startOverlay === "queue")
            queuePanel.open();
        else if (root.startOverlay === "login") {
            loginDialog.open();
            simSession.state = root.loginState;
        }
    }

    // 导航：推荐/我的/搜索 是顶层枢轴；排行榜、列表、歌手、正在播放压在其上，history 记录来路
    property var history: []
    readonly property var pageNames: ({ home: "推荐", mine: "我的", search: "搜索", charts: "排行榜", playlists: "全部歌单",
                                        nowplaying: "正在播放" })
    readonly property string parentName: {
        const prev = root.history.length > 0 ? root.history[root.history.length - 1] : { page: "home" };
        if (prev.page === "list")
            return Source.list(prev.listKey).heading;
        if (prev.page === "artist")
            return Source.artist(Number(prev.listKey.slice(7))).name;
        return root.pageNames[prev.page] || "推荐";
    }

    // 深处组件经 Actions 发来的导航请求：歌手页、专辑页（列表页的一种）、任意歌单（含榜单）
    Connections {
        target: Actions
        function onArtistRequested(id) { root.open("artist", "artist:" + id); }
        function onAlbumRequested(id) { root.open("list", "album:" + id); }
        function onPlaylistRequested(id) {
            // 热歌榜在快照里有现成曲目，用它；其余歌单按 id 取
            root.open("list", id === Source.d.playlist.id ? "hot" : "playlist:" + id);
        }
        function onTrackMenuRequested(track, context) { trackMenu.show(track, context); }
        function onLogoutRequested() { logoutDialog.open(); }
        function onPlaylistMenuRequested(playlist) {
            playlistMenu.playlist = playlist;
            playlistMenu.popup();
        }
        function onNewPlaylistRequested() { playlistNameDialog.ask(0, "", null); }
    }

    // 歌单磁贴的菜单（「我的」与「全部歌单」）：自己建的可改名/删除（「我喜欢的音乐」除外），收藏的可取消收藏
    Menu {
        id: playlistMenu
        property var playlist: ({ id: 0, name: "", owned: false })
        readonly property bool editable: playlistMenu.playlist.owned && playlistMenu.playlist.id !== (Source.pe.liked.id || 0)
        MenuItem {
            icon.name: "view-media-playlist"
            text: "打开"
            onTriggered: Actions.openPlaylist(playlistMenu.playlist.id)
        }
        MenuSeparator {}
        MenuItem {
            visible: playlistMenu.editable
            height: visible ? implicitHeight : 0
            icon.name: "edit-rename"
            text: "重命名…"
            onTriggered: playlistNameDialog.ask(playlistMenu.playlist.id, playlistMenu.playlist.name, null)
        }
        MenuItem {
            visible: playlistMenu.editable
            height: visible ? implicitHeight : 0
            icon.name: "edit-delete"
            text: "删除歌单…"
            onTriggered: deleteDialog.ask(playlistMenu.playlist.id, playlistMenu.playlist.name)
        }
        MenuItem {
            visible: !playlistMenu.playlist.owned
            height: visible ? implicitHeight : 0
            icon.name: "bookmark-remove"
            text: "取消收藏"
            onTriggered: {
                root.noteOnSuccess(playlistMenu.playlist.id, "取消收藏歌单", "已取消收藏「" + playlistMenu.playlist.name + "」");
                Source.setPlaylistSubscribed(playlistMenu.playlist.id, false);
            }
        }
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
            playback.play(Source.fmTracks, 0, "fm");
            root.open("nowplaying");
        } else if (!root.loggedIn && key !== "similar")
            root.requestLogin();
        else
            root.open("list", key);
    }

    // 键盘与鼠标侧键：每个常用动作都不必离开键盘
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
                                    charts: chartsPage, list: listPage, artist: artistPage, playlists: playlistsPage,
                                    nowplaying: nowPlayingPage })
    onPageChanged: {
        pageStack.clear();
        pageStack.push(root.pages[root.page]);
    }
    // 「我的」在登录前后是两个不同的页（引导 / 内容）：登录状态一变就换掉，不必切走再切回来
    onLoggedInChanged: if (root.page === "mine") root.pageChanged()
    Component.onCompleted: {
        root.applyCommandLine();
        // 先换好数据源与队列，再推首页
        root.applyFixture();
        root.pageChanged();
    }

    // 应用里截图与调试：命令行 -- 之后的 key=value 与 qmlshot 同一格式，设到根对象的同名属性上
    //（例：yunting --screenshot a.png -- page=artist listKey=artist:6452）。
    // 只认已有的字符串、数字、布尔属性：函数、对象、只读属性都不会被命令行覆盖；认不出的写进日志
    function applyCommandLine() {
        const args = Qt.application.arguments;
        const start = args.indexOf("--");
        if (start < 0)
            return;
        for (const pair of args.slice(start + 1)) {
            const eq = pair.indexOf("=");
            const key = pair.slice(0, eq);
            const value = pair.slice(eq + 1);
            const kind = eq > 0 && key in root ? typeof root[key] : "";
            try {
                if (kind === "string")
                    root[key] = value;
                else if (kind === "boolean")
                    root[key] = value === "true";
                else if (kind === "number" && !isNaN(Number(value)))
                    root[key] = Number(value);
                else
                    console.warn(logUi, "ignored command-line argument:", pair);
            } catch (e) {
                console.warn(logUi, "cannot set", key, "from command line:", e.message);
            }
        }
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
        onLikeToggled: {
            if (!root.loggedIn) {
                root.showPassiveNotification("红心会同步到你的网易云账号，登录后可用", "long", "扫码登录", () => root.requestLogin());
                return;
            }
            Source.setLiked(root.currentTrack.id, !Source.isLiked(root.currentTrack.id));
        }
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
            onToplistOpened: index => Actions.openPlaylist(Source.toplists[index].id)
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
            onRenameRequested: (id, name) => playlistNameDialog.ask(id, name, null)
            onDeleteRequested: (id, name) => deleteDialog.ask(id, name)
            onSubscribeToggled: (id, name, subscribed) => {
                root.noteOnSuccess(id, subscribed ? "收藏歌单" : "取消收藏歌单", (subscribed ? "已收藏「" : "已取消收藏「") + name + "」");
                Source.setPlaylistSubscribed(id, subscribed);
            }
        }
    }

    Component {
        id: artistPage
        ArtistPage {
            id: artistView
            artistId: Number(root.listKey.slice(7))
            pane: root.artistPane
            loggedIn: root.loggedIn
            vip: root.vip
            parentText: root.parentName
            currentId: root.currentTrack ? root.currentTrack.id : 0
            onActivated: index => root.play(artistView.songs, index)
            onPlayAllRequested: root.play(artistView.artist.tracks, 0)
            onShuffleRequested: root.play(artistView.artist.tracks, Math.floor(Math.random() * artistView.artist.tracks.length))
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
            onAllPlaylistsRequested: root.open("playlists")
        }
    }

    Component {
        id: playlistsPage
        PlaylistsPage {
            loggedIn: root.loggedIn
            parentText: root.parentName
            onLeaveRequested: root.goBack()
            onLoginRequested: root.requestLogin()
        }
    }

    Component {
        id: searchPage
        SearchPage {
            pivots: root.pivots
            loggedIn: root.loggedIn
            query: root.searchQuery
            collection: root.searchCollection
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
