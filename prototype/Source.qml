// 数据模块：页面只问「要什么」（某个列表、首页入口、某首歌的歌词），不碰快照的原始形状。
// 缺数据时怎么退化（没有封面、空列表、没有种子歌）也只在这里决定，页面拿到的永远是可直接渲染的值。
// 原始数据有三个来源，形状完全相同（data / personal 两个对象，字段见 DESIGN.md「后端接口」）：
//   live 非空      = C++ Store（应用里注入，实时接口，按需加载）
//   fixture 非空   = Fixtures.js 压力数据（worst | empty | one）
//   都没有         = 快照（Data.js 匿名 + Personal.js 个人）
pragma Singleton

import QtQuick
import "Data.js" as Data
import "Personal.js" as Personal
import "Fixtures.js" as Fixtures

QtObject {
    id: source

    property string fixture: ""
    property var live: null         // C++ Store（鸭子类型）

    readonly property var d: source.live ? source.live.data : source.fixture === "" ? Data : Fixtures[source.fixture].data
    readonly property var p: source.live ? source.live.personal : source.fixture === "" ? Personal : Fixtures[source.fixture].personal

    // 按需加载：只有 live 来源需要（快照与压力数据是一次给全的）。key 见 DESIGN.md，Store 自己去重
    function need(key) {
        if (source.live)
            Qt.callLater(() => source.live.request(key));
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

    // ── 红心：id → true 的查找表（一千多首，逐行查要 O(1)）──
    readonly property var likedSet: {
        const set = {};
        for (const id of (source.p.likedIds || []))
            set[id] = true;
        return set;
    }
    function isLiked(id) { return source.likedSet[id] === true; }

    // ── 热门评论：只有 live 按需取；快照里带的是歌词那首的 ──
    function comments(songId) {
        const c = source.d.comments;
        if (c && c.songId === songId)
            return c.items;
        source.need("comments:" + songId);
        return [];
    }

    // ── 账号 ──
    readonly property var account: source.p.account   // null = 没有登录快照

    // ── 榜单 ──
    readonly property var toplists: source.d.toplists
    readonly property string chartsCaption: source.toplists.length > 0 ? source.toplists.length + " 个官方与特色榜单" : "暂时没有榜单"
    readonly property var hotChart: ({ name: source.d.playlist.name, count: source.d.playlist.count, tracks: source.d.tracks })

    // ── 推荐歌单（首页第二排）──
    readonly property var playlists: source.p.playlists

    // ── 私人漫游：一个流，直接播放 ──
    readonly property var fmTracks: source.pe.fm.tracks

    // ── 我的：最近播放、云盘、创建与收藏的歌单（缺字段时给稳定的空形状）──
    readonly property var recent: source.p.recent || { total: 0, tracks: [] }
    readonly property var cloud: source.p.cloud || { count: 0, size: 0, maxSize: 0, tracks: [] }
    readonly property var myPlaylists: (source.p.mine || []).map(pl => Object.assign({ owned: true }, pl))
        .concat((source.p.subscribed || []).map(pl => Object.assign({ owned: false }, pl)))

    // ── 搜索：快照只有一个查询词的结果；热门搜索是匿名可得的 ──
    readonly property var hotSearches: source.d.hotSearches || []
    readonly property var searchResult: source.d.search || { query: "", songs: [], artists: [], playlists: [], albums: [] }

    // 一位小数且去掉多余的 .0：59.9 GB / 60 GB——取整会把将满显示成「60 / 60」
    function gigabytes(bytes) {
        return Number((bytes / 1073741824).toFixed(1)) + " GB";
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
    function firstCover(tracks) { return tracks.length > 0 ? tracks[0].cover : ""; }
    function songs(n) { return { value: String(n), label: "首" }; }

    // ── 首页第一排：为你推荐的入口。每项 { key, title, caption, cover | symbol, locked } ──
    function homeEntries(loggedIn) {
        const p = source.pe;
        const fm = p.fm.tracks;
        const similar = p.similar;
        const fmEntry = fm.length > 0
            ? { key: "fm", title: "私人漫游", cover: source.sized(fm[0].cover, 400), caption: "从" + source.quoted(fm[0].name) + "开启漫游" }
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

    // ── 列表页：{ heading, cover, facts, description, tracks, ranked, emptyText } ──
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
                const id = Number(key.slice(9));
                const card = source.playlists.concat(source.myPlaylists).find(pl => pl.id === id) || { id: id, name: "", cover: "", count: 0 };
                const detail = (p.playlistDetails || {})[id] || {};
                const tracks = (card.tracks && card.tracks.length > 0) ? card.tracks : detail.tracks || [];
                if (tracks.length === 0 && card.count > 0)
                    source.need(key);
                return { heading: card.name, cover: source.sized(card.cover, 560), tracks: tracks,
                         facts: card.count > 0 ? [source.songs(card.count)] : [],
                         description: card.description || detail.description || card.reason || "",
                         emptyText: source.live ? "正在取回曲目…" : "这个歌单的曲目还没取到" };
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
