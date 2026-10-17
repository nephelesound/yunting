// 列表与网格的滚轮 / 触控板 / 键盘滚动：用 KDE 原生的 Kirigami.WheelHandler，与 Plasma 自带应用一致：
// 鼠标滚轮按系统设置的行数平滑滚动（可被下一格接续，不会一格一跳），触控板按像素直接跟手，
// 方向键 / PageUp / PageDown / Home / End 可用；鼠标拖拽不再当成触屏甩动（桌面上那会误触）。
import QtQuick
import org.kde.kirigami as Kirigami

Kirigami.WheelHandler {
    required property Flickable flickable

    target: flickable
    filterMouseEvents: true
    keyNavigationEnabled: true
}
