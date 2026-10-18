.pragma library

// 压力测试数据（break-ui）：与 Data.js / Personal.js 同形状，只在数据边界替换，组件不改。
// 每个值都是网易云上真实会出现的形态，不是随机长串：Live 版长标题、多人合唱、一小时混音、
// 付费专辑（fee=4）、缺封面、坏链接、空列表、1 首、五位数红心。
// 用法：main.qml 的 fixture 属性 = worst | empty | one（截图：-- fixture=worst page=home）

var C1 = "http://p1.music.126.net/H2DFRdwP2y5SuCAn-Tuikw==/109951172568091306.jpg?param=120y120";
var C2 = "https://p1.music.126.net/urByD_AmfBDBrs7fA9-O8A==/109951167976973225.jpg?param=300y300";
var BROKEN = "https://p1.music.126.net/does-not-exist/0.jpg?param=120y120";

// x：可选的新字段（alias 别名 / quality 音质 / reason 推荐理由 / mv / artistId / albumId）。
// 没有专辑名的曲目也没有专辑 id（曲目菜单里「专辑」项随之不可点）
function t(id, name, artist, album, dur, fee, cover, x) {
    x = x || {};
    const artistId = x.artistId !== undefined ? x.artistId : 1;
    return { id: id, name: name, artist: artist, album: album, dur: dur, fee: fee, cover: cover,
             alias: x.alias || "", quality: x.quality || "", reason: x.reason || "", mv: x.mv || 0,
             artistId: artistId, albumId: x.albumId !== undefined ? x.albumId : (album ? 1 : 0),
             artists: artist.split(" / ").map((n, i) => ({ id: artistId > 0 ? artistId + i : 0, name: n })) };
}

// 前几行各带一种失败，像真实数据那样混在一起
var worstTracks = [
    t(901, "我想要两颗西柚（Live）- 2023 网易云音乐年度盛典现场版", "茄子蛋", "2023 网易云音乐年度盛典 Live 合辑", 251, 1, C1,
      { alias: "电视剧《我的人间烟火》片尾曲·特别企划合唱版", quality: "hires", reason: "根据你喜欢的单曲《浪子回头》以及最近常听的台语独立音乐推荐" }),
    t(902, "Pretender", "Official髭男dism / Aimer / LiSA / 藤井風 / 米津玄師 / King Gnu", "Traveler", 326, 4, C2,
      { alias: "伪装者", quality: "lossless", reason: "猜你喜欢" }),
    t(903, "雨", "A", "", 61, 0, "", { quality: "lossless" }),
    t(904, "Lo-fi Hip Hop Radio - beats to relax/study to (3 Hour Mix)", "Lofi Girl", "Lofi Girl Mixes Vol. 12: Late Night Study Session", 10835, 8, BROKEN),
    t(905, "Supercalifragilisticexpialidocious (From \"Mary Poppins\" / Soundtrack Version)", "Julie Andrews / Dick Van Dyke / The Pearlies", "Mary Poppins (Original Motion Picture Soundtrack / 50th Anniversary Edition)", 0, 1, C1,
      { quality: "hires", alias: "欢乐满人间" }),
    t(906, "😭😭😭", "🌙", "🌙", 199, 8, C2),
];
for (var i = 7; i <= 1200; i++)
    worstTracks.push(t(900 + i, "第 " + i + " 首：平凡的一天", "毛不易", "平凡的一天", 240, i % 3 === 0 ? 1 : 8, i % 7 === 0 ? "" : C1,
                       { quality: i % 2 ? "lossless" : "", reason: i % 5 === 0 ? "最近常听" : "" }));

var worstLyrics = [
    { t: 0, text: "作词 : Official髭男dism　作曲 : Official髭男dism　编曲 : Official髭男dism / 蔦谷好位置", tr: "" },
    { t: 10, text: "もっと違う設定で もっと違う関係で 出会える世界線 選べたらよかった（更长的一句日文歌词，看它在半屏宽度里会不会被截断）",
      tr: "如果能选择一个以不同的设定、不同的关系相遇的世界线就好了（同样很长的一句译文，看它会不会被截断）" },
    { t: 20, text: "纯音乐，请欣赏", tr: "" },
    { t: 30, text: "", tr: "" },
    { t: 40, text: "最后一句", tr: "the last line" },
];

