#!/usr/bin/env python3
"""给 QML 原型生成真实数据快照。

prototype/Data.js      匿名可得的数据（榜单、热歌榜、歌词、搜索、热搜），入库。
prototype/Personal.js  个人数据（每日推荐、心动模式、私人漫游、私人雷达、相似推荐、红心、推荐歌单、最近在听、云盘、收藏歌单），不入库。
                       有 netease-cloud-music-gtk4 的登录 cookie 时抓个人版；没有时写匿名替身（公共漫游、公共推荐歌单）。

cookie 只在本进程内拼进请求头，不打印、不写盘。
这里的归一化函数是数据形状的唯一定义：C++ Store 照它移植，scripts/check_contract.py 用它的输出做参照。
用法: scripts/snapshot.py [--anonymous] [--out DIR]   （DIR 默认 prototype/）
"""
import json
import os
import re
import sys
import time
import urllib.request

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OUT = sys.argv[sys.argv.index("--out") + 1] if "--out" in sys.argv else os.path.join(ROOT, "prototype")
COOKIE_FILE = os.path.join(os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share"),
                           "netease-cloud-music-gtk4", "cookies.json")
HOT_CHART = 3778678
RADAR = 3136952023
CURRENT = 2  # 原型里「正在播放」的是热歌榜第 3 首，歌词跟着它取


def cookie_header():
    if "--anonymous" in sys.argv or not os.path.exists(COOKIE_FILE):
        return ""
    jar = {}
    for c in json.load(open(COOKIE_FILE)):
        name, _, value = c["raw_cookie"].split(";", 1)[0].partition("=")
        jar[name] = value  # 后出现的覆盖先出现的
    return "; ".join(f"{k}={jar[k]}" for k in ("MUSIC_U", "__csrf", "NMTID") if k in jar)


COOKIE = cookie_header()


def get(path, personal=False):
    headers = {"Referer": "https://music.163.com", "User-Agent": "Mozilla/5.0 (X11; Linux x86_64) Chrome/140.0"}
    if personal and COOKIE:
        headers["Cookie"] = COOKIE
    req = urllib.request.Request("https://music.163.com/" + path, headers=headers)
    with urllib.request.urlopen(req, timeout=15) as r:
        return json.load(r)


def track(s, reason=""):
    """新旧两种歌曲结构（ar/al/dt 与 artists/album/duration）统一成原型用的字段。"""
    artists = s.get("ar") or s.get("artists") or []
    album = s.get("al") or s.get("album") or {}
    alias = s.get("alia") or s.get("alias") or s.get("tns") or s.get("transNames") or [""]
    return {
        "id": s["id"],
        "name": s["name"],
        "artist": " / ".join(a["name"] for a in artists),
        "album": album.get("name") or "",
        "dur": (s.get("dt") or s.get("duration") or 0) // 1000,
        "fee": s.get("fee", 0),
        "cover": sized(album.get("picUrl"), 120),
        # 别名/译名取第一项（新结构 alia，旧结构 alias，再退 tns/transNames）；quality 看有没有更高一档音频
        "alias": alias[0],
        "quality": "hires" if (s.get("hr") or s.get("hrMusic")) else ("lossless" if (s.get("sq") or s.get("sqMusic")) else ""),
        "mv": s.get("mv") or s.get("mvid") or 0,
        # 推荐理由只有每日推荐带（data.recommendReasons 里 songId 对上的那条），其余为空
        "reason": reason,
    }


def sized(url, px):
    """图片地址加尺寸参数；没有图（搜索结果等常见 null）就是空串，不能拼出只有参数的假地址。"""
    return f"{url}?param={px}y{px}" if url else ""


def parse_lrc(lrc, tlyric=""):
    """LRC → [{t, text, tr}]；译文按取整到两位小数的时间戳对上，没有为 ""。"""
    def stamps(text):
        out = {}
        for m in re.finditer(r"\[(\d+):(\d+(?:\.\d+)?)\](.*)", text):
            if m[3].strip():
                out.setdefault(round(int(m[1]) * 60 + float(m[2]), 2), m[3].strip())
        return out
    tr = stamps(tlyric)
    return [{"t": t, "text": text, "tr": tr.get(t, "")} for t, text in stamps(lrc).items()]


def cloud_track(item):
    """云盘条目：有 simpleSong 用它（新结构），否则用条目自己的 songName/artist/album 字段。"""
    if item.get("simpleSong"):
        return track(item["simpleSong"])
    return {"id": item.get("songId") or 0, "name": item.get("songName") or item.get("fileName") or "",
            "artist": item.get("artist") or "", "album": item.get("album") or "", "dur": 0, "fee": 0, "cover": ""}


def playlist_card(p, cover_key):
    # copywriter 是推荐理由（「根据你喜欢的单曲《X》推荐」），首页磁贴用它当信息气味；没有时退回曲目数
    return {"id": p["id"], "name": p["name"], "cover": sized(p.get(cover_key), 300),
            "count": p.get("trackCount") or 0, "reason": p.get("copywriter") or ""}


