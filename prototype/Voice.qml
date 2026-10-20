// 评论语音：全应用同一时间只放一条。player 在应用里是 C++ VoicePlayer（backend.voice），
// 原型里是 SimVoice，都由 main.qml 设置；放语音时歌要不要停也在 main.qml。
pragma Singleton

import QtQuick

QtObject {
    property var player: null

    readonly property string source: player ? player.source : ""
    readonly property real position: player ? player.position : 0

    // 放这一条；正在放的就是它时停下
    function toggle(url) { if (player && url) player.toggle(url); }
    function stop() { if (player) player.stop(); }
}