var worstComments = { songId: 903, items: [
    { user: "一只想要变成猫但是没有成功的柴犬Official", userId: 42, avatar: C2, likes: 1284567, time: 1700000000000, id: 9001, liked: false,
      content: "第一次听这首歌是在高三晚自习结束后回家的路上，耳机里放着它，路灯一盏一盏往后退。那时候觉得未来很远，现在回头看，原来那就是最好的时候。这条评论写得很长，用来看评论在半屏宽度里最多显示几行、省略号落在哪里。还要再长一点，再长一点，再长一点。" },
    { user: "A", userId: 43, avatar: "", likes: 1, time: 1700000000000, id: 9002, liked: false, content: "好" },
    { user: "🌙", userId: 44, avatar: BROKEN, likes: 99999, time: 1700000000000, id: 9003, liked: true, content: "😭😭😭" },
] };

var worstHot = [];
for (var h = 1; h <= 20; h++)
    worstHot.push({ word: h === 1 ? "我想要两颗西柚（Live）- 2023 网易云音乐年度盛典现场版" : "热搜 " + h, note: h === 2 ? "这条热搜下面附带了一句很长很长的说明文字，用来看它在半屏宽度里会不会挤掉词本身" : (h % 4 === 0 ? "" : "新"), score: 1000000 - h });

var worstSearch = {
    query: "我想要两颗西柚（Live）",
    songs: worstTracks.slice(0, 12),
    artists: [
        { id: 1, name: "Official髭男dism（ヒゲダン）Japanese Pop Band", cover: C2, albums: 1284 },
        { id: 2, name: "A", cover: "", albums: 0 },
        { id: 3, name: "🌙", cover: BROKEN, albums: 1 },
    ],
    playlists: [
        { id: 1, name: "【温柔女声】在深夜被治愈：适合一个人听的 100 首华语民谣与独立音乐", cover: C2, count: 12345, creator: "一只想要变成猫但是没有成功的柴犬Official" },
        { id: 2, name: "x", cover: "", count: 0, creator: "" },
    ],
    albums: [
        { id: 1, name: "2023 网易云音乐年度盛典 Live 合辑（Deluxe Edition / 含未公开彩排录音与幕后访谈）", artist: "群星", cover: C2, year: 2023 },
        { id: 2, name: "1", artist: "", cover: "", year: 0 },
        { id: 3, name: "🌙", artist: "🌙", cover: BROKEN, year: 1999 },
    ],
};

// 歌手页：长名 + 长别名 + 长简介；单曲与专辑数到五位；专辑里混着缺封面、坏链接、没有年份的
var worstArtist = {
    id: 1, name: "Official髭男dism（ヒゲダン）Japanese Pop Band", cover: C2, alias: "官方髭男 / 胡子男 / Higedan",
    description: "日本四人乐队，2012 年在岛根县组建。成员为藤原聪（主唱、键盘）、小笹大辅（吉他）、楢崎诚（贝斯、萨克斯）与松浦匡希（鼓）。代表作《Pretender》《I LOVE...》《Subtitle》，多次登上公信榜年度榜首。",
    musicSize: 12345, albumSize: 1284, followed: false, tracks: worstTracks.slice(0, 50),
    albums: [
        { id: 1, name: "Traveler（初回限定盘 / 附 Blu-ray 演唱会实录与纪录片）", cover: C2, year: 2019, count: 14 },
        { id: 2, name: "Editorial", cover: "", year: 2021, count: 0 },
        { id: 3, name: "🌙", cover: BROKEN, year: 0, count: 1 },
        { id: 4, name: "Rejoice", cover: C1, year: 2024, count: 12 },
        { id: 5, name: "Subtitle", cover: C2, year: 2022, count: 2 },
    ],
};
var worstAlbum = {
    id: 1, name: "2023 网易云音乐年度盛典 Live 合辑（Deluxe Edition / 含未公开彩排录音与幕后访谈）", artist: "群星", artistId: 1,
    cover: C2, description: "收录 2023 年度盛典全部现场演出。\n第二段说明录音与混音的出处、版权方，以及数字版与实体版的曲目差异，足够把三行占满还溢出。",
    company: "网易云音乐（杭州）文化传媒有限公司", publishTime: 1700000000000, tracks: worstTracks.slice(0, 40),
};

