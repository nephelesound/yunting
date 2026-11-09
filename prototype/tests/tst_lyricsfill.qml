// 逐字歌词填色跟着折行走：折成几行的当前句，唱过的行整行填色，正在唱的那行填到唱到的位置（#6）
import QtQuick
import QtTest
import ".."

Item {
    id: root
    width: 1000
    height: 700

    Component { id: snapshotComponent; Snapshot {} }
    Component { id: pageComponent; NowPlayingPage { width: root.width; height: root.height } }

    TestCase {
        name: "LyricsFill"
        when: windowShown

        function initTestCase() {
            Source.snapshot = snapshotComponent.createObject(root, { fixture: "worst" });
        }
        function cleanupTestCase() {
            Source.snapshot.fixture = "";
        }

        function test_wrappedLineFillsRowByRow() {
            // 第 2 句从 10 秒唱到 18 秒：14 秒时唱了一半
            const page = createTemporaryObject(pageComponent, root, {
                track: { id: 903, name: "歌", artist: "歌手", cover: "", dur: 300 }, elapsed: 14 });
            const view = findChild(page, "lyrics");
            tryVerify(() => view.count > 3 && view.itemAtIndex(1) !== null);
            compare(page.lyricIndex, 1);
            fuzzyCompare(page.fill, 0.5, 0.01);

            const text = findChild(view.itemAtIndex(1), "lyricText");
            tryVerify(() => text.lineCount > 2, 2000, "这句在半屏里要折成至少三行，行数 " + text.lineCount);
            const rows = sungRows(text);
            compare(rows.length, text.lineCount, "每行一个填色裁剪");
            // 按行依次填：前面若干行整行，至多一行填一段，后面都空；填的总量是唱过的那一半
            const widths = rows.map(r => Math.round(r.width));
            // f = 整行，p = 一段，n = 空
            const state = rows.map(r => r.width >= r.modelData.width - 0.5 ? "f" : r.width > 0.5 ? "p" : "n").join("");
            verify(/^f+p?n+$/.test(state), "填色顺序：" + state + " " + widths);
            const total = rows.reduce((sum, r) => sum + r.modelData.width, 0);
            const filled = rows.reduce((sum, r) => sum + r.width, 0);
            fuzzyCompare(filled, total * page.fill, 1, "填了唱过的那一半");
            verify(rows[1].y > rows[0].y, "各行上下排开，不叠在第一行");

            // 换到下一句（单行）再换回来：只有当前句收集行，填色跟着当前句走
            page.elapsed = 22;
            tryCompare(page, "lyricIndex", 2);
            const next = findChild(view.itemAtIndex(2), "lyricText");
            tryVerify(() => sungRows(next).length === 1 && sungRows(next)[0].width > 0, 2000, "下一句填上了");
            compare(sungRows(text).length, 0, "上一句不再填色");
            page.elapsed = 14;
            tryCompare(page, "lyricIndex", 1);
            tryVerify(() => sungRows(text).length === text.lineCount, 2000, "回到长句，每行一个裁剪");
            fuzzyCompare(sungRows(text).reduce((sum, r) => sum + r.width, 0), total * page.fill, 1);
        }

        function sungRows(text) {
            const rows = [];
            for (let i = 0; i < text.children.length; ++i)
                if (text.children[i].objectName === "sungRow")
                    rows.push(text.children[i]);
            return rows;
        }
    }
}
