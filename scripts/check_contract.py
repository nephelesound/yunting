#!/usr/bin/env python3
"""契约检查：对比 yunting --dump-store 的 JSON 与 scripts/snapshot.py 写出的快照的键名与 JSON 类型。

用法: check_contract.py <dump.json> <Data.js> <Personal.js>
参照快照与 dump 必须是同一种会话（都带 cookie，或都匿名：snapshot.py --anonymous --out DIR）。
数据形状的唯一定义是 snapshot.py；这里不另写一份。
只比键名与类型（列表元素形状 = 首元素），不比任何值。
参照非空而 dump 为空的列表算失配，EMPTY_OK 里按需加载的除外。
退出码：0 = 全部一致；1 = 形状不一致；2 = dump 离线/无数据，没法比较。
"""
import json
import re
import sys

TYPE_NAMES = {
    type(None): "null", bool: "bool", int: "number", float: "number",
    str: "string", list: "list", dict: "map",
}

# 参照非空、dump 为空仍算合法的列表：
# - lyrics / 热评 / 搜索：按需数据，dump 只是试探性请求，取不到不算形状错误；
# - cloud.tracks：新账号的云盘本来就没有上传。
# 云盘之外的首屏列表（tracks / daily / radar / liked …）不在此列，空了就是请求失败。
EMPTY_OK = {
    "data.lyrics",
    "data.comments.items",
    "data.search.songs",
    "data.search.artists",
    "data.search.playlists",
    "data.search.albums",
    "data.search.mvs",
    "personal.cloud.tracks",
}

# 按 id 存的映射（歌曲 / 歌手 / 专辑 / 用户 / MV）：参照与 dump 取的不是同一个 id，
# 只比每一项的形状（各取一项）。空映射同 EMPTY_OK：按需数据，dump 时没取到不算形状错误
KEYED_BY_ID = {"data.lyrics", "data.artist", "data.album", "data.user", "data.mv", "data.wiki", "data.song"}

# 首屏必有的列表；全空说明 dump 时没有网络（或请求全部失败），不是形状问题
FIRST_SCREEN = ("toplists", "tracks", "hotSearches")

# 每首归一化曲目（有 id 与 fee）都必须带这两个字段。两侧同时漏掉时通用 walk 比不出来，
# 所以单独点一次名，避免可播性字段悄悄从某一边消失。
TRACK_FIELDS = ("playable", "blockedBy")

# 每条归一化评论（有 likes 与 beReplied）都必须带 replyCount 与 voice。两侧同时漏掉时通用 walk 比不出来，
# 所以像 TRACK_FIELDS 一样单独点名，避免回复数悄悄从某一边消失。
COMMENT_FIELDS = ("replyCount", "voice")


def kind(value):
    t = type(value)
    return TYPE_NAMES.get(t, "unknown")


def walk(a, b, path, out):
    ta, tb = kind(a), kind(b)
    if ta != tb:
        out.append(f"{path}: expected {ta} got {tb}")
        return
    if isinstance(a, dict) and path in KEYED_BY_ID:
        if a and b:
            walk(next(iter(a.values())), next(iter(b.values())), path + "{}", out)
        return
    if isinstance(a, dict):
        if "id" in a and "fee" in a:
            for key in TRACK_FIELDS:
                if key not in a and key not in b:
                    out.append(f"{path}.{key}: missing from both sides")
        if "likes" in a and "beReplied" in a:
            for key in COMMENT_FIELDS:
                if key not in a and key not in b:
                    out.append(f"{path}.{key}: missing from both sides")
        for k in sorted(set(a) | set(b)):
            if k not in a:
                out.append(f"{path}.{k}: expected missing got {kind(b[k])}")
            elif k not in b:
                out.append(f"{path}.{k}: expected {kind(a[k])} got missing")
            else:
                walk(a[k], b[k], f"{path}.{k}", out)
    elif isinstance(a, list):
        if a and b:
            walk(a[0], b[0], path + "[]", out)
        elif a and not b and path not in EMPTY_OK:
            out.append(f"{path}: expected non-empty list got empty")


def load_js(path):
    """读取 Data.js / Personal.js：去掉 .pragma library 与注释，剥掉 `var x = ` 前缀。"""
    text = "\n".join(
        ln for ln in open(path, encoding="utf-8")
        if not ln.strip().startswith((".pragma", ".import", "//"))
    )
    out = {}
    for m in re.finditer(r"var (\w+) = (.*?)(?=\nvar \w+ = |\Z)", text, re.S):
        out[m.group(1)] = json.loads(m.group(2))
    varnames = set(re.findall(r"^\s*var (\w+) =", text, re.M))
    missing = varnames - set(out)
    if missing:
        sys.exit(f"{path}: unparseable vars: {sorted(missing)}")
    return out


def report(label, expected, actual):
    out = []
    walk(expected, actual, label, out)
    return out


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    dump_path, data_js, personal_js = sys.argv[1:]
    dump = json.load(open(dump_path, encoding="utf-8"))
    ref = load_js(data_js)
    # 参照有首屏数据而 dump 首屏全空：这是一次离线的 dump，先报原因而不是形状失配
    ref_has_data = any(isinstance(ref.get(k), list) and ref[k] for k in FIRST_SCREEN)
    dump_has_data = any(isinstance(dump.get("data", {}).get(k), list) and dump["data"][k]
                        for k in FIRST_SCREEN)
    if ref_has_data and not dump_has_data:
        print(f"{dump_path}: dump 没有数据（首屏 toplists/tracks/hotSearches 全空）；"
              "多半是抓取时没有网络，或所有请求都失败了。检查网络后重新生成 dump。",
              file=sys.stderr)
        sys.exit(2)
    mismatches = report("data", ref, dump.get("data", {}))
    mismatches += report("personal", load_js(personal_js), dump.get("personal", {}))
    for line in mismatches:
        print(line)
    if mismatches:
        sys.exit(1)
    print("contract ok")


if __name__ == "__main__":
    main()