// 百科的最坏情况：长简介、多标签、长名字、没有 id 的创作者、坏封面、无效的副歌时间（end ≤ start）
var worstWiki = {
    songId: 903, loaded: true, empty: false, songName: "我想要两颗西柚（Live）- 2023 网易云音乐年度盛典现场版", albumName: "", artistName: "茄子蛋 / 告五人", cover: BROKEN,
    description: "这首歌最早是在一次深夜排练里即兴出来的，后来在年度盛典上第一次完整演出。\n现场版保留了观众合唱的部分，所以比录音室版本长了将近一分钟，这一段也成了很多人反复循环的理由。编曲加入了弦乐四重奏与一段很长的吉他独奏，副歌之前的停顿是故意留给观众的。",
    memory: { period: "清晨", season: "初冬", time: "06:05", timestamp: 1577836800000, playCount: 1234567, text: "这首歌你听了一百多万次，比任何人都多。" },
    chorus: { start: 0, end: 0, locked: false },
    basic: [{ title: "语种", content: "国语", tags: [] },
            { title: "曲风", content: "", tags: ["华语流行", "独立摇滚", "后摇", "民谣", "现场", "City Pop", "Neo-Soul", "实验电子"] },
            { title: "推荐场景", content: "深夜独处、长途驾驶、雨天、通勤路上、一个人散步的时候", tags: [] }],
    creators: [{ role: "作词", people: [{ id: 2, name: "一个名字特别长的作词人（Featuring 另一个也很长的名字）", avatar: "", subscribed: false }] },
               { role: "作曲", people: [{ id: 3, name: "茄子蛋", avatar: "", subscribed: true }, { id: 4, name: "告五人", avatar: "", subscribed: false }, { id: 0, name: "没有歌手页的合作者", avatar: "", subscribed: false }] },
               { role: "编曲 / 混音 / 母带", people: [{ id: 0, name: "Jo", avatar: "", subscribed: false }] }],
    mvs: [{ id: 1, jumpUrl: "orpheus://mv/1", name: "我想要两颗西柚（Live）- 2023 网易云音乐年度盛典现场版 官方完整版 MV（含幕后花絮与彩蛋）", artist: "茄子蛋 / 告五人 / 草东没有派对 / 落日飞车", cover: BROKEN, duration: 3725, publishTime: "2023-12-31", playCount: 99999999999 },
          { id: 2, jumpUrl: "orpheus://mv/2", name: "x", artist: "", cover: "", duration: 0, publishTime: "", playCount: 0 }],
    reviews: [{ id: 1, type: "精选乐评", content: "这是一段很长的乐评，长到会在窄窗里换好几行。它讲的是一个人在凌晨的出租车上第一次听到这首歌，窗外是还没亮的城市，司机把收音机的声音调大了一点，然后两个人谁也没有说话，一直到目的地。", user: "一只想要变成猫但是没有成功的柴犬Official", likes: 123456 },
              { id: 2, type: "", content: "好听", user: "", likes: 0 }],
    sheets: [],
    entertainments: [{ title: "一部名字非常长的电视剧：第二季第十二集片尾曲（导演剪辑版）", type: "电视剧", resourceType: "", cover: "" },
                     { title: "综艺", type: "", resourceType: "", cover: "" }],
    awards: [{ title: "第 2 届某流行音乐奖暨年度华语音乐盛典（网络投票单元）", awardName: "年度最受欢迎现场演出歌曲", result: "提名", cover: BROKEN, jumpUrl: "" }],
};

