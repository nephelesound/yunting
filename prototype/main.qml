// 应用外壳：Kirigami.ApplicationWindow + GlobalDrawer（导航）+ ContextDrawer（播放队列）
//            + 底部播放条 + 一处 InlineMessage 表达真实状态。
// 组合范式参考 Elisa（KDE 官方 QML 音乐播放器）：docs/reference/elisa/src/qml/ElisaMainWindow.qml
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Data.js" as Data

Kirigami.ApplicationWindow {
    id: root

    visible: true
    title: root.currentTrack ? root.currentTrack.name + " — " + root.currentTrack.artist : "NCM Player"
    width: Kirigami.Units.gridUnit * 60
    height: Kirigami.Units.gridUnit * 38
    minimumWidth: Kirigami.Units.gridUnit * 40
    minimumHeight: Kirigami.Units.gridUnit * 28

    // ── 播放状态（当前是原型内的模拟状态机，之后由 Player 后端驱动）──
    property int trackIndex: 0
    property bool playing: true
    property real positionSec: 0
    property string thisToplist: Data.toplists.length > 3 ? Data.toplists[3].name : ""

    readonly property var currentTrack: Data.tracks.length > 0 ? Data.tracks[trackIndex] : null
    readonly property real currentDuration: currentTrack ? currentTrack.dur : 1

    Timer {
        interval: 1000
        repeat: true
        running: root.playing
        onTriggered: root.positionSec = Math.min(root.positionSec + 1, root.currentDuration)
    }

    function selectTrack(i) {
        root.trackIndex = i;
        root.positionSec = 0;
        root.playing = true;
    }
    function nextTrack() { root.selectTrack((root.trackIndex + 1) % Data.tracks.length) }
    function previousTrack() { root.selectTrack((root.trackIndex - 1 + Data.tracks.length) % Data.tracks.length) }

    // ── 导航 ──
    globalDrawer: Kirigami.GlobalDrawer {
        title: "NCM Player"
        titleIcon: "media-optical-audio"

        actions: [
            Kirigami.Action {
                text: "排行榜"
                icon.name: "office-chart-bar"
                checkable: true
                checked: true
            },
            Kirigami.Action {
                text: "每日推荐"
                icon.name: "view-calendar"
                checkable: true
            },
            Kirigami.Action {
                text: "云盘"
                icon.name: "folder-cloud"
                checkable: true
            },
            Kirigami.Action { separator: true },
            Kirigami.Action {
                text: "搜索"
                icon.name: "edit-find"
                onTriggered: root.showPassiveNotification("搜索：待接 NcmClient.search()")
            }
        ]
    }

    // ── 播放队列（右侧抽屉，Elisa 同款位置语义）──
    contextDrawer: Kirigami.ContextDrawer {
        id: queueDrawer
        handleClosedIcon.name: "view-media-playlist"
        handleOpenIcon.name: "window-close"

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Kirigami.Heading {
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.topMargin: Kirigami.Units.largeSpacing
                Layout.bottomMargin: Kirigami.Units.smallSpacing
                level: 4
                text: "播放队列"
            }

            ListView {
                id: queueView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: Data.tracks
                currentIndex: root.trackIndex
                delegate: QueueRow {
                    current: ListView.isCurrentItem
                    onActivated: function (index) { root.selectTrack(index) }
                }
            }
        }
    }

    // ── 主内容 ──
    pageStack.initialPage: Kirigami.Page {
        title: "排行榜"

        actions: [
            Kirigami.Action {
                text: "刷新"
                icon.name: "view-refresh"
                onTriggered: root.showPassiveNotification("已刷新（原型：占位动作）")
            },
            Kirigami.Action {
                text: "播放队列"
                icon.name: "view-media-playlist"
                onTriggered: queueDrawer.drawerOpen = !queueDrawer.drawerOpen
            }
        ]

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // 状态语义：把「VIP 曲拿不到播放地址」这件事实讲清楚，而不是静默失败
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                Layout.margins: Kirigami.Units.largeSpacing
                Layout.bottomMargin: Kirigami.Units.smallSpacing
                visible: true
                type: Kirigami.MessageType.Information
                text: "匿名会话：免费曲可直接取 320kbps 播放地址；VIP 曲接口返回 code=-110，需扫码登录。"
                actions: [
                    Kirigami.Action {
                        text: "扫码登录"
                        onTriggered: root.showPassiveNotification("登录：待接 NcmClient.loginQr()")
                    }
                ]
            }

            // 榜单：少量、需浏览封面 → 横向列表 + 方形封面
            ListView {
                id: toplistView
                Layout.fillWidth: true
                Layout.preferredHeight: Kirigami.Units.gridUnit * 10
                orientation: ListView.Horizontal
                spacing: Kirigami.Units.largeSpacing
                leftMargin: Kirigami.Units.largeSpacing
                rightMargin: Kirigami.Units.largeSpacing
                clip: true
                model: Data.toplists
                delegate: ToplistCard {
                    onActivated: function (index) {
                        root.thisToplist = Data.toplists[index].name;
                        root.showPassiveNotification("切换到：" + Data.toplists[index].name + "（原型：数据未变）");
                    }
                }
            }

            // 歌曲：同质、大量 → 行式列表（原生 delegate 虚拟化）
            ListView {
                id: trackView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: Data.tracks
                delegate: TrackRow {
                    current: index === root.trackIndex
                    playing: root.playing
                    onActivated: function (index) { root.selectTrack(index) }
                }
                header: Kirigami.ListSectionHeader {
                    width: trackView.width
                    text: root.thisToplist + " · " + Data.tracks.length + " 首"
                }
            }
        }
    }

    // ── 底部播放条 ──
    footer: NowPlayingBar {
        height: Kirigami.Units.gridUnit * 3
        trackTitle: root.currentTrack ? root.currentTrack.name : ""
        trackArtist: root.currentTrack ? root.currentTrack.artist : ""
        trackCover: root.currentTrack ? root.currentTrack.cover : ""
        playing: root.playing
        position: root.positionSec
        duration: root.currentDuration
        onPlayPauseRequested: root.playing = !root.playing
        onNextRequested: root.nextTrack()
        onPreviousRequested: root.previousTrack()
        onSeekRequested: function (seconds) { root.positionSec = seconds }
    }
}
