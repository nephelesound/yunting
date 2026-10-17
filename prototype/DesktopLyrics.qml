// 桌面歌词：独立的无边框透明窗口，压在桌面上显示当前句（36）与译文或下一句（24）。
// Wayland 下经 backend.layerShell 放到屏幕底部的 overlay 层（普通窗口在 Wayland 上做不到置顶）；
// 没有它（X11 / 没装 LayerShellQt）时是普通的置顶无边框窗口，可以拖动。
// 文字压在未知的壁纸上：描边是为了可读（功能），不是装饰。
// 悬停时出现半透明底与「锁定 / 关闭」；锁定后整个窗口对鼠标透明（点击穿透），在播放条关掉再打开即解锁。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Window {
    id: lyricsWindow

    property var track: null
    property real elapsed
    property var layerShell: null   // backend.layerShell（可能没有）
    property bool locked: false

    signal closeRequested()

    readonly property var lines: lyricsWindow.track ? Source.lyrics(lyricsWindow.track.id) : []
    readonly property int index: Source.lyricIndex(lyricsWindow.lines, lyricsWindow.elapsed)
    readonly property var current: lyricsWindow.lines.length > 0 ? lyricsWindow.lines[lyricsWindow.index] : null
    // 第二行：有译文显示译文，否则显示下一句
    readonly property string second: !lyricsWindow.current ? ""
        : lyricsWindow.current.tr ? lyricsWindow.current.tr
        : lyricsWindow.index + 1 < lyricsWindow.lines.length ? lyricsWindow.lines[lyricsWindow.index + 1].text : ""

    title: "云听 · 桌面歌词"
    width: 2 * Tokens.trackColumn + Tokens.measureMax    // 960
    height: 2 * Tokens.height12 + 2 * Tokens.space4       // 128
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
           | (lyricsWindow.locked ? Qt.WindowTransparentForInput : 0)

    onVisibleChanged: {
        if (lyricsWindow.visible && lyricsWindow.layerShell)
            lyricsWindow.layerShell.makeOverlay(lyricsWindow, 2 /* bottom */, Tokens.height12 * 2, false, "yunting-lyrics");
        if (!lyricsWindow.visible)
            lyricsWindow.locked = false;
    }

    HoverHandler { id: hover; enabled: !lyricsWindow.locked }
    // 没有 layer-shell 时可以拖到任意位置
    DragHandler {
        enabled: !lyricsWindow.locked && !lyricsWindow.layerShell
        target: null
        onActiveChanged: if (active) lyricsWindow.startSystemMove()
    }

    // 悬停底：告诉用户这块可以拖动、可以操作（浮在桌面上的面，用浮层圆角）
    Rectangle {
        anchors.fill: parent
        radius: Tokens.radiusLg
        color: Qt.alpha("black", Tokens.desktopBacking)
        opacity: hover.hovered ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Tokens.motionFast } }
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: parent.width - 2 * Tokens.space6
        spacing: Tokens.space1

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: lyricsWindow.current ? lyricsWindow.current.text
                : lyricsWindow.track ? lyricsWindow.track.name + " — " + lyricsWindow.track.artist : "云听"
            font.pixelSize: Tokens.text4xl
            font.weight: Font.Medium
            color: "white"
            style: Text.Outline
            styleColor: Qt.alpha("black", Tokens.muted)
            elide: Text.ElideRight
        }
        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            horizontalAlignment: Text.AlignHCenter
            text: lyricsWindow.second
            font.pixelSize: Tokens.text2xl
            color: Qt.alpha("white", Tokens.navHover)
            style: Text.Outline
            styleColor: Qt.alpha("black", Tokens.muted)
            elide: Text.ElideRight
        }
    }

    // 悬停时才出现的操作（右上角）
    Row {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Tokens.space2
        spacing: Tokens.space1
        opacity: hover.hovered ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Tokens.motionFast } }
        // 底是固定的深色，图标固定用白色（ToolButton 的图标色来自样式调色板，不跟 Kirigami.Theme）
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
}