var worst = {
    data: {
        hotSearches: worstHot,
        search: worstSearch,
        toplists: [
            { id: 1, name: "飙升榜", cover: C2, update: "每天更新", count: 100, preview: [{ name: "我想要两颗西柚（Live）- 2023 网易云音乐年度盛典现场版", artist: "茄子蛋 / 告五人 / 草东没有派对 / 落日飞车" }, { name: "雨", artist: "A" }] },
            { id: 2, name: "网易云中文说唱榜单（每周五 0 点更新，数据来自全平台播放与收藏）", cover: "", update: "每周五更新", count: 50, preview: [] },
            { id: 3, name: "新歌榜", cover: BROKEN, update: "", count: 0, preview: [{ name: "😭", artist: "🌙" }, { name: "x", artist: "y" }, { name: "z", artist: "w" }] },
        ],
        playlist: { id: 3778678, name: "网易云音乐 2026 年度听歌报告：你最常在深夜循环的 200 首歌", cover: "", description: "这是一段非常长的歌单简介，包含换行。\n第二段说明它是怎么生成的，以及更新频率、数据口径、收录规则和版权说明，足够把三行占满还溢出。", count: 12345, plays: 99999999999 },
        tracks: worstTracks.slice(0, 200),
        lyricsTrackId: 903,   // 原型的「正在播放」是热歌榜第 3 首
        lyrics: worstLyrics,
        lyricsPureMusic: false,
        comments: worstComments,
        artist: worstArtist,
        album: worstAlbum,
        wiki: worstWiki,
        userSubcount: { artistCount: 1284, mvCount: 99999, createdPlaylistCount: 2, subPlaylistCount: 1, djRadioCount: 12 },
        newSongs: worstTracks.slice(0, 20),
        newAlbums: [{ id: 1, name: "我想要两颗西柚（Live）- 2023 网易云音乐年度盛典现场版 豪华纪念版（含未公开 Demo 与现场返场）", cover: BROKEN, artist: "茄子蛋 / 告五人 / 草东没有派对 / 落日飞车", artistId: 2, publishTime: 1700000000000, size: 128 },
                    { id: 2, name: "x", cover: "", artist: "", artistId: 0, publishTime: 0, size: 0 },
                    { id: 3, name: "雨", cover: C2, artist: "A", artistId: 1, publishTime: 1600000000000, size: 1 }],
        highqualityTags: [{ name: "华语", hot: true }, { name: "流行", hot: true }, { name: "摇滚", hot: false }, { name: "民谣", hot: true },
                          { name: "电子", hot: false }, { name: "说唱", hot: false }, { name: "轻音乐", hot: true }, { name: "爵士", hot: false },
                          { name: "古典", hot: false }, { name: "ACG", hot: true }, { name: "影视原声", hot: false }, { name: "夜晚", hot: false }],
        highquality: { cat: "全部", more: true, next: 1700000000000, items: [
            { id: 1, name: "【温柔女声】在深夜被治愈：适合一个人听的 100 首华语民谣与独立音乐", cover: C2, count: 100, plays: 99999999999,
              description: "这是一段非常长的歌单简介，足够把两行占满还溢出。", creator: "一只想要变成猫但是没有成功的柴犬Official" },
            { id: 2, name: "x", cover: "", count: 0, plays: 0, description: "", creator: "" },
            { id: 3, name: "动态歌单", cover: BROKEN, count: 1, plays: 1, description: "", creator: "Jo" }] },
        recommendedMvs: [{ id: 1, name: "我想要两颗西柚（Live）- 2023 网易云音乐年度盛典现场版 官方完整版 MV（含幕后花絮与彩蛋）", cover: BROKEN, artist: "茄子蛋 / 告五人 / 草东没有派对 / 落日飞车", artistId: 2, duration: 3725, playCount: 99999999999, reason: "编辑推荐：一段很长很长的推荐理由，会被省略" },
                         { id: 2, name: "x", cover: "", artist: "", artistId: 0, duration: 0, playCount: 0, reason: "" }],
        recommendedPrograms: [program(992, "第 128 期：在深夜被治愈——和三位独立音乐人聊他们各自最难熬的那一年（上）", "一只想要变成猫但是没有成功的柴犬Official", "深夜电台：给睡不着的人", 10800),
                              program(993, "x", "", "", 0)],
        radio: { id: 7, name: "深夜电台：给睡不着的人，以及那些在路上的人（每周五 0 点更新）", cover: BROKEN,
                 description: "这是一段非常长的电台简介，包含换行。\n第二段说明节目怎么做、多久更新、由谁主持，以及投稿方式，足够把三行占满还溢出。",
                 dj: { id: 2, name: "一只想要变成猫但是没有成功的柴犬Official", avatar: BROKEN }, programCount: 1284, subCount: 99999999, playCount: 99999999999,
                 more: true, next: 2, programs: [program(992, "第 128 期：在深夜被治愈——和三位独立音乐人聊他们各自最难熬的那一年（上）", "一只想要变成猫但是没有成功的柴犬Official", "深夜电台", 10800),
                                                  program(993, "x", "", "", 0)] },
        searchSuggest: { query: "x", order: ["artists", "songs", "playlists", "albums"],
                         songs: [{ id: 901, name: "我想要两颗西柚（Live）- 2023 网易云音乐年度盛典现场版", artist: "茄子蛋 / 告五人 / 草东没有派对 / 落日飞车", album: "" },
                                 { id: 902, name: "x", artist: "", album: "" }],
                         artists: [{ id: 2, name: "一只想要变成猫但是没有成功的柴犬Official", cover: BROKEN }],
                         albums: [], playlists: [{ id: 1, name: "【温柔女声】在深夜被治愈：适合一个人听的 100 首华语民谣与独立音乐", cover: C2, count: 100 }] },
        playlistComments: { playlistId: 11, total: 319443, items: worstComments.items },
        mv: { id: 1, name: "我想要两颗西柚（Live）- 2023 网易云音乐年度盛典现场版 官方完整版 MV", cover: C1,
              artist: "一只想要变成猫但是没有成功的柴犬Official", artistId: 2, duration: 241000, url: "",
              playCount: 123456789, subscribed: false, description: "", failed: false },
        searchUsers: { query: "x", items: [{ id: 2, name: "一只想要变成猫但是没有成功的柴犬Official", cover: BROKEN, followed: false, fans: 99999999, follows: 1284, playlists: 0, signature: "" },
                                           { id: 3, name: "x", cover: "", followed: true, fans: 0, follows: 0, playlists: 0, signature: "" }] },
        user: { id: 2, name: "一只想要变成猫但是没有成功的柴犬Official", avatar: BROKEN, background: "", signature: "这是一段很长的签名，长到会在窄窗里换好几行，里面还有一个链接 https://music.163.com/ 和几个表情 🐕🐈",
                followed: false, follows: 1284, followeds: 99999999, playlistCount: 0, vipType: 11, vipRights: {}, level: 10, listenSongs: 99999,
                createTime: 0, mutual: false, playlists: [], restricted: false, failed: false },
        userEvents: { userId: 2, more: true, next: 1600000000000, items: [
            { id: 1, action: "分享单曲", text: "这是一段很长的动态，长到会在窄窗里换好几行。凌晨三点，窗外下着雨，循环了这首歌一百遍，突然觉得一切都会好起来的。#深夜# #循环#", time: 1700000000000,
              user: { id: 1001, name: "一只想要变成猫但是没有成功的柴犬Official", avatar: BROKEN },
              resource: { kind: "song", id: 901, name: worstTracks[0].name, cover: BROKEN, artist: worstTracks[0].artist, track: worstTracks[0] }, likes: 123456, comments: 9999, shares: 0 },
            { id: 2, action: "分享歌单", text: "", time: 1600000000000, user: { id: 1001, name: "x", avatar: "" },
              resource: { kind: "playlist", id: 1, name: "x", cover: "", artist: "" }, likes: 0, comments: 0, shares: 0 },
            { id: 3, action: "", text: "只有文字的动态", time: 1500000000000, user: { id: 1001, name: "x", avatar: "" },
              resource: { kind: "other", id: 0, name: "", cover: "", artist: "" }, likes: 1, comments: 1, shares: 1 }] },
        notices: { more: true, next: 1600000000000, items: [
            { id: 1, time: 1700000000000, user: { id: 2, name: "一只想要变成猫但是没有成功的柴犬Official", avatar: BROKEN }, text: "收藏了你的歌单",
              resource: { kind: "playlist", id: 11, name: "我喜欢的音乐之外的、真正只在深夜一个人开车时才会听的歌（2019—2026 持续更新）", cover: C2, artist: "" } },
            { id: 2, time: 1600000000000, user: { id: 0, name: "", avatar: "" }, text: "你的账号已在新设备登录，如果不是你本人操作，请尽快修改密码并检查账号安全设置。",
              resource: { kind: "other", id: 0, name: "", cover: "", artist: "" } }] },
        commentNotices: { more: true, next: 1600000000000, total: 99999, items: [
            { id: "1", time: 1700000000000, user: { id: 2, name: "一只想要变成猫但是没有成功的柴犬Official", avatar: BROKEN },
              content: "这是一条很长的回复，长到会在窄窗里换好几行。我也是在凌晨的出租车上第一次听到的，司机把收音机的声音调大了一点。",
              repliedContent: "这是一段很长的乐评，长到会在窄窗里换好几行。它讲的是一个人在凌晨的出租车上第一次听到这首歌。",
              resource: { kind: "song", id: 901, name: worstTracks[0].name, cover: "", artist: worstTracks[0].artist } },
            { id: "2", time: 1600000000000, user: { id: 3, name: "x", avatar: "" }, content: "好", repliedContent: "",
              resource: { kind: "other", id: 0, name: "", cover: "", artist: "" } }] },
        forwards: { more: true, next: 2, newCount: 99, items: [
            { id: 1, time: 1700000000000, user: { id: 2, name: "一只想要变成猫但是没有成功的柴犬Official", avatar: BROKEN },
              message: "@一只想要变成猫但是没有成功的柴犬Official 这首歌你一定要听，凌晨三点循环了一百遍。",
              resource: { kind: "song", id: 901, name: worstTracks[0].name, cover: BROKEN, artist: worstTracks[0].artist, track: worstTracks[0] } },
            { id: 2, time: 1600000000000, user: { id: 3, name: "x", avatar: "" }, message: "@了你",
              resource: { kind: "video", id: 0, vid: "A1B2", name: "x", cover: "", artist: "" } }] },
    },
    personal: {
        account: { id: 1001, name: "一只想要变成猫但是没有成功的柴犬Official", vip: false, avatar: "" },
        savedArtists: { more: true, next: 3, count: 1284, items: [
            { id: 2, name: "一只想要变成猫但是没有成功的柴犬Official", cover: BROKEN, alias: "Shiba Inu Who Wanted To Be A Cat", albums: 128, mvs: 99 },
            { id: 3, name: "x", cover: "", alias: "", albums: 0, mvs: 0 }, { id: 1, name: "A", cover: C2, alias: "", albums: 1, mvs: 1 }] },
        savedAlbums: { more: false, next: 2, count: 2, items: [
            { id: 1, name: "我想要两颗西柚（Live）- 2023 网易云音乐年度盛典现场版 豪华纪念版", cover: BROKEN, artist: "茄子蛋 / 告五人 / 草东没有派对 / 落日飞车", artistId: 2, size: 128 },
            { id: 2, name: "x", cover: "", artist: "", artistId: 0, size: 0 }] },
        savedMvs: { more: false, next: 2, count: 2, items: [
            { kind: "mv", id: 1, vid: "", name: "我想要两颗西柚（Live）- 2023 网易云音乐年度盛典现场版 官方完整版 MV", cover: BROKEN, artist: "茄子蛋 / 告五人", duration: 3725, playCount: 99999999999 },
            { kind: "video", id: 0, vid: "A1B2", name: "x", cover: "", artist: "", duration: 0, playCount: 0 }] },
        daily: { date: "10月17日", day: "17", tracks: worstTracks.slice(0, 30) },
        heart: { seed: worstTracks[1], tracks: worstTracks.slice(1, 3) },
        radar: { id: 3136952023, name: "私人雷达", description: "今天《我想要两颗西柚（Live）- 2023 网易云音乐年度盛典现场版》爱不释耳", cover: BROKEN, tracks: worstTracks.slice(0, 30) },
        similar: { seed: worstTracks[0], tracks: [] },
        liked: { id: 1, count: 12345, tracks: worstTracks },
        likedIds: [901, 902, 905, 906],
        recent: { total: 99999, tracks: worstTracks.slice(0, 30) },
        cloud: { count: 1284, size: 64315222016, maxSize: 64424509440, tracks: worstTracks.slice(0, 30) },
        mine: [
            { id: 11, name: "我喜欢的音乐之外的、真正只在深夜一个人开车时才会听的歌（2019—2026 持续更新）", cover: C2, count: 1284, reason: "" },
            { id: 12, name: "空", cover: "", count: 0, reason: "" },
        ],
        subscribed: [
            { id: 13, name: "收藏的歌单", cover: BROKEN, count: 1, reason: "" },
        ],
        playlists: [
            { id: 1, name: "【温柔女声】在深夜被治愈：适合一个人听的 100 首华语民谣与独立音乐", cover: C2, count: 100, reason: "根据你喜欢的单曲《我想要两颗西柚（Live）- 2023 网易云音乐年度盛典现场版》推荐" },
            { id: 2, name: "x", cover: "", count: 0, reason: "" },
            { id: 3, name: "动态歌单", cover: BROKEN, count: 0, reason: "" },
            { id: 4, name: "1", cover: C2, count: 1, reason: "" },
        ],
        fm: { tracks: [worstTracks[3]] },
    },
};

