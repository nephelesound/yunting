.pragma library

// 显示格式：全应用只有这一份，组件里不各写一遍

// 时长：m:ss；一小时以上 h:mm:ss；未知（0 或缺失）返回空串——缺失不显示成 0:00
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

// 音质标签：Hi-Res / 无损；极高及以下不标（几乎所有歌都有，标了没信息量）
function quality(q) {
    return q === "hires" ? "Hi-Res" : q === "lossless" ? "无损" : "";
}
