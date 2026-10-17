// 播放条（HUD）：顶边是一条通栏进度线，既显示/拖动进度，也是内容区与播放条的分界（网易云同做法）。
// 线下 80 高（48 封面 + 上下 16），三段：
//   左＝封面 · 曲名/歌手 · 喜欢（作用于这首歌，所以紧挨曲名）
//   中＝播放模式 · 上一首 · 播放（40 圆，强调色实底，是这块区域唯一的主动作）· 下一首 · 播放队列，对称
//      播放模式是一个按钮循环切换 列表循环 → 单曲循环 → 随机（三者互斥，不做成两个各自开关的按钮）
//   右＝已播/总长 · 歌词 · 音量
// 左右两端挂在页边线上：封面左缘 = pageMargin，最后一个图标的图形右缘 = 窗宽 − pageMargin。
pragma ComponentBehavior: Bound

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
    property real volume: 0.7
    property bool muted: false
    property string quality: "auto"   // auto · standard · higher · exhigh · lossless · hires
    property var audio: null          // AudioEngine：音质之外还要取输出设备与 EQ
    // 共享封面转场的一端（另一端是正在播放页的封面）
    readonly property Item coverItem: coverButton

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
    signal modeRequested(int mode)
    signal volumeRequested(real volume)
    signal muteToggled()
    signal qualityRequested(string quality)
    signal desktopLyricsToggled()
    signal miniRequested()
    signal fullscreenRequested()

    property bool desktopLyricsShown

    function openEqualizer() { eqPanel.showAt(bar); }
    function openQualityMenu() { qualityMenu.popup(qualityButton, 0, -qualityMenu.implicitHeight - Tokens.space2); }

    // 没有曲目或时长未知时进度为 0、不能拖动，传输控件整体禁用
    readonly property real duration: bar.track ? bar.track.dur : 0
    readonly property bool active: !!bar.track

    implicitHeight: Tokens.lineStrong + Tokens.hudHeight

    // 顶边：通栏进度线
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
        id: hud
        anchors.top: progressTrack.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Tokens.pageMargin
        anchors.rightMargin: Tokens.pageMargin

        // 左：当前曲目
        RowLayout {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(Tokens.trackColumn, transport.x - Tokens.space6)
            spacing: Tokens.space3

            CoverButton {
                id: coverButton
                enabled: bar.active
                source: bar.track ? bar.track.cover : ""
                hintIcon: bar.nowPlayingShown ? "go-down-symbolic" : "go-up-symbolic"
                hint: bar.nowPlayingShown ? "收起正在播放" : "正在播放与歌词"
                onClicked: bar.nowPlayingToggled()
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                spacing: 0
                // 歌名与封面去同一处（正在播放页）；歌手去歌手页
                LinkText {
                    Layout.fillWidth: true
                    parts: [{ text: bar.active ? bar.track.name : "未在播放", link: bar.active }]
                    font.pixelSize: Tokens.textBase
                    font.weight: bar.active ? Font.Medium : Font.Normal
                    color: bar.active ? Kirigami.Theme.textColor : Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    onActivated: bar.nowPlayingToggled()
                }
                LinkText {
                    Layout.fillWidth: true
                    visible: bar.active
                    parts: Format.artistLinks(bar.track)
                    font.pixelSize: Tokens.textSm
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                }
            }
            IconButton {
                enabled: bar.active
                // 喜欢 = 实心红心（与列表行里的小红心同一个图标与颜色）
                icon.name: bar.liked ? "emblem-favorite-symbolic" : "love-symbolic"
                icon.color: bar.liked ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor
                text: bar.liked ? "取消喜欢" : "喜欢"
                onClicked: bar.likeToggled()
            }
        }

        // 中：传输控制（对称）
        RowLayout {
            id: transport
            enabled: bar.active
            anchors.centerIn: parent
            spacing: Tokens.space4

            IconButton {
                icon.name: bar.modes[bar.mode].icon
                text: bar.modes[bar.mode].text
                onClicked: bar.modeRequested((bar.mode + 1) % bar.modes.length)
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
                // 与 AccentButton 同一套：叠前景色的悬停 / 按下档位；键盘焦点是隔 2px 的外圈
                background: Rectangle {
                    radius: height / 2
                    color: Qt.tint(Kirigami.Theme.highlightColor, Qt.alpha(Kirigami.Theme.highlightedTextColor,
                                   play.down ? Tokens.tintPressed : play.hovered ? Tokens.tintHover : 0))
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -2 * Tokens.lineStrong
                        visible: play.visualFocus
                        radius: height / 2
                        color: "transparent"
                        border.width: Tokens.lineStrong
                        border.color: Kirigami.Theme.highlightColor
                    }
                }
                ToolTip.text: text
                ToolTip.visible: hovered
                ToolTip.delay: 600
            }
            IconButton { icon.name: "media-skip-forward"; text: "下一首"; onClicked: bar.nextRequested() }
            IconButton { icon.name: "view-media-playlist"; text: "播放队列"; onClicked: bar.queueRequested() }
        }

        // 右：时间与视图。放不下时（与中段传输控件之间不足 24）音质、桌面歌词、迷你播放器收进「更多」：
        // 时间、正在播放、音量最常用，始终在外面。按各项的固有宽度判断，与是否显示无关，不会绑定成环
        readonly property real fullRightWidth: qualityButton.implicitWidth + timeLabel.implicitWidth + Tokens.space3
                                               + 4 * Tokens.height8 + 5 * Tokens.space1
        readonly property bool compact: fullRightWidth > (width - transport.width) / 2 - Tokens.space6
        RowLayout {
            anchors.right: parent.right
            anchors.rightMargin: -Tokens.space2 // 图形右缘落在页边线上
            anchors.verticalCenter: parent.verticalCenter
            spacing: Tokens.space1

            // 音质：当前档位的小字按钮，点开是单选菜单（「这首歌怎么在播」与时间放在一起）
            Button {
                id: qualityButton
                visible: !hud.compact
                implicitHeight: Tokens.height8
                flat: true
                text: Format.qualityLevel(bar.quality)
                font.pixelSize: Tokens.textSm
                font.features: { "tnum": 1 }
                Accessible.name: "音质：" + text
                ToolTip.text: "播放音质"
                ToolTip.visible: hovered
                ToolTip.delay: 600
                onClicked: qualityMenu.popup(qualityButton, 0, -qualityMenu.implicitHeight - Tokens.space2)

                QualityMenu {
                    id: qualityMenu
                    quality: bar.quality
                    audio: bar.audio
                    onQualityRequested: q => bar.qualityRequested(q)
                    onEqualizerRequested: eqPanel.showAt(bar)
                }
            }
            Label {
                id: timeLabel
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
            IconButton {
                visible: !hud.compact
                icon.name: "draw-text"
                text: bar.desktopLyricsShown ? "关闭桌面歌词" : "桌面歌词"
                checkable: true
                checked: bar.desktopLyricsShown
                onClicked: bar.desktopLyricsToggled()
            }
            IconButton {
                visible: !hud.compact
                icon.name: "window-minimize-pip"
                text: "迷你播放器"
                onClicked: bar.miniRequested()
            }
            // 窄窗口：收起的三项
            IconButton {
                id: moreButton
                visible: hud.compact
                icon.name: "overflow-menu"
                text: "更多"
                onClicked: moreMenu.popup(moreButton, 0, -moreMenu.implicitHeight - Tokens.space2)
                Menu {
                    id: moreMenu
                    QualityMenu {
                        quality: bar.quality
                        audio: bar.audio
                        onQualityRequested: q => bar.qualityRequested(q)
                        onEqualizerRequested: eqPanel.showAt(bar)
                    }
                    MenuItem {
                        icon.name: "draw-text"
                        text: "桌面歌词"
                        checkable: true
                        checked: bar.desktopLyricsShown
                        onTriggered: bar.desktopLyricsToggled()
                    }
                    MenuItem {
                        icon.name: "window-minimize-pip"
                        text: "迷你播放器"
                        onTriggered: bar.miniRequested()
                    }
                    MenuItem {
                        icon.name: "view-fullscreen"
                        text: "全屏封面"
                        onTriggered: bar.fullscreenRequested()
                    }
                }
            }
            // 音量：点开一个小浮层（滑块 + 静音）；在按钮上滚滚轮直接调，每格 5%
            IconButton {
                id: volumeButton
                icon.name: bar.muted || bar.volume === 0 ? "audio-volume-muted"
                         : bar.volume < 0.34 ? "audio-volume-low"
                         : bar.volume < 0.67 ? "audio-volume-medium" : "audio-volume-high"
                text: bar.muted ? "音量（已静音）" : "音量 " + Math.round(bar.volume * 100) + "%"
                checkable: true
                checked: volumePopup.visible
                onClicked: volumePopup.visible ? volumePopup.close() : volumePopup.open()
                WheelHandler {
                    onWheel: event => bar.volumeRequested(Math.max(0, Math.min(1, bar.volume + (event.angleDelta.y > 0 ? 0.05 : -0.05))))
                }

                Popup {
                    id: volumePopup
                    // 浮在按钮正上方，右缘与按钮右缘对齐；Popup 是真正浮起的一层，用样式自带的面与投影
                    x: volumeButton.width - width
                    y: -height - Tokens.space2
                    padding: Tokens.space2
                    contentItem: RowLayout {
                        spacing: Tokens.space2
                        IconButton {
                            icon.name: bar.muted ? "audio-volume-muted" : "audio-volume-high"
                            text: bar.muted ? "取消静音" : "静音"
                            checkable: true
                            checked: bar.muted
                            onClicked: bar.muteToggled()
                        }
                        Slider {
                            Layout.preferredWidth: Tokens.tileLg
                            from: 0
                            to: 1
                            value: bar.muted ? 0 : bar.volume
                            onMoved: bar.volumeRequested(value)
                        }
                        Label {
                            Layout.preferredWidth: Tokens.height10
                            horizontalAlignment: Text.AlignRight
                            text: bar.muted ? "静音" : Math.round(bar.volume * 100) + "%"
                            font.pixelSize: Tokens.textSm
                            font.features: { "tnum": 1 }
                            color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                        }
                    }
                }
            }
        }
    }

    EqualizerPanel {
        id: eqPanel
        audio: bar.audio
    }
}