def search_artist(a):
    return {"id": a["id"], "name": a["name"], "cover": sized(a.get("picUrl") or a.get("img1v1Url"), 300),
            "albums": a.get("albumSize") or 0}


def search_playlist(p):
    return {"id": p["id"], "name": p["name"], "cover": sized(p.get("coverImgUrl"), 300),
            "count": p.get("trackCount") or 0, "creator": (p.get("creator") or {}).get("nickname") or ""}


def search_album(a):
    ms = a.get("publishTime") or 0
    return {"id": a["id"], "name": a["name"], "artist": (a.get("artist") or {}).get("name") or "",
            "cover": sized(a.get("picUrl"), 300),
            "year": time.gmtime(ms / 1000).tm_year if ms > 0 else 0}


def js(name, value):
    return f"var {name} = {json.dumps(value, ensure_ascii=False, indent=1)}\n"


def write(name, header, parts):
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name)
    with open(path, "w") as f:
        f.write(".pragma library\n\n// " + header + "\n\n" + "\n".join(parts))
    print("wrote", os.path.relpath(path, ROOT))


def hot_comments(song_id):
    """热门评论：按接口给的顺序，最多 10 条；avatar 是用户头像（96 方图）。"""
    resp = get(f"api/v1/resource/hotcomments/R_SO_4_{song_id}?limit=10")
    # 实测热评在顶层 hotComments（不在 data 里），保持接口顺序
    return {"songId": song_id,
            "items": [{"user": (c.get("user") or {}).get("nickname") or "",
                       "avatar": sized((c.get("user") or {}).get("avatarUrl"), 96),
                       "content": c.get("content") or "",
                       "likes": c.get("likedCount") or 0,
                       "time": c.get("time") or 0}
                      for c in (resp.get("hotComments") or [])[:10]]}


def anonymous():
    top = get("api/toplist/detail")["list"]
    hot = get(f"api/v6/playlist/detail?id={HOT_CHART}&n=40")["playlist"]
    tracks = [track(s) for s in hot["tracks"][:40]]
    lyric = get(f"api/song/lyric?id={tracks[CURRENT]['id']}&lv=1&tv=-1")
    lyrics = parse_lrc(lyric.get("lrc", {}).get("lyric", ""),
                       (lyric.get("tlyric") or {}).get("lyric") or "")
    toplists = [
        {
            "id": t["id"],
            "name": t["name"],
            "cover": sized(t.get("coverImgUrl"), 300),
            "update": t.get("updateFrequency") or "",
            "count": t.get("trackCount") or 0,
            "preview": [{"name": p["first"], "artist": p["second"]} for p in (t.get("tracks") or [])],
        }
        for t in top[:12]
    ]
    playlist = {
        "id": hot["id"],
        "name": hot["name"],
        "cover": sized(hot.get("coverImgUrl"), 400),
        "description": (hot.get("description") or "").split("\n")[0],
        "count": hot["trackCount"],
        "plays": hot["playCount"],
    }
    q = urllib.parse.quote("林俊杰")
    search = {
        "query": "林俊杰",
        "songs": [track(s) for s in get(f"api/search/get?s={q}&type=1&limit=10")["result"]["songs"]],
        "artists": [search_artist(a) for a in get(f"api/search/get?s={q}&type=100&limit=6")["result"]["artists"]],
        "playlists": [search_playlist(p) for p in get(f"api/search/get?s={q}&type=1000&limit=6")["result"]["playlists"]],
        "albums": [search_album(a) for a in get(f"api/search/get?s={q}&type=10&limit=6")["result"]["albums"]],
    }
    hot = get("api/hotsearchlist/get").get("data") or []
    hot_searches = [{"word": h.get("searchWord") or "", "note": h.get("content") or "", "score": h.get("score") or 0}
                    for h in hot[:20]]
    write("Data.js", f"匿名接口快照（{time.strftime('%Y-%m-%d')}），由 scripts/snapshot.py 生成",
          [js("toplists", toplists), js("playlist", playlist), js("tracks", tracks),
           js("lyricsTrackId", tracks[CURRENT]["id"]), js("lyrics", lyrics),
           js("comments", hot_comments(tracks[CURRENT]["id"])),
           js("search", search), js("hotSearches", hot_searches)])
    return tracks


