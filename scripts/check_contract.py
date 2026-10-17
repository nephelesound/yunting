#!/usr/bin/env python3
"""契约检查：对比 ncm-player --dump-store 的 JSON 与 scripts/snapshot.py 写出的快照的键名与 JSON 类型。

用法: check_contract.py <dump.json> <Data.js> <Personal.js>
参照快照与 dump 必须是同一种会话（都带 cookie，或都匿名：snapshot.py --anonymous --out DIR）。
数据形状的唯一定义是 snapshot.py；这里不另写一份。
只比键名与类型（列表元素形状 = 首元素），不比任何值；不一致即非零退出。
"""
import json
import re
import sys

TYPE_NAMES = {
    type(None): "null", bool: "bool", int: "number", float: "number",
    str: "string", list: "list", dict: "map",
}


def kind(value):
    t = type(value)
    return TYPE_NAMES.get(t, "unknown")


def walk(a, b, path, out):
    ta, tb = kind(a), kind(b)
    if ta != tb:
        out.append(f"{path}: expected {ta} got {tb}")
        return
    if isinstance(a, dict):
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
        elif a and not b:
            # 空列表是「没有数据」，不是形状错误（云盘为空、按需加载的歌词还没取）；列出来但不算失败
            print(f"not checked (empty): {path}[]")


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
    mismatches = report("data", load_js(data_js), dump.get("data", {}))
    mismatches += report("personal", load_js(personal_js), dump.get("personal", {}))
    for line in mismatches:
        print(line)
    if mismatches:
        sys.exit(1)
    print("contract ok")


if __name__ == "__main__":
    main()
