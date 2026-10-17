.pragma library

// 显示格式：全应用只有这一份，组件里不各写一遍

// 时长：m:ss；一小时以上 h:mm:ss；未知（0 或缺失）返回空串，缺失不显示成 0:00
function duration(seconds) {
    const total = Math.floor(seconds || 0);
    if (total <= 0)
        return "";
    const h = Math.floor(total / 3600);
    const m = Math.floor(total % 3600 / 60);
    const s = total % 60;
    const ss = (s < 10 ? "0" : "") + s;
    return h > 0 ? h + ":" + (m < 10 ? "0" : "") + m + ":" + ss : m + ":" + ss;
}

// 播放进度：已播时间允许为 0（显示 0:00），所以不走 duration() 的「缺失为空」
function elapsed(seconds) {
    return duration(seconds) || "0:00";
}

// 曲目的购买限制：VIP 曲 / 付费专辑；其余返回空串
function restriction(fee) {
    return fee === 1 ? "VIP" : fee === 4 ? "付费" : "";
}

// 当前账号为什么不能播这首歌："" 能播，"vip" 要会员，"paid" 要单独购买，"unavailable" 已下架/无版权。
// 以接口的 privilege 为准（后端给出 playable / blockedBy）；歌曲没带 privilege 时后端只能按 fee 猜
// 「会员歌」，所以会员账号不受 "vip" 限制
function blockedBy(track, vip) {
    if (!track || track.playable !== false)
        return "";
    if (track.blockedBy === "vip" && vip)
        return "";
    return track.blockedBy || "unavailable";
}

// 评论时间（毫秒时间戳）：今天只写时刻，今年写月日，更早写年月日；没有时间返回空串
function commentTime(ms) {
    if (!ms)
        return "";
    const d = new Date(ms);
    const now = new Date();
    const pad = n => (n < 10 ? "0" : "") + n;
    if (d.toDateString() === now.toDateString())
        return pad(d.getHours()) + ":" + pad(d.getMinutes());
    const md = (d.getMonth() + 1) + "月" + d.getDate() + "日";
    return d.getFullYear() === now.getFullYear() ? md : d.getFullYear() + "年" + md;
}

// 播放音质档位（AudioEngine.quality）的显示名
function qualityLevel(level) {
    return ({ auto: "自动", standard: "标准", higher: "较高", exhigh: "极高", lossless: "无损", hires: "Hi-Res" })[level] || "自动";
}

// 音质标签：Hi-Res / 无损；极高及以下不标（几乎所有歌都有，标了没信息量）
function quality(q) {
    return q === "hires" ? "Hi-Res" : q === "lossless" ? "无损" : "";
}

// 次数（播放、点赞、评论数）：一万以下照写，一亿以下按万（十万以下留一位小数），再往上按亿。
// 小数是 .0 时去掉（1.0 万 → 1 万）
function count(n) {
    const short = (value, digits) => value.toFixed(digits).replace(/\.0$/, "");
    if (n >= 1e8)
        return short(n / 1e8, 1) + " 亿";
    if (n >= 1e4)
        return short(n / 1e4, n >= 1e5 ? 0 : 1) + " 万";
    return String(n);
}

// 曲目的歌手，交给 LinkText：每位各自可点。旧数据没有 artists 时只有第一位带 id
function artistLinks(track) {
    if (!track)
        return [];
    if (track.artists && track.artists.length > 0)
        return track.artists.map(a => ({ text: a.name, artistId: a.id }));
    return track.artist ? track.artist.split(" / ").map((n, i) => ({ text: n, artistId: i === 0 ? track.artistId : 0 })) : [];
}