def personal(hot_tracks):
    # playlistDetails：按需加载的歌单曲目 { "<id>": { tracks, description } }，快照里为空，由应用的 Store 填
    # likedIds：全部红心曲目 id，登录后由 /api/song/like/get 填
    data = {"account": None, "daily": None, "heart": None, "radar": None, "liked": None, "likedIds": [],
            "mine": [], "recent": None, "cloud": None, "subscribed": [], "playlistDetails": {}}
    account = get("api/nuser/account/get", personal=True) if COOKIE else {}
    profile = account.get("profile") or {}
    uid = profile.get("userId")

    if uid:
        data["account"] = {"name": profile["nickname"], "vip": (profile.get("vipType") or 0) > 0,
                           "avatar": sized(profile.get("avatarUrl"), 96),
                           "background": sized(profile.get("backgroundUrl"), 1200)}
        daily = get("api/v3/discovery/recommend/songs", personal=True)["data"]
        # 推荐理由按 songId 对上：data.recommendReasons 里每条 { songId, reason }
        reasons = {r.get("songId"): r.get("reason") or "" for r in daily.get("recommendReasons") or []}
        data["daily"] = {"date": time.strftime("%m月%d日").lstrip("0"), "day": time.strftime("%d").lstrip("0"),
                         "tracks": [track(s, reasons.get(s["id"], "")) for s in daily["dailySongs"][:30]]}

        lists = get(f"api/user/playlist?uid={uid}&limit=30&offset=0", personal=True)["playlist"]
        # 全部红心曲目 id（播放条与行内小红心用）；没有红心时接口也可能回 null
        data["likedIds"] = get(f"api/song/like/get?uid={uid}", personal=True).get("ids") or []
        liked_list = lists[0]
        liked = get(f"api/v6/playlist/detail?id={liked_list['id']}&n=30", personal=True)["playlist"]
        data["liked"] = {"id": liked["id"], "count": liked["trackCount"], "tracks": [track(s) for s in liked["tracks"][:30]]}
        data["mine"] = [playlist_card(p, "coverImgUrl") for p in lists[1:]
                        if (p.get("creator") or {}).get("userId") == uid][:12]

        seed = data["liked"]["tracks"][0]
        heart = get(f"api/playmode/intelligence/list?songId={seed['id']}&type=fromPlayAll"
                    f"&playlistId={liked_list['id']}&startMusicId={seed['id']}&count=1", personal=True)
        data["heart"] = {"seed": seed, "tracks": [track(x["songInfo"]) for x in heart.get("data") or []][:30]}

        radar = get(f"api/v6/playlist/detail?id={RADAR}&n=30", personal=True)["playlist"]
        data["radar"] = {"id": radar["id"], "name": "私人雷达", "description": radar["name"],
                         "cover": sized(radar.get("coverImgUrl"), 300), "tracks": [track(s) for s in radar["tracks"][:30]]}

        recommended = get("api/v1/discovery/recommend/resource", personal=True).get("recommend") or []
        data["playlists"] = [playlist_card(p, "picUrl") for p in recommended]
        # 前 6 个推荐歌单补 30 首试听曲目和一句简介；其余保持空，原型里点开再取
        for card in data["playlists"][:6]:
            pl = get(f"api/v6/playlist/detail?id={card['id']}&n=30", personal=True)["playlist"]
            card["tracks"] = [track(s) for s in pl["tracks"][:30]]
            card["description"] = (pl.get("description") or "").split("\n")[0]
        for card in data["playlists"][6:]:
            card["tracks"] = []
            card["description"] = ""

        record = get("api/play-record/song/list?limit=30", personal=True).get("data") or {}
        data["recent"] = {"total": record.get("total") or 0,
                          "tracks": [track(x["data"]) for x in (record.get("list") or [])][:30]}

        cloud_resp = get("api/v1/cloud/get?limit=30&offset=0", personal=True)
        cloud_items = cloud_resp.get("data") or []
        data["cloud"] = {"count": cloud_resp.get("count") or 0,
                         "size": cloud_resp.get("size") or 0,
                         "maxSize": int(cloud_resp.get("maxSize") or 0),
                         "tracks": [cloud_track(x) for x in cloud_items][:30]}

        # 收藏（非自己创建）的歌单，与 mine 同款磁贴
        data["subscribed"] = [playlist_card(p, "coverImgUrl") for p in lists[1:]
                              if (p.get("creator") or {}).get("userId") != uid][:12]

        similar_seed = seed
    else:
        data["playlists"] = [playlist_card(p, "picUrl") for p in get("api/personalized/playlist?limit=12")["result"]]
        for card in data["playlists"]:
            card["tracks"] = []
            card["description"] = ""
        similar_seed = hot_tracks[0]

    # 公共接口：没登录也能取（登录后漫游是个人的）
    fm = get("api/v1/radio/get", personal=True).get("data") or []
    data["fm"] = {"tracks": [track(s) for s in fm]}
    similar = get(f"api/v1/discovery/simiSong?songid={similar_seed['id']}&limit=20").get("songs") or []
    data["similar"] = {"seed": similar_seed, "tracks": [track(s) for s in similar]}

    kind = "个人数据" if uid else "匿名替身（未找到登录 cookie）"
    write("Personal.js", f"{kind}快照（{time.strftime('%Y-%m-%d')}），由 scripts/snapshot.py 生成，不入库",
          [js(k, v) for k, v in data.items()])


if __name__ == "__main__":
    personal(anonymous())
