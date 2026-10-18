// 队列循环规则：列表循环只剩一首时，自然播完必须重播，而不是停住。
import QtQuick
import QtTest
import ".."

Item {
    id: root
    width: 400
    height: 300

    Component { id: engineComponent; SimEngine {} }
    Component { id: playbackComponent; Playback {} }

    TestCase {
        name: "Playback"
        when: windowShown

        function test_singleTrackListLoopRepeats() {
            const engine = createTemporaryObject(engineComponent, root);
            const player = createTemporaryObject(playbackComponent, root,
                { engine: engine, vip: false });
            const track = { id: 1, name: "Only", artist: "A", album: "", dur: 200, fee: 0, cover: "" };
            player.play([track], 0);
            compare(player.current.id, 1);
            verify(engine.playing);

            engine.finished();
            compare(player.current.id, 1);
            verify(engine.playing);
            compare(engine.position, 0);
        }
    }
}
