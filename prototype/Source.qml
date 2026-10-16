// 数据模块：页面只问「要什么」（某个列表、首页入口、某首歌的歌词），不碰快照的原始形状。
// 缺数据时怎么退化（没有封面、空列表、没有种子歌）也只在这里决定，页面拿到的永远是可直接渲染的值。
// 数据来源：fixture 为空 = 真实快照（Data.js 匿名 + Personal.js 个人）；worst | empty | one = Fixtures.js 压力数据。
// 以后接 C++ 后端时，替换的就是这一个模块。
pragma Singleton

import QtQuick
import "Data.js" as Data
import "Personal.js" as Personal
import "Fixtures.js" as Fixtures

QtObject {
    id: source

    property string fixture: ""

    readonly property var d: source.fixture === "" ? Data : Fixtures[source.fixture].data
    readonly property var p: source.fixture === "" ? Personal : Fixtures[source.fixture].personal

    // ── 账号 ──
    readonly property var account: source.p.account   // null = 没有登录快照

    // ── 榜单 ──
    readonly property var toplists: source.d.toplists
    readonly property string chartsCaption: source.toplists.length > 0 ? source.toplists.length + " 个官方与特色榜单" : "暂时没有榜单"
    readonly property var hotChart: ({ name: source.d.playlist.name, count: source.d.playlist.count, tracks: source.d.tracks })

    // ── 推荐歌单（首页第二排）──
    readonly property var playlists: source.p.playlists

    // ── 私人漫游：一个流，直接播放 ──
    readonly property var fmTracks: source.p.fm.tracks

    function lyrics(trackId) {
        return trackId === source.d.lyricsTrackId ? source.d.lyrics : [];
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
        const p = source.p;
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
        const p = source.p;
        switch (key) {
        case "daily": return {
            heading: "每日推荐", cover: source.sized(source.firstCover(p.daily.tracks), 560), tracks: p.daily.tracks,
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
        default: {
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
