// 音频引擎的原型实现：用计时器模拟播放进度。接口与 C++ AudioEngine 相同（见 DESIGN.md「后端接口」）：
//   track（由 Playback 设置）· playing · position / duration（秒）· volume（0–1）· muted · toggle() · seek(s)
//   finished()：自然播完；failed(reason)：取不到播放地址等
import QtQuick

Item {
    id: engine

    property var track: null
    property bool playing: false
    property real position: 0
    property real volume: 0.7      // 0–1，滑块刻度
    property bool muted: false
    readonly property real duration: engine.track ? engine.track.dur : 0

    signal finished()
    signal failed(string reason)

    onTrackChanged: engine.position = 0

    function toggle() { engine.playing = !engine.playing; }
    function seek(seconds) { engine.position = Math.max(0, Math.min(engine.duration, seconds)); }

    Timer {
        interval: 1000
        repeat: true
        running: engine.playing && engine.track !== null
        onTriggered: {
            engine.position = Math.min(engine.position + 1, engine.duration);
            if (engine.duration > 0 && engine.position >= engine.duration)
                engine.finished();
        }
    }
}
