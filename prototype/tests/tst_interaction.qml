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

        // 评论点赞从页面转发到后端时，参数不能丢（songId, commentId, 目标状态）。
        function test_commentLikeForwardsToLive() {
            const calls = [];
            const saved = Source.live;
            Source.live = {
                setCommentLiked: function(songId, commentId, liked) {
                    calls.push([songId, commentId, liked]);
                }
            };
            Source.setCommentLiked(1456890009, 9667509659, true);
            Source.live = saved;
            compare(calls.length, 1);
            compare(calls[0][0], 1456890009);
            compare(calls[0][1], 9667509659);
            compare(calls[0][2], true);
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
