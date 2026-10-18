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
    // 680：排行榜的上带、列表页的固定窗格在这个高度以下会压进播放条（这些版面按设计不滚动）
    minimumHeight: 680

    // 截图 harness 可从命令行覆盖（先给 listKey 再给 page）：
    //   page = home | charts | list | nowplaying | mine | search；listKey = daily | heart | radar | similar | liked | hot
    property string page: "home"
    property string listKey: "daily"
    property real mvId: 0
    property real userId: 0
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

    // 一起听：应用里是 C++ ListenTogether，原型里是替身（截图 harness 用 listenState 给定状态）
    property string listenState: "none"   // none | waiting | together
    SimListenTogether { id: simListen; state: root.listenState }
    readonly property var listenRoom: root.backend ? root.backend.listenTogether : simListen
    // 和谁一起听：房间里除自己以外的那个人
    readonly property string togetherText: {
        const me = Source.account ? Source.account.id : 0;
        const other = (root.listenRoom.users || []).find(u => u.userId !== me);
        return other ? "正在和 " + other.name + " 一起听" : "一起听：等好友加入";
    }
    ListenTogetherLink { room: root.listenRoom; playback: playback }
    Connections {
        target: root.listenRoom
        function onLeftRoom() { root.notify("一起听已结束"); }
    }

    // 用好友发来的链接加入：链接或房间号都行
    Kirigami.PromptDialog {
        id: joinDialog
        title: "加入一起听"
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: "加入"
                icon.name: "dialog-ok"
                enabled: joinField.text.trim().length > 0
                onTriggered: joinDialog.accept()
            },
            Kirigami.Action {
                text: "取消"
                icon.name: "dialog-cancel"
                onTriggered: joinDialog.reject()
            }
        ]
        onOpened: {
            joinField.text = "";
            joinField.forceActiveFocus();
        }
        onAccepted: root.listenRoom.joinRoom(joinField.text.trim())
        TextField {
            id: joinField
            implicitWidth: Tokens.trackColumn + Tokens.albumColumn
            placeholderText: "粘贴好友发来的一起听链接"
            onAccepted: if (joinField.text.trim().length > 0) joinDialog.accept()
        }
    }

    // 房主结束：房间散了，好友那边也会停；说清楚再结束
    Kirigami.PromptDialog {
        id: endListenDialog
        title: "结束一起听？"
        subtitle: "好友那边也会停止。"
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: "结束"
                icon.name: "system-log-out"
                onTriggered: endListenDialog.accept()
            },
            Kirigami.Action {
                text: "取消"
                icon.name: "dialog-cancel"
                onTriggered: endListenDialog.reject()
            }
        ]
        onAccepted: root.listenRoom.leaveRoom()
    }

    Playback {
        id: playback
        engine: root.backend ? root.backend.audio : simEngine
        vip: root.vip
        onBlocked: (track, reason) => {
            if (reason === "paid")
                root.fail("「" + track.name + "」所在专辑需要单独购买");
            else if (reason === "unavailable")
                root.fail("「" + track.name + "」已下架或暂时没有版权，不能播放");
            else if (root.loggedIn)
                root.fail("「" + track.name + "」是 VIP 曲目，开通会员后可播放");
            else
                root.fail("「" + track.name + "」是 VIP 曲目，登录后可播放", "扫码登录", () => root.requestLogin());
        }
    }
    Connections {
        target: playback.engine
        function onFailed(reason) {
            console.warn(logUi, "playback failed:", root.currentTrack ? root.currentTrack.id : 0, reason);
            root.fail(reason);
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
    // 智能续播：只在应用里持久化口味；原型/截图不写配置
    SmartQueue {
        id: smartQueue
        player: playback
        source: Source
        persist: root.backend !== null
    }

    // 红心是乐观更新：失败时 Store 已经改回，这里只说明原因
    Connections {
        target: root.backend ? root.backend.store : null
        ignoreUnknownSignals: true
        function onLikeFailed(songId, liked, message) {
            console.warn(logUi, "like failed:", songId, liked ? "on" : "off", message);
            root.fail((liked ? "没能加上红心：" : "没能取消红心：") + message);
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
    // 逐字歌词：每换一首歌要一次（后端永久缓存；不在绑定里要，绑定会反复求值）
    readonly property real currentTrackId: root.currentTrack ? root.currentTrack.id : 0
    onCurrentTrackIdChanged: if (root.currentTrackId > 0) Source.need("lyricsNew:" + root.currentTrackId)

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
    // 操作结果与提示用 notify；做不成的事（失败、不能播放）用 fail，提示条是错误配色。
    // 窗口不在眼前（藏在托盘、在别的窗口后面）时，失败另发一条系统通知，否则没人看得到
    function notify(message, timeout, actionText, action) { toast.show(message, timeout, actionText, action, false); }
    function fail(message, actionText, action) {
        toast.show(message, "long", actionText, action, true);
        if (!root.active && root.backend && root.backend.tray)
            root.backend.tray.notify(message);
    }
    Toast {
        id: toast
        parent: root.overlay
        anchors.fill: parent
        z: 1
    }
    // 「关于」窗口：第一次打开时才创建
    Loader {
        id: aboutLoader
        active: false
        sourceComponent: AboutWindow {
            diagnostics: root.backend && root.backend.diagnostics ? root.backend.diagnostics : null
        }
    }

    // 浮层：播放队列（右侧抽屉）与扫码登录（对话框）
    // 截图 harness 可用 startOverlay = queue | login | describe（歌单简介）…，以及 loginState = waiting | scanned | expired
    // （不能叫 overlay：会遮住 ApplicationWindow.overlay，Kirigami 的对话框就挂不上去了）
    property string startOverlay: ""
    // 截图 harness：打开的页切到这一栏（用户页的 events、消息页的 comments 之类）
    property string startPane: ""
    onStartPaneChanged: Qt.callLater(() => {
        const item = root.pageStack.currentItem;
        if (item && "pane" in item)
            item.pane = root.startPane;
    })
    property string loginState: "waiting"
    onStartOverlayChanged: root.openOverlay()

    QueuePanel {
        id: queuePanel
        queue: playback.queue
        currentIndex: playback.index
        vip: root.vip
        smartEnabled: smartQueue.enabled
        onSmartToggled: enabled => smartQueue.enabled = enabled
        onActivated: index => playback.play(playback.queue, index)
        onNowPlayingRequested: {
            queuePanel.close();
            if (root.page !== "nowplaying")
                root.toggleNowPlaying();
        }
        onRemoveRequested: index => playback.remove(index)
        onClearRequested: playback.clearUpcoming()
        onMoveRequested: (from, to) => playback.move(from, to)
        room: root.loggedIn ? root.listenRoom : null
        onListenStartRequested: root.listenRoom.createRoom()
        onListenJoinRequested: joinDialog.open()
        onListenEndRequested: root.listenRoom.host ? endListenDialog.open() : root.listenRoom.leaveRoom()
        onListenLinkCopied: root.notify("已复制邀请链接")
    }

    LoginDialog {
        id: loginDialog
        session: root.backend ? root.backend.session : simSession
    }

    ShareDialog {
        id: shareDialog
        qrAvailable: root.backend && root.backend.qr === true
    }

    // 曲目菜单与歌单写操作
    TrackMenu {
        id: trackMenu
        player: playback
        loggedIn: root.loggedIn
        onLoginRequested: root.requestLogin()
        onNewPlaylistRequested: track => playlistNameDialog.ask(0, "", track)
        onShareRequested: track => root.shareTrack(track)
        onAddRequested: (playlist, track) => {
            root.noteOnSuccess(playlist.id, "添加歌曲", "已添加到「" + playlist.name + "」");
            Source.addToPlaylist(playlist.id, track.id);
        }
        onRemoveRequested: (playlistId, track) => {
            root.noteOnSuccess(playlistId, "移出歌单", "已从歌单移除「" + track.name + "」");
            Source.removeFromPlaylist(playlistId, track.id);
        }
        onCopied: root.notify("已复制歌曲链接")
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

    // 歌单简介：多行文字，可以清空（空简介也是合法的保存）
    Kirigami.PromptDialog {
        id: playlistDescriptionDialog
        property real playlistId
        function ask(id, description) {
            playlistDescriptionDialog.playlistId = id;
            descriptionArea.text = description;
            playlistDescriptionDialog.open();
            descriptionArea.forceActiveFocus();
            descriptionArea.cursorPosition = descriptionArea.length;
        }
        title: "编辑歌单简介"
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: "保存"
                icon.name: "dialog-ok"
                onTriggered: playlistDescriptionDialog.accept()
            },
            Kirigami.Action {
                text: "取消"
                icon.name: "dialog-cancel"
                onTriggered: playlistDescriptionDialog.reject()
            }
        ]
        onAccepted: {
            root.noteOnSuccess(playlistDescriptionDialog.playlistId, "更新歌单描述", "已更新歌单简介");
            Source.setPlaylistDescription(playlistDescriptionDialog.playlistId, descriptionArea.text.trim());
        }
        TextArea {
            id: descriptionArea
            implicitWidth: Tokens.trackColumn + Tokens.albumColumn
            implicitHeight: Tokens.tileLg
            placeholderText: "介绍一下这个歌单"
            wrapMode: TextEdit.Wrap
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
                icon.source: Tokens.logoutIcon
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
                root.notify("已创建歌单「" + name + "」");
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
            root.notify(queued[0].text);
        }
        function onFollowFailed(action, message) {
            root.fail(action + "失败：" + message);
        }
        function onCommentLikeFailed(songId, commentId, message) {
            root.fail("评论点赞失败：" + message);
        }
        function onRequestRestricted(key, message) {
            Source.markRequestRestricted(key, message);
        }
        function onRequestFailed(key, message) {
            // 逐字歌词只是增强：失败不提示，等下次换歌再试。
            if (key.startsWith("lyricsNew:"))
                return;
            Source.markRequestFailed(key, message);
            // 页面数据失败由各页自己的占位/重试说明（离线状态另在 ScreenFrame 顶部）；
            // 这里不再把被动加载失败弹成全局「网络不可用」。
            console.warn(logUi, "request failed:", key.split(":")[0], message);
        }
        function onPlaylistActionFailed(action, message) {
            console.warn(logUi, "playlist action failed:", action, message);
            // 只撤掉这次失败的那条；新建失败时才放弃「建好后要加入的歌」
            root.takeNote(note => note.action === action);
            if (action === "创建歌单")
                root.trackForNewPlaylist = null;
            root.fail(message);
        }
    }

    function openOverlay() {
        if (root.startOverlay === "flight") {
            // 截图：像点播放条封面那样打开正在播放页，记下转场两端的位置，最终帧应当是封面已落地显示
            // 等初始属性（page 等）都设完再触发，否则 page 的初始值会把刚打开的正在播放页切回去
            Qt.callLater(() => {
                root.toggleNowPlaying();
                flightProbe.start();
            });
        } else if (root.startOverlay === "trackmenu") {
            // 截图：在列表第一行右侧打开曲目菜单（与点行尾「更多」的位置相同）
            trackMenu.track = root.openList.tracks[0];
            trackMenu.context = {};
            trackMenu.popup(root.width - Tokens.pageMargin - trackMenu.implicitWidth, Tokens.height12 * 3);
        } else if (root.startOverlay === "queue")
            queuePanel.open();
        else if (root.startOverlay === "equalizer")
            playerBar.openEqualizer();
        else if (root.startOverlay === "quality")
            Qt.callLater(() => playerBar.openQualityMenu());
        else if (root.startOverlay === "fullscreen")
            root.fullscreenShown = true;
        else if (root.startOverlay === "share")
            Qt.callLater(() => root.shareTrack(root.currentTrack || root.openList.tracks[0]));
        else if (root.startOverlay === "reorder")
            Qt.callLater(() => (root.pageStack.currentItem as PlaylistsPage).reordering = true)
        else if (root.startOverlay === "suggest")
            Qt.callLater(() => (root.pageStack.currentItem as SearchPage).showSuggestions())
        else if (root.startOverlay === "describe")
            Qt.callLater(() => playlistDescriptionDialog.ask(root.openList.listId || 0, root.openList.description || ""));
        else if (root.startOverlay === "login") {
            loginDialog.open();
            simSession.state = root.loginState;
        }
    }

    // 导航：推荐/我的/搜索 是顶层枢轴；排行榜、列表、歌手、正在播放压在其上，history 记录来路
    property var history: []
    readonly property int maxHistory: 64
    readonly property var pageNames: ({ home: "推荐", mine: "我的", search: "搜索", charts: "发现", playlists: "全部歌单",
                                        saved: "全部收藏", nowplaying: "正在播放", mv: "MV", user: "用户", inbox: "消息" })
    // 返回键上的字：离开那一页时记下的标题（见 open）；记下时还没取到名字就用页的类别名，永远不为空
    readonly property string parentName: root.history.length > 0
        ? root.history[root.history.length - 1].title || root.pageNames[root.history[root.history.length - 1].page] || "推荐"
        : "推荐"

    // 深处组件经 Actions 发来的导航请求：歌手页、专辑页（列表页的一种）、任意歌单（含榜单）
    Connections {
        target: Actions
        function onPlayRequested(tracks, index) { root.play(tracks, index); }
        function onInboxRequested() { root.open("inbox"); }
        function onAboutRequested() {
            aboutLoader.active = true;
            const window = aboutLoader.item as AboutWindow;
            window.show();
            window.raise();
            window.requestActivate();
        }
        function onArtistRequested(id) { root.open("artist", "artist:" + id); }
        function onAlbumRequested(id) { root.open("list", "album:" + id); }
        function onRadioRequested(id) { root.open("list", "radio:" + id); }
        function onMvRequested(id) {
            playback.engine.playing = false;   // MV 自带音轨：不要和主播放器同时出声
            root.open("mv", "", { mvId: id });
        }
        function onUserRequested(id) {
            root.open("user", "", { userId: id });
        }
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
        function onLikeToggleRequested(track) {
            if (!root.loggedIn) {
                root.notify("红心会同步到你的网易云账号，登录后可用", "long", "扫码登录", () => root.requestLogin());
                return;
            }
            Source.setLiked(track.id, !Source.isLiked(track.id));
        }
    }

    // 歌单磁贴的菜单（「我的」「全部歌单」「搜索」）：自己建的可改名/删除（「我喜欢的音乐」除外），
    // 别人的可收藏/取消收藏。归属与收藏状态按 id 从「我的歌单」里查，调用方只给 { id, name }
    Menu {
        id: playlistMenu
        property var playlist: ({ id: 0, name: "" })
        readonly property var mine: Source.myPlaylists.find(pl => pl.id === playlistMenu.playlist.id)
        readonly property bool owned: playlistMenu.mine !== undefined && playlistMenu.mine.owned
        readonly property bool subscribed: playlistMenu.mine !== undefined && !playlistMenu.mine.owned
        readonly property bool editable: playlistMenu.owned && playlistMenu.playlist.id !== (Source.pe.liked.id || 0)
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
            visible: !playlistMenu.owned
            height: visible ? implicitHeight : 0
            icon.name: playlistMenu.subscribed ? "bookmark-remove" : "bookmark-new"
            text: playlistMenu.subscribed ? "取消收藏" : "收藏歌单"
            onTriggered: {
                if (!root.loggedIn) {
                    root.requestLogin();
                    return;
                }
                const subscribe = !playlistMenu.subscribed;
                root.noteOnSuccess(playlistMenu.playlist.id, subscribe ? "收藏歌单" : "取消收藏歌单",
                                   (subscribe ? "已收藏「" : "已取消收藏「") + playlistMenu.playlist.name + "」");
                Source.setPlaylistSubscribed(playlistMenu.playlist.id, subscribe);
            }
        }
    }

    // 打开一页：先把「现在在哪」连同它此刻的标题记进来路，再换成新的页与参数（ids：{ mvId, userId }）。
    // 标题存快照：返回键上直接显示，不再按 id 去取数据（单槽数据会被来回挤掉，取数会互相触发、标题会变空）。
    // 已经在这一页（同一个列表 / 歌手 / 用户 / MV）就什么都不做，来路里不会出现重复的一格
    // 一条路由 = 页面 + 该页的参数。页面相同但参数不同（例如换一个用户）也是一次新跳转。
    function sameRoute(page, listKey, mvId, userId) {
        return page === root.page && listKey === root.listKey
            && mvId === root.mvId && userId === root.userId;
    }
    function open(page, listKey, ids) {
        const next = { listKey: listKey || root.listKey, mvId: root.mvId, userId: root.userId };
        Object.assign(next, ids || {});
        if (root.sameRoute(page, next.listKey, next.mvId, next.userId))
            return;
        const view = root.saveCurrentView();
        let h = root.history.concat([{ page: root.page, listKey: root.listKey, mvId: root.mvId,
                                       userId: root.userId, title: root.currentTitle(), view: view }]);
        // 来路是历史，不是无限日志；连续循环也只能保留最近 maxHistory 层。
        if (h.length > root.maxHistory)
            h = h.slice(h.length - root.maxHistory);
        root.history = h;
        root.listKey = next.listKey;
        root.mvId = next.mvId;
        root.userId = next.userId;
        root.page = page;
    }
    function goBack() {
        // 没有来路的子页（例如启动时直接打开）：返回键写着「推荐」，就回推荐，任何时候都回得去
        if (root.history.length === 0) {
            if (root.page !== "home" && root.page !== "mine" && root.page !== "search")
                root.switchPivot("home");
            return;
        }
        const prev = root.history[root.history.length - 1];
        root.history = root.history.slice(0, -1);
        root.listKey = prev.listKey;
        root.mvId = prev.mvId || 0;
        root.userId = prev.userId || 0;
        root.page = prev.page;
        root.restoreCurrentView(prev.view);
    }
    // 返回时回到离开时的样子（枢轴、搜索词、滚动位置）：页面每次导航都会重建，状态记在来路里。
    // 顶层枢轴（推荐 / 我的 / 搜索）切换会清空来路，另记在 pivotViews 里
    property var pivotViews: ({})
    readonly property var pivotKeys: ["home", "mine", "search"]
    function saveCurrentView() {
        const item = pageStack.currentItem as ScreenFrame;
        const view = item ? item.saveView() : null;
        if (view && root.pivotKeys.indexOf(root.page) >= 0) {
            const views = Object.assign({}, root.pivotViews);
            views[root.page] = view;
            root.pivotViews = views;
        }
        return view;
    }
    function restoreCurrentView(view) {
        const item = pageStack.currentItem as ScreenFrame;
        if (item)
            item.restoreView(view);
    }
    function currentTitle() {
        const item = pageStack.currentItem as ScreenFrame;
        return item && item.heading ? item.heading : (root.pageNames[root.page] || "推荐");
    }
    function switchPivot(key) {
        if (root.page === key && root.history.length === 0)
            return;
        root.saveCurrentView();
        root.history = [];
        root.page = key;
        root.restoreCurrentView(root.pivotViews[key]);
    }
    // 打开 / 关闭正在播放页都带共享封面转场：封面在播放条与正在播放页之间飞（只在有曲目时）
    function sceneRect(item) {
        if (!item || !item.visible)
            return { x: 0, y: 0, width: 0, height: 0 };
        const p = item.mapToItem(null, 0, 0);
        return { x: p.x, y: p.y, width: item.width, height: item.height };
    }
    function toggleNowPlaying() {
        const cover = root.currentTrack ? Source.sized(root.currentTrack.cover, 560) : "";
        if (root.page === "nowplaying") {
            const view = root.nowPlayingView();
            const from = root.sceneRect(view ? view.coverItem : null);
            root.goBack();
            coverFlight.fly(cover, from, root.sceneRect(playerBar.coverItem), null);
            return;
        }
        const from = root.sceneRect(playerBar.coverItem);
        root.open("nowplaying");
        // 等页面封面排好版（推入后要过几帧才有位置）再起飞；每次都取当前显示的页面（页面可能被重建），
        // 10 帧还没位置就不飞，封面直接显示
        flightWait.source = cover;
        flightWait.from = from;
        flightWait.tries = 0;
        flightWait.restart();
    }
    // 当前歌曲的评论：没在正在播放页就先过去（同样的封面转场），再切到「评论」
    function openComments() {
        if (!root.currentTrack)
            return;
        if (root.page !== "nowplaying")
            root.toggleNowPlaying();
        const page = root.pageStack.currentItem as NowPlayingPage;
        if (page)
            page.pane = "comments";
    }
    function nowPlayingView(): NowPlayingPage {
        const page = root.pageStack.currentItem as NowPlayingPage;
        return root.page === "nowplaying" && page && page.coverItem ? page : null;
    }
    // 截图 harness（startOverlay=flight）：转场结束后记下两端位置与封面是否已显示
    Timer {
        id: flightProbe
        interval: 2000
        onTriggered: {
            const page = root.nowPlayingView();
            console.warn(logUi, "cover flight from", JSON.stringify(root.sceneRect(playerBar.coverItem)),
                         "to", JSON.stringify(root.sceneRect(page ? page.coverItem : null)),
                         "cover shown:", page !== null && !page.coverHidden,
                         "tries:", flightWait.tries);
        }
    }
    Timer {
        id: flightWait
        property string source
        property var from
        property int tries
        interval: 16
        repeat: true
        onTriggered: {
            const page = root.nowPlayingView();
            if (!page) {
                flightWait.stop();
                return;
            }
            page.coverHidden = true;
            const to = root.sceneRect(page.coverItem);
            if (to.width > 0 || ++flightWait.tries >= 10) {
                flightWait.stop();
                coverFlight.fly(flightWait.source, flightWait.from, to, () => page.coverHidden = false);
            }
        }
    }
    // 桌面歌词与迷你播放器：独立的顶层窗口，状态（显示与否）由主窗口持有
    property bool desktopLyricsShown: false

    // 看 MV 时屏幕常亮（放歌时不睡眠由 C++ 直接跟着 AudioEngine 设，屏幕照常熄灭）
    property bool videoPlaying: false
    Binding {
        target: root.backend ? root.backend.power : null
        property: "keepScreenOn"
        value: root.videoPlaying
        when: target !== null
    }
    property bool fullscreenShown: false
    DesktopLyrics {
        visible: root.desktopLyricsShown
        track: root.currentTrack
        elapsed: playback.engine.position
        playing: playback.engine.playing
        persist: root.backend !== null
        layerShell: root.backend && root.backend.layerShell ? root.backend.layerShell : null
        onCloseRequested: root.desktopLyricsShown = false
    }
    FullscreenCover {
        id: fullscreenCover
        parent: Overlay.overlay
        anchors.fill: parent
        visible: root.fullscreenShown
        track: root.currentTrack
        playing: playback.engine.playing
        elapsed: playback.engine.position
        onCloseRequested: root.fullscreenShown = false
        onToggleRequested: playback.engine.toggle()
        onPreviousRequested: playback.previous()
        onNextRequested: playback.next()
    }
    MiniPlayer {
        id: miniPlayer
        track: root.currentTrack
        playing: playback.engine.playing
        elapsed: playback.engine.position
        volume: playback.engine.volume
        liked: root.currentTrack !== null && Source.isLiked(root.currentTrack.id)
        onVolumeRequested: v => {
            playback.engine.volume = v;
            playback.engine.muted = false;
        }
        onPlayPauseRequested: playback.engine.toggle()
        onPreviousRequested: playback.previous()
        onNextRequested: playback.next()
        onLikeToggled: Actions.toggleLike(root.currentTrack)
        onExpandRequested: root.leaveMini()
    }
    function enterMini() {
        miniPlayer.show();
        root.hide();
    }
    function leaveMini() {
        miniPlayer.hide();
        root.show();
        root.raise();
        root.requestActivate();
    }

    CoverFlight {
        id: coverFlight
        parent: Overlay.overlay
        anchors.fill: parent
    }
    function shareTrack(track) {
        if (!track)
            return;
        shareDialog.shareTitle = track.name;
        shareDialog.shareSubtitle = track.artist;
        shareDialog.shareUrl = Source.shareUrl("song", track.id);
        shareDialog.shareText = "分享歌曲" + Source.quoted(track.name) + " - " + track.artist;
        shareDialog.cover = track.cover;
        shareDialog.trackId = track.id;
        shareDialog.open();
    }
    function shareList(kind, id, title, cover) {
        if (!id)
            return;
        shareDialog.shareTitle = title;
        shareDialog.shareSubtitle = kind === "album" ? "专辑" : "歌单";
        shareDialog.shareUrl = Source.shareUrl(kind, id);
        shareDialog.shareText = "分享" + shareDialog.shareSubtitle + Source.quoted(title);
        shareDialog.cover = cover;
        shareDialog.trackId = 0;
        shareDialog.open();
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
    Shortcut { sequences: [StandardKey.Find]; onActivated: root.switchPivot("search") }
    Shortcut {
        sequences: ["Esc", StandardKey.Back]
        onActivated: {
            if (root.fullscreenShown)
                root.fullscreenShown = false;
            else
                root.goBack();
        }
    }
    Shortcut { sequence: "Ctrl+L"; onActivated: root.toggleNowPlaying() }
    Shortcut { sequence: "F11"; onActivated: root.fullscreenShown = !root.fullscreenShown }
    TapHandler {
        acceptedButtons: Qt.BackButton
        onTapped: root.goBack()
    }

    // 「我的」未登录时是登录引导；搜索词由 harness 的 searchQuery 预填（空 = 热门搜索）
    property string searchQuery: ""
    readonly property var pages: ({ home: homePage, mine: root.loggedIn ? minePage : placeholderPage, search: searchPage,
                                    charts: chartsPage, list: listPage, artist: artistPage, playlists: playlistsPage,
                                    saved: savedPage, nowplaying: nowPlayingPage, mv: mvPage, user: userPage, inbox: inboxPage })
    onPageChanged: {
        // 路由参数作为页面的初始属性交给页面实例：返回 / 换页时不再把 root 上的
        // 临时 0 绑进仍然存活的页面（曾经因此请求过 /user/detail/0）。
        const params = {};
        if (root.page === "list")
            params.listKey = root.listKey;
        else if (root.page === "artist")
            params.artistId = Number(root.listKey.slice(7));
        else if (root.page === "user")
            params.userId = root.userId;
        else if (root.page === "mv")
            params.mvId = root.mvId;
        else if (root.page === "saved")
            params.kind = root.listKey;   // artists | albums | mvs
        pageStack.clear();
        pageStack.push(root.pages[root.page], params);
    }
    // 「我的」在登录前后是两个不同的页（引导 / 内容）：登录状态一变就换掉，不必切走再切回来
    onLoggedInChanged: if (root.page === "mine") root.pageChanged()
    Component.onCompleted: {
        if (root.backend && root.backend.clipboard)
            Clipboard.live = root.backend.clipboard;
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
        const deferred = {};
        const apply = (key, value) => {
            const kind = key in root ? typeof root[key] : "";
            try {
                if (kind === "string")
                    root[key] = value;
                else if (kind === "boolean")
                    root[key] = value === "true";
                else if (kind === "number" && !isNaN(Number(value)))
                    root[key] = Number(value);
                else
                    console.warn(logUi, "ignored command-line argument:", key);
            } catch (e) {
                console.warn(logUi, "cannot set", key, "from command line:", e.message);
            }
        };
        for (const pair of args.slice(start + 1)) {
            const eq = pair.indexOf("=");
            if (eq <= 0) {
                console.warn(logUi, "ignored command-line argument:", pair);
                continue;
            }
            const key = pair.slice(0, eq);
            const value = pair.slice(eq + 1);
            // page 触发的 pageChanged 会立即建页；路由参数必须先就位，否则页面拿到 0。
            if (key === "page" || key === "startOverlay")
                deferred[key] = value;
            else
                apply(key, value);
        }
        if ("page" in deferred)
            apply("page", deferred.page);
        if ("startOverlay" in deferred)
            apply("startOverlay", deferred.startOverlay);
    }

    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false
    background: Rectangle { color: Kirigami.Theme.backgroundColor }

    pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.None
    pageStack.columnView.columnResizeMode: Kirigami.ColumnView.SingleColumn

    footer: PlayerBar {
        id: playerBar
        // MV 页有自己的画面控制，收起播放条把高度让给画面
        visible: root.page !== "mv"
        together: root.listenRoom.inRoom
        togetherText: root.togetherText
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
        quality: playback.engine.quality
        audio: playback.engine
        onQualityRequested: q => playback.engine.quality = q
        desktopLyricsShown: root.desktopLyricsShown
        onDesktopLyricsToggled: root.desktopLyricsShown = !root.desktopLyricsShown
        onMiniRequested: root.enterMini()
        onShareRequested: root.shareTrack(root.currentTrack)
        onCommentsRequested: root.openComments()
        onFullscreenRequested: root.fullscreenShown = true
        liked: root.currentTrack !== null && Source.isLiked(root.currentTrack.id)
        nowPlayingShown: root.page === "nowplaying"
        onPlayPauseRequested: playback.engine.toggle()
        onNextRequested: playback.next()
        onPreviousRequested: playback.previous()
        onModeRequested: m => playback.mode = m
        onSeekRequested: seconds => playback.engine.seek(seconds)
        onLikeToggled: Actions.toggleLike(root.currentTrack)
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
            listKey: ""
            list: Source.list(listKey)
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
            onDescribeRequested: (id, description) => playlistDescriptionDialog.ask(id, description)
            onDeleteRequested: (id, name) => deleteDialog.ask(id, name)
            onSubscribeToggled: (id, name, subscribed) => {
                root.noteOnSuccess(id, subscribed ? "收藏歌单" : "取消收藏歌单", (subscribed ? "已收藏「" : "已取消收藏「") + name + "」");
                Source.setPlaylistSubscribed(id, subscribed);
            }
            onShareRequested: (kind, id, title, cover) => root.shareList(kind, id, title, cover)
            onAlbumSubscriptionToggled: (id, subscribed) => {
                if (!root.loggedIn) {
                    root.notify("收藏专辑需要登录", "long", "扫码登录", () => root.requestLogin());
                    return;
                }
                Source.setAlbumSubscribed(id, subscribed);
            }
        }
    }

    Component {
        id: artistPage
        ArtistPage {
            id: artistView
            artistId: 0
            pane: root.artistPane
            loggedIn: root.loggedIn
            vip: root.vip
            parentText: root.parentName
            currentId: root.currentTrack ? root.currentTrack.id : 0
            onActivated: index => root.play(artistView.songs, index)
            onPlayAllRequested: root.play(artistView.songs, 0)
            onShuffleRequested: root.play(artistView.songs, Math.floor(Math.random() * artistView.songs.length))
            onLeaveRequested: root.goBack()
            onLoginRequested: root.requestLogin()
            onFollowToggled: (id, followed) => {
                if (!root.loggedIn) {
                    root.notify("关注歌手需要登录", "long", "扫码登录", () => root.requestLogin());
                    return;
                }
                Source.setArtistSubscribed(id, followed);
            }
        }
    }

    Component {
        id: userPage
        UserPage {
            userId: 0
            currentId: root.currentTrack ? root.currentTrack.id : 0
            loggedIn: root.loggedIn
            parentText: root.parentName
            onLeaveRequested: root.goBack()
            onLoginRequested: root.requestLogin()
            onFollowToggled: (id, followed) => {
                if (!root.loggedIn) {
                    root.notify("关注需要登录", "long", "扫码登录", () => root.requestLogin());
                    return;
                }
                Source.setUserFollowed(id, followed);
            }
        }
    }

    Component {
        id: inboxPage
        InboxPage {
            loggedIn: root.loggedIn
            currentId: root.currentTrack ? root.currentTrack.id : 0
            parentText: root.parentName
            onLeaveRequested: root.goBack()
            onLoginRequested: root.requestLogin()
        }
    }

    Component {
        id: mvPage
        MvPage {
            mvId: 0
            loggedIn: root.loggedIn
            parentText: root.parentName
            onLeaveRequested: root.goBack()
            onLoginRequested: root.requestLogin()
            onVideoPlayingChanged: root.videoPlaying = videoPlaying
            Component.onDestruction: root.videoPlaying = false
        }
    }

    Component {
        id: nowPlayingPage
        NowPlayingPage {
            pane: root.nowPlayingPane
            track: root.currentTrack
            elapsed: playback.engine.position
            playing: playback.engine.playing
            loggedIn: root.loggedIn
            parentText: root.parentName
            onLeaveRequested: root.goBack()
            onLoginRequested: root.requestLogin()
            onSeekRequested: seconds => {
                playback.engine.seek(seconds);
                if (!playback.engine.playing)
                    playback.engine.playing = true;
            }
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
            onAllSavedRequested: kind => root.open("saved", kind)
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
        id: savedPage
        SavedPage {
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
            onAllSongsRequested: root.open("list", "searchSongs")
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
