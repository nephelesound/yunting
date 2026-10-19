// 播放：队列、当前位置、播放模式与「哪些歌能放」的规则都在这里；声音由 engine 负责（原型是 SimEngine，
// 应用里是 C++ AudioEngine）。界面只调用 play / next / previous，被拦下时收到 blocked 信号再决定怎么提示。
import QtQuick
import "Format.js" as Format

QtObject {
    id: playback

    required property var engine    // 鸭子类型：SimEngine 或 C++ AudioEngine
    property bool vip

    property var queue: []
    property int index: -1
    property int mode: 0            // 0 列表循环 · 1 单曲循环 · 2 随机
    property string origin          // 队列从哪来："fm" = 私人漫游、"heart" = 心动模式（都是流，快放完时续取），其余为空

    readonly property var current: playback.index >= 0 && playback.index < playback.queue.length ? playback.queue[playback.index] : null

    // reason：见 Format.blockedBy（"vip" / "paid" / "unavailable"）
    signal blocked(var track, string reason)

    function playable(track) {
        return Format.blockedBy(track, playback.vip) === "";
    }

    function play(tracks, i, origin) {
        if (i < 0 || i >= tracks.length)
            return;
        const track = tracks[i];
        if (!playback.playable(track)) {
            playback.blocked(track, Format.blockedBy(track, playback.vip));
            return;
        }
        // 队列内切歌（next/previous/点队列里的一首）沿用原来的来源；换了一份列表就按传入的来源
        if (tracks !== playback.queue)
            playback.origin = origin || "";
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

    // 下一首播放 / 加入队列：队列里已有这首就先挪走（不重复）；队列空着就直接开始放
    function insert(track, at) {
        if (playback.current === null) {
            playback.play([track], 0);
            return;
        }
        if (playback.current.id === track.id)
            return;
        const q = playback.queue.filter(t => t.id !== track.id);
        const index = q.findIndex(t => t.id === playback.current.id);
        q.splice(at === "next" ? index + 1 : q.length, 0, track);
        playback.queue = q;
        playback.index = index;
    }
    // 续上来的歌接在队尾（已在队列里的不重复）；不改变正在播放的那首
    function append(tracks) {
        const ids = {};
        for (const t of playback.queue)
            ids[t.id] = true;
        const fresh = tracks.filter(t => !ids[t.id]);
        if (fresh.length > 0)
            playback.queue = playback.queue.concat(fresh);
    }
    function playNext(track) { playback.insert(track, "next"); }
    function enqueue(track) { playback.insert(track, "end"); }

    // 拖动排序：把第 from 首挪到第 to 首的位置，正在播放的那首跟着它的新位置走
    function move(from, to) {
        if (from === to || from < 0 || to < 0 || from >= playback.queue.length || to >= playback.queue.length)
            return;
        const q = playback.queue.slice();
        const playingId = playback.current ? playback.current.id : null;
        q.splice(to, 0, q.splice(from, 1)[0]);
        playback.queue = q;
        playback.index = q.findIndex(t => t.id === playingId);
    }

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
            } else if (playback.queue.length === 1) {
                // 列表循环/随机只剩一首时 next() 无处可去：重播当前这首，别停住
                playback.engine.seek(0);
                playback.engine.playing = true;
            } else {
                playback.next();
            }
        }
    }
}
