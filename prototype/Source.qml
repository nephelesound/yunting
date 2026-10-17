// 数据模块：页面只问「要什么」（某个列表、首页入口、某首歌的歌词），不碰快照的原始形状。
// 缺数据时怎么退化（没有封面、空列表、没有种子歌）也只在这里决定，页面拿到的永远是可直接渲染的值。
// 原始数据有两个来源，形状完全相同（data / personal 两个对象，字段见 DESIGN.md「后端接口」）：
//   live     = C++ Store（应用里注入，实时接口，按需加载）
//   snapshot = Snapshot.qml（原型：快照或压力数据，main.qml 在没有后端时装上）
// 两者都还没装上的一瞬间是同形的空值，页面照常求值、不报错。
pragma Singleton

import QtQuick

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

    // 按需加载：只有 live 来源需要（快照与压力数据是一次给全的）。key 见 DESIGN.md，Store 自己去重
    function need(key) {
        if (source.live)
            Qt.callLater(() => source.live.request(key));
    }
    // 按需请求失败：Store 发来 requestFailed 后由 main 标记；页面可显示「重试」而不是一直转圈
    property var requestFailures: ({})
    function markRequestFailed(key, message) {
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
    function retryKey(key) {
        source.clearRequestFailure(key);
        source.need(key);
    }

    // 连接与会话状态（live 才有；原型里可由截图 harness 用 simulate* 模拟）
    property bool simulateOffline: false
    property bool simulateExpired: false
    readonly property bool online: source.live ? source.live.online : !source.simulateOffline
    readonly property bool sessionExpired: source.live ? source.live.sessionExpired : source.simulateExpired
    function retry() {
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

    // 热门评论：只有 live 按需取；快照里带的是歌词那首的
    function comments(songId) {
        const c = source.d.comments;
        if (c && c.songId === songId)
            return c.items;
        source.need("comments:" + songId);
        return [];
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
            if (source.latestComments.loading)
                source.latestComments = Object.assign({}, source.latestComments, { loading: false, failed: true });
            if (source.artistSongs.loading)
                source.artistSongs = Object.assign({}, source.artistSongs, { loading: false, failed: true });
        }
    }

    // 最新评论：打开评论或滚到底时调用 moreComments
    property var latestComments: ({ owner: 0, items: [], total: 0, more: false, next: 0, loading: false })
    readonly property var commentPageRaw: source.d.commentPage || null
    onCommentPageRawChanged: {
        const page = source.commentPageRaw;
        if (page)
            source.latestComments = source.appendPage(source.latestComments, page.songId, page, page.items);
    }
    function moreComments(songId) {
        const acc = source.nextPage(source.latestComments, songId, 0, []);
        if (acc === null)
            return;
        source.latestComments = acc;
        source.need("comments:" + songId + ":" + acc.next);
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
        source.need("artistSongs:" + artistId + ":" + acc.next);
    }

    // 账号
    readonly property var account: source.p.account   // null = 没有登录快照

    // 榜单
    readonly property var toplists: source.d.toplists
    readonly property string chartsCaption: source.toplists.length > 0 ? source.toplists.length + " 个官方与特色榜单" : "暂时没有榜单"
    readonly property var hotChart: ({ name: source.d.playlist.name, count: source.d.playlist.count, tracks: source.d.tracks })

    // 推荐歌单（首页第二排）
    readonly property var playlists: source.p.playlists

    // 私人漫游：一个流，直接播放；快放完时 moreFm 续取一批（追加到 fmTracks）
    readonly property var fmTracks: source.pe.fm.tracks
    function moreFm() { source.need("fm:next"); }

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

    // 一位小数且去掉多余的 .0：59.9 GB / 60 GB。取整会把将满显示成「60 / 60」
    function gigabytes(bytes) {
        return Number((bytes / 1073741824).toFixed(1)) + " GB";
    }

    // 歌手 / 专辑：data 里只放最近取回的一个，id 对上才用；没对上就向 live 要，先给同形的空值
    // 简介常带全角空格的段首缩进：去掉，每行都从窗格左缘开始（对齐页边线）
    function unindent(text) {
        return (text || "").replace(/^[\s　]+/gm, "");
    }
    function artist(id) {
        const a = source.d.artist;
        if (a && (a.id === id || !source.live))
            return Object.assign({}, a, { description: source.unindent(a.description) });
        source.need("artist:" + id);
        return { id: id, name: "", cover: "", alias: "", description: "", musicSize: 0, albumSize: 0,
                 tracks: [], albums: [], failed: Boolean(source.failure("artist:" + id)) };
    }
    function album(id) {
        const a = source.d.album;
        if (a && (a.id === id || !source.live))
            return a;
        source.need("album:" + id);
        return { id: id, name: "", artist: "", artistId: 0, cover: "", description: "", company: "", publishTime: 0, tracks: [] };
    }

    // 用户主页：data 里只放最近取回的一个；资料 + 公开歌单
    function user(id) {
        const u = source.d.user;
        if (u && (u.id === id || !source.live))
            return u;
        source.need("user:" + id);
        return { id: id, name: "", avatar: "", background: "", signature: "",
                 followed: false, follows: 0, followeds: 0, playlistCount: 0,
                 vipType: 0, vipRights: {}, level: 0, listenSongs: 0,
                 createTime: 0, mutual: false, playlists: [],
                 failed: Boolean(source.failure("user:" + id)) };
    }

    // MV：data 里只放最近取回的一个，id 对上才用；没对上就向 live 要
    function mv(id) {
        const m = source.d.mv;
        if (m && (m.id === id || !source.live))
            return m;
        source.need("mv:" + id);
        return { id: id, name: "", cover: "", artist: "", artistId: 0, duration: 0, url: "",
                 playCount: 0, subscribed: false, description: "" };
    }

    // 写操作：都交给 live（Store），结果经 Store 的信号回来；原型里不写任何地方
    // 「加到歌单」的候选：自己创建的歌单，去掉「我喜欢的音乐」（往里加就是点红心）
    readonly property var addableLists: source.myPlaylists.filter(pl => pl.owned && pl.id !== (source.pe.liked.id || 0))
    function addToPlaylist(playlistId, songId) { if (source.live) source.live.addSongToPlaylist(playlistId, songId); }
    function removeFromPlaylist(playlistId, songId) { if (source.live) source.live.removeSongFromPlaylist(playlistId, songId); }
    function createPlaylist(name) { if (source.live) source.live.createPlaylist(name, false); }
    function renamePlaylist(id, name) { if (source.live) source.live.renamePlaylist(id, name); }
    function deletePlaylist(id) { if (source.live) source.live.deletePlaylist(id); }
    function setPlaylistSubscribed(id, subscribed) { if (source.live) source.live.setPlaylistSubscribed(id, subscribed); }
    // 歌单内曲目排序：ids 是完整的新顺序；原型只读，不写
    function reorderPlaylistTracks(id, ids) { if (source.live) source.live.reorderPlaylistTracks(id, ids); }
    function setArtistSubscribed(id, subscribed) { if (source.live) source.live.setArtistSubscribed(id, subscribed); }
    function setAlbumSubscribed(id, subscribed) { if (source.live) source.live.setAlbumSubscribed(id, subscribed); }
    // 评论点赞：live 走 Store 乐观更新；原型不写网络
    function setCommentLiked(songId, commentId, liked) { if (source.live) source.live.setCommentLiked(songId, commentId, liked); }
    function logout() { if (source.live) source.live.logout(); }

    // 当前句：最后一个开始时间不晚于 elapsed 的句子（正在播放页与桌面歌词共用这一条规则）
    function lyricIndex(lines, elapsed) {
        let i = 0;
        while (i + 1 < lines.length && lines[i + 1].t <= elapsed)
            i++;
        return i;
    }

    function lyrics(trackId) {
        if (trackId === source.d.lyricsTrackId)
            return source.d.lyrics;
        source.need("lyrics:" + trackId);
        return [];
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
            ? { key: "fm", title: "私人漫游", cover: source.sized(fm[0].cover, 400), caption: "立即播放：从" + source.quoted(fm[0].name) + "开始" }   // 唯一点了就放的入口，先说清楚
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
                { key: "charts", title: "排行榜", symbol: "office-chart-line-symbolic", caption: source.chartsCaption },
                { key: "liked", title: "我喜欢的音乐", locked: true, caption: "登录后同步红心歌曲" },
            ];
        }

        const daily = p.daily.tracks;
        // 心动模式的封面避开种子歌（种子就是红心第一首，和「我喜欢」的封面会重复）
        const heartCover = p.heart.tracks.find(t => !p.heart.seed || t.id !== p.heart.seed.id);
        const radar = p.radar;
        return [
            daily.length > 0
                ? { key: "daily", title: "每日推荐", cover: source.sized(daily[0].cover, 400), caption: "从" + source.quoted(daily[0].name) + "听起" }
                : { key: "daily", title: "每日推荐", symbol: "view-calendar-day-symbolic", caption: "今天的推荐还在生成" },
            p.heart.seed && p.heart.tracks.length > 0
                ? { key: "heart", title: "心动模式", cover: source.sized(heartCover ? heartCover.cover : p.heart.seed.cover, 400), caption: "从" + source.quoted(p.heart.seed.name) + "出发" }
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
        case "heart": return {
            heading: "心动模式", cover: source.sized(p.heart.seed ? p.heart.seed.cover : "", 560), tracks: p.heart.tracks,
            facts: [source.songs(p.heart.tracks.length)],
            description: p.heart.seed ? "从红心歌曲" + source.quoted(p.heart.seed.name) + "出发，混合你的红心与相似推荐" : "",
            emptyText: "先给喜欢的歌点个红心，心动模式会从它出发" };
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
        case "cloud": return {
            heading: "音乐云盘", cover: source.sized(source.firstCover(source.cloud.tracks), 560), tracks: source.cloud.tracks,
            facts: [source.songs(source.cloud.count), { value: source.gigabytes(source.cloud.size), label: "/ " + source.gigabytes(source.cloud.maxSize) }],
            description: "", emptyText: "云盘里还没有歌，可以在官方客户端上传" };
        default: {
            // 推荐歌单：key = "playlist:<序号>"，快照只带了前几个的曲目
            // 歌单：key = "playlist:<id>"，推荐歌单与「我的」歌单都可以。曲目先看卡片自带的（快照），
            // 再看按需取回的 playlistDetails；都没有就向 live 要一次
            if (key.startsWith("playlist:")) {
                // 卡片可能来自推荐歌单、我的歌单或排行榜；都没有时用按需取回的详情（它自带名称与封面）
                const id = Number(key.slice(9));
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
            return { heading: pl.name, cover: source.sized(pl.cover, 560), tracks: source.d.tracks, ranked: true,
                     facts: facts, description: pl.description, emptyText: "榜单暂时为空" };
        }
        }
    }
}
