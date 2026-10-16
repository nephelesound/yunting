// 设计语言参照 Rune（Zune/Metro 一脉）：展示字做导航，扁平磁贴，极简播放条。
// 当前阶段只做「底板」：版面、令牌、对齐、密度。整窗一块底色，页面与播放条都没有自己的面，
// 内部不画分隔线；装饰（封面取色、滚动边缘渐隐、悬停动效）留到版面定稿之后。
// 数据：Data.js（匿名快照，入库）+ Personal.js（个人快照，不入库），都由 scripts/snapshot.py 生成。
pragma ComponentBehavior: Bound

import QtQuick
import org.kde.kirigami as Kirigami
import "Data.js" as Data
import "Personal.js" as Personal

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

    readonly property bool loggedIn: root.account !== "anonymous" && Personal.account !== null
    readonly property bool vip: root.loggedIn && Personal.account.vip
    readonly property var pivots: [{ key: "home", text: "推荐" }, { key: "mine", text: "我的" }, { key: "search", text: "搜索" }]

    // ── 列表：首页各入口与榜单共用一种列表页 ──
    function large(url) { return url.replace("120y120", "560y560").replace("300y300", "560y560"); }
    function count(tracks) { return { value: String(tracks.length), label: "首" }; }
    function listFor(key) {
        switch (key) {
        case "daily": return { heading: "每日推荐", cover: large(Personal.daily.tracks[0].cover), tracks: Personal.daily.tracks,
            facts: [{ label: Personal.daily.date }, count(Personal.daily.tracks)], description: "根据你的口味生成，每天 6:00 更新" };
        case "heart": return { heading: "心动模式", cover: large(Personal.heart.seed.cover), tracks: Personal.heart.tracks,
            facts: [count(Personal.heart.tracks)], description: "从红心歌曲「" + Personal.heart.seed.name + "」出发，混合你的红心与相似推荐" };
        case "radar": return { heading: "私人雷达", cover: large(Personal.radar.cover), tracks: Personal.radar.tracks,
            facts: [count(Personal.radar.tracks)], description: Personal.radar.description.split("|")[0] };
        case "similar": return { heading: "相似歌曲", cover: large(Personal.similar.tracks[0].cover), tracks: Personal.similar.tracks,
            facts: [count(Personal.similar.tracks)], description: "和「" + Personal.similar.seed.name + "」（" + Personal.similar.seed.artist + "）相似" };
        case "liked": return { heading: "我喜欢的音乐", cover: large(Personal.liked.tracks[0].cover), tracks: Personal.liked.tracks,
            facts: [{ value: String(Personal.liked.count), label: "首" }], description: "" };
        default: return { heading: Data.playlist.name, cover: Data.playlist.cover, tracks: Data.tracks, ranked: true,
            facts: [{ label: "官方榜" }, { value: String(Data.playlist.count), label: "首" }, { value: (Data.playlist.plays / 1e8).toFixed(1) + " 亿", label: "次播放" }],
            description: Data.playlist.description };
        }
    }
    readonly property var openList: root.listFor(root.listKey)

    // ── 播放状态（原型内模拟，之后由 Player 后端驱动）──
    property var queue: Data.tracks
    property int trackIndex: 2
    property bool playing: true
    property real positionSec: 83
    property bool liked: false

    readonly property var currentTrack: root.queue[root.trackIndex]

    Timer {
        interval: 1000
        repeat: true
        running: root.playing
        onTriggered: root.positionSec = Math.min(root.positionSec + 1, root.currentTrack.dur)
    }

    function canPlay(track) {
        return root.vip || track.fee !== 1;
    }
    function play(tracks, i) {
        if (!root.canPlay(tracks[i])) {
            root.showPassiveNotification("「" + tracks[i].name + "」是 VIP 曲目，登录后可播放", "long", "扫码登录", () => root.requestLogin());
            return;
        }
        root.queue = tracks;
        root.trackIndex = i;
        root.positionSec = 0;
        root.playing = true;
    }
    function step(delta) {
        let i = root.trackIndex;
        do {
            i = (i + delta + root.queue.length) % root.queue.length;
        } while (!root.canPlay(root.queue[i]) && i !== root.trackIndex);
        root.play(root.queue, i);
    }
    function requestLogin() {
        root.showPassiveNotification("扫码登录：待接 NcmClient.loginQr()");
    }

    // ── 导航：推荐/我的/搜索 是顶层枢轴；排行榜、列表、正在播放压在其上，history 记录来路 ──
    property var history: []
    readonly property var pageNames: ({ home: "推荐", mine: "我的", search: "搜索", charts: "排行榜" })
    readonly property string parentName: {
        const prev = root.history.length > 0 ? root.history[root.history.length - 1] : { page: "home" };
        return prev.page === "list" ? root.listFor(prev.listKey).heading : root.pageNames[prev.page] || "推荐";
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
            root.play(Personal.fm.tracks, 0);
            root.open("nowplaying");
        } else if (!root.loggedIn && key !== "similar")
            root.requestLogin();
        else
            root.open("list", key);
    }

    // ── 键盘与鼠标侧键：每个常用动作都不必离开键盘 ──
    Shortcut { sequence: "Space"; onActivated: root.playing = !root.playing }
    Shortcut { sequence: "Left"; onActivated: root.positionSec = Math.max(0, root.positionSec - 5) }
    Shortcut { sequence: "Right"; onActivated: root.positionSec = Math.min(root.currentTrack.dur, root.positionSec + 5) }
    Shortcut { sequence: "Ctrl+Left"; onActivated: root.step(-1) }
    Shortcut { sequence: "Ctrl+Right"; onActivated: root.step(1) }
    Shortcut { sequence: StandardKey.Find; onActivated: root.switchPivot("search") }
    Shortcut { sequences: ["Esc", StandardKey.Back]; onActivated: root.goBack() }
    Shortcut { sequence: "Ctrl+L"; onActivated: root.toggleNowPlaying() }
    TapHandler {
        acceptedButtons: Qt.BackButton
        onTapped: root.goBack()
    }

    readonly property var pages: ({ home: homePage, mine: placeholderPage, search: placeholderPage, charts: chartsPage, list: listPage, nowplaying: nowPlayingPage })
    onPageChanged: {
        pageStack.clear();
        pageStack.push(root.pages[root.page]);
    }
    Component.onCompleted: root.pageChanged()

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
    }

    Component {
        id: homePage
        HomePage {
            pivots: root.pivots
            loggedIn: root.loggedIn
            onPivotActivated: key => root.switchPivot(key)
            onLoginRequested: root.requestLogin()
            onEntryOpened: key => root.openEntry(key)
            onPlaylistOpened: index => root.showPassiveNotification("歌单详情：待接 NcmClient.playlist()")
        }
    }

    Component {
        id: chartsPage
        ChartsPage {
            loggedIn: root.loggedIn
            parentText: root.parentName
            currentId: root.currentTrack.id
            onLeaveRequested: root.goBack()
            onLoginRequested: root.requestLogin()
            onToplistOpened: index => root.open("list", "hot")
            onTrackActivated: index => root.play(Data.tracks, index)
        }
    }

    Component {
        id: listPage
        PlaylistPage {
            list: root.openList
            loggedIn: root.loggedIn
            vip: root.vip
            parentText: root.parentName
            currentId: root.currentTrack.id
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
        id: placeholderPage
        ScreenFrame {
            pivots: root.pivots
            currentPivot: root.page
            loggedIn: root.loggedIn
            onPivotActivated: key => root.switchPivot(key)
            onLoginRequested: root.requestLogin()

            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - 2 * Tokens.pageMargin
                icon.name: root.page === "mine" ? "love-symbolic" : "edit-find"
                text: root.page === "mine" ? (root.loggedIn ? "我的音乐" : "登录后显示你的音乐") : "搜索歌曲、歌手、歌单"
                explanation: root.page === "mine" ? (root.loggedIn ? "待做：红心、最近播放与自建歌单磁贴" : "我喜欢的音乐、每日推荐、云盘和 VIP 曲目都跟随网易云账号。") : "待接 NcmClient.search()"
                helpfulAction: root.page === "mine" && !root.loggedIn ? loginAction : null
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
