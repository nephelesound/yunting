// 交互测试（qmltestrunner）：截图看不到悬停与点击，这里用真实的鼠标事件验证。
// 运行：make qmltest
import QtQuick
import QtQuick.Controls
import QtTest
import ".."
import "../Format.js" as Format

Item {
    id: root
    width: 1000
    height: 700

    readonly property var track: ({ id: 900001, name: "晴天", artist: "周杰伦 / 费玉清", artistId: 7,
                                    artists: [{ id: 7, name: "周杰伦" }, { id: 8, name: "费玉清" }],
                                    album: "叶惠美", albumId: 70, dur: 269, fee: 0, cover: "" })

    Component { id: linkComponent; LinkText { font.pixelSize: Tokens.textSm } }
    Component { id: tileComponent; TrackTile { width: 900 } }
    Component { id: nowPlayingComponent; NowPlayingPage { width: root.width; height: root.height } }
    Component { id: spyComponent; SignalSpy {} }
    Component { id: queueComponent; QueuePanel {} }
    // 分页列表：与评论区、歌手页同一种接法（GrowingModel + values[index]）
    Component {
        id: pagedComponent
        ListView {
            id: paged
            property var items: []
            width: 300
            height: 200
            model: GrowingModel { values: paged.items }
            delegate: Text {
                required property int index
                width: 300
                height: 20
                text: (paged.items[index] || { name: "" }).name
            }
        }
    }
    Component { id: lyricsComponent; DesktopLyrics {} }
    // 假的 layer-shell：只记下最后一次设的锚定边与边距，并数一共请求了几次
    QtObject {
        id: fakeLayerShell
        property var margins: null
        property int anchors: 0
        property int placements: 0
        function makeOverlay(window, anchors, margin, keyboard, scope) {}
        function placeOverlay(window, anchors, left, top, right, bottom) {
            fakeLayerShell.anchors = anchors;
            fakeLayerShell.margins = [left, top, right, bottom];
            fakeLayerShell.placements++;
        }
    }
    TextEdit { id: pasteTarget; visible: false }

    SignalSpy { id: artistSpy; target: Actions; signalName: "artistRequested" }
    SignalSpy { id: likeSpy; target: Actions; signalName: "likeToggleRequested" }

    TestCase {
        name: "Interaction"
        when: windowShown

        // 数据用快照（与截图 harness 同一份），歌词页才有歌词可点
        function initTestCase() {
            Source.snapshot = Qt.createComponent("../Snapshot.qml").createObject(root);
        }

        function init() {
            artistSpy.clear();
            likeSpy.clear();
        }

        function test_linkTextHoversAndOpensThePartUnderThePointer() {
            const link = createTemporaryObject(linkComponent, root, { parts: Format.artistLinks(root.track) });
            mouseMove(link, link.width - 2, link.height / 2);
            compare(link.hoveredPart, 1);
            verify(link.text.indexOf("<u>") > 0, "只有指针下的那位歌手加下划线");
            mouseClick(link, link.width - 2, link.height / 2);
            compare(artistSpy.count, 1);
            compare(artistSpy.signalArguments[0][0], 8);
        }

        function test_partWithoutIdIsPlainText() {
            const link = createTemporaryObject(linkComponent, root, { parts: [{ text: "群星", artistId: 0 }] });
            mouseMove(link, 2, link.height / 2);
            compare(link.hoveredPart, -1);
            compare(link.text.indexOf("<u>"), -1);
            mouseClick(link, 2, link.height / 2);
            compare(artistSpy.count, 0);
        }

        function test_artistLinkInARowDoesNotPlayTheRow() {
            const tile = createTemporaryObject(tileComponent, root, { index: 0, modelData: root.track });
            const played = createTemporaryObject(spyComponent, root, { target: tile, signalName: "activated" });
            const artist = findChild(tile, "artistLinks");
            // 不先移动指针：按下点本身决定是不是点在链接上
            mouseClick(artist, 2, artist.height / 2);
            compare(artistSpy.count, 1);
            compare(artistSpy.signalArguments[0][0], 7);
            compare(played.count, 0, "点歌手名只去歌手页，不播放这一行");
            mouseMove(artist, 2, artist.height / 2);
            verify(tile.hovered, "指针在歌手名上时整行仍是悬停态（喜欢、更多仍显示）");
            // 歌手名右边的空白仍属于行：点了播放（隔开双击间隔，否则第二下算双击）
            wait(Qt.styleHints.mouseDoubleClickInterval + 50);
            mouseClick(artist, artist.width - 2, artist.height / 2);
            compare(played.count, 1);
        }

        function test_likeColumnAppearsOnHoverAndToggles() {
            const tile = createTemporaryObject(tileComponent, root, { index: 0, modelData: root.track });
            const like = findChild(tile, "likeButton");
            verify(!like.visible, "未喜欢的歌静止时不显示空心");
            mouseMove(tile, tile.width / 2, tile.height / 2);
            tryVerify(() => like.visible);
            mouseClick(like);
            compare(likeSpy.count, 1);
            compare(likeSpy.signalArguments[0][0].id, root.track.id);
        }

        // 队列变化只动变的那一行：移出、加入、切到下一首时，其余行是同一个对象（没有重建，所以不跳）
        function test_queueChangesKeepOtherRows() {
            const song = (id, name) => ({ id: id, name: name, artist: "歌手", album: "", dur: 200, fee: 0, cover: "" });
            const a = song(1, "A"), b = song(2, "B"), c = song(3, "C"), d = song(4, "D"), x = song(5, "X");
            const panel = createTemporaryObject(queueComponent, root, { queue: [a, b, c, d], currentIndex: 0 });
            panel.open();
            const list = findChild(panel, "queueList");
            tryVerify(() => list.count === 3 && list.itemAtIndex(2) !== null);
            const names = () => { const out = []; for (let i = 0; i < list.count; i++) { const it = list.itemAtIndex(i); out.push(it ? it.track.name : "?"); } return out.join(""); };
            compare(names(), "BCD");
            const rowB = list.itemAtIndex(0), rowD = list.itemAtIndex(2);

            panel.queue = [a, b, d];                       // 移出 C（淡出期间它还在，等过渡结束）
            compare(list.count, 2);
            tryVerify(() => names() === "BD");
            verify(list.itemAtIndex(0) === rowB && list.itemAtIndex(1) === rowD, "移出一首后其余行原地保留");

            panel.queue = [a, b, x, d];                    // 在 B 后加入 X
            tryVerify(() => names() === "BXD");
            verify(list.itemAtIndex(0) === rowB && list.itemAtIndex(2) === rowD, "加入一首后其余行原地保留");

            panel.currentIndex = 1;                        // 切到下一首：B 离开「接下来」
            tryVerify(() => names() === "XD");
            verify(list.itemAtIndex(1) === rowD, "切歌后其余行原地保留");
        }

        // 桌面歌词：当前句按进度填色；播放时在两次引擎进度之间也连续前进；位置夹在屏幕内
        function desktopLyrics(props) {
            const track = { id: Source.d.lyricsTrackId, name: "歌", artist: "歌手", cover: "", dur: 300 };
            const w = createTemporaryObject(lyricsComponent, root, Object.assign({ track: track, layerShell: fakeLayerShell }, props));
            tryVerify(() => w.lines.length > 3);
            return w;
        }

        function test_desktopLyricsFillFollowsTheLine() {
            const probe = desktopLyrics({});
            const lines = probe.lines;
            const mid = (lines[2].t + lines[3].t) / 2;
            const w = desktopLyrics({ elapsed: mid, playing: false });
            compare(w.index, 2);
            fuzzyCompare(w.fill, 0.5, 0.01);
        }

        function test_desktopLyricsFillMovesBetweenEngineUpdates() {
            const probe = desktopLyrics({});
            const w = desktopLyrics({ elapsed: probe.lines[2].t, playing: false });
            w.visible = true;
            const before = w.fill;
            w.playing = true;                 // 引擎进度不变，逐帧时钟照样往前走
            tryVerify(() => w.fill > before + 0.01, 2000);
            w.playing = false;                // 暂停：回到引擎的值
            fuzzyCompare(w.clock, w.elapsed, 0.001);
        }

        function test_desktopLyricsPlacementStaysOnScreen() {
            const w = desktopLyrics({});
            w.placeDefault();                 // 默认：水平居中
            fuzzyCompare(w.left, (w.screenWidth - w.width) / 2, 1);
            w.place(-5000, 99999);            // 拖出屏幕左上：夹回来，不是回到居中
            compare(w.left, 0);
            compare(w.bottomGap, w.screenHeight - w.height);
            compare(fakeLayerShell.margins, [0, 0, 0, w.screenHeight - w.height]);
        }

        // 拖动：按下时窗口铺满屏幕（一次请求），拖动中只在窗口里挪歌词、不再打扰合成器，松手时缩回并落到新位置（一次请求）
        function test_desktopLyricsDragDoesNotMoveTheSurface() {
            const w = desktopLyrics({});
            w.visible = true;
            w.placeDefault();
            const normalWidth = w.width;
            w.beginDrag(Qt.point(10, 10));
            compare(w.width, w.screenWidth);
            compare(fakeLayerShell.anchors, 1 | 2 | 4 | 8);
            tryVerify(() => w.expandedReady);
            const before = fakeLayerShell.placements;
            const target = Math.floor((w.screenWidth - normalWidth) / 2);   // 屏幕内的一个横向位置
            for (let x = 10; x <= 10 + target; x += 4)
                w.dragTo(Qt.point(x, 200));
            compare(fakeLayerShell.placements, before, "拖动中不请求合成器挪窗口");
            compare(w.left, target);
            w.endDrag();
            compare(w.width, normalWidth);
            compare(fakeLayerShell.anchors, 2 | 4);
            compare(fakeLayerShell.margins, [target, 0, 0, w.screenHeight - (200 - 10) - w.height]);
        }

        // 滚到底续取下一页：位置不跳回顶部，已有的行不重建；状态变化（同样内容的新数组）不动；换内容才重建
        function test_pagingKeepsScrollPosition() {
            const page = n => Array.from({ length: n }, (_, i) => ({ id: i + 1, name: "第 " + (i + 1) + " 条" }));
            const view = createTemporaryObject(pagedComponent, root, { items: page(30) });
            compare(view.count, 30);
            view.positionViewAtEnd();
            const bottom = view.contentY;
            verify(bottom > 0);
            const firstRow = view.itemAtIndex(29);

            view.items = page(60);                       // 下一页到了
            compare(view.count, 60);
            compare(view.contentY, bottom, "不跳回顶部");
            verify(view.itemAtIndex(29) === firstRow, "已有的行原地保留");

            view.items = page(60);                       // 只是「加载中」之类的状态变了：内容相同
            compare(view.contentY, bottom);
            verify(view.itemAtIndex(29) === firstRow);

            view.items = [{ id: 999, name: "另一首歌的评论" }];   // 换了歌：重建
            compare(view.count, 1);
            tryVerify(() => view.itemAtIndex(0) !== null && view.itemAtIndex(0).text === "另一首歌的评论");
        }

        function lyricsPage() {
            const page = createTemporaryObject(nowPlayingComponent, root, {
                track: { id: Source.d.lyricsTrackId, name: "歌", artist: "歌手", cover: "", dur: 300 }, elapsed: 0 });
            const view = findChild(page, "lyrics");
            tryVerify(() => view.count > 3 && view.height > 0);
            wait(50);   // 初次定位在 callLater 里
            return { page: page, view: view };
        }

        function test_wheelBrowsesThenFollowResumes() {
            const p = lyricsPage();
            const before = p.view.contentY;
            mouseWheel(p.view, p.view.width / 2, p.view.height / 2, 0, -360);
            verify(p.view.browsing, "滚轮之后暂停跟随");
            tryVerify(() => p.view.contentY !== before);
            tryVerify(() => !p.view.browsing, 5000, "3 秒不动后回到当前句");
            tryCompare(p.view, "contentY", before, 2000);
        }

        function test_clickALineSeeksToIt() {
            const p = lyricsPage();
            const seeks = createTemporaryObject(spyComponent, root, { target: p.page, signalName: "seekRequested" });
            const line = p.view.itemAtIndex(2);
            verify(line !== null);
            mouseClick(line, line.width / 4, line.height / 2);
            compare(seeks.count, 1);
            compare(seeks.signalArguments[0][0], p.page.lines[2].t);
        }

        function test_copyALine() {
            const p = lyricsPage();
            const line = p.view.itemAtIndex(1);
            mouseClick(line, line.width / 4, line.height / 2, Qt.RightButton);
            const copy = findChild(p.page, "copyLine");
            verify(copy !== null);
            tryVerify(() => copy.menu.opened, 1000, "右键出菜单");
            copy.triggered();
            pasteTarget.text = "";
            pasteTarget.paste();
            verify(pasteTarget.text.indexOf(p.page.lines[1].text) === 0);
        }
    }
}
