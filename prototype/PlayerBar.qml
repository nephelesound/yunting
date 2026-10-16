// 播放条（HUD）：顶边是一条通栏进度线，既显示/拖动进度，也是内容区与播放条的分界（网易云同做法）。
// 线下 80 高（48 封面 + 上下 16），三段：
//   左＝封面 · 曲名/歌手 · 喜欢（作用于这首歌，所以紧挨曲名）
//   中＝播放模式 · 上一首 · 播放（40 圆，强调色实底，是这块区域唯一的主动作）· 下一首 · 播放队列，对称
//      播放模式是一个按钮循环切换 列表循环 → 单曲循环 → 随机（三者互斥，不做成两个各自开关的按钮）
//   右＝已播/总长 · 歌词 · 音量
// 左右两端挂在页边线上：封面左缘 = pageMargin，最后一个图标的图形右缘 = 窗宽 − pageMargin。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Format.js" as Format

Item {
    id: bar

    property var track
    property bool playing
    property real elapsed
    property bool liked
    property bool nowPlayingShown
    property int mode: 0 // 0 列表循环 · 1 单曲循环 · 2 随机

    readonly property var modes: [
        { icon: "media-playlist-repeat-symbolic", text: "列表循环" },
        { icon: "media-playlist-repeat-song-symbolic", text: "单曲循环" },
        { icon: "media-playlist-shuffle-symbolic", text: "随机播放" },
    ]

    signal playPauseRequested()
    signal nextRequested()
    signal previousRequested()
    signal seekRequested(real seconds)
    signal likeToggled()
    signal nowPlayingToggled()
    signal queueRequested()

    // 没有曲目或时长未知时进度为 0、不能拖动，传输控件整体禁用
    readonly property real duration: bar.track ? bar.track.dur : 0
    readonly property bool active: !!bar.track

    implicitHeight: Tokens.lineStrong + Tokens.hudHeight

    // ── 顶边：通栏进度线 ──
    Rectangle {
        id: progressTrack
        anchors.top: parent.top
        width: parent.width
        height: Tokens.lineStrong
        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.track)

        Rectangle {
            width: bar.duration > 0 ? parent.width * Math.min(1, bar.elapsed / bar.duration) : 0
            height: parent.height
            color: Kirigami.Theme.highlightColor
        }
    }
    MouseArea {
        // 命中区比线粗：线上下各扩 space2
        enabled: bar.duration > 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: progressTrack.verticalCenter
        height: Tokens.space4
        cursorShape: Qt.PointingHandCursor
        function seekTo(x) { bar.seekRequested(bar.duration * Math.max(0, Math.min(1, x / width))); }
        onPressed: mouse => seekTo(mouse.x)
        onPositionChanged: mouse => { if (pressed) seekTo(mouse.x); }
    }

    Item {
        anchors.top: progressTrack.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Tokens.pageMargin
        anchors.rightMargin: Tokens.pageMargin

        // ── 左：当前曲目 ──
        RowLayout {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(Tokens.trackColumn, transport.x - Tokens.space6)
            spacing: Tokens.space3

            AbstractButton {
                Layout.preferredWidth: Tokens.height12
                Layout.preferredHeight: Tokens.height12
                enabled: bar.active
                hoverEnabled: true
                onClicked: bar.nowPlayingToggled()
                contentItem: Cover {
                    source: bar.track ? bar.track.cover : ""
                    pixelSize: 96
                }
                ToolTip.text: "正在播放与歌词"
                ToolTip.visible: hovered
                ToolTip.delay: 600
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    text: bar.active ? bar.track.name : "未在播放"
                    font.pixelSize: Tokens.textBase
                    font.weight: bar.active ? Font.Medium : Font.Normal
                    color: bar.active ? Kirigami.Theme.textColor : Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    visible: bar.active
                    text: bar.active ? bar.track.artist : ""
                    font.pixelSize: Tokens.textSm
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    elide: Text.ElideRight
                }
            }
            IconButton {
                enabled: bar.active
                icon.name: bar.liked ? "love-amarok" : "love-symbolic"
                text: bar.liked ? "取消喜欢" : "喜欢"
                onClicked: bar.likeToggled()
            }
        }

        // ── 中：传输控制（对称）──
        RowLayout {
            id: transport
            enabled: bar.active
            anchors.centerIn: parent
            spacing: Tokens.space4

            IconButton {
                icon.name: bar.modes[bar.mode].icon
                text: bar.modes[bar.mode].text
                onClicked: bar.mode = (bar.mode + 1) % bar.modes.length
            }
            IconButton { icon.name: "media-skip-backward"; text: "上一首"; onClicked: bar.previousRequested() }
            RoundButton {
                id: play
                implicitWidth: Tokens.height10
                implicitHeight: Tokens.height10
                // 按下反馈：缩到 0.95（只在指针按下时；空格快捷键不经过这里，不做动画）
                scale: play.down ? 0.95 : 1
                Behavior on scale { NumberAnimation { duration: Tokens.motionFast; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut } }
                text: bar.active && bar.playing ? "暂停" : "播放"
                display: AbstractButton.IconOnly
                icon.name: bar.active && bar.playing ? "media-playback-pause" : "media-playback-start"
                icon.width: Tokens.iconMd
                icon.height: Tokens.iconMd
                icon.color: Kirigami.Theme.highlightedTextColor
                opacity: enabled ? 1 : Tokens.fadedHover
                hoverEnabled: true
                onClicked: bar.playPauseRequested()
                background: Rectangle {
                    radius: height / 2
                    color: play.down ? Qt.darker(Kirigami.Theme.highlightColor, 1.15)
                         : play.hovered ? Qt.lighter(Kirigami.Theme.highlightColor, 1.08)
                         : Kirigami.Theme.highlightColor
                }
                ToolTip.text: text
                ToolTip.visible: hovered
                ToolTip.delay: 600
            }
            IconButton { icon.name: "media-skip-forward"; text: "下一首"; onClicked: bar.nextRequested() }
            IconButton { icon.name: "view-media-playlist"; text: "播放队列"; onClicked: bar.queueRequested() }
        }

        // ── 右：时间与视图 ──
        RowLayout {
            anchors.right: parent.right
            anchors.rightMargin: -Tokens.space2 // 图形右缘落在页边线上
            anchors.verticalCenter: parent.verticalCenter
            spacing: Tokens.space1

            Label {
                Layout.rightMargin: Tokens.space3
                visible: bar.duration > 0
                text: Format.elapsed(bar.elapsed) + " / " + Format.duration(bar.duration)
                font.pixelSize: Tokens.textSm
                font.features: { "tnum": 1 }
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            }
            IconButton {
                icon.name: "view-media-lyrics"
                text: "正在播放与歌词"
                checkable: true
                checked: bar.nowPlayingShown
                onClicked: bar.nowPlayingToggled()
            }
            IconButton { icon.name: "audio-volume-high"; text: "音量" }
        }
    }
}
