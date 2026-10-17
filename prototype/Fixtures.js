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
    return { id: id, name: name, artist: artist, album: album, dur: dur, fee: fee, cover: cover,
             alias: x.alias || "", quality: x.quality || "", reason: x.reason || "", mv: x.mv || 0,
             artistId: x.artistId !== undefined ? x.artistId : 1, albumId: x.albumId !== undefined ? x.albumId : (album ? 1 : 0) };
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
    { user: "一只想要变成猫但是没有成功的柴犬Official", avatar: C2, likes: 1284567, time: 1700000000000,
      content: "第一次听这首歌是在高三晚自习结束后回家的路上，耳机里放着它，路灯一盏一盏往后退。那时候觉得未来很远，现在回头看，原来那就是最好的时候。这条评论写得很长，用来看评论在半屏宽度里最多显示几行、省略号落在哪里。还要再长一点，再长一点，再长一点。" },
    { user: "A", avatar: "", likes: 1, time: 1700000000000, content: "好" },
    { user: "🌙", avatar: BROKEN, likes: 99999, time: 1700000000000, content: "😭😭😭" },
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
        comments: worstComments,
        artist: worstArtist,
        album: worstAlbum,
    },
    personal: {
        account: { name: "一只想要变成猫但是没有成功的柴犬Official", vip: false, avatar: "" },
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
    data: { toplists: [], playlist: { id: 0, name: "空歌单", cover: "", description: "", count: 0, plays: 0 }, tracks: [], lyricsTrackId: 0, lyrics: [],
            hotSearches: [], search: { query: "没有结果的搜索词", songs: [], artists: [], playlists: [], albums: [] } },
    personal: {
        account: { name: "新用户", vip: false, avatar: "" },
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
var one = {
    data: {
        toplists: [{ id: 1, name: "飙升榜", cover: C2, update: "每天更新", count: 1, preview: [{ name: "雨", artist: "A" }] }],
        playlist: { id: 1, name: "雨", cover: C2, description: "", count: 1, plays: 1 },
        tracks: oneTrack,
        lyricsTrackId: 990,
        lyrics: [{ t: 0, text: "只有一句" }],
        hotSearches: [{ word: "雨", note: "", score: 1 }],
        search: { query: "雨", songs: oneTrack, artists: [{ id: 1, name: "A", cover: C2, albums: 1 }], playlists: [{ id: 1, name: "雨", cover: C2, count: 1, creator: "Jo" }], albums: [] },
    },
    personal: {
        account: { name: "Jo", vip: true, avatar: "" },
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
