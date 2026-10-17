// 剪贴板：QML 没有剪贴板 API，用一个不可见的文本框走系统复制。全应用只有这一处。
pragma Singleton

import QtQuick

QtObject {
    // 应用里由 C++ 后端（main.qml 注入）写系统剪贴板；qml 工具/截图没有后端时退回不可见 TextEdit。
    property var live: null
    readonly property TextEdit editor: TextEdit { visible: false }

    function copy(text) {
        if (live && typeof live.copy === "function")
            return live.copy(text);
        editor.text = text;
        editor.selectAll();
        editor.copy();
        editor.text = "";
        return false;   // 原型没有 C++ 剪贴板桥，不能假装复制成功
    }
    // 图片（分享图）：path 是本地 PNG。只有 C++ 后端做得到
    function copyImage(path) {
        return live !== null && typeof live.copyImage === "function" && live.copyImage(path);
    }
}
