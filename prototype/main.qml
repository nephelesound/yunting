// 设计语言参照 Rune（Zune/Metro 一脉）：展示字做导航，扁平磁贴，横向全景，极简播放条。
// 当前阶段只做「底板」：版面、令牌、对齐、密度。整窗一块底色，页面与播放条都没有自己的面，
// 内部不画分隔线；装饰（封面取色光晕、滚动边缘渐隐、悬停动效）留到版面定稿之后。
pragma ComponentBehavior: Bound

import QtQuick
import org.kde.kirigami as Kirigami
import "Data.js" as Data

Kirigami.ApplicationWindow {
    id: root

    visible: true
    title: root.currentTrack ? root.currentTrack.name + " — " + root.currentTrack.artist : "NCM Player"
    width: 1280
    height: 800
    minimumWidth: 800
    minimumHeight: 560

    // 截图 harness 可从命令行覆盖：discover | playlist | mine | search | nowplaying
    property string page: "discover"
    property string account: "anonymous"

    readonly property bool loggedIn: root.account !== "anonymous"
    readonly property var pivots: [{ key: "discover", text: "发现" }, { key: "mine", text: "我的" }, { key: "search", text: "搜索" }]

    // ── 播放状态（原型内模拟，之后由 Player 后端驱动）──
    property int trackIndex: 2
    property bool playing: true
    property real positionSec: 83
    property bool liked: false

    readonly property var currentTrack: Data.tracks[root.trackIndex]

    Timer {
        interval: 1000
        repeat: true
        running: root.playing
        onTriggered: root.positionSec = Math.min(root.positionSec + 1, root.currentTrack.dur)
    }

    function canPlay(track) {
        return root.loggedIn || track.fee !== 1;
    }
    function playTrack(i) {
        if (!root.canPlay(Data.tracks[i])) {
            root.showPassiveNotification("「" + Data.tracks[i].name + "」是 VIP 曲目，登录后可播放", "long", "扫码登录", () => root.requestLogin());
            return;
        }
        root.trackIndex = i;
        root.positionSec = 0;
        root.playing = true;
    }
    function step(delta) {
        let i = root.trackIndex;
        do {
            i = (i + delta + Data.tracks.length) % Data.tracks.length;
        } while (!root.canPlay(Data.tracks[i]) && i !== root.trackIndex);
        root.playTrack(i);
    }
    function requestLogin() {
        root.showPassiveNotification("扫码登录：待接 NcmClient.loginQr()");
    }

    // 页面层级：发现/我的/搜索 是顶层枢轴；歌单与正在播放压在其上，返回即回到发现
    property string previousPage: "discover"
    onPageChanged: {
        pageStack.clear();
        const top = root.page === "mine" || root.page === "search" ? placeholderPage : discoverPage;
        pageStack.push(top);
        if (root.page === "playlist")
            pageStack.push(playlistPage);
        if (root.page === "nowplaying")
            pageStack.push(nowPlayingPage);
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
        onNowPlayingToggled: {
            if (root.page === "nowplaying") {
                root.page = root.previousPage;
            } else {
                root.previousPage = root.page;
                root.page = "nowplaying";
            }
        }
    }

    Component {
        id: discoverPage
        DiscoverPage {
            pivots: root.pivots
            loggedIn: root.loggedIn
            currentIndex: root.trackIndex
            onPivotActivated: key => root.page = key
            onLoginRequested: root.requestLogin()
            onToplistOpened: index => root.page = "playlist"
            onTrackActivated: index => root.playTrack(index)
        }
    }

    Component {
        id: playlistPage
        PlaylistPage {
            currentIndex: root.trackIndex
            loggedIn: root.loggedIn
            onActivated: index => root.playTrack(index)
            onPlayAllRequested: root.playTrack(0)
            onShuffleRequested: root.step(1 + Math.floor(Math.random() * (Data.tracks.length - 1)))
            onLeaveRequested: root.page = "discover"
            onLoginRequested: root.requestLogin()
        }
    }

    Component {
        id: nowPlayingPage
        NowPlayingPage {
            track: root.currentTrack
            elapsed: root.positionSec
            loggedIn: root.loggedIn
            onLeaveRequested: root.page = root.previousPage === "nowplaying" ? "discover" : root.previousPage
            onLoginRequested: root.requestLogin()
        }
    }

    Component {
        id: placeholderPage
        ScreenFrame {
            pivots: root.pivots
            currentPivot: root.page
            loggedIn: root.loggedIn
            onPivotActivated: key => root.page = key
            onLoginRequested: root.requestLogin()

            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - 2 * Tokens.pageMargin
                icon.name: root.page === "mine" ? "love-symbolic" : "edit-find"
                text: root.page === "mine" ? "登录后显示你的音乐" : "搜索歌曲、歌手、歌单"
                explanation: root.page === "mine" ? "我喜欢的音乐、每日推荐、云盘和 VIP 曲目都跟随网易云账号。" : "待接 NcmClient.search()"
                helpfulAction: root.page === "mine" ? loginAction : null
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
