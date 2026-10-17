// 播放：队列、当前位置、播放模式与「哪些歌能放」的规则都在这里；声音由 engine 负责（原型是 SimEngine，
// 应用里是 C++ AudioEngine）。界面只调用 play / next / previous，被拦下时收到 blocked 信号再决定怎么提示。
import QtQuick

QtObject {
    id: playback

    required property var engine    // 鸭子类型：SimEngine 或 C++ AudioEngine
    property bool vip

    property var queue: []
    property int index: -1
    property int mode: 0            // 0 列表循环 · 1 单曲循环 · 2 随机

    readonly property var current: playback.index >= 0 && playback.index < playback.queue.length ? playback.queue[playback.index] : null

    // reason: "paid" 付费专辑 · "vip" 需要会员
    signal blocked(var track, string reason)

    // 付费专辑（fee=4）要单独购买，VIP 也不能播；VIP 曲（fee=1）要会员
    function playable(track) {
        return track.fee !== 4 && (playback.vip || track.fee !== 1);
    }

    function play(tracks, i) {
        if (i < 0 || i >= tracks.length)
            return;
        const track = tracks[i];
        if (!playback.playable(track)) {
            playback.blocked(track, track.fee === 4 ? "paid" : "vip");
            return;
        }
        playback.queue = tracks;
        playback.index = i;
        playback.engine.track = track;
        playback.engine.playing = true;
    }

    // 手动切歌：跳过不能放的；随机模式随机选一首能放的
    function step(delta) {
        const n = playback.queue.length;
        if (n === 0)
            return;
        if (playback.mode === 2) {
            const candidates = [];
            for (let k = 0; k < n; k++)
                if (k !== playback.index && playback.playable(playback.queue[k]))
                    candidates.push(k);
            if (candidates.length > 0)
                playback.play(playback.queue, candidates[Math.floor(Math.random() * candidates.length)]);
            return;
        }
        let i = playback.index;
        do {
            i = (i + delta + n) % n;
        } while (!playback.playable(playback.queue[i]) && i !== playback.index);
        if (i !== playback.index)
            playback.play(playback.queue, i);
    }
    function next() { playback.step(1); }
    function previous() { playback.step(-1); }

    function remove(i) {
        const q = playback.queue.slice();
        q.splice(i, 1);
        if (i < playback.index)
            playback.index--;
        playback.queue = q;
    }
    function clearUpcoming() {
        playback.queue = playback.queue.slice(0, playback.index + 1);
    }

    // 不改变当前播放的情况下换队列（原型启动时用；也给「恢复上次的队列」用）
    function restore(tracks, i) {
        playback.queue = tracks;
        playback.index = Math.min(i, tracks.length - 1);
        playback.engine.track = playback.current;
    }

    // 自然播完：单曲循环重播，其余进下一首
    readonly property Connections _engine: Connections {
        target: playback.engine
        function onFinished() {
            if (playback.mode === 1) {
                playback.engine.seek(0);
                playback.engine.playing = true;
            } else {
                playback.next();
            }
        }
    }
}
