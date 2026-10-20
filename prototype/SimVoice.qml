// 评论语音的原型实现：计时器推进进度。接口与 C++ VoicePlayer 相同：
//   source（正在放的地址，空 = 没在放）· position（秒）· volume（0–1）· toggle(url) · stop()
// 替身不知道语音多长：放到 limit 秒就当放完
import QtQuick

Item {
    id: voice

    property string source: ""
    property real position: 0
    property real volume: 1
    property real limit: 60
    property bool frozen: false   // 截图：停在给定的那一秒（进度一动画面就一直在变，截图等不到安定）

    function toggle(url) {
        if (!url || url === voice.source) {
            voice.stop();
            return;
        }
        voice.position = 0;
        voice.source = url;
    }
    function stop() {
        voice.source = "";
        voice.position = 0;
    }

    Timer {
        interval: 250
        repeat: true
        running: voice.source !== "" && !voice.frozen
        onTriggered: {
            voice.position += 0.25;
            if (voice.position >= voice.limit)
                voice.stop();
        }
    }
}
