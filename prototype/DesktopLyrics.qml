// 桌面歌词：独立的无边框透明窗口，压在桌面上显示当前句与译文（或下一句）。
// Wayland 下经 backend.layerShell 放到 overlay 层（普通窗口在 Wayland 上做不到置顶），平时锚定左下角。
// 拖动：overlay 不能被合成器拖动；若每动一下就请合成器挪窗口，指针坐标相对的是还没挪到位的窗口，窗口会来回抖。
//   所以拖动时订阅指针的相对位移，按位移改边距，窗口尺寸不变。没有 layer-shell（X11）时交给系统拖动。
// 当前句随播放从左到右点亮：引擎的进度几次一秒，点亮走逐帧时钟（与引擎进度对齐），所以是连续的。
// 观感：纯白的字配不透明度，直接压在桌面上，默认没有底板、没有阴影。还没唱到的字与第二行稍淡，唱到的字点亮成全白。
// 应用读不到窗口背后的壁纸（Wayland 不给），白字在亮壁纸上看不见，所以字色可换成纯黑，由用户按壁纸选。
// 悬停时字的后面出现一块方角的半透明底板（白字是黑，黑字是白），按钮直接放在底板上方（一块面，不另套工具栏的框）：
// 系统的 ToolButton，几组之间只用间距分开：字号、「样式」菜单（字色、唱到的字的颜色、文字阴影）、
// 第二行（开着时图标是强调色）、复位、锁定、关闭。菜单开在自己的窗口里（popupType: Window）：
// 歌词窗口很矮，菜单留在窗口里会被裁成方角、挤成一团。文字阴影默认关，可选柔和 / 浓，总是黑色。
// 锁定后点击穿透，在播放条关掉再打开即解锁。样式与位置只在应用里记住（persist），原型与截图不读不写。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Effects
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
    property bool darkInk: false     // 字色：白（默认）或黑
    property int colorIndex: 0       // fills 的下标：0 = 字色本身
    property int shadowLevel: 0      // Tokens.desktopShadows 的下标：0 = 无
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
    // 没有时间轴的歌词 index 是 -1：没有当前句，桌面歌词显示曲名
    readonly property var current: lyricsWindow.index >= 0 ? lyricsWindow.lines[lyricsWindow.index] : null
    readonly property var next: lyricsWindow.index >= 0 && lyricsWindow.index + 1 < lyricsWindow.lines.length
        ? lyricsWindow.lines[lyricsWindow.index + 1] : null
    // 当前句唱到了哪：有逐字歌词时逐字推进，否则按时间比例（规则在 Source.lineFill）
    readonly property real fill: Source.lineFill(lyricsWindow.current, lyricsWindow.next,
                                                 lyricsWindow.track ? lyricsWindow.track.dur || 0 : 0, lyricsWindow.clock)
    // 第二行：有译文显示译文，否则显示下一句
    readonly property string second: !lyricsWindow.showSecond || !lyricsWindow.current ? ""
        : lyricsWindow.current.tr ? lyricsWindow.current.tr : lyricsWindow.next ? lyricsWindow.next.text : ""
    readonly property color ink: lyricsWindow.darkInk ? "black" : "white"
    // 与字相反的颜色：悬停底板用它
    readonly property color backing: lyricsWindow.darkInk ? "white" : "black"
    // 唱到的字：字色本身，或系统强调色、几种固定的中间调。强调色取色相，饱和度至少 70%、亮度落在 50%–60%：
    // 原样的强调色可能很深或很灰，与半透明的字分不开
    readonly property color accent: Kirigami.Theme.highlightColor
    readonly property var fills: [lyricsWindow.ink,
        Qt.hsla(lyricsWindow.accent.hslHue, Math.max(0.7, lyricsWindow.accent.hslSaturation),
                Math.max(0.5, Math.min(0.6, lyricsWindow.accent.hslLightness)), 1)].concat(Tokens.desktopLyricColors)
    // 菜单里的名字，与 fills 一一对应（Tokens.desktopLyricColors 是蓝、红、绿、橙）
    readonly property var fillNames: [lyricsWindow.darkInk ? "黑色（与字同色）" : "白色（与字同色）", "系统强调色",
                                      "蓝色", "红色", "绿色", "橙色"]
    readonly property color fillColor: lyricsWindow.fills[lyricsWindow.colorIndex]
    readonly property var sizes: Tokens.desktopLyricSizes[lyricsWindow.sizeStep]

    readonly property int screenWidth: lyricsWindow.screen ? lyricsWindow.screen.width : 1920
    readonly property int screenHeight: lyricsWindow.screen ? lyricsWindow.screen.height : 1080

    readonly property int maxLeft: Math.max(0, lyricsWindow.screenWidth - lyricsWindow.width)
    readonly property int maxBottomGap: Math.max(0, lyricsWindow.screenHeight - lyricsWindow.height)

    // 拖动中的位置不取整：指针的相对位移常常只有零点几像素，逐次取整会让慢拖动不动
    property bool dragging: false
    property real dragLeft
    property real dragBottomGap

    title: "云听 · 桌面歌词"
    color: "transparent"
    width: Math.min(2 * Tokens.trackColumn + Tokens.measureMax, lyricsWindow.screenWidth - 2 * Tokens.space6)   // 至多 960
    height: Tokens.space1 + tools.implicitHeight + Tokens.space2 + textColumn.implicitHeight
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
                property bool darkInk: false
                property int colorIndex: 0
                property int shadowLevel: 0
                property bool showSecond: true
                property int left: -1
                property int bottomGap: 2 * Tokens.height12
            }
            Component.onCompleted: {
                lyricsWindow.sizeStep = Math.max(0, Math.min(Tokens.desktopLyricSizes.length - 1, saved.sizeStep));
                lyricsWindow.darkInk = saved.darkInk;
                lyricsWindow.colorIndex = Math.max(0, Math.min(lyricsWindow.fills.length - 1, saved.colorIndex));
                lyricsWindow.shadowLevel = Math.max(0, Math.min(Tokens.desktopShadows.length - 1, saved.shadowLevel));
                lyricsWindow.showSecond = saved.showSecond;
                lyricsWindow.left = saved.left;
                lyricsWindow.bottomGap = saved.bottomGap;
                persisted.restored = true;
            }
            Binding { when: persisted.restored; target: saved; property: "sizeStep"; value: lyricsWindow.sizeStep }
            Binding { when: persisted.restored; target: saved; property: "darkInk"; value: lyricsWindow.darkInk }
            Binding { when: persisted.restored; target: saved; property: "colorIndex"; value: lyricsWindow.colorIndex }
            Binding { when: persisted.restored; target: saved; property: "shadowLevel"; value: lyricsWindow.shadowLevel }
            Binding { when: persisted.restored; target: saved; property: "showSecond"; value: lyricsWindow.showSecond }
            Binding { when: persisted.restored; target: saved; property: "left"; value: lyricsWindow.left }
            Binding { when: persisted.restored; target: saved; property: "bottomGap"; value: lyricsWindow.bottomGap }
        }
    }

    Item {
        id: stage
        anchors.fill: parent
        readonly property bool active: hover.hovered || drag.active || styleMenu.visible

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

        component LyricText: Label {
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }

        // 一个工具按钮：系统的 ToolButton，图标、悬停、按下、勾选都由样式画；悬停一会儿显示说明
        // 底板上的按钮：系统的 flat ToolButton 画悬停与按下的框（KDE 工具栏按钮的样子），
        // 图标与文字由 contentItem 按字色画——底板可深可浅，样式按系统配色画的图标在深底板上看不见。
        // 不给按钮设 icon：样式会在底下再画一个系统色的图标，与这里的叠成两个
        component Tool: ToolButton {
            id: tool
            property string iconName
            property color tint: lyricsWindow.ink
            flat: true
            // 样式只画框：IconOnly 且没有 icon 时它不画任何图标与文字（text 只当提示与无障碍名称用）
            display: AbstractButton.IconOnly
            // 与应用里其他图标按钮同一尺寸：32 的方按钮、16 的图标居中
            implicitWidth: Tokens.height8
            implicitHeight: Tokens.height8
            ToolTip.text: text
            ToolTip.visible: hovered
            ToolTip.delay: 600
            contentItem: Item {
                Kirigami.Icon {
                    anchors.centerIn: parent
                    width: Tokens.iconSm
                    height: Tokens.iconSm
                    source: tool.iconName
                    color: tool.tint
                    isMask: true
                }
            }
        }

        // 悬停底板：托住按钮与歌词的唯一一块面（Metro：内容在前，不在面里再套一个工具栏的框），
        // 铺满整个歌词窗口（也就是能拖动的范围），方角。不跟着字宽变：歌词一句一换，底板会一直伸缩
        Rectangle {
            visible: stage.active
            anchors.fill: parent
            color: Qt.alpha(lyricsWindow.backing, Tokens.desktopPlate)
        }

        // 按钮：悬停（或拖动）时直接放在底板上，没有自己的工具栏底与分隔线，几组之间只用间距分开。
        // 一天要碰很多次，出现与消失都不做动画；位置一直留着，窗口高度不随悬停跳动。
        // 按钮见上面的 Tool：系统的 flat ToolButton，图标与文字按字色画
        RowLayout {
            id: tools
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: Tokens.space1
            visible: stage.active
            spacing: 0

                Tool {
                    iconName: "format-font-size-less"
                    text: "字号缩小"
                    enabled: lyricsWindow.sizeStep > 0
                    onClicked: lyricsWindow.sizeStep--
                }
                Tool {
                    iconName: "format-font-size-more"
                    text: "字号放大"
                    enabled: lyricsWindow.sizeStep < Tokens.desktopLyricSizes.length - 1
                    onClicked: lyricsWindow.sizeStep++
                }
                Item { implicitWidth: Tokens.space4 }
                // 「样式」：打开菜单的按钮，字色、唱到的字、阴影都在里面
                Tool {
                    id: styleButton
                    iconName: "format-text-color"
                    text: "样式（字色、唱到的字、阴影）"
                    Accessible.role: Accessible.ButtonMenu
                    down: styleMenu.visible
                    onClicked: styleMenu.popup(styleButton, 0, styleButton.height)
                    Menu {
                        id: styleMenu
                        popupType: Popup.Window
                        // 菜单是系统菜单的样子，不跟着按钮所在的底板配色
                        Kirigami.Theme.colorSet: Kirigami.Theme.Window
                        Kirigami.Theme.inherit: false
                        MenuItem {
                            text: "白字（暗壁纸）"
                            checkable: true
                            checked: !lyricsWindow.darkInk
                            onTriggered: lyricsWindow.darkInk = false
                        }
                        MenuItem {
                            text: "黑字（亮壁纸）"
                            checkable: true
                            checked: lyricsWindow.darkInk
                            onTriggered: lyricsWindow.darkInk = true
                        }
                        MenuSeparator {}
                        Menu {
                            id: fillMenu
                            title: "唱到的字"
                            popupType: Popup.Window
                            // 每种颜色前一个实心圆点，就是这种颜色
                            Instantiator {
                                model: lyricsWindow.fillNames
                                delegate: MenuItem {
                                    required property int index
                                    required property string modelData
                                    text: modelData
                                    icon.name: "media-record-symbolic"
                                    icon.color: lyricsWindow.fills[index]
                                    checkable: true
                                    checked: lyricsWindow.colorIndex === index
                                    onTriggered: lyricsWindow.colorIndex = index
                                }
                                onObjectAdded: (index, object) => fillMenu.insertItem(index, object)
                                onObjectRemoved: (index, object) => fillMenu.removeItem(object)
                            }
                        }
                        Menu {
                            id: shadowMenu
                            title: "文字阴影"
                            popupType: Popup.Window
                            Instantiator {
                                model: ["无", "柔和", "浓"]
                                delegate: MenuItem {
                                    required property int index
                                    required property string modelData
                                    text: modelData
                                    checkable: true
                                    checked: lyricsWindow.shadowLevel === index
                                    onTriggered: lyricsWindow.shadowLevel = index
                                }
                                onObjectAdded: (index, object) => shadowMenu.insertItem(index, object)
                                onObjectRemoved: (index, object) => shadowMenu.removeItem(object)
                            }
                        }
                    }
                }
                Tool {
                    iconName: "view-list-text"
                    // 开着用强调色的图标表示，不用勾选底（勾选底是又一层框，且各控件样式画得不一样）
                    tint: lyricsWindow.showSecond ? Kirigami.Theme.highlightColor : lyricsWindow.ink
                    text: lyricsWindow.showSecond ? "只显示当前句" : "显示译文或下一句"
                    onClicked: lyricsWindow.showSecond = !lyricsWindow.showSecond
                }
                Item { implicitWidth: Tokens.space4 }
                Tool {
                    iconName: "edit-reset-symbolic"
                    text: "回到默认位置"
                    visible: lyricsWindow.layerShell !== null
                    onClicked: lyricsWindow.placeDefault()
                }
                Tool {
                    iconName: "object-locked-symbolic"
                    text: "锁定（鼠标穿透；在播放条关掉再打开即解锁）"
                    onClicked: lyricsWindow.locked = true
                }
                Tool {
                    iconName: "window-close-symbolic"
                    text: "关闭桌面歌词"
                    onClicked: lyricsWindow.closeRequested()
                }
        }

        // 文字阴影（选了才有）：同样的字用不透明的黑色另画一份，模糊后垫在歌词下面、下移一像素。
        // 不给歌词本身加阴影：歌词逐帧点亮，阴影会跟着逐帧重新模糊，且随字的透明度深浅不一。
        // 这一份只在换句时重画
        Column {
            id: shadowColumn
            readonly property var level: Tokens.desktopShadows[lyricsWindow.shadowLevel]
            visible: lyricsWindow.shadowLevel > 0
            anchors.left: textColumn.left
            anchors.right: textColumn.right
            anchors.bottom: textColumn.bottom
            anchors.bottomMargin: -Tokens.line
            spacing: textColumn.spacing
            opacity: level[0]
            layer.enabled: visible
            layer.effect: MultiEffect {
                blurEnabled: true
                blur: shadowColumn.level[1]
                blurMax: Tokens.space4
            }

            LyricText {
                anchors.horizontalCenter: parent.horizontalCenter
                width: lineBox.width
                text: base.text
                font: base.font
                color: "black"
            }
            LyricText {
                width: parent.width
                visible: secondLine.visible
                text: secondLine.text
                font: secondLine.font
                color: "black"
            }
        }

        // 歌词：直接压在桌面上。字重取 DemiBold：半透明的笔画细了会被壁纸吃掉
        Column {
            id: textColumn
            anchors.bottom: parent.bottom
            width: parent.width
            spacing: Tokens.space1

            // 当前句：底层是半透明的字，上层同样的字用点亮色、按进度裁出左边一段
            Item {
                id: lineBox
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(Math.ceil(base.implicitWidth), textColumn.width)
                height: base.implicitHeight

                LyricText {
                    id: base
                    width: parent.width
                    text: lyricsWindow.current ? lyricsWindow.current.text
                        : lyricsWindow.track ? lyricsWindow.track.name + " — " + lyricsWindow.track.artist : "云听"
                    font.pixelSize: lyricsWindow.sizes[0]
                    font.weight: Font.DemiBold
                    // 还没有歌词时显示的歌名不会被点亮，直接是全色
                    color: lyricsWindow.current ? Qt.alpha(lyricsWindow.ink, Tokens.desktopInk) : lyricsWindow.ink
                }
                Item {
                    width: parent.width * lyricsWindow.fill
                    height: parent.height
                    clip: true
                    LyricText {
                        width: lineBox.width
                        text: base.text
                        font: base.font
                        color: lyricsWindow.fillColor
                    }
                }
            }
            LyricText {
                id: secondLine
                width: textColumn.width
                visible: text.length > 0
                text: lyricsWindow.second
                font.pixelSize: lyricsWindow.sizes[1]
                font.weight: Font.Medium
                color: Qt.alpha(lyricsWindow.ink, Tokens.desktopInk)
            }
        }
    }
}
