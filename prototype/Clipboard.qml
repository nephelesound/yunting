// 剪贴板：QML 没有剪贴板 API，用一个不可见的文本框走系统复制。全应用只有这一处。
pragma Singleton

import QtQuick

QtObject {
    readonly property TextEdit editor: TextEdit { visible: false }

    function copy(text) {
        editor.text = text;
        editor.selectAll();
        editor.copy();
        editor.text = "";
    }
}
