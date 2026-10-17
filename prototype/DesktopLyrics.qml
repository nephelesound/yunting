// 桌面歌词：独立的无边框透明窗口，压在桌面上显示当前句与译文（或下一句）。
// Wayland 下经 backend.layerShell 放到 overlay 层（普通窗口在 Wayland 上做不到置顶），锚定左下角；
// overlay 不能被合成器拖动，拖动时改它离屏幕左边、下边的距离。没有 layer-shell（X11）时是普通置顶窗口，交给系统拖动。
// 当前句随播放从左到右填色：引擎的进度几次一秒，填色走逐帧时钟（与引擎进度对齐），所以是连续的。
// 文字压在未知的壁纸上：描边是为了可读（功能），不是装饰。字体跟随系统（不指定字体族）。
// 悬停时出现半透明底与工具条：字号、填色、第二行、复位、锁定、关闭；锁定后点击穿透，在播放条关掉再打开即解锁。
// 样式与位置只在应用里记住（persist），原型与截图不读不写。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtCore
import org.kde.kirigami as Kirigami

Window {
    id: lyricsWindow

    property var track: null
    property real elapsed
    property bool playing
    property var layerShell: null   // backend.layerShell（可能没有）
    property bool persist: false
    property bool locked: false

    // 样式与位置（persist 时写进配置）
    property int sizeStep: 1         // Tokens.desktopLyricSizes 的下标
    property int colorIndex: -1      // -1 = 系统强调色，否则 Tokens.desktopLyricColors 的下标
    property bool showSecond: true
    property int left: -1            // 离屏幕左边；-1 = 水平居中
    property int bottomGap: 2 * Tokens.height12

    signal closeRequested()

    readonly property var lines: lyricsWindow.track ? Source.lyrics(lyricsWindow.track.id) : []
    // 逐帧时钟：播放时每帧前进，引擎进度到了就对齐（偏差大或暂停时直接跳到引擎的值）
    property real clock: 0
    onElapsedChanged: if (!lyricsWindow.playing || Math.abs(lyricsWindow.elapsed - lyricsWindow.clock) > 0.35) lyricsWindow.clock = lyricsWindow.elapsed
    onPlayingChanged: lyricsWindow.clock = lyricsWindow.elapsed
    FrameAnimation {
        running: lyricsWindow.visible && lyricsWindow.playing && lyricsWindow.current !== null
        onTriggered: lyricsWindow.clock += frameTime
    }

    readonly property int index: Source.lyricIndex(lyricsWindow.lines, lyricsWindow.clock)
    readonly property var current: lyricsWindow.lines.length > 0 ? lyricsWindow.lines[lyricsWindow.index] : null
    readonly property var next: lyricsWindow.index + 1 < lyricsWindow.lines.length ? lyricsWindow.lines[lyricsWindow.index + 1] : null
    // 当前句唱到了哪：本句开始到下一句开始（最后一句到歌曲结束）之间的比例
    readonly property real fill: {
        if (!lyricsWindow.current)
            return 0;
        const end = lyricsWindow.next ? lyricsWindow.next.t
            : Math.max(lyricsWindow.current.t + 1, lyricsWindow.track ? lyricsWindow.track.dur || 0 : 0);
        return Math.max(0, Math.min(1, (lyricsWindow.clock - lyricsWindow.current.t) / (end - lyricsWindow.current.t)));
    }
    // 第二行：有译文显示译文，否则显示下一句
    readonly property string second: !lyricsWindow.showSecond || !lyricsWindow.current ? ""
        : lyricsWindow.current.tr ? lyricsWindow.current.tr : lyricsWindow.next ? lyricsWindow.next.text : ""
    // 默认取系统强调色的色相，饱和度至少 70%、亮度落在 60%–70%：强调色可能很深或很灰（如 MaterialYou 的暗紫），
    // 原样用在深色壁纸上看不清，只提亮又会淡得和未唱的白字分不开
    readonly property color accent: Kirigami.Theme.highlightColor
    readonly property color fillColor: lyricsWindow.colorIndex < 0
        ? Qt.hsla(lyricsWindow.accent.hslHue, Math.max(0.7, lyricsWindow.accent.hslSaturation),
                  Math.max(0.6, Math.min(0.7, lyricsWindow.accent.hslLightness)), 1)
        : Tokens.desktopLyricColors[lyricsWindow.colorIndex]
    readonly property var sizes: Tokens.desktopLyricSizes[lyricsWindow.sizeStep]

    title: "云听 · 桌面歌词"
    width: Math.min(2 * Tokens.trackColumn + Tokens.measureMax, lyricsWindow.screenWidth - 2 * Tokens.space6)   // 至多 960
    height: column.implicitHeight + 2 * Tokens.space2
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
           | (lyricsWindow.locked ? Qt.WindowTransparentForInput : 0)

    readonly property int screenWidth: lyricsWindow.screen ? lyricsWindow.screen.width : 1920
    readonly property int screenHeight: lyricsWindow.screen ? lyricsWindow.screen.height : 1080

    // 放到 (left, bottomGap)，夹在屏幕内
    function place(left, bottomGap) {
        const maxLeft = Math.max(0, lyricsWindow.screenWidth - lyricsWindow.width);
        lyricsWindow.left = Math.round(Math.max(0, Math.min(maxLeft, left)));
        lyricsWindow.bottomGap = Math.round(Math.max(0, Math.min(lyricsWindow.screenHeight - lyricsWindow.height, bottomGap)));
        if (lyricsWindow.layerShell)
            lyricsWindow.layerShell.setOverlayMargins(lyricsWindow, lyricsWindow.left, 0, 0, lyricsWindow.bottomGap);
    }
    // 默认位置：水平居中、离下边两行高
    function placeDefault() {
        lyricsWindow.place((lyricsWindow.screenWidth - lyricsWindow.width) / 2, 2 * Tokens.height12);
    }
    // 存下的位置；还没放过（left 为 -1）时用默认位置
    function placeSaved() {
        if (lyricsWindow.left < 0)
            lyricsWindow.placeDefault();
        else
            lyricsWindow.place(lyricsWindow.left, lyricsWindow.bottomGap);
    }

    onVisibleChanged: {
        if (lyricsWindow.visible && lyricsWindow.layerShell) {
            lyricsWindow.layerShell.makeOverlay(lyricsWindow, 2 | 4 /* bottom | left */, 0, false, "yunting-lyrics");
            lyricsWindow.placeSaved();
        }
        if (!lyricsWindow.visible)
            lyricsWindow.locked = false;
    }
    // 字号变了窗口变高：位置按下边距保持，重新夹一次
    onHeightChanged: if (lyricsWindow.visible) lyricsWindow.placeSaved()

    // 与 SessionState 同一做法：先读存档，读完再开始回写（不让默认值覆盖存档）
    Loader {
        active: lyricsWindow.persist
        sourceComponent: Item {
            id: persisted
            property bool restored: false
            Settings {
                id: saved
                category: "desktopLyrics"
                property int sizeStep: 1
                property int colorIndex: -1
                property bool showSecond: true
                property int left: -1
                property int bottomGap: 2 * Tokens.height12
            }
            Component.onCompleted: {
                lyricsWindow.sizeStep = Math.max(0, Math.min(Tokens.desktopLyricSizes.length - 1, saved.sizeStep));
                lyricsWindow.colorIndex = Math.max(-1, Math.min(Tokens.desktopLyricColors.length - 1, saved.colorIndex));
                lyricsWindow.showSecond = saved.showSecond;
                lyricsWindow.left = saved.left;
                lyricsWindow.bottomGap = saved.bottomGap;
                persisted.restored = true;
            }
            Binding { when: persisted.restored; target: saved; property: "sizeStep"; value: lyricsWindow.sizeStep }
            Binding { when: persisted.restored; target: saved; property: "colorIndex"; value: lyricsWindow.colorIndex }
            Binding { when: persisted.restored; target: saved; property: "showSecond"; value: lyricsWindow.showSecond }
            Binding { when: persisted.restored; target: saved; property: "left"; value: lyricsWindow.left }
            Binding { when: persisted.restored; target: saved; property: "bottomGap"; value: lyricsWindow.bottomGap }
        }
    }

    HoverHandler { id: hover; enabled: !lyricsWindow.locked }
    // 拖动：按下的那一点始终跟着指针。新位置 = 现位置 + 指针相对按下点的位移；
    // 合成器还没应用上一次移动时会多走一步，下一次事件就校正回来（位移是相对当前位置算的）
    DragHandler {
        id: drag
        enabled: !lyricsWindow.locked
        target: null
        property point press
        onActiveChanged: {
            if (!active)
                return;
            if (!lyricsWindow.layerShell) {
                lyricsWindow.startSystemMove();
                return;
            }
            drag.press = drag.centroid.scenePressPosition;
        }
        onCentroidChanged: {
            if (!drag.active || !lyricsWindow.layerShell)
                return;
            const p = drag.centroid.scenePosition;
            lyricsWindow.place(lyricsWindow.left + p.x - drag.press.x, lyricsWindow.bottomGap - (p.y - drag.press.y));
        }
    }

    // 悬停底：告诉用户这块可以拖动、可以操作（浮在桌面上的面，用浮层圆角）
    Rectangle {
        anchors.fill: parent
        radius: Tokens.radiusLg
        color: Qt.alpha("black", Tokens.desktopBacking)
        opacity: hover.hovered || drag.active ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Tokens.motionFast; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut } }
    }

    component LyricText: Label {
        horizontalAlignment: Text.AlignHCenter
        font.weight: Font.Medium
        style: Text.Outline
        styleColor: Qt.alpha("black", Tokens.muted)
        elide: Text.ElideRight
    }

    ColumnLayout {
        id: column
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Tokens.space2
        width: parent.width - 2 * Tokens.space6
        spacing: Tokens.space1

        // 工具条：只在悬停时出现，平时位置留着（窗口高度不随悬停跳动）。底是固定的深色，图标固定用白色
        Row {
            Layout.alignment: Qt.AlignRight
            Layout.rightMargin: -Tokens.space4
            spacing: Tokens.space1
            opacity: hover.hovered ? 1 : 0
            enabled: opacity > 0
            Behavior on opacity { NumberAnimation { duration: Tokens.motionFast; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut } }
            IconButton {
                icon.color: "white"
                icon.name: "format-font-size-less"
                text: "字号缩小"
                enabled: lyricsWindow.sizeStep > 0
                onClicked: lyricsWindow.sizeStep--
            }
            IconButton {
                icon.color: "white"
                icon.name: "format-font-size-more"
                text: "字号放大"
                enabled: lyricsWindow.sizeStep < Tokens.desktopLyricSizes.length - 1
                onClicked: lyricsWindow.sizeStep++
            }
            // 填色：在系统强调色与几种固定颜色之间轮换；按钮上的小色块就是当前颜色
            IconButton {
                icon.color: "white"
                icon.name: "format-text-color"
                text: "填色（点一下换下一种）"
                onClicked: lyricsWindow.colorIndex = lyricsWindow.colorIndex + 1 < Tokens.desktopLyricColors.length ? lyricsWindow.colorIndex + 1 : -1
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Tokens.space1
                    width: Tokens.iconSm
                    height: Tokens.lineStrong
                    color: lyricsWindow.fillColor
                }
            }
            IconButton {
                icon.color: "white"
                icon.name: "view-list-text"
                text: lyricsWindow.showSecond ? "只显示当前句" : "显示译文或下一句"
                // 开关状态用图标明暗表示：样式的「按下」底在这块深色底上是一个突兀的亮块
                opacity: lyricsWindow.showSecond ? 1 : Tokens.navInactive
                onClicked: lyricsWindow.showSecond = !lyricsWindow.showSecond
            }
            IconButton {
                icon.color: "white"
                icon.name: "edit-reset-symbolic"
                text: "回到默认位置"
                visible: lyricsWindow.layerShell !== null
                onClicked: lyricsWindow.placeDefault()
            }
            IconButton {
                icon.color: "white"
                icon.name: "object-locked-symbolic"
                text: "锁定（鼠标穿透；在播放条关掉再打开即解锁）"
                onClicked: lyricsWindow.locked = true
            }
            IconButton {
                icon.color: "white"
                icon.name: "window-close-symbolic"
                text: "关闭桌面歌词"
                onClicked: lyricsWindow.closeRequested()
            }
        }

        // 当前句：底层白字，上层同样的字用填色、按进度裁出左边一段
        Item {
            id: lineBox
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Math.min(base.implicitWidth, column.width)
            implicitHeight: base.implicitHeight

            LyricText {
                id: base
                width: parent.width
                text: lyricsWindow.current ? lyricsWindow.current.text
                    : lyricsWindow.track ? lyricsWindow.track.name + " — " + lyricsWindow.track.artist : "云听"
                font.pixelSize: lyricsWindow.sizes[0]
                color: "white"
            }
            Item {
                width: parent.width * lyricsWindow.fill
                height: parent.height
                clip: true
                LyricText {
                    width: lineBox.width
                    text: base.text
                    font.pixelSize: base.font.pixelSize
                    color: lyricsWindow.fillColor
                }
            }
        }
        LyricText {
            Layout.fillWidth: true
            visible: text.length > 0
            text: lyricsWindow.second
            font.pixelSize: lyricsWindow.sizes[1]
            font.weight: Font.Normal
            color: Qt.alpha("white", Tokens.navHover)
        }
    }
}
