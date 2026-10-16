#!/usr/bin/env bash
# 复原/更新只读参考库：普通克隆（不是 submodule），不入库，SHA 记录在 scripts/references.txt
# 用法: scripts/fetch-references.sh
set -euo pipefail

cd "$(dirname "$0")/.."

while IFS='|' read -r name url sha; do
    [ -z "${name:-}" ] && continue
    case "$name" in \#*) continue ;; esac

    dir="docs/reference/$name"

    if [ -d "$dir/.git" ]; then
        # 已有：尝试切到记录的 SHA
        if ! git -C "$dir" rev-parse --verify -q "$sha" >/dev/null 2>&1; then
            git -C "$dir" fetch --depth 1 origin "$sha" >/dev/null 2>&1 \
                || git -C "$dir" fetch origin >/dev/null 2>&1 || true
        fi
        git -C "$dir" checkout -q "$sha" 2>/dev/null \
            || echo "warn: $name 无法切到 $sha（保留当前版本）" >&2
    else
        git clone --depth 1 "$url" "$dir" >/dev/null 2>&1
        if ! git -C "$dir" rev-parse --verify -q "$sha" >/dev/null 2>&1; then
            git -C "$dir" fetch --depth 1 origin "$sha" >/dev/null 2>&1 \
                && git -C "$dir" checkout -q "$sha" 2>/dev/null \
                || echo "warn: $name 记录的 SHA 不可取，使用默认分支" >&2
        fi
    fi

    printf '%-16s %s\n' "$name" "$(git -C "$dir" rev-parse --short HEAD)"
done < scripts/references.txt
