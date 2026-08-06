#!/usr/bin/env bash
# Report which consuming repos pin an out-of-date datazoo-banner commit.
#
# The failure this catches: a fix lands here, some consumers get the bump and
# others do not, and the stale ones fail in CI for a reason that looks like a
# bug in their own code. That happened with DBConfig::HasExtensionOption, which
# does not exist in DuckDB's 1.4 LTS line -- erpl-tunnel was bumped, erpl-idoc
# was not, and only the second one's CI went red.
#
# Usage: ./check-consumer-pins.sh [parent-dir]   (default: the directory above)

set -uo pipefail

parent="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
latest="$(git -C "$(dirname "$0")" rev-parse origin/main 2>/dev/null || git -C "$(dirname "$0")" rev-parse HEAD)"

echo "latest: ${latest:0:8}"
stale=0
for repo in "$parent"/*/; do
    for candidate in "$repo/datazoo-banner" "$repo/third_party/datazoo-banner"; do
        [ -d "$candidate" ] || continue
        cur="$(git -C "$candidate" rev-parse HEAD 2>/dev/null)" || continue
        name="$(basename "$repo")"
        if [ "$cur" = "$latest" ]; then
            printf '  %-24s %s ok\n' "$name" "${cur:0:8}"
        else
            printf '  %-24s %s STALE\n' "$name" "${cur:0:8}"
            stale=$((stale + 1))
        fi
    done
done

[ "$stale" -eq 0 ] || echo "$stale consumer(s) behind; bump before relying on a fix landing here."
exit 0
