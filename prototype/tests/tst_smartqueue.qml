// 智能续播的本地排序/补队规则。用假的 Source + 真 Playback/SimEngine，不碰网络也不写配置。
import QtQuick
import QtTest
import ".."

Item {
    id: root
    width: 800
    height: 600

    Component { id: engineComponent; SimEngine {} }
    Component { id: playbackComponent; Playback {} }
    Component { id: smartComponent; SmartQueue {} }

    readonly property var tracks: [
        { id: 1, name: "常听 A", artist: "歌手甲", album: "", dur: 200, fee: 0, cover: "" },
        { id: 2, name: "常听 B", artist: "歌手乙", album: "", dur: 180, fee: 0, cover: "" },
        { id: 3, name: "没听过", artist: "歌手丙", album: "", dur: 220, fee: 0, cover: "" },
        { id: 4, name: "马上跳过", artist: "歌手丁", album: "", dur: 190, fee: 0, cover: "" }
    ]

    function fakeSource() {
        return {
            pe: {
                daily: { tracks: root.tracks.slice(0, 2) },
                radar: { tracks: [root.tracks[2]] },
                similar: { tracks: [root.tracks[3]] },
                heart: { tracks: [] },
                liked: { tracks: [] }
            },
            fmTracks: [],
            recent: { tracks: [] },
            hotChart: { tracks: [] },
            isLiked: id => id === 1
        };
    }

    TestCase {
        name: "SmartQueue"
        when: windowShown

        function make() {
            const engine = createTemporaryObject(engineComponent, root);
            const player = createTemporaryObject(playbackComponent, root, { engine: engine, vip: false });
            const smart = createTemporaryObject(smartComponent, root,
                { player: player, source: fakeSource(), persist: false });
            smart.enabled = false;   // 测试自己控制补队时机
            return { engine: engine, player: player, smart: smart };
        }

        function test_ranksLikedAndCompletedTracksAboveSkippedOnes() {
            const ctx = make();
            ctx.smart.profile = {
                tracks: {
                    "1": { plays: 8, finishes: 8, skips: 0, artist: "歌手甲", last: Date.now() },
                    "4": { plays: 8, finishes: 0, skips: 8, artist: "歌手丁", last: Date.now() }
                },
                artists: { "歌手甲": 3, "歌手丁": -3 },
                updated: 0
            };
            const ranked = ctx.smart.rankCandidates([
                { track: root.tracks[3], weight: 0 },
                { track: root.tracks[1], weight: 0 },
                { track: root.tracks[0], weight: 0 }
            ], 3, {});
            compare(ranked.length, 3);
            compare(ranked[0].id, 1);
            verify(ranked[0].smartReason.indexOf("红心") >= 0);
        }

        function test_diversifiesArtistsAndExcludesQueue() {
            const ctx = make();
            const sameArtist = { id: 9, name: "同歌手", artist: "歌手乙", dur: 100, fee: 0, cover: "" };
            const ranked = ctx.smart.rankCandidates([
                { track: root.tracks[1], weight: 0 },
                { track: sameArtist, weight: 9 },
                { track: root.tracks[2], weight: 0 }
            ], 3, {});
            compare(ranked.length, 3);
            verify(ranked[0].artist !== ranked[1].artist,
                   "第一轮每位歌手只取一首，不连续堆同一个人");
            // 已在队列里的歌被排除，不会重复补进来
            const excluded = ctx.smart.rankCandidates([
                { track: root.tracks[1], weight: 0 },
                { track: root.tracks[2], weight: 0 }
            ], 3, { "2": true });
            compare(excluded.length, 1);
            compare(excluded[0].id, 3);
        }

        function test_recordsSkipAndFinish() {
            const ctx = make();
            ctx.smart.recordStart(root.tracks[0]);
            ctx.smart.recordEnd(root.tracks[0], 10, 200, false);
            compare(ctx.smart.profile.tracks["1"].skips, 1);
            ctx.smart.recordStart(root.tracks[0]);
            ctx.smart.recordEnd(root.tracks[0], 200, 200, true);
            compare(ctx.smart.profile.tracks["1"].finishes, 1);
        }

        function test_appendsUniquePlayableTracks() {
            const ctx = make();
            ctx.player.play([root.tracks[0]], 0);
            ctx.smart.enabled = true;
            ctx.smart.lastAppendAt = 0;
            ctx.smart.maybeExtend();
            verify(ctx.player.queue.length > 1, "队列只剩当前一首时会补歌");
            const ids = ctx.player.queue.map(t => t.id);
            compare(new Set(ids).size, ids.length, "补队不产生重复");
            verify(ids.indexOf(1) >= 0, "当前歌仍在队列");
        }

        function test_doesNotExtendWhenDisabled() {
            const ctx = make();
            ctx.player.play([root.tracks[0]], 0);
            ctx.smart.enabled = false;
            ctx.smart.lastAppendAt = 0;
            ctx.smart.maybeExtend();
            compare(ctx.player.queue.length, 1);
        }
    }
}
