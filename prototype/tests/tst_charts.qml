// 排行榜：上带的高度必须装得下网易实际给的全部榜单卡，否则多出的一行压到热歌榜上；一屏放不下时要滚得到（#10）
import QtQuick
import QtTest
import ".."

Item {
    id: root
    width: 1400
    height: 900

    Component { id: snapshotComponent; Snapshot {} }

    TestCase {
        name: "Charts"
        when: windowShown

        function initTestCase() {
            Source.snapshot = snapshotComponent.createObject(root, { fixture: "worst" });
        }
        function cleanupTestCase() {
            Source.snapshot.fixture = "";
        }

        function test_bandHoldsEveryChart_data() {
            return [{ tag: "wide", width: 1400, height: 900 }, { tag: "narrow", width: 700, height: 900 },
                    { tag: "short", width: 700, height: 500 }];
        }
        function test_bandHoldsEveryChart(data) {
            const page = createTemporaryObject(Qt.createComponent("../ChartsPage.qml"), root,
                                               { width: data.width, height: data.height });
            verify(page.official.length > 4 && page.others.length > 8, "the worst fixture overflows the old fixed rows");
            const band = findChild(page, "chartsBand");
            const hot = findChild(page, "hotBand");
            waitForRendering(page);     // 官方榜的列数在页宽定下后才变，等布局走完一轮再量
            // 热歌榜的行数按 bandHeight 预留的空间算：少算了，热歌榜就被挤上来压住最后一行榜单卡
            for (let i = 0; i < band.children.length; ++i) {
                const column = band.children[i];
                verify(column.implicitHeight <= page.headerHeight + page.bandHeight,
                       data.tag + " column " + i + ": " + column.implicitHeight + " > " + (page.headerHeight + page.bandHeight));
                // 一屏放不下就滚：最后一张卡也要滚得到
                verify(page.scroller.contentHeight >= band.y + column.implicitHeight, data.tag + ": column " + i + " unreachable");
                if (hot.visible)
                    verify(hot.y >= band.y + column.implicitHeight, data.tag + ": hot chart overlaps column " + i);
            }
        }
    }
}
