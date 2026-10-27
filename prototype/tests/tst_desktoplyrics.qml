// 桌面歌词是独立浮层：主窗口收起（迷你模式、托盘隐藏）时不能被窗口管理器一起藏掉，
// 锁定状态也不能在关掉再打开时自己丢掉（issue #4 / #5）。
import QtQuick
import QtQuick.Window
import QtTest
import ".."

Item {
    id: root
    width: 900
    height: 600

    Window {
        id: host
        width: 900
        height: 600
        visible: true

        DesktopLyrics {
            id: lyrics
            persist: false
            visible: true
            track: ({ id: 1, name: "晴天", artist: "周杰伦", dur: 269 })
            elapsed: 0
            playing: true
        }
    }

    TestCase {
        name: "DesktopLyrics"
        when: windowShown

        function test_notTransientForTheMainWindow() {
            // 嵌套 Window 默认会拿到 host 作为 transientParent；桌面歌词显式清掉它
            compare(lyrics.transientParent, null);
        }

        function test_lockSurvivesHiding() {
            lyrics.locked = true;
            lyrics.visible = false;
            wait(50);
            verify(lyrics.locked, "隐藏窗口不再把锁定状态清掉");
            lyrics.visible = true;
        }

        // 菜单里的圆点就是各自的颜色：Breeze 的 media-record-symbolic 写死红色，
        // 以前六种颜色都显示成红点（issue #6）
        function test_fillMenuDotsUseTheirOwnColors() {
            const items = findChild(lyrics, "fillItems");
            verify(items !== null);
            tryVerify(() => items.count === lyrics.fills.length, 1000);
            for (let i = 0; i < items.count; ++i) {
                const item = items.objectAt(i);
                verify(item !== null);
                const dot = item.contentItem.children[0];
                verify(dot !== null && dot.radius > 0, "圆点");
                verify(Qt.colorEqual(dot.color, lyrics.fills[i]), "第 " + i + " 个圆点用 fills[" + i + "]");
            }
        }
    }
}
