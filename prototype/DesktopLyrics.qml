// 桌面歌词：独立的无边框透明窗口，压在桌面上显示当前句与译文（或下一句）。
// Wayland 下经 backend.layerShell 放到 overlay 层（普通窗口在 Wayland 上做不到置顶），平时锚定左下角、大小正好包住歌词。
// 拖动：overlay 不能被合成器拖动；若每动一下就请合成器挪窗口，指针坐标相对的是还没挪到位的窗口，窗口会来回抖。
//   所以开始拖动时窗口在同一帧里铺满屏幕（透明），拖动只在窗口里挪歌词，松手时再在同一帧里缩回、落到新位置。
//   没有 layer-shell（X11）时是普通置顶窗口，交给系统拖动。
// 当前句随播放从左到右填色：引擎的进度几次一秒，填色走逐帧时钟（与引擎进度对齐），所以是连续的。
// 观感与 Plasma 一致：字压在壁纸上用柔和的阴影（与桌面图标的文字同一做法）；悬停时出现 Plasma OSD 那种深色底板
//（Complementary 配色、系统阴影）与工具条：字号、填色、第二行、复位、锁定、关闭。字体跟随系统。
// 锁定后点击穿透，在播放条关掉再打开即解锁。样式与位置只在应用里记住（persist），原型与截图不读不写。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Effects
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
    property int left: -1            // 窗口离屏幕左边；-1 = 还没放过（用默认位置）
    property int bottomGap: 2 * Tokens.height12   // 窗口离屏幕下边

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
    // 原样用在深色壁纸上看不清，只提亮又会淡得和未唱的字分不开
    readonly property color accent: Kirigami.Theme.highlightColor
    readonly property color fillColor: lyricsWindow.colorIndex < 0
        ? Qt.hsla(lyricsWindow.accent.hslHue, Math.max(0.7, lyricsWindow.accent.hslSaturation),
                  Math.max(0.6, Math.min(0.7, lyricsWindow.accent.hslLightness)), 1)
        : Tokens.desktopLyricColors[lyricsWindow.colorIndex]
    readonly property var sizes: Tokens.desktopLyricSizes[lyricsWindow.sizeStep]

    readonly property int screenWidth: lyricsWindow.screen ? lyricsWindow.screen.width : 1920
    readonly property int screenHeight: lyricsWindow.screen ? lyricsWindow.screen.height : 1080

    // 窗口大小：歌词框外留一圈给底板的阴影。拖动时也不变（overlay 改尺寸会有一帧错位）
    readonly property int pad: Tokens.space4
    readonly property int maxLeft: Math.max(0, lyricsWindow.screenWidth - lyricsWindow.width)
    readonly property int maxBottomGap: Math.max(0, lyricsWindow.screenHeight - lyricsWindow.height)

    // 拖动中的位置不取整：指针的相对位移常常只有零点几像素，逐次取整会让慢拖动不动
    property bool dragging: false
    property real dragLeft
    property real dragBottomGap

    title: "云听 · 桌面歌词"
    color: "transparent"
    width: Math.min(2 * Tokens.trackColumn + Tokens.measureMax, lyricsWindow.screenWidth - 2 * Tokens.space6)   // 至多 960
    height: box.implicitHeight + 2 * lyricsWindow.pad
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
           | (lyricsWindow.locked ? Qt.WindowTransparentForInput : 0)

    // 放到 (left, bottomGap)，夹在屏幕内
    function place(left, bottomGap) {
        lyricsWindow.left = Math.round(Math.max(0, Math.min(lyricsWindow.maxLeft, left)));
        lyricsWindow.bottomGap = Math.round(Math.max(0, Math.min(lyricsWindow.maxBottomGap, bottomGap)));
        if (lyricsWindow.layerShell)
            lyricsWindow.layerShell.placeOverlay(lyricsWindow, 2 | 4 /* bottom | left */, lyricsWindow.left, 0, 0, lyricsWindow.bottomGap);
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

    // overlay 不能交给合成器拖动：按住期间订阅指针的相对位移，按位移改边距。
    // moved 是拖动生效前（越过拖动阈值）已经走过的距离，窗口那时还没动，补上它
    function beginDrag(moved) {
        if (!lyricsWindow.layerShell || !lyricsWindow.layerShell.beginRelativeDrag())
            return false;
        lyricsWindow.dragging = true;
        lyricsWindow.dragLeft = lyricsWindow.left;
        lyricsWindow.dragBottomGap = lyricsWindow.bottomGap;
        lyricsWindow.dragBy(moved.x, moved.y);
        return true;
    }
    function dragBy(dx, dy) {
        lyricsWindow.dragLeft = Math.max(0, Math.min(lyricsWindow.maxLeft, lyricsWindow.dragLeft + dx));
        lyricsWindow.dragBottomGap = Math.max(0, Math.min(lyricsWindow.maxBottomGap, lyricsWindow.dragBottomGap - dy));
        lyricsWindow.place(lyricsWindow.dragLeft, lyricsWindow.dragBottomGap);
    }
    function endDrag() {
        if (!lyricsWindow.dragging)
            return;
        lyricsWindow.dragging = false;
        lyricsWindow.layerShell.endRelativeDrag();
    }
    Connections {
        target: lyricsWindow.layerShell
        enabled: lyricsWindow.dragging
        function onRelativeMotion(dx, dy) { lyricsWindow.dragBy(dx, dy); }
    }

    onVisibleChanged: {
        if (lyricsWindow.visible && lyricsWindow.layerShell) {
            lyricsWindow.layerShell.makeOverlay(lyricsWindow, 2 | 4 /* bottom | left */, 0, false, "yunting-lyrics");
            lyricsWindow.placeSaved();
        }
        if (!lyricsWindow.visible) {
            lyricsWindow.locked = false;
            lyricsWindow.endDrag();
        }
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

    // 歌词框：贴着窗口，四周留阴影的位置。
    // 字与底板不跟配色方案：浅色方案里 Complementary 也是浅的，白底黑字压在深色壁纸上就看不见了。
    // 与 Plasma 桌面图标的文字同一做法：始终白字 + 柔和的深色阴影；悬停底板是深色半透明
    Item {
        id: box
        readonly property color ink: "white"
        x: lyricsWindow.pad
        y: lyricsWindow.pad
        width: lyricsWindow.width - 2 * lyricsWindow.pad
        height: implicitHeight
        implicitHeight: column.implicitHeight + 2 * Tokens.space3

        readonly property bool active: hover.hovered || drag.active

        HoverHandler { id: hover; enabled: !lyricsWindow.locked }
        DragHandler {
            id: drag
            enabled: !lyricsWindow.locked
            target: null
            // 普通窗口（X11、没有 layer-shell）交给合成器拖
            onActiveChanged: {
                if (!drag.active)
                    lyricsWindow.endDrag();
                else if (!lyricsWindow.beginDrag(Qt.point(drag.centroid.scenePosition.x - drag.centroid.scenePressPosition.x,
                                                          drag.centroid.scenePosition.y - drag.centroid.scenePressPosition.y)))
                    lyricsWindow.startSystemMove();
            }
        }

        // 悬停底板：只是一块深色半透明面。不加描边（深色面上的亮边是一圈白线），
        // 不加阴影（半透明的面透出自己的阴影，边缘发脏）
        Rectangle {
            anchors.fill: parent
            radius: Tokens.radiusLg
            color: Qt.alpha("black", Tokens.desktopPanel)
            opacity: box.active ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Tokens.motionFast; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut } }
        }

        component LyricText: Label {
            horizontalAlignment: Text.AlignHCenter
            font.weight: Font.Medium
            color: box.ink
            elide: Text.ElideRight
        }

        ColumnLayout {
            id: column
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: Tokens.space3
            width: parent.width - 2 * Tokens.space4
            spacing: Tokens.space1

            // 工具条：悬停（或拖动）时出现，平时位置留着（窗口高度不随悬停跳动）。按钮是系统的，图标白色（底板固定是深色）
            Row {
                Layout.alignment: Qt.AlignRight
                Layout.rightMargin: -Tokens.space2
                spacing: Tokens.space1
                opacity: box.active ? 1 : 0
                enabled: opacity > 0
                Behavior on opacity { NumberAnimation { duration: Tokens.motionFast; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut } }
                IconButton {
                    icon.name: "format-font-size-less"
                    icon.color: box.ink
                    text: "字号缩小"
                    enabled: lyricsWindow.sizeStep > 0
                    onClicked: lyricsWindow.sizeStep--
                }
                IconButton {
                    icon.name: "format-font-size-more"
                    icon.color: box.ink
                    text: "字号放大"
                    enabled: lyricsWindow.sizeStep < Tokens.desktopLyricSizes.length - 1
                    onClicked: lyricsWindow.sizeStep++
                }
                // 填色：在系统强调色与几种固定颜色之间轮换；按钮下沿的色条就是当前颜色
                IconButton {
                    icon.name: "format-text-color"
                    icon.color: box.ink
                    text: "填色（点一下换下一种）"
                    onClicked: lyricsWindow.colorIndex = lyricsWindow.colorIndex + 1 < Tokens.desktopLyricColors.length ? lyricsWindow.colorIndex + 1 : -1
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: Tokens.space1
                        width: Tokens.iconSm
                        height: Tokens.lineStrong
                        radius: Tokens.radiusSm
                        color: lyricsWindow.fillColor
                    }
                }
                IconButton {
                    icon.name: "view-list-text"
                    icon.color: box.ink
                    text: lyricsWindow.showSecond ? "只显示当前句" : "显示译文或下一句"
                    // 开关状态用图标明暗表示
                    opacity: lyricsWindow.showSecond ? 1 : Tokens.navInactive
                    onClicked: lyricsWindow.showSecond = !lyricsWindow.showSecond
                }
                IconButton {
                    icon.name: "edit-reset-symbolic"
                    icon.color: box.ink
                    text: "回到默认位置"
                    visible: lyricsWindow.layerShell !== null
                    onClicked: lyricsWindow.placeDefault()
                }
                IconButton {
                    icon.name: "object-locked-symbolic"
                    icon.color: box.ink
                    text: "锁定（鼠标穿透；在播放条关掉再打开即解锁）"
                    onClicked: lyricsWindow.locked = true
                }
                IconButton {
                    icon.name: "window-close-symbolic"
                    icon.color: box.ink
                    text: "关闭桌面歌词"
                    onClicked: lyricsWindow.closeRequested()
                }
            }

            // 歌词：贴着字形的一圈窄阴影勾出轮廓（与 Plasma 桌面图标的文字同一做法），浅色壁纸上白字也立得住。
            // 只有几像素宽：再宽就成了字后面一团脏雾
            Item {
                id: lyricsBlock
                Layout.fillWidth: true
                implicitHeight: lines.implicitHeight
                layer.enabled: true
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: Qt.alpha("black", Tokens.fadedHover)
                    blurMax: Tokens.space2
                    shadowBlur: 0.5
                    shadowVerticalOffset: Tokens.line
                    shadowHorizontalOffset: 0
                }

                ColumnLayout {
                    id: lines
                    width: parent.width
                    spacing: Tokens.space1

                    // 当前句：底层是未唱的字，上层同样的字用填色、按进度裁出左边一段
                    Item {
                        id: lineBox
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: Math.min(base.implicitWidth, lines.width)
                        implicitHeight: base.implicitHeight

                        LyricText {
                            id: base
                            width: parent.width
                            text: lyricsWindow.current ? lyricsWindow.current.text
                                : lyricsWindow.track ? lyricsWindow.track.name + " — " + lyricsWindow.track.artist : "云听"
                            font.pixelSize: lyricsWindow.sizes[0]
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
                        color: box.ink   // 层级靠字号；半透明的白在浅色壁纸上会糊掉
                    }
                }
            }
        }
    }
}
