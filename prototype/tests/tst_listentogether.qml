// 一起听的接线（ListenTogetherLink）：房间歌单 ↔ 播放队列、对方的指令 → 播放，且不回声。
// 房间用一个记账的替身：记下 syncQueue 的调用与 remoteApplying 的开关。
import QtQuick
import QtTest
import ".."

Item {
    id: root

    readonly property var tracks: [
        { id: 1, name: "一", artist: "A", album: "", dur: 200, fee: 0, cover: "" },
        { id: 2, name: "二", artist: "A", album: "", dur: 200, fee: 0, cover: "" },
        { id: 3, name: "三", artist: "A", album: "", dur: 200, fee: 0, cover: "" },
    ]

    Component { id: engineComponent; SimEngine {} }
    Component { id: playbackComponent; Playback {} }
    Component {
        id: roomComponent
        QtObject {
            property bool inRoom: true
            property bool host: true
            property var tracks: []
            property var synced: []
            property var applyingLog: []
            signal stateChanged()
            signal commandReceived(string type, real songId, real progressMs, bool playing)
            function syncQueue(ids) { synced = synced.concat([ids]); }
            function setRemoteApplying(applying) { applyingLog = applyingLog.concat([applying]); }
        }
    }
    Component { id: linkComponent; ListenTogetherLink {} }

    TestCase {
        name: "ListenTogether"

        function make(host) {
            const engine = engineComponent.createObject(root);
            const playback = playbackComponent.createObject(root, { engine: engine });
            const room = roomComponent.createObject(root, { host: host });
            linkComponent.createObject(root, { room: room, playback: playback });
            return { engine: engine, playback: playback, room: room };
        }

        // 房主换了队列：同步给房间（一轮改动只同步一次）
        function test_hostQueueIsShared() {
            const t = make(true);
            t.playback.play(root.tracks, 0);
            tryVerify(() => t.room.synced.length === 1, 1000);
            compare(t.room.synced[0], [1, 2, 3]);
        }

        // 不是房主：本机换队列不改房间的歌单
        function test_guestQueueIsNotShared() {
            const t = make(false);
            t.playback.play(root.tracks, 0);
            wait(50);
            compare(t.room.synced.length, 0);
        }

        // 房间的歌单到了：队列换成它，正在放的那首在里面就原地不动；采纳来的歌单不回传
        function test_roomTracksBecomeTheQueue() {
            const t = make(true);
            t.playback.play([root.tracks[1]], 0);
            tryVerify(() => t.room.synced.length === 1, 1000);
            t.room.tracks = root.tracks;
            compare(t.playback.queue.map(x => x.id), [1, 2, 3]);
            compare(t.playback.current.id, 2);
            wait(50);
            compare(t.room.synced.length, 1);
        }

        // 对方切歌：本机跳到那首、到对方的位置，应用期间 remoteApplying 开着
        function test_remoteGotoPlaysTheTrack() {
            const t = make(false);
            t.room.tracks = root.tracks;
            t.room.commandReceived("GOTO", 3, 42000, true);
            compare(t.playback.current.id, 3);
            compare(t.engine.position, 42);
            verify(t.engine.playing);
            compare(t.room.applyingLog, [true, false]);
        }

        // 对方暂停：本机停下；位置差不到 2 秒不跳
        function test_remotePauseStopsWithoutJumping() {
            const t = make(false);
            t.room.tracks = root.tracks;
            t.playback.play(root.tracks, 0);
            t.engine.seek(10);
            t.room.commandReceived("PAUSE", 1, 11000, false);
            verify(!t.engine.playing);
            compare(t.engine.position, 10);
        }
    }
}