var empty = {
    data: { toplists: [], playlist: { id: 0, name: "空歌单", cover: "", description: "", count: 0, plays: 0 }, tracks: [], lyricsTrackId: 0, lyrics: [], lyricsPureMusic: false,
            hotSearches: [], search: { query: "没有结果的搜索词", songs: [], artists: [], playlists: [], albums: [] } },
    personal: {
        account: { id: 1002, name: "新用户", vip: false, avatar: "" },
        daily: { date: "10月17日", day: "17", tracks: [] },
        heart: { seed: null, tracks: [] },
        radar: { id: 3136952023, name: "私人雷达", description: "", cover: "", tracks: [] },
        similar: { seed: null, tracks: [] },
        liked: { id: 1, count: 0, tracks: [] },
        recent: { total: 0, tracks: [] },
        cloud: { count: 0, size: 0, maxSize: 0, tracks: [] },
        mine: [],
        subscribed: [],
        playlists: [],
        fm: { tracks: [] },
    },
};

var oneTrack = [t(990, "雨", "A", "雨", 61, 8, C1)];
// 播客节目：能直接播放的曲目（id 是节目的主歌曲），另带节目字段
function program(id, name, dj, radio, dur, x) {
    return Object.assign(t(id, name, dj, radio, dur, 0, C2, { artistId: 0, albumId: 0 }),
                         { programId: id + 10000, radioId: 7, serial: 1, createTime: 1700000000000, description: "" }, x || {});
}
var oneProgram = program(991, "第 1 期：雨声", "Jo", "雨天电台", 1800);
// 每栏一项、各字段齐全的百科（形状见 src/sdk/samples/song_wiki.normalized.json）
var oneWiki = {
    songId: 990, loaded: true, empty: false, songName: "雨", albumName: "雨", artistName: "A", cover: C2,
    description: "《雨》是 A 写给夏天最后一场雷阵雨的歌。",
    memory: { period: "深夜", season: "仲夏", time: "01:25", timestamp: 1657387559000, playCount: 1887, text: "喜欢的歌，就要听到天荒地老！" },
    chorus: { start: 42.402, end: 80.866, locked: false },
    basic: [{ title: "语种", content: "国语", tags: [] }, { title: "曲风", content: "", tags: ["流行"] }],
    creators: [{ role: "作词", people: [{ id: 1, name: "A", avatar: "", subscribed: false }] }],
    mvs: [{ id: 22621587, jumpUrl: "orpheus://mv/22621587", name: "雨", artist: "A", cover: C2, duration: 221, publishTime: "2022-08-03", playCount: 2750 }],
    reviews: [{ id: 1, type: "精选乐评", content: "第一次听是在回家的末班车上。", user: "某人", likes: 7 }],
    sheets: [{ id: 1, name: "雨（钢琴独奏）", types: ["钢琴"], difficulty: "初级", key: "C", bpm: 72, pages: 2, cover: "" }],
    entertainments: [{ title: "夏天的尾巴", type: "电视剧插曲", resourceType: "", cover: "" }],
    awards: [{ title: "第 2 届某流行音乐奖", awardName: "年度歌曲", result: "获奖", cover: "", jumpUrl: "" }],
};
var one = {
    data: {
        toplists: [{ id: 1, name: "飙升榜", cover: C2, update: "每天更新", count: 1, preview: [{ name: "雨", artist: "A" }] }],
        playlist: { id: 1, name: "雨", cover: C2, description: "", count: 1, plays: 1 },
        tracks: oneTrack,
        lyricsTrackId: 990,
        lyrics: [{ t: 0, text: "只有一句" }],
        lyricsPureMusic: false,
        hotSearches: [{ word: "雨", note: "", score: 1 }],
        search: { query: "雨", songs: oneTrack, artists: [{ id: 1, name: "A", cover: C2, albums: 1 }], playlists: [{ id: 1, name: "雨", cover: C2, count: 1, creator: "Jo" }], albums: [] },
        wiki: oneWiki,
        userSubcount: { artistCount: 1, mvCount: 1, createdPlaylistCount: 1, subPlaylistCount: 0, djRadioCount: 0 },
        newSongs: oneTrack,
        newAlbums: [{ id: 1, name: "雨", cover: C2, artist: "A", artistId: 1, publishTime: 1700000000000, size: 1 }],
        highqualityTags: [{ name: "华语", hot: true }],
        highquality: { cat: "全部", more: false, next: 0, items: [{ id: 1, name: "雨", cover: C2, count: 1, plays: 1, description: "", creator: "Jo" }] },
        recommendedMvs: [{ id: 1, name: "雨", cover: C2, artist: "A", artistId: 1, duration: 221, playCount: 1, reason: "" }],
        recommendedPrograms: [oneProgram],
        radio: { id: 7, name: "雨天电台", cover: C2, description: "下雨天听的节目", dj: { id: 1, name: "Jo", avatar: "" },
                 programCount: 1, subCount: 1, playCount: 1, more: false, next: 1, programs: [oneProgram] },
        searchSuggest: { query: "雨", order: ["songs", "artists", "albums", "playlists"], songs: [{ id: 990, name: "雨", artist: "A", album: "雨" }],
                         artists: [{ id: 1, name: "A", cover: C2 }], albums: [{ id: 1, name: "雨", artist: "A", artistId: 1 }],
                         playlists: [{ id: 1, name: "雨", cover: C2, count: 1 }] },
        userEvents: { userId: 1003, more: false, next: 0, items: [
            { id: 1, action: "分享单曲", text: "下雨了。", time: 1700000000000, user: { id: 1003, name: "Jo", avatar: "" },
              resource: { kind: "song", id: 990, name: "雨", cover: C1, artist: "A", track: oneTrack[0] }, likes: 1, comments: 0, shares: 0 }] },
        notices: { more: false, next: 0, items: [{ id: 1, time: 1700000000000, user: { id: 2, name: "B", avatar: "" }, text: "收藏了你的歌单",
                                                   resource: { kind: "playlist", id: 1, name: "雨", cover: C2, artist: "" } }] },
        commentNotices: { more: false, next: 0, total: 1, items: [{ id: "1", time: 1700000000000, user: { id: 2, name: "B", avatar: "" },
                                                                    content: "我也是。", repliedContent: "第一次听是在末班车上。",
                                                                    resource: { kind: "song", id: 990, name: "雨", cover: "", artist: "A" } }] },
        forwards: { more: false, next: 1, newCount: 0, items: [{ id: 1, time: 1700000000000, user: { id: 2, name: "B", avatar: "" }, message: "@Jo 听这个",
                                                                 resource: { kind: "song", id: 990, name: "雨", cover: C1, artist: "A", track: oneTrack[0] } }] },
    },
    personal: {
        account: { id: 1003, name: "Jo", vip: true, avatar: "" },
        savedArtists: { more: false, next: 1, count: 1, items: [{ id: 1, name: "A", cover: C2, alias: "", albums: 1, mvs: 1 }] },
        savedAlbums: { more: false, next: 1, count: 1, items: [{ id: 1, name: "雨", cover: C2, artist: "A", artistId: 1, size: 1 }] },
        savedMvs: { more: false, next: 1, count: 1, items: [{ kind: "mv", id: 1, vid: "", name: "雨", cover: C2, artist: "A", duration: 221, playCount: 1 }] },
        daily: { date: "10月17日", day: "17", tracks: oneTrack },
        heart: { seed: oneTrack[0], tracks: oneTrack },
        radar: { id: 3136952023, name: "私人雷达", description: "今天《雨》爱不释耳", cover: C2, tracks: oneTrack },
        similar: { seed: oneTrack[0], tracks: oneTrack },
        liked: { id: 1, count: 1, tracks: oneTrack },
        recent: { total: 1, tracks: oneTrack },
        cloud: { count: 1, size: 4194304, maxSize: 64424509440, tracks: oneTrack },
        mine: [{ id: 1, name: "雨", cover: C2, count: 1, reason: "" }],
        subscribed: [],
        playlists: [{ id: 1, name: "雨", cover: C2, count: 1, reason: "" }],
        fm: { tracks: oneTrack },
    },
};
