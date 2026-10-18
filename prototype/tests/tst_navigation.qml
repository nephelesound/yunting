// 导航状态机的核心约束：同一路由重复打开不能增长历史；切换路由后返回只退一层。
import QtQuick
import QtTest
import ".."

Item {
    id: root
    width: 800
    height: 600

    Component { id: snapshotComponent; Snapshot {} }

    TestCase {
        name: "Navigation"
        when: windowShown

        function initTestCase() {
            Source.snapshot = snapshotComponent.createObject(root);
        }

        function makeWindow() {
            const component = Qt.createComponent("../main.qml");
            verify(component.status === Component.Ready, component.errorString());
            const window = component.createObject(root, { backend: null });
            verify(window !== null);
            return window;
        }

        function test_repeated_same_route_does_not_grow_history() {
            const window = makeWindow();
            window.history = [];
            window.page = "user";
            window.userId = 42;
            window.open("user", "", { userId: 42 });
            window.open("user", "", { userId: 42 });
            window.open("user", "", { userId: 42 });
            compare(window.history.length, 0);
            compare(window.page, "user");
            compare(window.userId, 42);
            window.close();
        }


        function test_history_is_capped() {
            const window = makeWindow();
            window.page = "list";
            window.listKey = "route:0";
            for (let i = 1; i <= 100; i++)
                window.open("list", "route:" + i);
            compare(window.history.length, window.maxHistory);
            compare(window.listKey, "route:100");
            window.close();
        }

        // 页面持有进入时的路由参数；退栈时 root 参数归零不能把页面重新指向 0 号用户。
        function test_pageOwnsRouteParams() {
            const window = makeWindow();
            window.open("user", "", { userId: 42 });
            tryVerify(() => window.pageStack.currentItem !== null && window.page === "user");
            const page = window.pageStack.currentItem;
            compare(page.userId, 42);
            window.userId = 0;
            compare(page.userId, 42, "page keeps its own userId");
            window.close();
        }

        // 从一个用户的关注 / 粉丝点进另一个用户：页名不变，页面也要换成那个人的，返回时换回来
        function test_switching_user_replaces_content_not_history_depth() {
            const window = makeWindow();
            window.open("user", "", { userId: 42 });
            window.history = [];
            window.open("user", "", { userId: 7 });
            compare(window.userId, 7);
            compare(window.page, "user");
            compare(window.history.length, 1, "换用户只压入一层来路");
            tryCompare(window.pageStack.currentItem, "userId", 7);
            window.goBack();
            tryCompare(window.pageStack.currentItem, "userId", 42);
            window.close();
        }

        function test_switching_artist_shows_the_new_artist() {
            const window = makeWindow();
            window.open("artist", "artist:6452");
            window.open("artist", "artist:3684");
            tryCompare(window.pageStack.currentItem, "artistId", 3684);
            window.goBack();
            tryCompare(window.pageStack.currentItem, "artistId", 6452);
            window.close();
        }
    }
}
