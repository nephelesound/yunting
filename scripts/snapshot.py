#!/usr/bin/env python3
"""给 QML 原型生成真实数据快照。

prototype/Data.js      匿名可得的数据（榜单、热歌榜、歌词），入库。
prototype/Personal.js  个人数据（每日推荐、心动模式、私人漫游、私人雷达、相似推荐、红心、推荐歌单），不入库。
                       有 netease-cloud-music-gtk4 的登录 cookie 时抓个人版；没有时写匿名替身（公共漫游、公共推荐歌单）。

cookie 只在本进程内拼进请求头，不打印、不写盘。
用法: scripts/snapshot.py [--anonymous]
"""
import json
import os
import re
import sys
import time
import urllib.request

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
COOKIE_FILE = os.path.expanduser("~/.local/share/netease-cloud-music-gtk4/cookies.json")
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


def track(s):
    """新旧两种歌曲结构（ar/al/dt 与 artists/album/duration）统一成原型用的字段。"""
    artists = s.get("ar") or s.get("artists") or []
    album = s.get("al") or s.get("album") or {}
    return {
        "id": s["id"],
        "name": s["name"],
        "artist": " / ".join(a["name"] for a in artists),
        "album": album.get("name") or "",
        "dur": (s.get("dt") or s.get("duration") or 0) // 1000,
        "fee": s.get("fee", 0),
        "cover": (album.get("picUrl") or "") + "?param=120y120",
    }


def playlist_card(p, cover_key):
    return {"id": p["id"], "name": p["name"], "cover": p[cover_key] + "?param=300y300", "count": p.get("trackCount") or 0}


def js(name, value):
    return f"var {name} = {json.dumps(value, ensure_ascii=False, indent=1)}\n"


def write(path, header, parts):
    with open(os.path.join(ROOT, path), "w") as f:
        f.write(".pragma library\n\n// " + header + "\n\n" + "\n".join(parts))
    print("wrote", path)


def anonymous():
    top = get("api/toplist/detail")["list"]
    hot = get(f"api/v6/playlist/detail?id={HOT_CHART}&n=40")["playlist"]
    tracks = [track(s) for s in hot["tracks"][:40]]
    lrc = get(f"api/song/lyric?id={tracks[CURRENT]['id']}&lv=1&tv=-1").get("lrc", {}).get("lyric", "")
    lyrics = [
        {"t": round(int(m[1]) * 60 + float(m[2]), 2), "text": m[3].strip()}
        for m in re.finditer(r"\[(\d+):(\d+(?:\.\d+)?)\](.*)", lrc)
        if m[3].strip()
    ]
    toplists = [
        {
            "id": t["id"],
            "name": t["name"],
            "cover": t["coverImgUrl"] + "?param=300y300",
            "update": t.get("updateFrequency") or "",
            "count": t.get("trackCount") or 0,
            "preview": [{"name": p["first"], "artist": p["second"]} for p in (t.get("tracks") or [])],
        }
        for t in top[:12]
    ]
    playlist = {
        "id": hot["id"],
        "name": hot["name"],
        "cover": hot["coverImgUrl"] + "?param=400y400",
        "description": (hot.get("description") or "").split("\n")[0],
        "count": hot["trackCount"],
        "plays": hot["playCount"],
    }
    write("prototype/Data.js", f"匿名接口快照（{time.strftime('%Y-%m-%d')}），由 scripts/snapshot.py 生成",
          [js("toplists", toplists), js("playlist", playlist), js("tracks", tracks),
           js("lyricsTrackId", tracks[CURRENT]["id"]), js("lyrics", lyrics)])
    return tracks


def personal(hot_tracks):
    data = {"account": None, "daily": None, "heart": None, "radar": None, "liked": None, "mine": []}
    account = get("api/nuser/account/get", personal=True) if COOKIE else {}
    profile = account.get("profile") or {}
    uid = profile.get("userId")

    if uid:
        data["account"] = {"name": profile["nickname"], "vip": (profile.get("vipType") or 0) > 0,
                           "avatar": profile["avatarUrl"] + "?param=96y96"}
        daily = get("api/v3/discovery/recommend/songs", personal=True)["data"]["dailySongs"]
        data["daily"] = {"date": time.strftime("%m月%d日").lstrip("0"), "day": time.strftime("%d").lstrip("0"),
                         "tracks": [track(s) for s in daily[:30]]}

        lists = get(f"api/user/playlist?uid={uid}&limit=30&offset=0", personal=True)["playlist"]
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
                         "cover": radar["coverImgUrl"] + "?param=300y300", "tracks": [track(s) for s in radar["tracks"][:30]]}

        recommended = get("api/v1/discovery/recommend/resource", personal=True).get("recommend") or []
        data["playlists"] = [playlist_card(p, "picUrl") for p in recommended]
        similar_seed = seed
    else:
        data["playlists"] = [playlist_card(p, "picUrl") for p in get("api/personalized/playlist?limit=12")["result"]]
        similar_seed = hot_tracks[0]

    # 公共接口：没登录也能取（登录后漫游是个人的）
    fm = get("api/v1/radio/get", personal=True).get("data") or []
    data["fm"] = {"tracks": [track(s) for s in fm]}
    similar = get(f"api/v1/discovery/simiSong?songid={similar_seed['id']}&limit=20").get("songs") or []
    data["similar"] = {"seed": similar_seed, "tracks": [track(s) for s in similar]}

    kind = "个人数据" if uid else "匿名替身（未找到登录 cookie）"
    write("prototype/Personal.js", f"{kind}快照（{time.strftime('%Y-%m-%d')}），由 scripts/snapshot.py 生成，不入库",
          [js(k, v) for k, v in data.items()])


if __name__ == "__main__":
    personal(anonymous())
