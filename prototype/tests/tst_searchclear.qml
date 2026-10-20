// 搜索框的清除按钮只清输入与结果。它以前顺手发 accepted()，撞上「空着回车＝搜推荐词」：
// 一按清除，输入框就被热搜词填回去并立刻发起搜索（issue #2）。
// 回车搜推荐词本身是想要的行为，这里一并守住。
import QtQuick
import QtTest
import ".."

Item {
    id: root
    width: 1000
    height: 700

    Component {
        id: searchComponent
        SearchPage { width: root.width; height: root.height }
    }

    // SearchField 没有 objectName，按它独有的属性找
    function searchField(item) {
        if (item.rightActions !== undefined && item.placeholderText !== undefined)
            return item;
        const kids = item.children || [];
        for (let i = 0; i < kids.length; ++i) {
            const found = searchField(kids[i]);
            if (found)
                return found;
        }
        return null;
    }

    TestCase {
        name: "SearchClear"
        when: windowShown

        property var saved: null

        function initTestCase() {
            saved = Source.snapshot;
            Source.snapshot = Qt.createComponent("../Snapshot.qml").createObject(root);
            Source.snapshot.data.defaultSearch = { showKeyword: "推荐热搜", realKeyword: "推荐热搜" };
        }

        function cleanupTestCase() {
            Source.snapshot = saved;
        }

        function test_clearKeepsTheFieldEmpty() {
            const page = createTemporaryObject(searchComponent, root, { query: "晴天" });
            verify(page);
            const field = searchField(page);
            verify(field, "搜索框");
            compare(field.text, "晴天");

            const clear = field.rightActions[0];
            verify(clear, "清除按钮");
            verify(clear.visible);
            clear.trigger();

            compare(field.text, "", "清除按钮不能再把推荐词填回输入框");
            compare(page.query, "", "清除后回到热门搜索");
        }

        // 空着回车仍按网易推荐的词搜（与网易云客户端一样）
        function test_enterOnEmptySearchesTheSuggestion() {
            const page = createTemporaryObject(searchComponent, root, { query: "" });
            verify(page);
            const field = searchField(page);
            verify(field, "搜索框");

            field.forceActiveFocus();
            field.accepted();

            compare(field.text, "推荐热搜");
            compare(page.query, "推荐热搜");
        }
    }
}
