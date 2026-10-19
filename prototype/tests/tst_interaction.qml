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
    Component { id: inboxComponent; InboxPage { width: root.width; height: root.height } }
    Component { id: spyComponent; SignalSpy {} }
    Component { id: queueComponent; QueuePanel {} }
    Component { id: roomComponent; SimListenTogether {} }
    Component { id: barComponent; PlayerBar { width: root.width } }
    Component { id: listPageComponent; PlaylistPage { width: root.width; height: root.height } }
    // 定位按钮：与歌单页、歌手页同一种接法（LocateCurrent 挂在 ListView 上）
    Component {
        id: locateComponent
        ListView {
            id: longList
            width: 400
            height: 300
            model: 200
            delegate: Text {
                required property int index
                width: 400
                height: 30
                text: "第 " + index + " 行"
            }
            LocateCurrent {
                objectName: "locate"
                parent: longList
                view: longList
                index: 150
            }
        }
    }
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
    Component { id: trackMenuComponent; TrackMenu {} }
    Component { id: shareTargetsComponent; ShareTargets {} }
    Component { id: commentRowComponent; CommentRow { width: 600 } }
    Component { id: signalSpyComponent; SignalSpy {} }
    // 假的 layer-shell：记下最后一次设的锚定边与边距；相对拖动由测试直接发 relativeMotion
    QtObject {
        id: fakeLayerShell
        property var margins: null
        property int anchors: 0
        property bool relativeDrag: false
        signal relativeMotion(real dx, real dy)
        function makeOverlay(window, anchors, margin, keyboard, scope) {}
        function placeOverlay(window, anchors, left, top, right, bottom) {
            fakeLayerShell.anchors = anchors;
            fakeLayerShell.margins = [left, top, right, bottom];
        }
        function beginRelativeDrag() { fakeLayerShell.relativeDrag = true; return true; }
        function endRelativeDrag() { fakeLayerShell.relativeDrag = false; }
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

        // 拖进度条：拖动中进度线跟着手、不跳转（逐步跳转会解出一串碎片，听起来是噪音）；松手只跳一次
        function test_scrubbingSeeksOnceOnRelease() {
            const bar = createTemporaryObject(barComponent, root, { track: root.track, elapsed: 10 });
            const seeks = createTemporaryObject(spyComponent, root, { target: bar, signalName: "seekRequested" });
            const area = findChild(bar, "progressArea");
            const y = area.height / 2;
            mousePress(area, area.width * 0.2, y);
            mouseMove(area, area.width * 0.4, y);
            mouseMove(area, area.width * 0.6, y);
            compare(seeks.count, 0, "拖动中不跳转");
            fuzzyCompare(bar.shownElapsed, root.track.dur * 0.6, 1);
            mouseRelease(area, area.width * 0.6, y);
            compare(seeks.count, 1);
            fuzzyCompare(seeks.signalArguments[0][0], root.track.dur * 0.6, 1);
            compare(bar.shownElapsed, 10, "松手后回到播放位置（由引擎更新）");
        }

        // 一起听的「结束」：好友在时在原地问一句（不另开一层弹窗、下面的队列不跳），取消或关上抽屉都不结束；
        // 还没人来、或好友自己退出，点了就走
        function listenPanel(state, host) {
            const room = createTemporaryObject(roomComponent, root, { state: state, host: host });
            const panel = createTemporaryObject(queueComponent, root, { room: room, queue: [], currentIndex: -1 });
            panel.listenEndRequested.connect(room.leaveRoom);
            panel.open();
            const end = findChild(panel, "listenEnd");
            tryVerify(() => end.visible && end.width > 0);
            return { room: room, panel: panel, end: end };
        }

        function test_hostEndAsksInPlaceWhileFriendIsThere() {
            const p = listenPanel("together", true);
            const slot = findChild(p.panel, "listenHintSlot");
            const height = slot.height;
            mouseClick(p.end);
            verify(p.room.inRoom, "第一次点「结束」只是问一句");
            compare(slot.height, height, "确认与说明共用一格，下面的队列不跳");
            const cancel = findChild(p.panel, "listenEndCancel");
            tryVerify(() => cancel.visible && cancel.opacity === 1);
            mouseClick(cancel);
            verify(!p.panel.confirmingEnd);
            verify(p.room.inRoom);

            tryVerify(() => p.end.enabled && p.end.opacity === 1);
            mouseClick(p.end);
            const confirm = findChild(p.panel, "listenEndConfirm");
            tryVerify(() => confirm.visible && confirm.opacity === 1);
            mouseClick(confirm);
            verify(!p.room.inRoom);
        }

        function test_closingTheQueueCancelsTheEndQuestion() {
            const p = listenPanel("together", true);
            mouseClick(p.end);
            verify(p.panel.confirmingEnd);
            p.panel.close();
            tryVerify(() => !p.panel.confirmingEnd);
            verify(p.room.inRoom);
        }

        function test_endWithoutFriendOrAsGuestLeavesAtOnce() {
            const waiting = listenPanel("waiting", true);
            mouseClick(waiting.end);
            verify(!waiting.room.inRoom, "还没人来：没人会被影响，不问");
            const guest = listenPanel("together", false);
            mouseClick(guest.end);
            verify(!guest.room.inRoom, "好友退出只是自己离开，不问");
        }

        // 桌面歌词：当前句按进度填色；播放时在两次引擎进度之间也连续前进；位置夹在屏幕内
        // 快照里带歌词的那首（歌词按歌存：data.lyrics[<songId>]）
        function lyricTrackId() { return Number(Object.keys(Source.d.lyrics)[0]); }

        function desktopLyrics(props) {
            const track = { id: lyricTrackId(), name: "歌", artist: "歌手", cover: "", dur: 300 };
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

        // 拖动：窗口尺寸始终不变，边距按指针的相对位移走（零点几像素的慢拖动也累积起来），松手停止订阅
        function test_desktopLyricsDragFollowsRelativeMotion() {
            const w = desktopLyrics({});
            w.visible = true;
            verify(w.maxLeft >= 16 && w.maxBottomGap >= 214);
            w.place(0, 200);
            const size = Qt.size(w.width, w.height);
            verify(w.beginDrag(Qt.point(6, -4)));   // 越过拖动阈值前走过的距离
            verify(fakeLayerShell.relativeDrag);
            compare(fakeLayerShell.margins, [6, 0, 0, 204]);
            for (let i = 0; i < 40; ++i)
                fakeLayerShell.relativeMotion(0.25, -0.25);
            compare(fakeLayerShell.margins, [16, 0, 0, 214]);
            fakeLayerShell.relativeMotion(-99999, 0);   // 推出屏幕左缘再拉回：从边上立刻往回走
            fakeLayerShell.relativeMotion(3, 0);
            compare(w.left, 3);
            compare(Qt.size(w.width, w.height), size, "拖动中窗口不改尺寸");
            compare(fakeLayerShell.anchors, 2 | 4);
            w.endDrag();
            verify(!fakeLayerShell.relativeDrag);
            fakeLayerShell.relativeMotion(50, 0);       // 松手后的位移不再挪窗口
            compare(w.left, 3);
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

        // 正在播放的那一行滚出视野才出现定位按钮；点一下回到那一行，按钮随之消失
        // 导航：从一个用户进另一个用户再返回，回到的是来的那一个；返回键的字从不为空；
        // 重复打开同一页不多一格来路；没有来路的子页返回到推荐
        function test_navigationReturnsWhereItCameFrom() {
            const win = createTemporaryObject(Qt.createComponent("../main.qml"), root, { visible: false });
            verify(win !== null);
            win.open("user", "", { userId: 11 });
            win.open("user", "", { userId: 22 });   // 在 11 的粉丝里点了 22
            compare(win.history.length, 2);
            verify(win.parentName.length > 0, "返回键总有字");
            win.open("user", "", { userId: 22 });   // 再点一次同一个人
            compare(win.history.length, 2, "同一页不重复入栈");
            win.goBack();
            compare(win.page, "user");
            compare(win.userId, 11, "返回到来的那个人，而不是停在当前这个人");
            win.goBack();
            compare(win.page, "home");
            win.open("mv", "", { mvId: 5 });
            win.history = [];
            win.goBack();
            compare(win.page, "home", "没有来路时返回推荐");
        }

        // 当前句的填色：逐字歌词按字推进（唱完的字 + 正在唱的字唱了多少），否则按句子时间比例
        function test_lineFillFollowsWords() {
            const line = { t: 1, text: "abcd", words: [{ t: 1, d: 1, text: "ab" }, { t: 2, d: 2, text: "cd" }] };
            compare(Source.lineFill(line, null, 0, 0.5), 0);
            fuzzyCompare(Source.lineFill(line, null, 0, 1.5), 0.25, 0.001);   // 第一个词唱了一半：1/4
            fuzzyCompare(Source.lineFill(line, null, 0, 3), 0.75, 0.001);     // 第一个词唱完 + 第二个词一半
            compare(Source.lineFill(line, null, 0, 9), 1);
            fuzzyCompare(Source.lineFill({ t: 10, text: "x" }, { t: 20 }, 0, 15), 0.5, 0.001);
        }

        // 关注/粉丝被对方隐私挡住：页面拿到 restricted，不再显示重试或当网络错误
        function test_peoplePrivacyIsInlineState() {
            Source.markRequestRestricted("userFolloweds:4242", "用户隐私无权限查看");
            const people = Source.people("userFolloweds", 4242);
            verify(people.restricted === true);
            verify(people.pending === false);
            verify(people.failed === false);
            compare(people.restrictedMessage, "用户隐私无权限查看");
        }

        // 用户粉丝被隐私挡住时，UserPage 在自己的占位区说明，不给重试按钮。
        function test_userPageShowsPrivacyInline() {
            Source.markRequestRestricted("userFolloweds:4242", "用户隐私无权限查看");
            const page = createTemporaryObject(Qt.createComponent("../UserPage.qml"), root, {
                userId: 4242, width: root.width, height: root.height
            });
            verify(page !== null);
            page.pane = "fans";
            // 关注与粉丝各有一条说明（Repeater 的两个网格），要找粉丝那条
            const message = findChild(page, "peopleMessage:fans");
            verify(message !== null);
            compare(message.restricted, true);
            compare(message.failed, false);
            verify(message.text.indexOf("隐私") >= 0);
        }

        // 退栈时 id 会短暂变成 0：Source 这一层就不能把 user:0 / artist:0 发给后端。
        function test_zeroIdsDoNotRequest() {
            const saved = Source.live;
            const calls = [];
            Source.live = { request: key => calls.push(key) };
            Source.user(0);
            Source.artist(0);
            Source.album(0);
            Source.mv(0);
            Source.list("playlist:0");
            Source.live = saved;
            compare(calls.length, 0);
        }

        // 分页列表：没取过就要第一页；下一页的 key 是第一页的 key 加上 Store 给的 next
        function test_pagedListsAskForFirstThenNextPage() {
            const saved = Source.live;
            const calls = [];
            Source.live = { data: { highquality: { cat: "全部", more: true, next: 777, items: [{ id: 1 }] } },
                            personal: {}, request: key => calls.push(key) };
            const list = Source.highquality("全部");
            compare(list.items.length, 1);
            Source.morePaged(list);
            Source.saved("artists");
            tryVerify(() => calls.indexOf("discover:highquality:全部:777") >= 0, 1000, "next page key");
            tryVerify(() => calls.indexOf("library:artists") >= 0, 1000, "first page of an empty slot");
            Source.live = saved;
        }

        // 动态、通知里提到的歌只有 id 时也能放：补成最小的曲目交给播放器；视频没有页可开
        function test_mentionedSongPlaysFromItsId() {
            const spy = spyComponent.createObject(root, { target: Actions, signalName: "playRequested" });
            verify(Actions.canOpen({ kind: "song", id: 5, name: "歌", artist: "人" }));
            verify(!Actions.canOpen({ kind: "video", id: 0, vid: "A1" }));
            Actions.openResource({ kind: "song", id: 5, name: "歌", artist: "人", cover: "" });
            compare(spy.count, 1);
            const track = spy.signalArguments[0][0][0];
            compare(track.id, 5);
            compare(track.name, "歌");
            compare(track.artist, "人");
            spy.destroy();
        }

        // 评论点赞从页面转发到后端时，参数不能丢（线程种类与 id、commentId、目标状态）：歌单评论也能赞。
        function test_commentLikeForwardsToLive() {
            const calls = [];
            const saved = Source.live;
            Source.live = {
                setCommentLiked: function(kind, ownerId, commentId, liked) {
                    calls.push([kind, ownerId, commentId, liked]);
                }
            };
            Source.setCommentLiked(Source.commentThread("playlist", 24381616), 9667509659, true);
            Source.live = saved;
            compare(calls, [["playlist", 24381616, 9667509659, true]]);
        }

        // 专辑页：没登录也能分享（专辑的 id 在 albumId）；长简介被截断时给「展开全文」，对话框里是完整的一段
        function test_albumPageSharesAndShowsTheFullDescription() {
            const description = "这张专辑收录了十首歌。".repeat(40);
            const page = createTemporaryObject(listPageComponent, root, {
                loggedIn: false,
                list: { heading: "专辑名", cover: "", facts: [], description: description, tracks: [],
                        isAlbum: true, albumId: 32311, listId: 0 } });
            const share = findChild(page, "shareList");
            verify(share.visible, "专辑也能分享，不用登录");
            const spy = createTemporaryObject(spyComponent, root, { target: page, signalName: "shareRequested" });
            share.clicked();
            compare(spy.count, 1);
            compare(spy.signalArguments[0][0], "album");
            compare(spy.signalArguments[0][1], 32311);

            const more = findChild(page, "blurbMore");
            tryVerify(() => more.visible, 1000, "截断了才给「展开全文」");
            more.activated(0);
            const dialog = findChild(page, "blurbDialog");
            tryVerify(() => dialog.opened);
            compare(dialog.message, description);
            dialog.close();
        }

        // 左窗格只有 280 宽：歌单与专辑页各种登录 / 收藏 / 排序状态下，动作排都放得下，不伸出窗格
        function test_listActionsFitThePane() {
            const tracks = [root.track, Object.assign({}, root.track, { id: 900002 })];
            const cases = [
                { loggedIn: true, list: { isAlbum: true, albumId: 1, listId: 0, subscribed: false } },
                { loggedIn: true, list: { isAlbum: true, albumId: 1, listId: 0, subscribed: true } },
                { loggedIn: false, list: { isAlbum: true, albumId: 1, listId: 0, subscribed: false } },
                { loggedIn: true, list: { listId: 2, owned: true } },
                { loggedIn: true, list: { listId: 2, owned: true }, reordering: true },
                { loggedIn: true, list: { listId: 2, subscribed: true } },
                { loggedIn: false, list: { listId: 2 } },
            ];
            const tooWide = [];
            for (const c of cases) {
                const list = Object.assign({ heading: "名", cover: "", facts: [], description: "", tracks: tracks }, c.list);
                const page = createTemporaryObject(listPageComponent, root,
                    { loggedIn: c.loggedIn, list: list, reordering: c.reordering || false });
                const row = findChild(page, "listActions");
                if (row.implicitWidth > Tokens.trackColumn)
                    tooWide.push(JSON.stringify(c) + " 宽 " + row.implicitWidth);
            }
            compare(tooWide, []);
        }

        // 在途的下载只看 DownloadManager.active：点了就算在下（不等进度），在下时再点不重复下，
        // 取消后 active 里那条消失就回到「未下载」，不留页面自己的一份。
        function test_downloadStateFollowsManagerActive() {
            const manager = Qt.createQmlObject(`import QtQuick
                QtObject {
                    property var downloads: []
                    property var active: []
                    property var started: []
                    signal finished(real songId, string path)
                    signal failed(real songId, string message)
                    function downloadTrack(track, level) {
                        started = started.concat([track.id]);
                        active = active.concat([{ id: track.id, name: track.name, artist: "", progress: 0, level: level }]);
                    }
                    function cancel(songId) { active = active.filter(a => a.id !== songId); }
                }`, root);
            const saved = Source.downloads;
            Source.downloads = manager;
            Source.download({ id: 77, name: "歌" }, "lossless");
            const afterStart = Source.downloadState(77);
            Source.download({ id: 77, name: "歌" }, "lossless");
            manager.active = [{ id: 77, name: "歌", artist: "", progress: 0.5, level: "lossless" }];
            const progress = Source.downloading[77];
            Source.cancelDownload(77);
            const afterCancel = Source.downloadState(77);
            Source.downloads = saved;
            compare(afterStart, "active");
            compare(manager.started, [77]);
            compare(progress, 0.5);
            compare(afterCancel, "none");
            manager.destroy();
        }

        // 评论行：「N 条回复」展开楼中楼；楼中楼里回复的正是父评论时不再引一遍；长评论折起后可展开
        function test_commentRowRepliesQuoteAndExpand() {
            const quoteOfParent = [{ id: 1, user: "甲", userId: 5, content: "原话" }];
            const row = createTemporaryObject(commentRowComponent, root, {
                comment: { id: 2, user: "乙", userId: 6, content: "短", likes: 1, time: 0, replyCount: 3, beReplied: quoteOfParent } });
            const toggled = createTemporaryObject(signalSpyComponent, root, { target: row, signalName: "threadToggled" });
            verify(row.quoted !== null, "评论区里照常引出被回复的那条");
            row.threadOf = 1;
            verify(row.quoted === null, "楼中楼里回复的就是父评论：不重复引用");
            const link = findChild(row, "threadLink");
            mouseClick(link, 4, link.height / 2);
            compare(toggled.count, 1);

            const long = createTemporaryObject(commentRowComponent, root, {
                comment: { id: 3, user: "丙", content: Array(40).fill("很长的一段话，").join(""), likes: 0, time: 0 } });
            const expand = findChild(long, "expandLink");
            tryVerify(() => expand.visible, 2000, "超过 6 行时给「展开全文」");
            waitForRendering(long);   // 链接刚出现：等布局把它放到位再点（用户也是看到了才点）
            mouseClick(expand, 4, expand.height / 2);
            verify(long.expanded, "点「展开全文」后整段显示");
        }

        // 能不能播看后端按 privilege 给的 playable；后端只能按 fee 猜的「会员歌」，会员账号照样能播
        function test_blockedByFollowsPrivilege() {
            compare(Format.blockedBy({ playable: true, fee: 1 }, false), "");
            compare(Format.blockedBy({ playable: false, blockedBy: "vip" }, false), "vip");
            compare(Format.blockedBy({ playable: false, blockedBy: "vip" }, true), "");
            compare(Format.blockedBy({ playable: false, blockedBy: "paid" }, true), "paid");
            compare(Format.blockedBy({ playable: false, blockedBy: "unavailable" }, true), "unavailable");
            compare(Format.blockedBy(null, false), "");
        }

        // 曲目菜单：每位歌手各一项、按署名顺序排在专辑前；复制的链接与分享用同一个地址

        // 分享链接不能走 Purpose Export 的 urls（那会把 https 当本地文件给 Telegram）；
        // 这里锁定链接 scheme 与 URL 编码。
        function test_shareTargetsUseLinkSchemes() {
            const targets = createTemporaryObject(shareTargetsComponent, root, {
                url: "https://music.163.com/song?id=42",
                title: "歌 & 名",
                text: "分享歌曲"
            });
            const telegram = targets.telegramUrl();
            verify(telegram.startsWith("https://t.me/share/url?url=https%3A%2F%2Fmusic.163.com%2Fsong%3Fid%3D42"));
            verify(telegram.indexOf("text=%E5%88%86%E4%BA%AB%E6%AD%8C%E6%9B%B2") > 0);
            verify(targets.mailUrl().startsWith("mailto:?subject="));
            verify(targets.mailUrl().indexOf("https%3A%2F%2Fmusic.163.com%2Fsong%3Fid%3D42") > 0);
        }

        function test_trackMenuOffersEveryArtist() {
            const m = createTemporaryObject(trackMenuComponent, root, { player: ({}) });
            m.show(root.track, {});
            tryVerify(() => m.opened);
            const texts = [];
            for (let i = 0; i < m.count; ++i)
                texts.push(m.itemAt(i).text || "");
            const first = texts.indexOf("歌手：周杰伦");
            const second = texts.indexOf("歌手：费玉清");
            verify(first >= 0 && second === first + 1, texts.join(" | "));
            compare(texts[second + 1], "专辑：叶惠美");
            m.close();
        }

        // 正在播放的高亮靠 id 相等：新歌的 id 超出 32 位整数，页面的 currentId 存成 int 会被截断、永远对不上
        function test_pagesKeepFullTrackIds() {
            const id = 18478568150;
            const extra = { PlaylistPage: { list: Source.list("daily"), listKey: "daily" } };
            for (const name of ["ChartsPage", "MinePage", "PlaylistPage", "ArtistPage", "SearchPage"]) {
                const page = createTemporaryObject(Qt.createComponent("../" + name + ".qml"), root,
                                                   Object.assign({ currentId: id }, extra[name] || {}));
                verify(page !== null, name);
                compare(page.currentId, id, name);
            }
        }

        function test_locateCurrentAppearsOnlyWhenOutOfView() {
            const list = createTemporaryObject(locateComponent, root);
            const locate = findChild(list, "locate");
            tryCompare(locate, "opacity", 1);              // 第 150 行一开始就不在视野里
            mouseClick(locate);
            tryVerify(() => list.itemAtIndex(150) !== null && locate.inView);
            tryCompare(locate, "opacity", 0);
            list.positionViewAtBeginning();
            tryCompare(locate, "opacity", 1);              // 滚走了又出现
        }

        function lyricsPage() {
            const page = createTemporaryObject(nowPlayingComponent, root, {
                track: { id: lyricTrackId(), name: "歌", artist: "歌手", cover: "", dur: 300 }, elapsed: 0 });
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

        // 正在播放页的大封面：微圆角、向下偏的微投影；播放 / 暂停时封面不动（状态由播放条表达）
        function test_coverHasASteadySoftShadow() {
            const page = createTemporaryObject(nowPlayingComponent, root, {
                track: { id: 1, name: "歌", artist: "歌手", cover: "", dur: 300 }, elapsed: 0, playing: true });
            const cover = page.coverItem;
            verify(cover.radius > 0);
            verify(cover.shadowed);
            const y = cover.y;
            page.playing = false;
            wait(300);
            verify(cover.shadowed);
            compare(cover.y, y);
        }

        // 从一段长对话切到另一段：列表重建前旧的委托会用新列表读一次、下标越界。
        // 曾经回退成 {}，每个字段都是 undefined，日志里一串「Unable to assign [undefined]」和 TypeError
        function test_switchingConversationLogsNothing() {
            const message = (id, text) => ({ key: String(id), id: id, fromUserId: 5, fromName: "Jo", fromAvatar: "", mine: false,
                                             time: 1700000000000 + id, batchId: "", message: { kind: "text", text: text } });
            const saved = Source.snapshot;
            Source.snapshot = {
                data: Object.assign({}, saved.data, {
                    inbox: { unread: 0, more: false, items: [
                        { key: "5", userId: 5, name: "Jo", avatar: "", lastText: "三", lastTime: 1, unread: 0 },
                        { key: "6", userId: 6, name: "Al", avatar: "", lastText: "", lastTime: 1, unread: 0 }] },
                    inboxThread: { userId: 5, before: 0, more: false,
                                   items: [message(1, "一"), message(2, "二"), message(3, "三")] },
                }),
                personal: saved.personal,
            };
            try {
                failOnWarning(/Unable to assign|TypeError/);
                const page = createTemporaryObject(inboxComponent, root, { peer: 5 });
                tryVerify(() => page.thread.items.length === 3);
                wait(100);
                page.peer = 6;
                wait(100);
                compare(page.thread.items.length, 0);
            } finally {
                Source.snapshot = saved;
            }
        }

        // 纯音乐：歌词为空不是「没取到」，页面上说「纯音乐，请欣赏」
        function test_pureMusicSaysSoInsteadOfNoLyrics() {
            const saved = Source.snapshot;
            Source.snapshot = {
                data: Object.assign({}, saved.data, { lyrics: { "8080": { lines: [], pureMusic: true } } }),
                personal: saved.personal,
            };
            // 之前失败过、冷却后自动重取成功：旧的失败标记还在，也要以取到的为准
            Source.markRequestFailed("lyrics:8080", "旧的失败");
            try {
                const page = createTemporaryObject(nowPlayingComponent, root, {
                    track: { id: 8080, name: "纯音乐", artist: "A", cover: "", dur: 100 }, elapsed: 0 });
                const empty = findChild(page, "lyricsEmpty");
                tryVerify(() => empty !== null && empty.visible);
                compare(empty.parts[0].text, "纯音乐，请欣赏");
            } finally {
                Source.snapshot = saved;
                Source.clearRequestFailure("lyrics:8080");
                delete Source.failedAt["lyrics:8080"];
            }
        }
    }
}
