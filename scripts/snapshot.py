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
import math
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


def number(value):
    """JSON 数字；null、缺失、非数字都归 0（与 normalize.cpp 的 numOr 一致）。"""
    return value if isinstance(value, (int, float)) and not isinstance(value, bool) else 0


def truthy(value):
    """与 normalize.cpp 的 truthy 一致：对象/数组/字符串算有，null/缺失/0 算没有。"""
    if value is None:
        return False
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    return True


def playability(song, privilege=None):
    """与 normalize.cpp 的 trackPlayability 同一规则：privilege 决定，没带才退回 fee。"""
    privilege = privilege or song.get("privilege") or {}
    if not privilege:
        fee = number(song.get("fee"))
        if fee == 4:
            return False, "paid"
        if fee == 1:
            return False, "vip"
        return True, ""
    fee = number(privilege.get("fee")) if "fee" in privilege else number(song.get("fee"))
    if number(privilege.get("st")) < 0:
        return False, "unavailable"
    if truthy(privilege.get("cs")):
        return True, ""
    if number(privilege.get("pl")) > 0:
        return True, ""
    if fee == 1:
        return False, "vip"
    if fee == 4 and not truthy(privilege.get("payed")):
        return False, "paid"
    return False, "unavailable"


def track(s, reason="", privilege=None):
    """新旧两种歌曲结构（ar/al/dt 与 artists/album/duration）统一成原型用的字段。"""
    artists = s.get("ar") or s.get("artists") or []
    album = s.get("al") or s.get("album") or {}
    alias = s.get("alia") or s.get("alias") or s.get("tns") or s.get("transNames") or [""]
    playable, blocked = playability(s, privilege)
    return {
        "id": s["id"],
        "name": s["name"],
        "artist": " / ".join(a["name"] for a in artists),
        "artistId": artists[0].get("id") or 0 if artists else 0,
        # 每位歌手各自可点
        "artists": [{"id": a.get("id") or 0, "name": a["name"]} for a in artists],
        "albumId": album.get("id") or 0,
        "album": album.get("name") or "",
        "dur": (s.get("dt") or s.get("duration") or 0) // 1000,
        "fee": number(s.get("fee")),
        "cover": sized(album.get("picUrl"), 120),
        # 别名/译名取第一项（新结构 alia，旧结构 alias，再退 tns/transNames）；quality 看有没有更高一档音频
        "alias": alias[0],
        "quality": "hires" if (s.get("hr") or s.get("hrMusic")) else ("lossless" if (s.get("sq") or s.get("sqMusic")) else ""),
        "mv": s.get("mv") or s.get("mvid") or 0,
        "playable": playable,
        "blockedBy": blocked,
        # 推荐理由只有每日推荐带（data.recommendReasons 里 songId 对上的那条），其余为空
        "reason": reason,
    }


def privilege_index(privileges):
    return {p.get("id"): p for p in privileges or [] if isinstance(p, dict)}


def tracks(items, privileges=(), reasons=None):
    """接口常把 privileges 与 songs 分开发：按 id 对上后再归一化。"""
    reasons = reasons or {}
    index = privilege_index(privileges)
    return [track(s, reasons.get(s.get("id")) or "", index.get(s.get("id"))) for s in items]


def sized(url, px):
    """图片地址加尺寸参数；没有图（搜索结果等常见 null）就是空串，不能拼出只有参数的假地址。"""
    return f"{url}?param={px}y{px}" if url else ""


