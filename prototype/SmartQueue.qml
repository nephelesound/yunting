// 智能续播：本地口味档 + 队列尾部补歌。不联网、不上传；只有 C++ 应用里才读写 Settings。
// 口味信号：完整播放、过早跳过、红心、最近播放。候选只来自已取回的推荐/相似/漫游/最近/热歌，
// 排序与去重都在本地完成；补进来的歌带 smartReason，队列里仍可手动移走。
pragma ComponentBehavior: Bound

import QtQuick
import QtCore

QtObject {
    id: smart

    required property var player       // Playback
    required property var source       // Source
    property bool persist: false       // 原型/截图不写用户配置
    property bool enabled: true

    readonly property int maxAppend: 5
    property var profile: ({ tracks: {}, artists: {}, updated: 0 })
    property var lastTrack: null
    property bool lastStarted: false
    property real lastPosition: 0
    property bool lastFinished: false
    property var lastAppendAt: 0
    property string lastReason: ""
    property bool restored: false

    // 惰性创建：原型/测试不碰 QSettings，也不写用户配置
    property Component settingsComponent: Component {
        Settings {
            category: "smartQueue"
            property bool enabled: true
            property string taste: "{}"
        }
    }
    property var saved: null

    Component.onCompleted: {
        if (smart.persist) {
            smart.saved = smart.settingsComponent.createObject(smart);
            smart.enabled = smart.saved.enabled;
            try {
                const parsed = JSON.parse(smart.saved.taste);
                if (parsed && typeof parsed === "object")
                    smart.profile = smart.normalizeProfile(parsed);
            } catch (e) {
                // 坏档不影响播放：从空口味开始，下一次写入会覆盖它
            }
        }
        smart.restored = true;
        smart.lastTrack = smart.player.current ? Object.assign({}, smart.player.current) : null;
        smart.lastStarted = false;
        smart.lastPosition = 0;
        smart.lastFinished = false;
        smart.prefetchNext();
        smart.maybeExtend();
    }

    property Binding enabledBinding: Binding {
        when: smart.persist && smart.restored
        target: saved
        property: "enabled"
        value: smart.enabled
    }
    property Binding tasteBinding: Binding {
        when: smart.persist && smart.restored
        target: saved
        property: "taste"
        value: JSON.stringify(smart.profile)
    }

    // ---- 本地口味档 ----

    function normalizeProfile(raw) {
        const p = { tracks: {}, artists: {}, updated: 0 };
        if (raw && typeof raw.tracks === "object" && raw.tracks !== null)
            p.tracks = raw.tracks;
        if (raw && typeof raw.artists === "object" && raw.artists !== null)
            p.artists = raw.artists;
        p.updated = Number(raw && raw.updated) || 0;
        return p;
    }

    function artistScore(name) {
        return Number(smart.profile.artists[name] || 0);
    }

    function bumpArtist(name, delta) {
        if (!name)
            return;
        const next = Math.max(-8, Math.min(20, smart.artistScore(name) + delta));
        const artists = Object.assign({}, smart.profile.artists);
        artists[name] = next;
        smart.profile = { tracks: smart.profile.tracks, artists: artists, updated: Date.now() };
    }

    function recordStart(track) {
        if (!track || !track.id)
            return;
        const id = String(track.id);
        const old = smart.profile.tracks[id] || {};
        const stats = Object.assign({}, old, {
            plays: (Number(old.plays) || 0) + 1,
            finishes: Number(old.finishes) || 0,
            skips: Number(old.skips) || 0,
            artist: track.artist || old.artist || "",
            name: track.name || old.name || "",
            last: Date.now()
        });
        const tracks = Object.assign({}, smart.profile.tracks);
        tracks[id] = stats;
        smart.profile = { tracks: tracks, artists: smart.profile.artists, updated: Date.now() };
        smart.bumpArtist(stats.artist, 0.35);
    }

    // finished = 自然播完；position/duration 用于区分跳过与「听了一会儿」
    function recordEnd(track, position, duration, finished) {
        if (!track || !track.id)
            return;
        const id = String(track.id);
        const old = smart.profile.tracks[id];
        if (!old)
            return;
        const total = Number(duration) || Number(track.dur) || 0;
        const pos = Number(position) || 0;
        const ratio = total > 0 ? Math.min(1, pos / total) : (finished ? 1 : 0);
        const skipped = !finished && ratio < 0.5 && pos < 30;
        const completed = finished || ratio >= 0.9;
        const stats = Object.assign({}, old);
        if (completed)
            stats.finishes = (Number(old.finishes) || 0) + 1;
        if (skipped)
            stats.skips = (Number(old.skips) || 0) + 1;
        stats.last = Date.now();
        const tracks = Object.assign({}, smart.profile.tracks);
        tracks[id] = stats;
        smart.profile = { tracks: tracks, artists: smart.profile.artists, updated: Date.now() };
        if (completed)
            smart.bumpArtist(stats.artist, 0.25);
        else if (skipped)
            smart.bumpArtist(stats.artist, -0.2);
    }

    // ---- 候选与排序 ----

    function addPool(out, list, weight) {
        if (!list || !list.length)
            return;
        for (let i = 0; i < list.length; i++) {
            const track = list[i];
            if (!track || !track.id)
                continue;
            out.push({ track: track, weight: weight });
        }
    }

    function candidatePool() {
        const p = smart.source.pe || {};
        const out = [];
        smart.addPool(out, (p.daily || {}).tracks, 0.55);       // 每日推荐：最贴近近期口味
        smart.addPool(out, (p.radar || {}).tracks, 0.5);
        smart.addPool(out, (p.similar || {}).tracks, 0.45);
        smart.addPool(out, (p.heart || {}).tracks, 0.4);
        smart.addPool(out, (p.liked || {}).tracks, 0.35);
        smart.addPool(out, smart.source.fmTracks || [], 0.45);
        smart.addPool(out, (smart.source.recent || {}).tracks, 0.15);
        smart.addPool(out, (smart.source.hotChart || {}).tracks, -0.1);   // 冷启动兜底
        return out;
    }

    function score(track, weight) {
        const now = Date.now();
        const stats = smart.profile.tracks[String(track.id)] || {};
        const plays = Number(stats.plays) || 0;
        const finishes = Number(stats.finishes) || 0;
        const skips = Number(stats.skips) || 0;
        const completion = plays > 0 ? finishes / plays : 0;
        const skipRate = plays > 0 ? skips / plays : 0;
        const ageDays = stats.last ? (now - stats.last) / 86400000 : 999;
        const recency = Math.exp(-Math.max(0, ageDays) / 10);
        const liked = smart.source.isLiked && smart.source.isLiked(track.id) ? 1.3 : 0;
        const current = smart.player.current;
        const sameArtist = current && current.artist && track.artist === current.artist ? 0.35 : 0;
        return (Number(weight) || 0)
            + smart.artistScore(track.artist) * 0.55
            + completion * 1.6
            + liked
            + sameArtist
            + (plays === 0 ? 0.75 : 0)
            - recency * 1.1
            - skipRate * 1.4;
    }

    function reasonFor(track) {
        const stats = smart.profile.tracks[String(track.id)] || {};
        if (smart.source.isLiked && smart.source.isLiked(track.id))
            return "你点过红心";
        if (smart.artistScore(track.artist) > 1.0)
            return "常听 " + track.artist;
        if ((Number(stats.plays) || 0) > 0)
            return "最近常听";
        return "换换口味";
    }

    function rankCandidates(pool, limit, exclude) {
        const entries = [];
        const seen = {};
        for (let i = 0; i < (pool || []).length; i++) {
            const raw = pool[i];
            const track = raw && raw.track ? raw.track : raw;
            const weight = raw && raw.track ? raw.weight : 0;
            if (!track || !track.id)
                continue;
            const id = String(track.id);
            if (seen[id] || (exclude && exclude[id]))
                continue;
            seen[id] = true;
            entries.push({ track: track, weight: weight, score: smart.score(track, weight) });
        }
        entries.sort((a, b) => b.score - a.score || String(a.track.id).localeCompare(String(b.track.id)));

        const picked = [];
        const artistCount = {};
        // 先每位歌手取一首，保证队列不连续堆同一个人；不够再放开到两首
        for (let pass = 0; pass < 2 && picked.length < limit; pass++) {
            for (const entry of entries) {
                if (picked.length >= limit)
                    break;
                if (picked.indexOf(entry) >= 0)
                    continue;
                const artist = entry.track.artist || "";
                if (pass === 0 && artistCount[artist])
                    continue;
                artistCount[artist] = (artistCount[artist] || 0) + 1;
                picked.push(entry);
            }
        }
        return picked.map(entry => {
            const reason = smart.reasonFor(entry.track);
            return Object.assign({}, entry.track, { smartReason: reason, reason: reason });
        });
    }

    function playable(track) {
        if (smart.player.playable)
            return smart.player.playable(track);
        return track.fee !== 4 && (track.fee !== 1 || smart.player.vip);
    }

    function prefetchNext() {
        const q = smart.player.queue || [];
        const next = q[smart.player.index + 1];
        if (next && smart.player.engine && typeof smart.player.engine.prefetch === "function")
            smart.player.engine.prefetch(next.id);
    }

    function maybeExtend() {
        if (!smart.enabled || !smart.player.current)
            return;
        // 私人漫游、心动模式有自己的续取流，别再插一批口味歌把它打断
        if (smart.player.origin === "fm" || smart.player.origin === "heart")
            return;
        const remaining = smart.player.queue.length - smart.player.index - 1;
        if (remaining > 2)
            return;
        const now = Date.now();
        if (now - smart.lastAppendAt < 8000)
            return;
        const queued = {};
        for (const t of smart.player.queue)
            queued[String(t.id)] = true;
        const pool = smart.candidatePool().filter(entry => {
            const t = entry.track;
            return !queued[String(t.id)] && smart.playable(t);
        });
        const picks = smart.rankCandidates(pool, smart.maxAppend, queued);
        if (!picks.length)
            return;
        smart.player.append(picks);
        smart.lastAppendAt = now;
        smart.lastReason = picks[0].smartReason || "";
        smart.prefetchNext();
    }

    // ---- 播放事件 ----

    readonly property Connections _engine: Connections {
        target: smart.player.engine

        function onPositionChanged() {
            smart.lastPosition = smart.player.engine.position;
        }

        function onPlayingChanged() {
            if (!smart.player.engine.playing || smart.lastStarted || !smart.player.current)
                return;
            // 恢复上次队列后按空格继续时，第一首也要计入口味
            smart.lastTrack = Object.assign({}, smart.player.current);
            smart.lastStarted = true;
            smart.lastPosition = 0;
            smart.lastFinished = false;
            smart.recordStart(smart.player.current);
        }

        function onFinished() {
            smart.lastFinished = true;
            smart.lastPosition = Number(smart.player.engine.duration) || smart.lastPosition;
        }

        function onTrackChanged() {
            const previous = smart.lastTrack;
            if (previous && smart.lastStarted)
                smart.recordEnd(previous, smart.lastPosition, previous.dur, smart.lastFinished);
            smart.lastTrack = smart.player.current ? Object.assign({}, smart.player.current) : null;
            smart.lastStarted = !!smart.player.current;
            smart.lastPosition = 0;
            smart.lastFinished = false;
            if (smart.player.current)
                smart.recordStart(smart.player.current);
            smart.prefetchNext();
            smart.maybeExtend();
        }
    }

    readonly property Connections _queue: Connections {
        target: smart.player
        function onIndexChanged() {
            smart.prefetchNext();
            smart.maybeExtend();
        }
    }

    onEnabledChanged: smart.maybeExtend()
}
