// 一起听与本机播放的接线。房间状态、网络与「本机的播放 / 暂停 / 切歌 / 拖进度上报」都在 C++ ListenTogether
// （它直接看 AudioEngine）；这里只管两件事：
//   房间的歌单 ↔ 播放队列：歌单变了，队列换成它（正在放的那首在里面就原地不动，不打断；不在里面就留在最前）。
//     房主在本机换了队列（放了别的列表、加了歌）→ 同步给房间；只有房主能改房间的歌单。
//   对方的指令 → 播放：切歌、播放、暂停、拖进度。应用期间 remoteApplying，C++ 不把它当本机操作再报回去
import QtQuick

QtObject {
    id: link

    required property var room
    required property var playback

    // 正在把房间的东西写进播放：这期间的队列变化不回传给房间
    property bool applying: false

    function ids(tracks) { return (tracks || []).map(t => t.id); }
    function same(a, b) { return a.length === b.length && a.every((id, i) => id === b[i]); }

    function adopt() {
        const tracks = link.room.tracks || [];
        if (!link.room.inRoom || tracks.length === 0 || link.same(link.ids(tracks), link.ids(link.playback.queue)))
            return;
        const current = link.playback.current;
        const at = current ? tracks.findIndex(t => t.id === current.id) : -1;
        link.applying = true;
        if (at >= 0) {
            link.playback.queue = tracks;
            link.playback.index = at;
        } else if (current) {
            link.playback.queue = [current].concat(tracks);
            link.playback.index = 0;
        } else {
            link.playback.restore(tracks, 0);
        }
        link.applying = false;
    }

    function shareQueue() {
        if (link.room.inRoom && link.room.host && link.playback.queue.length > 0
                && !link.same(link.ids(link.playback.queue), link.ids(link.room.tracks)))
            link.room.syncQueue(link.ids(link.playback.queue));
    }

    function apply(type, songId, progressMs, playing) {
        const kind = type.toLowerCase();
        const engine = link.playback.engine;
        link.room.setRemoteApplying(true);
        link.applying = true;
        if (songId > 0 && (!link.playback.current || link.playback.current.id !== songId)) {
            let at = link.playback.queue.findIndex(t => t.id === songId);
            if (at < 0) {
                const track = (link.room.tracks || []).find(t => t.id === songId);
                if (track) {
                    link.playback.queue = link.playback.queue.concat([track]);
                    at = link.playback.queue.length - 1;
                }
            }
            if (at >= 0)
                link.playback.play(link.playback.queue, at);
        }
        // 切歌、拖进度照对方的位置；播放 / 暂停时差得多（超过 2 秒）才跟
        if (kind === "seek" || kind === "goto" || Math.abs(engine.position - progressMs / 1000) > 2)
            engine.seek(progressMs / 1000);
        engine.playing = kind === "pause" ? false : kind === "play" ? true : playing;
        link.applying = false;
        link.room.setRemoteApplying(false);
    }

    readonly property Connections _room: Connections {
        target: link.room
        function onTracksChanged() { link.adopt(); }
        function onCommandReceived(type, songId, progressMs, playing) { link.apply(type, songId, progressMs, playing); }
    }
    // 刚建好房间、房间里还没有歌：房主把手上的队列带进去
    readonly property Connections _inRoom: Connections {
        target: link.room
        function onStateChanged() { Qt.callLater(link.shareQueue); }
    }
    // 是不是本机的改动要当场判断（applying 只在写入那一刻为真）；同步本身推迟到这一轮改完
    readonly property Connections _queue: Connections {
        target: link.playback
        function onQueueChanged() {
            if (!link.applying)
                Qt.callLater(link.shareQueue);
        }
    }
}