def parse_lrc(lrc, tlyric=""):
    """LRC → [{t, text, tr}]；译文按取整到两位小数的时间戳对上，没有为 ""。"""
    def stamps(text):
        """与 normalize.cpp 的 lrcStamps 同一规则：行首可挂多个时间戳（重复句共用一行），
        小数点可写成冒号，逐字时间 <mm:ss.xx> 去掉；按时间排序，同一时间留先出现的一句。"""
        out = {}
        for raw in text.splitlines():
            line, times = raw.strip(), []
            while m := re.match(r"\[(\d+):(\d+)(?:[.:](\d+))?\]", line):
                times.append(int(m[1]) * 60 + int(m[2]) + (float("0." + m[3]) if m[3] else 0))
                line = line[m.end():]
            body = re.sub(r"<\d+:\d+(?:[.:]\d+)?>", "", line).strip()
            for t in times if body else []:
                # C++ 的 std::round(t * 100) / 100（正数四舍五入），不是 Python 的银行家舍入
                out.setdefault(math.floor(t * 100 + 0.5) / 100, body)
        return dict(sorted(out.items()))
    tr = stamps(tlyric)
    timed = [{"t": t, "text": text, "tr": tr.get(t, "")} for t, text in stamps(lrc).items()]
    if timed:
        return timed
    # 与 ncmParseLrc 一致：没有时间轴的歌词逐行给出，t = -1；署名行与标签行跳过
    return [{"t": -1.0, "text": line, "tr": ""} for line in map(str.strip, lrc.splitlines())
            if line and not line.startswith(("{", "["))]


def cloud_track(item):
    """云盘条目：有 simpleSong 用它（新结构），否则用条目自己的 songName/artist/album 字段。"""
    if item.get("simpleSong"):
        t = track(item["simpleSong"])
        t["playable"], t["blockedBy"] = True, ""
        return t
    return {"id": item.get("songId") or 0, "name": item.get("songName") or item.get("fileName") or "",
            "artist": item.get("artist") or "", "artistId": 0, "artists": [], "albumId": 0,
            "album": item.get("album") or "", "dur": 0, "fee": 0,
            "cover": "", "alias": "", "quality": "", "mv": 0,
            "playable": True, "blockedBy": "", "reason": ""}


def song_details(ids, personal=True):
    """批量歌曲详情：搜索接口的单曲没有封面，歌单接口只回前 1000 首，都用它补齐。"""
    out = {}
    ids = list(dict.fromkeys(i for i in ids if i))
    for start in range(0, len(ids), 250):
        c = "[" + ",".join('{"id":%s}' % i for i in ids[start:start + 250]) + "]"
        resp = get("api/v3/song/detail?c=" + urllib.parse.quote(c, safe=""), personal=personal)
        index = privilege_index(resp.get("privileges") or [])
        for s in resp.get("songs") or []:
            out[s["id"]] = track(s, privilege=index.get(s.get("id")))
    return out


def complete_playlist(pl, personal=True, privileges=()):
    """playlist.tracks 最多约 1000 条，trackIds 是完整的；按原顺序补齐并跳过已下架曲目。"""
    index = privilege_index(privileges)
    loaded = [track(s, privilege=index.get(s.get("id"))) for s in pl.get("tracks") or []]
    ids = [t.get("id") for t in pl.get("trackIds") or []]
    if not ids:
        return loaded
    by_id = {t["id"]: t for t in loaded}
    missing = [i for i in ids if i and i not in by_id]
    if missing:
        by_id.update(song_details(missing, personal=personal))
    return [by_id[i] for i in ids if i and i in by_id]


def playlist_card(p, cover_key):
    # copywriter 是推荐理由（「根据你喜欢的单曲《X》推荐」），首页磁贴用它当信息气味；没有时退回曲目数
    creator = p.get("creator") or {}
    return {"id": p["id"], "name": p["name"], "cover": sized(p.get(cover_key), 300),
            "count": p.get("trackCount") or 0,
            "description": (p.get("description") or "").split("\n")[0],
            "playCount": p.get("playCount") or 0,
            "creatorId": creator.get("userId") or 0,
            "creatorName": creator.get("nickname") or "",
            "updateTime": p.get("updateTime") or 0,
            "subscribed": bool(p.get("subscribed")),
            "reason": p.get("copywriter") or ""}


def search_artist(a):
    return {"id": a["id"], "name": a["name"], "cover": sized(a.get("picUrl") or a.get("img1v1Url"), 300),
            "albums": a.get("albumSize") or 0}


