// 读数据不能形成请求循环。曾经：请求失败 → main 写 requestFailures → 读它的绑定重算 → 再要 → 再失败……
// 一个接口立刻拒绝（风控、坏 JSON）时，这就是每轮事件循环一个请求的风暴。
// 替身 Store 收到请求就按 main 的做法记一次失败；数一数绑定重算时又要了几次。
import QtQuick
import QtTest
import ".."

Item {
    id: root

    Component { id: snapshotComponent; Snapshot { fixture: "worst" } }
    Component {
        id: storeComponent
        QtObject {
            property var data
            property var personal
            property bool online: true
            property bool sessionExpired: false
            property var requested: []
            property bool failRequests: true
            function request(key) {
                requested = requested.concat([key]);
                if (failRequests)
                    Qt.callLater(() => Source.markRequestFailed(key, "服务器拒绝"));
            }
            function refresh() {}
        }
    }
    Component {
        id: readerComponent
        Item {
            property var artist: Source.artist(4242)
            property var user: Source.user(4343)
        }
    }

    Component { id: commentsComponent; CommentsPane { width: 400; height: 400 } }

    TestCase {
        name: "Requests"
        when: windowShown

        property var store: null

        function init() {
            const snap = createTemporaryObject(snapshotComponent, root);
            store = createTemporaryObject(storeComponent, root, {
                data: Object.assign({}, snap.data, { artist: null, user: null }),
                personal: snap.personal,
            });
            Source.live = store;
        }
        function cleanup() {
            Source.live = null;
            Source.failedAt = ({});
            Source.requestFailures = ({});
        }

        function count(key) { return store.requested.filter(k => k === key).length; }

        // 首次渲染可能在失败落地前要过不止一次（真 Store 会把在途的同一个 key 合并）；
        // 要紧的是失败记下之后不再增长
        function test_failedReadIsNotRequestedAgainByItsOwnBinding() {
            const reader = createTemporaryObject(readerComponent, root);
            tryVerify(() => Source.failure("artist:4242") !== "" && Source.failure("user:4343") !== "");
            const artists = count("artist:4242");
            const users = count("user:4343");
            wait(300);   // 有循环的话，这段时间里会多出几十次请求
            compare(count("artist:4242"), artists);
            compare(count("user:4343"), users);
            verify(artists <= 2 && users <= 2);
            verify(reader.artist.failed, "页面看得到失败，可以显示「重试」");
        }

        // 换账号时最新评论被清空（含上一个账号的点赞）：开着的评论区要自己重新取，不能整段消失
        function test_openCommentsRestartAfterAccountSwitch() {
            const songId = Source.d.comments.songId;
            const pane = createTemporaryObject(commentsComponent, root, { thread: Source.commentThread("song", songId) });
            tryVerify(() => pane.latest !== null);
            wait(100);
            const pages = count("comments:" + songId + ":0");
            verify(pages >= 1);
            // 与 Source.onAccountIdChanged 一样：清空最新评论，也清掉上一个账号的失败冷却
            Source.failedAt = ({});
            Source.latestComments = ({ song: { owner: 0, items: [], total: 0, more: false, next: 0, loading: false },
                                       playlist: { owner: 0, items: [], total: 0, more: false, next: 0, loading: false } });
            tryVerify(() => count("comments:" + songId + ":0") === pages + 1, 1000, "评论区重新开始取");
            wait(200);
            compare(count("comments:" + songId + ":0"), pages + 1, "只重取一次，不循环");
        }

        function test_retryAsksOnceMore() {
            createTemporaryObject(readerComponent, root);
            tryVerify(() => Source.failure("artist:4242") !== "");
            wait(50);
            const before = count("artist:4242");
            Source.retryKey("artist:4242");
            tryVerify(() => count("artist:4242") > before);
            wait(300);
            // 清掉失败标记会让读它的绑定重算、也去要：与 retryKey 自己要的合成一个请求
            compare(count("artist:4242"), before + 1, "重试只再要一次，失败后同样不循环");
        }

        // 离开列表页后，那张歌单不再被要：曾经 main 里还留着一个常驻的 openList 绑定，
        // 数据一变就重算、又去要一次已经不在屏幕上的歌单
        function test_leftListIsNotRequestedAgain() {
            store.failRequests = false;   // 不让失败冷却掩盖重复的请求
            const window = Qt.createComponent("../main.qml").createObject(root, { backend: null, fixture: "worst" });
            Source.live = store;          // main 启动时按 backend 设过 live
            window.open("list", "playlist:777");
            tryVerify(() => count("playlist:777") >= 1);
            window.open("user", "", { userId: 42 });   // 往前走到别的页：路由里的 listKey 还是这张歌单
            wait(100);
            const before = count("playlist:777");
            store.data = Object.assign({}, store.data);   // 任何数据变化都会让常驻的绑定重算
            wait(100);
            compare(count("playlist:777"), before, "不在屏幕上的歌单不再要");
            window.close();
        }

        // 下一页按 Store 给的 next 翻，不按已有条数：去重后条数可能比服务器的偏移少
        function test_morePagesFollowTheCursor() {
            Source.morePeople("userFollows", { more: true, pending: false, userId: 5, next: 7, items: [{ id: 1 }, { id: 2 }] });
            store.data = Object.assign({}, store.data, { inbox: { more: true, next: 9, unread: 0, items: [{ key: "1" }] } });
            Source.moreInbox();
            tryVerify(() => count("userFollows:5:7") === 1 && count("inbox:9") === 1);
        }

        // 滚到底要下一页、点「重试」是用户的动作：这一页上次失败、还在冷却，也要立刻再要
        function test_loadingMoreIgnoresTheCooldown() {
            Source.failedAt["artistSongs:9:2"] = Date.now();
            Source.moreArtistSongs(9, [{ id: 1 }, { id: 2 }]);
            tryVerify(() => count("artistSongs:9:2") === 1, 1000, "冷却期内用户要下一页也发出去");
        }
    }
}
