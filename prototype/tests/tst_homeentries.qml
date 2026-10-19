// 首页入口点进去的列表页，封面与入口是同一张（只有尺寸不同）。心动模式是流（点了就放），没有列表页。
import QtQuick
import QtTest
import ".."

Item {
    id: root

    Component { id: snapshotComponent; Snapshot {} }

    TestCase {
        name: "HomeEntries"

        function initTestCase() {
            Source.snapshot = snapshotComponent.createObject(root, { fixture: "worst" });
        }
        function cleanupTestCase() {
            Source.snapshot.fixture = "";
        }

        function image(url) { return (url || "").split("?")[0]; }

        function test_tileAndPageShowTheSameCover() {
            const listed = ["daily", "radar", "similar", "liked"];
            const entries = Source.homeEntries(true).filter(e => listed.indexOf(e.key) >= 0 && e.cover);
            verify(entries.length >= 3, "the worst fixture has covers on most personal entries");
            for (const entry of entries)
                compare(image(Source.list(entry.key).cover), image(entry.cover), entry.key);
        }

        // 种子歌就是红心第一首：心动模式的封面用下一首，免得和「我喜欢的音乐」重复
        function test_heartCoverSkipsTheSeed() {
            const heart = Source.homeEntries(true).find(e => e.key === "heart");
            verify(image(heart.cover) !== image(Source.pe.heart.seed.cover));
        }
    }
}