def search_playlist(p):
    creator = p.get("creator") or {}
    return {"id": p["id"], "name": p["name"], "cover": sized(p.get("coverImgUrl"), 300),
            "count": p.get("trackCount") or 0,
            "creator": creator.get("nickname") or "",
            "creatorId": creator.get("userId") or 0,
            "description": (p.get("description") or "").split("\n")[0],
            "playCount": p.get("playCount") or 0}


def search_mv(m):
    return {"id": m["id"], "name": m["name"], "cover": sized(m.get("cover"), 300),
            "artist": m.get("artistName") or "", "artistId": m.get("artistId") or 0,
            "duration": (m.get("duration") or 0) // 1000, "playCount": m.get("playCount") or 0}


def search_album(a):
    ms = a.get("publishTime") or 0
    return {"id": a["id"], "name": a["name"], "artist": (a.get("artist") or {}).get("name") or "",
            "cover": sized(a.get("picUrl"), 300),
            "year": time.localtime(ms // 1000).tm_year if ms > 0 else 0}


def js(name, value):
    return f"var {name} = {json.dumps(value, ensure_ascii=False, indent=1)}\n"


def write(name, header, parts):
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name)
    with open(path, "w") as f:
        f.write(".pragma library\n\n// " + header + "\n\n" + "\n".join(parts))
    print("wrote", os.path.relpath(path, ROOT))


def hot_comments(song_id):
    """热门评论：v2 接口按热度排序，最多 10 条；avatar 是用户头像（96 方图），replyCount 是楼中楼回复数；
    total 是这首歌的评论总数（播放条评论按钮上的数字）。"""
    resp = get(f"api/v2/resource/comments?threadId=R_SO_4_{song_id}&pageNo=1&pageSize=10&sortType=2")
    # v2 把热评放在 data.comments；保持接口顺序
    return {"songId": song_id,
            "total": (resp.get("data") or {}).get("totalCount") or 0,
            "items": [{"user": (c.get("user") or {}).get("nickname") or "",
                       "userId": (c.get("user") or {}).get("userId") or 0,
                       "avatar": sized((c.get("user") or {}).get("avatarUrl"), 96),
                       "content": c.get("content") or "",
                       "likes": c.get("likedCount") or 0,
                       "time": c.get("time") or 0,
                       "id": c.get("commentId") or 0,
                       "liked": bool(c.get("liked")),
                       "parentId": c.get("parentCommentId") or 0,
                       "beReplied": [{"id": (r.get("beRepliedCommentId") or 0),
                                      "content": r.get("content") or "",
                                      "userId": ((r.get("user") or {}).get("userId") or 0),
                                      "user": ((r.get("user") or {}).get("nickname") or "")}
                                     for r in (c.get("beReplied") or [])],
                       "replyCount": (c.get("showFloorComment") or {}).get("replyCount") or 0}
                      for c in ((resp.get("data") or {}).get("comments") or [])[:10]]}


def anonymous():
    top = get("api/toplist/detail")["list"]
    hot_resp = get(f"api/v6/playlist/detail?id={HOT_CHART}&n=1000")
    hot = hot_resp["playlist"]
    tracks = complete_playlist(hot, personal=False, privileges=hot_resp.get("privileges") or [])
    lyric = get(f"api/song/lyric?id={tracks[CURRENT]['id']}&lv=-1&tv=-1")
    lyrics = parse_lrc(lyric.get("lrc", {}).get("lyric", ""),
                       (lyric.get("tlyric") or {}).get("lyric") or "")
    # 与 Store 一致：接口说这首本身没有歌词（pureMusic，旧字段 nolyric）
    pure_music = bool(lyric.get("pureMusic") or lyric.get("nolyric"))
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
    found_songs = [track(s) for s in get(f"api/search/get?s={q}&type=1&limit=100")["result"]["songs"]]
    details = song_details([s["id"] for s in found_songs], personal=False)
    search = {
        "query": "林俊杰",
        "songs": [details.get(s["id"], s) for s in found_songs],
        "artists": [search_artist(a) for a in get(f"api/search/get?s={q}&type=100&limit=30")["result"]["artists"]],
        "playlists": [search_playlist(p) for p in get(f"api/search/get?s={q}&type=1000&limit=30")["result"]["playlists"]],
        "albums": [search_album(a) for a in get(f"api/search/get?s={q}&type=10&limit=30")["result"]["albums"]],
        "mvs": [search_mv(m) for m in get(f"api/search/get?s={q}&type=1004&limit=12")["result"]["mvs"]],
    }
    hot = get("api/hotsearchlist/get").get("data") or []
    hot_searches = [{"word": h.get("searchWord") or "", "note": h.get("content") or "", "score": h.get("score") or 0}
                    for h in hot[:20]]
    write("Data.js", f"匿名接口快照（{time.strftime('%Y-%m-%d')}），由 scripts/snapshot.py 生成",
          [js("toplists", toplists), js("playlist", playlist), js("tracks", tracks),
           js("lyrics", {str(tracks[CURRENT]["id"]): {"lines": lyrics, "pureMusic": pure_music}}),
           js("comments", hot_comments(tracks[CURRENT]["id"])),
           js("commentPage", None), js("searchUsers", None), js("searchRadios", None),
           js("search", search), js("hotSearches", hot_searches),
           js("artist", {}), js("artistSongs", None), js("album", {}), js("mv", {}), js("user", {}), js("wiki", {}), js("inbox", None), js("inboxThread", None),
           js("listenTotal", None), js("listenRealtime", None), js("listenRank", None),
           js("notifications", None), js("userFollows", None), js("userFolloweds", None),
           js("userLevel", None), js("userSubcount", None), js("commentReplies", None), js("lyricsNew", None)])
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
        data["account"] = {"id": uid, "name": profile["nickname"],
                           "vip": (profile.get("vipType") or 0) > 0,
                           "vipType": profile.get("vipType") or 0,
                           "vipRights": profile.get("vipRights") or {},
                           "level": profile.get("level") or 0,
                           "listenSongs": profile.get("listenSongs") or 0,
                           "signature": profile.get("signature") or "",
                           "followed": bool(profile.get("followed")),
                           "follows": profile.get("follows") or 0,
                           "followeds": profile.get("followeds") or 0,
                           "playlistCount": profile.get("playlistCount") or 0,
                           "createTime": profile.get("createTime") or 0,
                           "mutual": bool(profile.get("mutual")),
                           "avatar": sized(profile.get("avatarUrl"), 96),
                           "background": sized(profile.get("backgroundUrl"), 1200)}
        daily_resp = get("api/v3/discovery/recommend/songs", personal=True)
        daily = daily_resp["data"]
        # 推荐理由按 songId 对上：data.recommendReasons 里每条 { songId, reason }
        reasons = {r.get("songId"): r.get("reason") or "" for r in daily.get("recommendReasons") or []}
        daily_priv = daily.get("privileges") or daily_resp.get("privileges") or []
        data["daily"] = {"date": time.strftime("%m月%d日").lstrip("0"), "day": time.strftime("%d").lstrip("0"),
                         "tracks": tracks(daily["dailySongs"][:30], daily_priv, reasons)}

        lists = get(f"api/user/playlist?uid={uid}&limit=1000&offset=0", personal=True)["playlist"]
        # 全部红心曲目 id（播放条与行内小红心用）；没有红心时接口也可能回 null
        data["likedIds"] = get(f"api/song/like/get?uid={uid}", personal=True).get("ids") or []
        liked_list = lists[0]
        liked_resp = get(f"api/v6/playlist/detail?id={liked_list['id']}&n=1000", personal=True)
        liked = liked_resp["playlist"]
        data["liked"] = {"id": liked["id"], "count": liked["trackCount"],
                         "tracks": complete_playlist(liked, personal=True,
                                                     privileges=liked_resp.get("privileges") or [])}
        data["mine"] = [playlist_card(p, "coverImgUrl") for p in lists[1:]
                        if (p.get("creator") or {}).get("userId") == uid]

        seed = data["liked"]["tracks"][0]
        heart = get(f"api/playmode/intelligence/list?songId={seed['id']}&type=fromPlayAll"
                    f"&playlistId={liked_list['id']}&startMusicId={seed['id']}&count=1", personal=True)
        heart_priv = privilege_index(heart.get("privileges") or [])
        data["heart"] = {"seed": seed,
                         "tracks": [track(x["songInfo"], privilege=heart_priv.get(x["songInfo"].get("id")))
                                    for x in (heart.get("data") or [])[:30]]}

        radar_resp = get(f"api/v6/playlist/detail?id={RADAR}&n=1000", personal=True)
        radar = radar_resp["playlist"]
        data["radar"] = {"id": radar["id"], "name": "私人雷达", "description": radar["name"],
                         "cover": sized(radar.get("coverImgUrl"), 300),
                         "tracks": complete_playlist(radar, personal=True,
                                                     privileges=radar_resp.get("privileges") or [])}

        recommended = get("api/v1/discovery/recommend/resource", personal=True).get("recommend") or []
        data["playlists"] = [playlist_card(p, "picUrl") for p in recommended]
        # 前 6 个推荐歌单补完整曲目和一句简介；其余保持空，原型里点开再取
        for card in data["playlists"][:6]:
            pl_resp = get(f"api/v6/playlist/detail?id={card['id']}&n=1000", personal=True)
            pl = pl_resp["playlist"]
            card["tracks"] = complete_playlist(pl, personal=True,
                                               privileges=pl_resp.get("privileges") or [])
            card["description"] = (pl.get("description") or "").split("\n")[0]
        for card in data["playlists"][6:]:
            card["tracks"] = []
            card["description"] = ""

        recent_resp = get("api/play-record/song/list?limit=300", personal=True)
        record = recent_resp.get("data") or {}
        recent_priv = privilege_index(record.get("privileges") or recent_resp.get("privileges") or [])
        data["recent"] = {"total": record.get("total") or 0,
                          "tracks": [track(x["data"], privilege=recent_priv.get(x["data"].get("id")))
                                     for x in (record.get("list") or [])]}

        cloud_items, cloud_offset = [], 0
        cloud_resp = {}
        while cloud_offset < 1000:
            cloud_resp = get(f"api/v1/cloud/get?limit=200&offset={cloud_offset}", personal=True)
            page = cloud_resp.get("data") or []
            cloud_items.extend(page)
            cloud_offset += len(page)
            if not page or cloud_offset >= (cloud_resp.get("count") or 0):
                break
        data["cloud"] = {"count": cloud_resp.get("count") or 0,
                         "size": cloud_resp.get("size") or 0,
                         "maxSize": int(cloud_resp.get("maxSize") or 0),
                         "tracks": [cloud_track(x) for x in cloud_items]}

        # 收藏（非自己创建）的歌单，与 mine 同款磁贴
        data["subscribed"] = [playlist_card(p, "coverImgUrl") for p in lists[1:]
                              if (p.get("creator") or {}).get("userId") != uid]

        similar_seed = seed
    else:
        data["playlists"] = [playlist_card(p, "picUrl") for p in get("api/personalized/playlist?limit=12")["result"]]
        for card in data["playlists"]:
            card["tracks"] = []
            card["description"] = ""
        similar_seed = hot_tracks[0]

    # 公共接口：没登录也能取（登录后漫游是个人的）
    fm = get("api/v1/radio/get", personal=True).get("data") or []
    data["fm"] = {"tracks": tracks(fm)}
    similar = get(f"api/v1/discovery/simiSong?songid={similar_seed['id']}&limit=20").get("songs") or []
    data["similar"] = {"seed": similar_seed, "tracks": tracks(similar)}

    kind = "个人数据" if uid else "匿名替身（未找到登录 cookie）"
    write("Personal.js", f"{kind}快照（{time.strftime('%Y-%m-%d')}），由 scripts/snapshot.py 生成，不入库",
          [js(k, v) for k, v in data.items()])


if __name__ == "__main__":
    if "--help" in sys.argv or "-h" in sys.argv:
        print(__doc__)
        sys.exit(0)
    personal(anonymous())
