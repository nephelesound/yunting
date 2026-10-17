// 返回一页时回到离开时的样子：滚动位置、枢轴、搜索词（页面每次导航都重建，状态记在来路与 pivotViews 里）
import QtQuick
import QtTest
import ".."

Item {
    id: root
    width: 1280
    height: 800

    Component { id: snapshotComponent; Snapshot {} }

    TestCase {
        name: "Returning"
        when: windowShown

        function initTestCase() {
            Source.snapshot = snapshotComponent.createObject(root);
        }

        function makeWindow() {
            const component = Qt.createComponent("../main.qml");
            verify(component.status === Component.Ready, component.errorString());
            const window = component.createObject(root, { backend: null, width: 1280, height: 800, visible: true });
            verify(window !== null);
            return window;
        }
        function frame(window) { return window.pageStack.currentItem as ScreenFrame; }
        function offset(f) { return f.contentY - f.originY; }

        // 榜单（快照里 200 首）往下滚，进歌手页再返回：回到原来的位置
        function test_back_restores_the_scroll_position() {
            const window = makeWindow();
            window.open("list", "hot");
            const list = frame(window).scroller;
            verify(list !== null);
            tryVerify(() => list.contentHeight > list.height + 1500, 5000, "曲目足够长");
            list.contentY = list.originY + 1200;
            window.open("artist", "artist:1");
            compare(window.page, "artist");
            window.goBack();
            compare(window.page, "list");
            tryVerify(() => frame(window).scroller !== null && Math.abs(offset(frame(window).scroller) - 1200) < 1, 5000,
                      "返回后滚动位置是离开时的 1200");
            window.close();
        }

        // 顶层枢轴来回切：搜索词还在
        function test_pivot_round_trip_keeps_the_search_query() {
            const window = makeWindow();
            window.switchPivot("search");
            frame(window).query = "晴天";
            window.switchPivot("home");
            compare(window.page, "home");
            window.switchPivot("search");
            compare(frame(window).query, "晴天");
            window.close();
        }

        // 用户先动了滚动，就不再跳到旧位置
        function test_user_scroll_cancels_a_pending_restore() {
            const window = makeWindow();
            window.open("list", "hot");
            const f = frame(window);
            f.restoreView({ props: {}, y: 999999 });   // 远超内容高度：等内容到
            verify(f.pendingY > 0);
            f.scroller.movementStarted();
            compare(f.pendingY, -1);
            window.close();
        }
    }
}
