// 数据模块：页面只问「要什么」（某个列表、首页入口、某首歌的歌词），不碰快照的原始形状。
// 缺数据时怎么退化（没有封面、空列表、没有种子歌）也只在这里决定，页面拿到的永远是可直接渲染的值。
// 原始数据有两个来源，形状完全相同（data / personal 两个对象，字段见 DESIGN.md「后端接口」）：
//   live     = C++ Store（应用里注入，实时接口，按需加载）
//   snapshot = Snapshot.qml（原型：快照或压力数据，main.qml 在没有后端时装上）
// 两者都还没装上的一瞬间是同形的空值，页面照常求值、不报错。
pragma Singleton

import QtQuick
import "Format.js" as Format

QtObject {
    id: source

    property var live: null         // C++ Store（鸭子类型）
    property var snapshot: null     // { data, personal }

    // 函数而不是中间属性：单例里绑定的求值顺序没有保证，d / p 求值时中间属性可能还没算出来
    function raw() {
        return source.live || source.snapshot || {
            data: { toplists: [], tracks: [], playlist: { name: "", count: 0, plays: 0, cover: "", description: "" } },
            personal: { playlists: [] },
        };
    }
    readonly property var d: source.raw().data
    readonly property var p: source.raw().personal

    // 按需加载：只有 live 来源需要（快照与压力数据是一次给全的）。key 见 DESIGN.md。
    // 读数据的函数在绑定里调用它，所以失败过的 key 在冷却期内不自动再要：失败会写 requestFailures，
    // 读它的绑定随之重算、又走到这里，不冷却就是一个请求失败、重要、再失败的死循环。
    // 冷却过后，下次自然重算才再试一次。用户自己的动作（点「重试」、滚到底要下一页、展开回复）
    // 走 retryKey，立刻再要。同一轮里重复要的 key 合成一个请求。
    readonly property int retryCooldownMs: 30000
    // failedAt 与 queued 只就地修改：读它们的绑定不会因此重算。
    // 整体换掉 failedAt（重新联网、换账号）则会让读过它的绑定重算、把失败过的再要一遍——正是想要的
    property var failedAt: ({})   // key → 失败时刻（毫秒）
    property var queued: ({})     // 这一轮已排上的 key
    function need(key) {
        if (!source.live)
            return;
        const at = source.failedAt[key];
        if (at !== undefined && Date.now() - at < source.retryCooldownMs)
            return;
        if (source.queued[key])
            return;
        source.queued[key] = true;
        Qt.callLater(source.sendQueued);   // 同一个函数在一轮里只排一次
    }
    function sendQueued() {
        const keys = Object.keys(source.queued);
        for (const key of keys)
            delete source.queued[key];
        if (source.live)
            keys.forEach(key => source.live.request(key));
    }
    // 按需请求失败：Store 发来 requestFailed 后由 main 标记；页面可显示「重试」而不是一直转圈
    property var requestFailures: ({})
    property var requestRestrictions: ({})
    function markRequestFailed(key, message) {
        source.failedAt[key] = Date.now();
        const f = Object.assign({}, source.requestFailures);
        f[key] = message || "请求失败";
        source.requestFailures = f;
    }
    function clearRequestFailure(key) {
        if (!(key in source.requestFailures))
            return;
        const f = Object.assign({}, source.requestFailures);
        delete f[key];
        source.requestFailures = f;
    }
    function failure(key) { return source.requestFailures[key] || ""; }
    // 预期内的不可见（隐私设置）：页面内说明，不显示重试
    function markRequestRestricted(key, message) {
        const r = Object.assign({}, source.requestRestrictions);
        r[key] = message || "对方设置了隐私，无法查看";
        source.requestRestrictions = r;
    }
    function restriction(key) { return source.requestRestrictions[key] || ""; }
    function retryKey(key) {
        delete source.failedAt[key];
        source.clearRequestFailure(key);
        source.need(key);
    }

    // 连接与会话状态（live 才有；原型里可由截图 harness 用 simulate* 模拟）
    property bool simulateOffline: false
    property bool simulateExpired: false
    readonly property bool online: source.live ? source.live.online : !source.simulateOffline
    readonly property bool sessionExpired: source.live ? source.live.sessionExpired : source.simulateExpired
    function retry() {
        source.failedAt = ({});   // 重新联网：失败过的页面数据下次读到时就再要
        if (source.live)
            source.live.refresh();
    }

    function search(query) {
        if (query.length > 0)
            source.need("search:" + query);
    }

    // 个人字段在未登录时是 null，登录后也是逐个到达（账号先到，每日推荐等稍后）：
    // 一律换成同形的空值，入口与列表照常渲染成「还没有」，不必在每处判空
    readonly property var pe: ({
        daily: source.p.daily || { date: "", tracks: [] },
        heart: source.p.heart || { seed: null, tracks: [] },
        radar: source.p.radar || { cover: "", description: "", tracks: [] },
        similar: source.p.similar || { seed: null, tracks: [] },
        liked: source.p.liked || { count: 0, tracks: [] },
        fm: source.p.fm || { tracks: [] },
        playlistDetails: source.p.playlistDetails || {},
    })

    // 红心：id → true 的查找表（一千多首，逐行查要 O(1)）
    // live：Store 先改 likedIds（乐观），失败时改回并发 likeFailed。
    // 原型：点红心只改这份内存里的覆盖，不写任何地方
    property var likedOverride: ({})
    readonly property var likedSet: {
        const set = {};
        for (const id of (source.p.likedIds || []))
            set[id] = true;
        for (const id in source.likedOverride)
            set[id] = source.likedOverride[id];
        return set;
    }
    function isLiked(id) { return source.likedSet[id] === true; }
    function setLiked(id, liked) {
        if (source.live) {
            source.live.setLiked(id, liked);
            return;
        }
        const o = Object.assign({}, source.likedOverride);
        o[id] = liked;
        source.likedOverride = o;
    }

    // 评论区：歌曲或歌单的评论。thread = { kind: "song" | "playlist", id }。
    // 两种的形状相同，只是槽、请求 key 与 owner 字段名不同（歌单评论只读，不能点赞）
    readonly property var commentKinds: ({
        song: { hot: "comments", page: "commentPage", replies: "commentReplies", owner: "songId" },
        playlist: { hot: "playlistComments", page: "playlistCommentPage", replies: "playlistCommentReplies", owner: "playlistId" },
    })
    function commentThread(kind, id) { return { kind: kind, id: id }; }
    function sameThread(a, b) { return Boolean(a) && Boolean(b) && a.kind === b.kind && a.id === b.id; }
    // 精彩评论（前 20 条，接口按点赞排好序）与总数：一次请求；快照里带的是歌词那首的
    function hotComments(t) {
        const k = source.commentKinds[t.kind];
        const c = source.d[k.hot];
        if (c && c[k.owner] === t.id)
            return { loaded: true, items: c.items || [], total: c.total || 0 };
        if (t.id > 0)
            source.need(k.hot + ":" + t.id);
        return { loaded: false, items: [], total: 0 };
    }
    function commentTotal(t) { return source.hotComments(t).total; }

    // 歌曲百科：data 里按 songId 存（最近 8 首），各读各的；取失败时不会拿别的歌顶上。
    // pending：还在取；empty：取到了但这首没有百科
    function wiki(songId) {
        const w = (source.d.wiki || {})[String(songId)];
        if (w)
            return Object.assign({ pending: false, failed: false }, w);
        if (songId > 0)
            source.need("wiki:" + songId);
        return { songId: songId, pending: Boolean(source.live), failed: Boolean(source.failure("wiki:" + songId)),
                 empty: true, description: "", memory: null, chorus: null, basic: [], creators: [], mvs: [],
                 reviews: [], sheets: [], entertainments: [], awards: [] };
    }


    // 最新评论：分页按需取（comments:<songId>:<offset>，每页 20），data 里只放最近一页，
    // 这里按歌累积；只接「正好是下一页」的那页，迟到或重复的页不会打乱顺序
    // 分页累积（评论、歌手的全部歌曲共用）：acc = { owner, items, total, more, next, loading }。
    // 只接 owner 对得上、offset 正好等于 next 的那页；items 按 id 去重（与已有的热门曲目不重复）
    function appendPage(acc, owner, page, list) {
        if (!page || owner !== acc.owner || page.offset !== acc.next)
            return acc;
        const seen = {};
        for (const item of acc.items)
            seen[item.id] = true;
        const fresh = list.filter(item => item.id === undefined || !seen[item.id]);
        return { owner: acc.owner, items: acc.items.concat(fresh), total: page.total,
                 more: page.more && list.length > 0, next: acc.next + list.length, loading: false, failed: false };
    }
    // 返回标成「在取」的新 acc（next 是要请求的 offset）；换了 owner 从 start 开始；在取或没有更多时返回 null
    function nextPage(acc, owner, start, seed) {
        if (!source.live)
            return null;
        if (acc.owner !== owner)
            acc = { owner: owner, items: seed, total: 0, more: true, next: start, loading: false, failed: false };
        if (acc.loading || !acc.more)
            return null;
        pageTimeout.restart();
        return Object.assign({}, acc, { loading: true, failed: false });
    }
    // Store 请求失败时不回调：在取超过 10 秒就标成 failed，界面给出「点此重试」，不会一直转圈
    readonly property Timer pageTimeout: Timer {
        interval: 10000
        onTriggered: {
            for (const kind in source.latestComments) {
                if (source.latestComments[kind].loading)
                    source.setLatest(kind, Object.assign({}, source.latestComments[kind], { loading: false, failed: true }));
            }
            if (source.artistSongs.loading)
                source.artistSongs = Object.assign({}, source.artistSongs, { loading: false, failed: true });
        }
    }

    // 最新评论：打开评论或滚到底时调用 moreComments；每种评论区各有一份累积（owner 是歌或歌单的 id）
    property var latestComments: ({ song: { owner: 0, items: [], total: 0, more: false, next: 0, loading: false },
                                    playlist: { owner: 0, items: [], total: 0, more: false, next: 0, loading: false } })
    function setLatest(kind, acc) {
        const all = Object.assign({}, source.latestComments);
        all[kind] = acc;
        source.latestComments = all;
    }
    function latestFor(t) {
        const acc = source.latestComments[t.kind];
        return acc.owner === t.id ? acc : null;
    }
    readonly property var commentPageRaw: source.d.commentPage || null
    onCommentPageRawChanged: {
        const page = source.commentPageRaw;
        if (page)
            source.setLatest("song", source.appendPage(source.latestComments.song, page.songId, page, page.items));
    }
    readonly property var playlistCommentPageRaw: source.d.playlistCommentPage || null
    onPlaylistCommentPageRawChanged: {
        const page = source.playlistCommentPageRaw;
        if (page)
            source.setLatest("playlist", source.appendPage(source.latestComments.playlist, page.playlistId, page, page.items));
    }
    function moreComments(t) {
        const acc = source.nextPage(source.latestComments[t.kind], t.id, 0, []);
        if (acc === null)
            return;
        source.setLatest(t.kind, acc);
        source.retryKey(source.commentKinds[t.kind].hot + ":" + t.id + ":" + acc.next);   // 用户的动作，不等冷却
    }

    // 歌手的全部歌曲：先是 artist:<id> 带的热门 50 首，滚到底再按热度从第 50 首往后取（artistSongs:<id>:<offset>）
    property var artistSongs: ({ owner: 0, items: [], total: 0, more: false, next: 0, loading: false })
    readonly property var artistSongsRaw: source.d.artistSongs || null
    onArtistSongsRawChanged: {
        const page = source.artistSongsRaw;
        if (page)
            source.artistSongs = source.appendPage(source.artistSongs, page.artistId, page, page.tracks);
    }
    function moreArtistSongs(artistId, hotTracks) {
        const acc = source.nextPage(source.artistSongs, artistId, hotTracks.length, hotTracks);
        if (acc === null)
            return;
        source.artistSongs = acc;
        source.retryKey("artistSongs:" + artistId + ":" + acc.next);   // 滚到底、点重试：用户的动作，不等冷却
    }

    // 账号
    readonly property var account: source.p.account   // null = 没有登录快照
    // 自己正戴着的头像挂件（profile 不带，Store 另外取 data.avatarPendant）；没戴、没登录是空串
    readonly property string ownPendant: source.d.avatarPendant ? source.d.avatarPendant.imageUrl || "" : ""
    readonly property double accountId: source.account ? source.account.id : 0
    // 换号（含匿名 → 登录 / 退出）时，评论分页与歌手分页里还留着上一个账号的 liked/followed 状态。
    // 只认 userId 变化；同一账号资料刷新不打断正在看的列表。
    onAccountIdChanged: {
        source.latestComments = ({ song: { owner: 0, items: [], total: 0, more: false, next: 0, loading: false },
                                   playlist: { owner: 0, items: [], total: 0, more: false, next: 0, loading: false } })
        source.artistSongs = ({ owner: 0, items: [], total: 0, more: false, next: 0, loading: false })
        source.likedOverride = ({})
        source.requestFailures = ({})
        source.failedAt = ({})
        source.requestRestrictions = ({})
    }

    // 榜单
    readonly property var toplists: source.d.toplists
    // 发现页入口的说明：里面有什么
    readonly property string chartsCaption: "排行榜、新歌新碟、精品歌单、播客"
    readonly property var hotChart: ({ name: source.d.playlist.name, count: source.d.playlist.count, tracks: source.d.tracks })

    // 推荐歌单（首页第二排）
    readonly property var playlists: source.p.playlists

    // 私人漫游：一个流，直接播放；快放完时 moreFm 续取一批（追加到 fmTracks）
    readonly property var fmTracks: source.pe.fm.tracks
    function moreFm() { source.need("fm:next"); }
    // 心动模式（与网易云相同）也是流，不是一张歌单：点了就放，从红心第一首出发；
    // 快放完时从正在放的这首续取（追加到 heartTracks），在里面点的红心会把后面的推荐带过去
    readonly property var heartTracks: source.pe.heart.tracks
    function moreHeart(songId) { source.need("heart:next:" + songId); }

    // 我的：最近播放、云盘、创建与收藏的歌单（缺字段时给稳定的空形状）
    readonly property var recent: source.p.recent || { total: 0, tracks: [] }
    readonly property var cloud: source.p.cloud || { count: 0, size: 0, maxSize: 0, tracks: [] }
    readonly property var myPlaylists: (source.p.mine || []).map(pl => Object.assign({ owned: true }, pl))
        .concat((source.p.subscribed || []).map(pl => Object.assign({ owned: false }, pl)))

    // 搜索：快照只有一个查询词的结果；热门搜索是匿名可得的
    readonly property var hotSearches: source.d.hotSearches || []
    readonly property var searchResult: source.d.search || { query: "", songs: [], artists: [], playlists: [], albums: [] }
    // 某个词的结果：data 里只放最后落地的一份，词对不上（慢的旧请求后到、新词还在路上）就当还没有，pending 为真
    function searchFor(query) {
        const r = source.searchResult;
        if (!source.live || r.query === query)
            return Object.assign({ pending: false, failed: false }, r);
        return { query: query, songs: [], artists: [], playlists: [], albums: [], pending: true,
                 failed: Boolean(source.failure("search:" + query)) };
    }

    // 不到 1 GB 按 MB 写（几首下载的歌写成「0.1 GB」看不出多少）；
    // 以上一位小数且去掉多余的 .0：59.9 GB / 60 GB。取整会把将满显示成「60 / 60」
    function fileSize(bytes) {
        return bytes < 1073741824 ? Math.round(bytes / 1048576) + " MB" : Number((bytes / 1073741824).toFixed(1)) + " GB";
    }

    // 歌手 / 专辑 / 用户 / MV / 百科：data 里按 id 存（最近 8 个），各读各的；没取到就向 live 要，先给同形的空值
    // 简介常带全角空格的段首缩进：去掉，每行都从窗格左缘开始（对齐页边线）
    function unindent(text) {
        return (text || "").replace(/^[\s　]+/gm, "");
    }
    function emptyArtist(id, failed) {
        return { id: id, name: "", cover: "", alias: "", description: "", musicSize: 0, albumSize: 0,
                 tracks: [], albums: [], failed: failed };
    }
    function artist(id) {
        if (!(id > 0))
            return emptyArtist(0, false);
        const a = (source.d.artist || {})[String(id)];
        if (a)
            return Object.assign({}, a, { description: source.unindent(a.description) });
        source.need("artist:" + id);
        return emptyArtist(id, Boolean(source.failure("artist:" + id)));
    }
    function emptyAlbum(id, failed) {
        return { id: id, name: "", artist: "", artistId: 0, cover: "", description: "", company: "",
                 publishTime: 0, tracks: [], failed: failed };
    }
    function album(id) {
        if (!(id > 0))
            return emptyAlbum(0, false);
        const a = (source.d.album || {})[String(id)];
        if (a)
            return a;
        source.need("album:" + id);
        return emptyAlbum(id, Boolean(source.failure("album:" + id)));
    }

    // 用户主页：data 里只放最近取回的一个；资料 + 公开歌单
    function emptyUser(id, failed) {
        return { id: id, name: "", avatar: "", background: "", signature: "",
                 followed: false, follows: 0, followeds: 0, playlistCount: 0,
                 vipLevel: "", avatarPendant: "", level: 0, listenSongs: 0,
                 createTime: 0, mutual: false, playlists: [], restricted: false,
                 failed: failed };
    }
    function user(id) {
        // 页面退栈时 userId 会短暂变成 0：这里绝不能发 user:0 请求
        if (!(id > 0))
            return emptyUser(0, false);
        const u = (source.d.user || {})[String(id)];
        if (u)
            return u;
        source.need("user:" + id);
        return emptyUser(id, Boolean(source.failure("user:" + id)));
    }

    // 下面几种数据后端不经缓存、不去重：这里只读不要，由页面在打开那一栏时 need 一次
    // 关注 / 粉丝：kind 是 userFollows | userFolloweds；data 里只放最近取回的一个人的
    function people(kind, uid) {
        if (!(uid > 0))
            return { userId: 0, items: [], more: false, pending: false, moreFailed: false,
                     restricted: false, restrictedMessage: "", failed: false };
        const key = kind + ":" + uid;
        const restricted = source.restriction(key);
        const l = source.d[kind];
        if (l && l.userId === uid)   // moreFailed：下一页（从 Store 给的 next 翻）没取到，可重试
            return Object.assign({ failed: false, moreFailed: Boolean(source.failure(kind + ":" + uid + ":" + l.next)),
                                   restricted: l.restricted === true || restricted.length > 0,
                                   restrictedMessage: l.message || restricted }, l);
        return { userId: uid, items: [], more: false, pending: restricted.length === 0,
                 moreFailed: false,
                 restricted: restricted.length > 0, restrictedMessage: restricted,
                 failed: restricted.length === 0 && Boolean(source.failure(key)) };
    }
    function morePeople(kind, list) {
        // 按 Store 给的 next 翻，不按条数：去重后的条数可能比服务器的偏移少，那样会重取、甚至卡住
        if (list.more && !list.pending)
            source.retryKey(kind + ":" + list.userId + ":" + list.next);
    }
    // 听歌排行（只有自己的）：type 是 week | month。排行条目不是完整的曲目，补成曲目行能用的形状
    function listenRank(type) {
        const r = source.d.listenRank;
        if (!r || r.type !== type)
            return { type: type, songCount: 0, tracks: [], pending: true, failed: Boolean(source.failure("listenRank:" + type)) };
        const tracks = r.items.map(i => ({
            id: i.id, name: i.name, album: i.album, albumId: 0, cover: i.cover, dur: 0, plays: i.playCount,
            artists: i.artists, artist: i.artists.map(a => a.name).join(" / "),
            artistId: i.artists.length > 0 ? i.artists[0].id : 0,
        }));
        return { type: type, songCount: r.songCount, tracks: tracks, pending: false, failed: false };
    }
    // 楼中楼：一条评论下的回复；data 里只放最近展开的那一条的
    function replies(t, commentId) {
        const k = source.commentKinds[t.kind];
        const key = k.replies + ":" + t.id + ":" + commentId;
        const r = source.d[k.replies];
        if (r && r[k.owner] === t.id && r.commentId === commentId)
            return Object.assign({ key: key, pending: false, failed: false }, r);
        return { key: key, commentId: commentId, before: 0, more: false, items: [], pending: true,
                 failed: Boolean(source.failure(key)) };
    }
    function moreReplies(r) {
        if (r.more && !r.pending)
            source.need(r.key + ":" + r.before);
    }

    // 私信（只读）：会话列表与当前打开的一段对话；对话在 data 里只放最近取回的一段
    // moreFailed：下一页没取到（失败按请求时的 key 记），可重试
    readonly property var inbox: source.d.inbox
        ? Object.assign({ pending: false, moreFailed: Boolean(source.failure("inbox:" + source.d.inbox.next)) }, source.d.inbox)
        : { unread: 0, more: false, items: [], pending: true, moreFailed: false }
    function moreInbox() {
        if (source.d.inbox && source.d.inbox.more)
            source.retryKey("inbox:" + source.d.inbox.next);   // Store 给的下一页起点，不是去重后的条数
    }
    function thread(uid) {
        const t = source.d.inboxThread;
        if (t && t.userId === uid)
            return Object.assign({ pending: false, failed: false,
                                   moreFailed: Boolean(source.failure("inboxThread:" + uid + ":" + t.before)) }, t);
        return { userId: uid, before: 0, more: false, items: [], pending: true, moreFailed: false,
                 failed: Boolean(source.failure("inboxThread:" + uid)) };
    }
    function moreThread(t) {
        if (t.more && !t.pending)
            source.retryKey("inboxThread:" + t.userId + ":" + t.before);
    }
    // 未读私信数（自己的主页上「私信」旁边用）：打开自己的主页、离开私信页时各取一次通知计数，不在后台轮询
    readonly property int unreadMessages: source.d.notifications ? source.d.notifications.msg : 0

    // 下载：应用里是 C++ DownloadManager，原型里是 SimDownloads（都由 main 装上）。
    // downloaded 是下好的（带 id/name/artist/album/cover/size/level/path）；正在下的进度只在信号里，记在 downloading
    property var downloads: null
    readonly property var downloaded: source.downloads ? source.downloads.downloads : []
    // songId → 已下比例 0–1。在途的下载只有 DownloadManager.active 一份：从点下载到下完、失败或取消
    readonly property var downloading: {
        const progress = {};
        for (const a of (source.downloads ? source.downloads.active : []))
            progress[a.id] = a.progress;
        return progress;
    }
    signal downloadFinished(string name)
    signal downloadFailed(string name, string message)
    function downloadState(songId) {
        if (source.downloading[songId] !== undefined)
            return "active";
        return source.downloaded.some(d => d.id === songId) ? "done" : "none";
    }
    // 下好的文件当曲目放：播放器按 id 先找本地文件
    function downloadedTrack(d) {
        return { id: d.id, name: d.name, artist: d.artist, album: d.album, albumId: 0, artistId: 0,
                 artists: [{ id: 0, name: d.artist }], cover: d.cover, dur: d.dur || 0, fee: 0, alias: "", quality: d.level || "",
                 reason: "", mv: 0, playable: true };
    }
    property var downloadNames: ({})   // songId → 歌名（下完 / 失败时提示用）
    function download(track, level) {
        if (!source.downloads || source.downloadState(track.id) !== "none")
            return;
        source.downloadNames = Object.assign({}, source.downloadNames, { [track.id]: track.name });
        source.downloads.downloadTrack(track, level);
    }
    // 自己取消不算失败：DownloadManager 不发 failed，active 里那条消失即可
    function cancelDownload(songId) { if (source.downloads) source.downloads.cancel(songId); }
    function removeDownload(songId) { if (source.downloads) source.downloads.remove(songId); }
    readonly property Connections _downloads: Connections {
        target: source.downloads
        function onFinished(songId, path) { source.downloadFinished(source.downloadNames[songId] || ""); }
        function onFailed(songId, message) { source.downloadFailed(source.downloadNames[songId] || "", message); }
    }

    // 自己的累计听歌时长（秒）与升级进度；还没取到时为 0 / null
    readonly property real listenSeconds: source.d.listenTotal ? source.d.listenTotal.seconds : 0
    readonly property var level: source.d.userLevel || null
    // 收藏数：歌手 / MV / 创建与收藏的歌单 / 电台
    readonly property var subcount: source.d.userSubcount || null

    // 分页列表（收藏、动态、通知、精品歌单、电台节目）：Store 把一页页接进同一个槽，槽里有 more 与 next
    //（下一页的游标或 offset），这里只读。matches：槽里的是不是要的那一份（同一个人 / 同一个分类）。
    // pending：第一页在取；failed：第一页没取到；moreFailed：下一页没取到（都可重试）
    function paged(slot, key, matches) {
        if (slot && matches)
            return Object.assign({ key: key, pending: false, failed: false,
                                   moreFailed: slot.more === true && Boolean(source.failure(key + ":" + slot.next)) }, slot);
        source.need(key);
        const failed = Boolean(source.failure(key));
        return { key: key, items: [], more: false, next: 0, pending: Boolean(source.live) && !failed, failed: failed, moreFailed: false };
    }
    function morePaged(list) {
        if (list.more && !list.pending)
            source.retryKey(list.key + ":" + list.next);
    }

    // 收藏的歌手 / 专辑 / MV（kind：artists | albums | mvs）
    function saved(kind) {
        const slot = source.p[{ artists: "savedArtists", albums: "savedAlbums", mvs: "savedMvs" }[kind]];
        return source.paged(slot, "library:" + kind, true);
    }
    // 用户动态（任何人的主页都有）；对方设置隐私时 restricted
    function events(uid) {
        if (!(uid > 0))
            return { key: "", items: [], more: false, next: 0, pending: false, failed: false, moreFailed: false, restricted: false };
        const key = "userEvents:" + uid;
        const restricted = source.restriction(key);
        const list = source.paged(source.d.userEvents, key, source.d.userEvents && source.d.userEvents.userId === uid);
        return Object.assign(list, { restricted: restricted.length > 0, restrictedMessage: restricted,
                                     pending: list.pending && restricted.length === 0 });
    }
    // 通知：kind 是 notices（系统通知）| comments（评论与回复）| forwards（@我与转发）
    function notices(kind) {
        const slot = source.d[{ notices: "notices", comments: "commentNotices", forwards: "forwards" }[kind]];
        return source.paged(slot, "notify:" + kind, true);
    }

    // 发现：新歌（可直接播放的曲目）、新碟、推荐 MV、推荐的播客节目（也是曲目）、精品歌单的分类
    function discover(kind) {
        const key = "discover:" + { newSongs: "newSongs", newAlbums: "newAlbums", recommendedMvs: "mvs",
                                    recommendedPrograms: "programs", highqualityTags: "highqualityTags" }[kind];
        const slot = source.d[kind];   // newSongs | newAlbums | recommendedMvs | recommendedPrograms | highqualityTags
        if (slot)
            return { key: key, items: slot, more: false, pending: false, failed: false, moreFailed: false };
        source.need(key);
        const failed = Boolean(source.failure(key));
        return { key: key, items: [], more: false, pending: Boolean(source.live) && !failed, failed: failed, moreFailed: false };
    }
    // 精品歌单：cat 是分类名，「全部」不限
    function highquality(cat) {
        const slot = source.d.highquality;
        return source.paged(slot, "discover:highquality:" + cat, slot && slot.cat === cat);
    }
    // 电台：详情 + 节目（节目是能直接播放的曲目）
    function radio(id) {
        const slot = source.d.radio;
        return source.paged(slot ? Object.assign({ items: slot.programs }, slot) : null, "radio:" + id, slot && slot.id === id);
    }

    // 电台、用户的搜索结果另外取（切到那一类时才要），只认这个词的。slot：searchRadios | searchUsers
    function searchSeparate(slot, query) {
        const r = source.d[slot];
        if (r && r.query === query)
            return { items: r.items, pending: false, failed: false };
        if (query.length > 0)
            source.need(slot + ":" + query);
        const failed = Boolean(source.failure(slot + ":" + query));
        return { items: [], pending: Boolean(source.live) && !failed, failed: failed };
    }

    // 搜索建议：打字时按停顿取一次，只认与当前输入相同的那份
    function suggestions(query) {
        const s = source.d.searchSuggest;
        return s && s.query === query ? s : null;
    }
    function suggest(query) { if (query.trim().length > 0) source.need("searchSuggest:" + query); }

    // 动态封面（登录后才有）：url 为空表示这首没有
    function dynamicCover(songId) {
        const c = source.d.dynamicCover;
        if (c && c.songId === songId)
            return c.url || "";
        if (songId > 0 && source.account)
            source.need("dynamicCover:" + songId);
        return "";
    }

    // MV：data 里只放最近取回的一个，id 对上才用；没对上就向 live 要
    function emptyMv(id, failed) {
        return { id: id, name: "", cover: "", artist: "", artistId: 0, duration: 0, url: "",
                 playCount: 0, subscribed: false, description: "", failed: failed };
    }
    // 搜索框空着时网易推荐的词：{ showKeyword 显示的一句, realKeyword 回车真正搜的 }；没取到是 null
    function defaultSearch() {
        const s = source.d.defaultSearch;
        if (!s)
            source.need("defaultSearch");
        return s || null;
    }
    // 一首歌的相似歌曲与相似歌单（正在播放页的「相似」栏）：两个槽各放一首歌的结果、带 songId，
    // 只有这一栏读，所以不按 id 分开存
    function similarTo(songId) {
        const s = source.d.similarSongs;
        const p = source.d.similarPlaylists;
        const songs = s && s.songId === songId ? s.tracks : null;
        const lists = p && p.songId === songId ? p.items : null;
        if (songId > 0 && songs === null)
            source.need("similarSongs:" + songId);
        if (songId > 0 && lists === null)
            source.need("similarPlaylists:" + songId);
        // 失败只看还没取到的那一半：后来取到了（哪怕是空的），旧的失败标记不算数
        return { songs: songs || [], playlists: lists || [], pending: songs === null || lists === null,
                 failed: Boolean((songs === null && source.failure("similarSongs:" + songId))
                                 || (lists === null && source.failure("similarPlaylists:" + songId))) };
    }
    // 只知道 id 的一首歌（外部链接打开的）：取到前是 null
    function song(id) {
        const s = (source.d.song || {})[String(id)];
        if (s)
            return s;
        if (id > 0)
            source.need("song:" + id);
        return null;
    }
    function mv(id) {
        if (!(id > 0))
            return emptyMv(0, false);
        const m = (source.d.mv || {})[String(id)];
        if (m)
            return m;
        source.need("mv:" + id);
        return emptyMv(id, Boolean(source.failure("mv:" + id)));
    }

    // 写操作：都交给 live（Store），结果经 Store 的信号回来；原型里不写任何地方
    // 「加到歌单」的候选：自己创建的歌单，去掉「我喜欢的音乐」（往里加就是点红心）
    readonly property var addableLists: source.myPlaylists.filter(pl => pl.owned && pl.id !== (source.pe.liked.id || 0))
    function addToPlaylist(playlistId, songId) { if (source.live) source.live.addSongToPlaylist(playlistId, songId); }
    function removeFromPlaylist(playlistId, songId) { if (source.live) source.live.removeSongFromPlaylist(playlistId, songId); }
    function createPlaylist(name) { if (source.live) source.live.createPlaylist(name, false); }
    function renamePlaylist(id, name) { if (source.live) source.live.renamePlaylist(id, name); }
    function setPlaylistDescription(id, description) { if (source.live) source.live.setPlaylistDescription(id, description); }
    function deletePlaylist(id) { if (source.live) source.live.deletePlaylist(id); }
    function setPlaylistSubscribed(id, subscribed) { if (source.live) source.live.setPlaylistSubscribed(id, subscribed); }
    // 自己创建的歌单的顺序：ids 是完整的新顺序
    function reorderPlaylists(ids) { if (source.live) source.live.reorderPlaylists(ids); }
    // 歌单内曲目排序：ids 是完整的新顺序；原型只读，不写
    function reorderPlaylistTracks(id, ids) { if (source.live) source.live.reorderPlaylistTracks(id, ids); }
    function setArtistSubscribed(id, subscribed) { if (source.live) source.live.setArtistSubscribed(id, subscribed); }
    function setAlbumSubscribed(id, subscribed) { if (source.live) source.live.setAlbumSubscribed(id, subscribed); }
    function setUserFollowed(id, followed) { if (source.live) source.live.setUserFollowed(id, followed); }
    // 评论点赞：live 走 Store 乐观更新；原型不写网络
    function setCommentLiked(t, commentId, liked) { if (source.live) source.live.setCommentLiked(t.kind, t.id, commentId, liked); }
    function logout() { if (source.live) source.live.logout(); }

    // 当前句：最后一个开始时间不晚于 elapsed 的句子（正在播放页与桌面歌词共用这一条规则）。
    // 没有时间轴的歌词（每句 t = -1）没有当前句：-1
    function lyricsTimed(lines) { return lines.length > 0 && lines[0].t >= 0; }
    function lyricIndex(lines, elapsed) {
        if (!source.lyricsTimed(lines))
            return -1;
        let i = 0;
        while (i + 1 < lines.length && lines[i + 1].t <= elapsed)
            i++;
        return i;
    }

    function lyrics(trackId) {
        // 有逐字歌词就用它：行的形状相同，每句多一个 words（逐字歌词由 main 在换歌时要一次）
        const timed = source.d.lyricsNew;
        if (timed && timed.songId === trackId && timed.lines.length > 0 && timed.lines[0].words !== undefined)
            return timed.lines;
        const entry = source.lyricsEntry(trackId);
        if (entry)
            return entry.lines || [];
        source.need("lyrics:" + trackId);
        return [];
    }
    // 一首歌的歌词 { lines, pureMusic }；还没取到时为 null。只读、不发请求。
    // Store 按歌存（最近几首），几处同时要不同的歌也互不覆盖
    function lyricsEntry(trackId) { return (source.d.lyrics || {})[String(trackId)] || null; }
    // 当前句唱到了哪（0–1）。有逐字时间：唱完的字 + 正在唱的那个字唱了多少，按字数计；
    // 没有：按本句开始到下一句开始（最后一句到歌曲结束）之间的时间比例
    function lineFill(line, next, songEnd, time) {
        if (!line)
            return 0;
        if (line.words && line.words.length > 0) {
            const total = line.words.reduce((n, w) => n + w.text.length, 0);
            let sung = 0;
            for (const w of line.words) {
                if (time >= w.t + w.d) {
                    sung += w.text.length;
                    continue;
                }
                if (time > w.t && w.d > 0)
                    sung += w.text.length * (time - w.t) / w.d;
                break;
            }
            return total > 0 ? Math.min(1, sung / total) : 0;
        }
        const end = next ? next.t : Math.max(line.t + 1, songEnd);
        return Math.max(0, Math.min(1, (time - line.t) / (end - line.t)));
    }

    // 封面 URL 换成需要的尺寸；没有封面就是空串，交给 Cover 显示占位
    function sized(url, px) {
        return url ? url.replace(/\?param=\d+y\d+$/, "?param=" + px + "y" + px) : "";
    }
    function quoted(name) { return "「" + name + "」"; }
    function shareUrl(kind, id) {
        switch (kind) {
        case "song": return "https://music.163.com/song?id=" + id;
        case "playlist": return "https://music.163.com/playlist?id=" + id;
        case "album": return "https://music.163.com/album?id=" + id;
        case "artist": return "https://music.163.com/artist?id=" + id;
        case "mv": return "https://music.163.com/mv?id=" + id;
        default: return "";
        }
    }
    function firstCover(tracks) { return tracks.length > 0 ? tracks[0].cover : ""; }
    function songs(n) { return { value: String(n), label: "首" }; }

    // 首页第一排：为你推荐的入口。每项 { key, title, caption, cover | symbol, locked }
    function homeEntries(loggedIn) {
        const p = source.pe;
        const fm = p.fm.tracks;
        const similar = p.similar;
        const fmEntry = fm.length > 0
            ? { key: "fm", title: "私人漫游", cover: source.sized(fm[0].cover, 400), caption: "立即播放：从" + source.quoted(fm[0].name) + "开始" }   // 点了就放的入口（还有心动模式），先说清楚
            : { key: "fm", title: "私人漫游", symbol: "radio-symbolic", caption: "暂时没有可漫游的歌" };
        const similarEntry = similar.tracks.length > 0
            ? { key: "similar", title: "相似歌曲", cover: source.sized(similar.tracks[0].cover, 400), caption: "和" + source.quoted(similar.seed.name) + "相似" }
            : { key: "similar", title: "相似歌曲", symbol: "fork-symbolic", caption: similar.seed ? "还没找到和" + source.quoted(similar.seed.name) + "相似的歌" : "先给喜欢的歌点个红心" };

        if (!loggedIn) {
            return [
                { key: "daily", title: "每日推荐", locked: true, caption: "登录后每天 6:00 更新" },
                { key: "heart", title: "心动模式", locked: true, caption: "登录后从红心歌曲出发" },
                fmEntry,
                similarEntry,
                { key: "charts", title: "发现", symbol: "compass-symbolic", caption: source.chartsCaption },
                { key: "liked", title: "我喜欢的音乐", locked: true, caption: "登录后同步红心歌曲" },
            ];
        }

        const daily = p.daily.tracks;
        const radar = p.radar;
        const heart = p.heart;
        // 封面避开种子歌：种子就是红心第一首，和旁边「我喜欢的音乐」的封面会重复
        const heartCover = (heart.tracks.find(t => !heart.seed || t.id !== heart.seed.id) || heart.seed || {}).cover;
        return [
            daily.length > 0
                ? { key: "daily", title: "每日推荐", cover: source.sized(daily[0].cover, 400), caption: "从" + source.quoted(daily[0].name) + "听起" }
                : { key: "daily", title: "每日推荐", symbol: "view-calendar-day-symbolic", caption: "今天的推荐还在生成" },
            heart.seed && heart.tracks.length > 0
                ? { key: "heart", title: "心动模式", cover: source.sized(heartCover, 400), caption: "立即播放：从" + source.quoted(heart.seed.name) + "出发" }
                : { key: "heart", title: "心动模式", symbol: "love-symbolic", caption: "先给喜欢的歌点个红心" },
            fmEntry,
            { key: "radar", title: "私人雷达", cover: source.sized(radar.cover, 400), symbol: "games-highscores-symbolic",
              caption: radar.description ? radar.description.split("|")[0] : "每天更新" },
            similarEntry,
            p.liked.count > 0
                ? { key: "liked", title: "我喜欢的音乐", cover: source.sized(source.firstCover(p.liked.tracks), 400), caption: p.liked.count + " 首" }
                : { key: "liked", title: "我喜欢的音乐", symbol: "love-symbolic", caption: "还没有红心歌曲" },
        ];
    }

    // 列表页：{ heading, cover, facts, description, tracks, ranked, emptyText, ... }
    function list(key) {
        const p = source.pe;
        switch (key) {
        case "daily": return {
            heading: "每日推荐", cover: source.sized(source.firstCover(p.daily.tracks), 560), tracks: p.daily.tracks,
            showReason: true,
            facts: [{ label: p.daily.date }, source.songs(p.daily.tracks.length)],
            description: "根据你的口味生成，每天 6:00 更新", emptyText: "今天的推荐还在生成，稍后再来" };
        case "radar": return {
            heading: "私人雷达", cover: source.sized(p.radar.cover, 560), tracks: p.radar.tracks,
            facts: [source.songs(p.radar.tracks.length)],
            description: p.radar.description ? p.radar.description.split("|")[0] : "", emptyText: "雷达还没扫到新歌" };
        case "similar": return {
            heading: "相似歌曲", cover: source.sized(source.firstCover(p.similar.tracks), 560), tracks: p.similar.tracks,
            facts: [source.songs(p.similar.tracks.length)],
            description: p.similar.seed ? "和" + source.quoted(p.similar.seed.name) + "（" + p.similar.seed.artist + "）相似" : "",
            emptyText: "还没找到相似的歌" };
        case "liked": return {
            heading: "我喜欢的音乐", cover: source.sized(source.firstCover(p.liked.tracks), 560), tracks: p.liked.tracks,
            facts: [source.songs(p.liked.count)], description: "", emptyText: "还没有红心歌曲" };
        case "recent": return {
            heading: "最近播放", cover: source.sized(source.firstCover(source.recent.tracks), 560), tracks: source.recent.tracks,
            facts: [source.songs(source.recent.total)], description: "", emptyText: "还没有播放记录" };
        case "downloads": {
            const tracks = source.downloaded.map(d => source.downloadedTrack(d));
            const bytes = source.downloaded.reduce((n, d) => n + (d.size || 0), 0);
            return {
                heading: "本地下载", cover: source.sized(source.firstCover(tracks), 560), tracks: tracks,
                facts: [source.songs(tracks.length)].concat(bytes > 0 ? [{ value: source.fileSize(bytes), label: "" }] : []),
                description: "", folder: source.downloads ? source.downloads.directory : "",
                emptyText: "还没有下载的歌：在歌曲的菜单里点「下载」" };
        }
        case "cloud": return {
            heading: "音乐云盘", cover: source.sized(source.firstCover(source.cloud.tracks), 560), tracks: source.cloud.tracks,
            facts: [source.songs(source.cloud.count), { value: source.fileSize(source.cloud.size), label: "/ " + source.fileSize(source.cloud.maxSize) }],
            description: "", emptyText: "云盘里还没有歌，可以在官方客户端上传" };
        default: {
            // 推荐歌单：key = "playlist:<序号>"，快照只带了前几个的曲目
            // 歌单：key = "playlist:<id>"，推荐歌单与「我的」歌单都可以。曲目先看卡片自带的（快照），
            // 再看按需取回的 playlistDetails；都没有就向 live 要一次
            if (key.startsWith("playlist:")) {
                // 卡片可能来自推荐歌单、我的歌单或排行榜；都没有时用按需取回的详情（它自带名称与封面）
                const id = Number(key.slice(9));
                if (!(id > 0))
                    return { heading: "", cover: "", tracks: [], facts: [], description: "",
                             listId: 0, owned: false, subscribed: false, playlistId: 0,
                             failed: false, emptyText: "歌单不存在" };
                const detail = (p.playlistDetails || {})[id] || {};
                const card = source.playlists.concat(source.myPlaylists, source.toplists).find(pl => pl.id === id)
                    || { id: id, name: detail.name || "", cover: detail.cover || "", count: detail.count || 0 };
                const tracks = (card.tracks && card.tracks.length > 0) ? card.tracks : detail.tracks || [];
                // 只在详情还没取回时要一次：取回来是空歌单（tracks 为 []）也算取到了，
                // 否则每次 put 触发重新求值都会再发一次，空歌单会让请求无限翻倍
                if (tracks.length === 0 && detail.tracks === undefined)
                    source.need(key);
                const mine = source.myPlaylists.find(pl => pl.id === id);
                const owned = mine !== undefined && mine.owned && id !== (p.liked.id || 0);
                return { heading: card.name, cover: source.sized(card.cover, 560), tracks: tracks,
                         ranked: source.toplists.some(t => t.id === id),
                         facts: card.count > 0 ? [source.songs(card.count)] : [],
                         description: card.description || detail.description || card.reason || "",
                         // 歌单操作（左栏「更多」）：自己建的可改名/删除，别人的可收藏/取消收藏；
                         // 自己建的歌单里，曲目菜单多一项「从歌单中移除」
                         listId: id,
                         owned: owned,
                         subscribed: mine !== undefined && !mine.owned,
                         playlistId: owned ? id : 0,
                         failed: Boolean(source.failure(key)),
                         emptyText: source.live ? "正在取回曲目…" : "这个歌单的曲目还没取到" };
            }
            if (key === "searchSongs") {
                const r = source.searchResult;
                return { heading: source.quoted(r.query) + "的单曲", cover: source.sized(source.firstCover(r.songs), 560),
                         tracks: r.songs, facts: [source.songs(r.songs.length)], description: "",
                         emptyText: "没有找到相关单曲" };
            }
            if (key.startsWith("radio:")) {
                // 电台：节目就是曲目（放的是节目的主歌），新的在前；节目一页页接着取。专辑列都是电台名，不显示
                const r = source.radio(Number(key.slice(6)));
                const facts = [];
                if (r.dj && r.dj.name)
                    facts.push({ label: r.dj.name });
                if (r.programCount > 0)
                    facts.push({ value: Format.count(r.programCount), label: "期节目" });
                if (r.subCount > 0)
                    facts.push({ value: Format.count(r.subCount), label: "人订阅" });
                return { heading: r.name || "", cover: source.sized(r.cover || "", 560), tracks: r.items, facts: facts,
                         description: source.unindent(r.description || ""), showAlbum: false, paging: r,
                         failed: r.failed, emptyText: r.pending ? "正在取回节目…" : "这个电台还没有节目" };
            }
            if (key.startsWith("album:")) {
                const al = source.album(Number(key.slice(6)));
                const year = al.publishTime > 0 ? new Date(al.publishTime).getFullYear() : 0;
                const facts = [{ label: al.artist, artistId: al.artistId }];
                if (year > 0)
                    facts.push({ label: String(year) });
                if (al.tracks.length > 0)
                    facts.push(source.songs(al.tracks.length));
                return { heading: al.name, cover: source.sized(al.cover, 560), tracks: al.tracks, facts: facts, isAlbum: true,
                         albumId: al.id, subscribed: al.subscribed === true,
                         description: source.unindent(al.description) || (al.company ? "发行：" + al.company : ""),
                         failed: Boolean(source.failure(key)),
                         emptyText: source.live ? "正在取回曲目…" : "这张专辑的曲目还没取到" };
            }
            const pl = source.d.playlist;
            const facts = [{ label: "官方榜" }, source.songs(pl.count)];
            if (pl.plays > 0)
                facts.push({ value: pl.plays >= 1e8 ? (pl.plays / 1e8).toFixed(1) + " 亿" : pl.plays >= 1e4 ? Math.round(pl.plays / 1e4) + " 万" : String(pl.plays), label: "次播放" });
            // 热歌榜本身就是一个歌单：带上 id，分享、收藏、评论与别的歌单一样
            const followed = source.myPlaylists.some(m => m.id === pl.id && !m.owned);
            return { heading: pl.name, cover: source.sized(pl.cover, 560), tracks: source.d.tracks, ranked: true,
                     facts: facts, description: pl.description, emptyText: "榜单暂时为空",
                     listId: pl.id || 0, owned: false, subscribed: followed, playlistId: 0 };
        }
        }
    }
}
