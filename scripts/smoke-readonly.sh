#!/usr/bin/env bash
# 只读 smoke：匿名会话，不读钥匙串、不写账号。覆盖 CLI + 核心数据端点。
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
APP=${APP:-"$ROOT/build/app/yunting"}
APP_CMD=("$APP" --anonymous)
DATA=${SMOKE_DATA:-"$ROOT/build/smoke-readonly-home"}
OUT=${SMOKE_OUT:-"$ROOT/build/smoke-readonly-last.json"}
mkdir -p "$DATA"

# 每次都用空缓存：app 先读缓存再刷新，复用旧缓存会让离线运行从上次的数据通过。
# SMOKE_CACHE 供调试时显式指定；否则在 build/ 下建一次性目录，退出时移进回收站。
if [ -n "${SMOKE_CACHE:-}" ]; then
    CACHE=$SMOKE_CACHE
    mkdir -p "$CACHE"
else
    mkdir -p "$ROOT/build"
    CACHE=$(mktemp -d "$ROOT/build/smoke-readonly-cache.XXXXXX")
    trap 'gio trash "$CACHE" 2>/dev/null || true' EXIT
fi

# 空缓存 + 断网时所有请求都会失败；先把原因说清楚
if ! python3 -c 'import socket; socket.create_connection(("music.163.com", 443), timeout=5)' 2>/dev/null; then
    echo "FAIL 网络不可达：smoke-readonly 每次都用空缓存，需要真实网络才能通过"
    exit 1
fi

export DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent
export XDG_CACHE_HOME="$CACHE"
export XDG_DATA_HOME="$DATA"
export YUNTING_KEYCHAIN_SERVICE=yunting-smoke

fail() {
    echo "FAIL $1"
    [ -f "$OUT" ] && cat "$OUT"
    exit 1
}

# check <label> <JSON 断言> <command...>
# 断言里用 d 指向解析后的 JSON；只写固定表达式，不接收外部输入。
check() {
    local label=$1 expr=$2
    shift 2
    if ! "$@" >"$OUT" 2>/tmp/yunting-smoke.err; then
        echo "FAIL $label"
        cat /tmp/yunting-smoke.err
        exit 1
    fi
    if ! python3 - "$OUT" "$expr" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
assert eval(sys.argv[2], {"d": d}), sys.argv[2]
PY
    then
        fail "$label"
    fi
    echo "ok   $label"
}

"$APP" --api list | grep -q 'api/v1/artist/<id>'
echo "ok   api list"

check "api artist" 'd["hotSongs"]' "${APP_CMD[@]}" --api get api/v1/artist/6452
check "artist detail" 'd["artist"]["6452"]["tracks"] and d["artist"]["6452"]["albums"]' "${APP_CMD[@]}" --detail artist:6452
check "album detail" 'd["album"]["32311"]["tracks"]' "${APP_CMD[@]}" --detail album:32311
check "artist songs" 'len(d["artistSongs"]["tracks"]) == 50' "${APP_CMD[@]}" --artist-songs 6452:100
check "comments page" 'len(d["commentPage"]["items"]) == 20' "${APP_CMD[@]}" --comments 108485:20
check "mv" 'd["mv"]["522365"]["url"] and d["mv"]["522365"]["br"] > 0' "${APP_CMD[@]}" --mv 522365
check "search users" 'd["searchUsers"]["items"]' "${APP_CMD[@]}" --search-users 林俊杰
check "search radios" 'd["searchRadios"]["items"]' "${APP_CMD[@]}" --search-radios 林俊杰

# 真引擎播放也不写账号：没有 --allow-write 时收听上报必须被拒绝。
# 不用 check（输出不是 JSON），只断言退出码与 AudioEngine 的拒绝日志。
AUDIO_OUT=/tmp/yunting-smoke-audio.out
AUDIO_ERR=/tmp/yunting-smoke-audio.err
if ! "${APP_CMD[@]}" --smoke-audio 108485 >"$AUDIO_OUT" 2>"$AUDIO_ERR"; then
    echo "FAIL smoke-audio"
    cat "$AUDIO_OUT" "$AUDIO_ERR"
    exit 1
fi
if ! grep -q 'listening reports disabled' "$AUDIO_ERR"; then
    echo "FAIL smoke-audio reported listening without --allow-write"
    cat "$AUDIO_OUT" "$AUDIO_ERR"
    exit 1
fi
echo "ok   smoke-audio reports nothing"
