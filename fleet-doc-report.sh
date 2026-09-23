#!/usr/bin/env bash
# Rank every DataZoo DuckDB extension by how much of it documents itself.
#
# This is the countermeasure to a report-only audit being ignored. A per-repo number
# nobody aggregates is a number nobody reads; a worst-first table with repo names on it
# is the thing that actually gets looked at. Run it weekly and paste it into the channel.
#
# Consumes the JSON that audit-function-docs.sh writes with --json. Either point this at
# a directory of those files, or let it discover and audit built artifacts itself.
#
# Usage:
#   ./fleet-doc-report.sh --json-dir /path/to/collected/json
#   ./fleet-doc-report.sh --scan /home/jr/Projects/datazoo     # audit what is built
#
# Options:
#   --json-dir DIR   Read *.json written by audit-function-docs.sh --json.
#   --scan DIR       Find built extensions under DIR and audit each one.
#   --duckdb PATH    duckdb CLI to use.

set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
JSON_DIR=""
SCAN_DIR=""
DUCKDB_BIN="${DUCKDB_BIN:-duckdb}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --json-dir) JSON_DIR="$2"; shift 2 ;;
        --scan)     SCAN_DIR="$2"; shift 2 ;;
        --duckdb)   DUCKDB_BIN="$2"; shift 2 ;;
        -h|--help)  sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 0 ;;
    esac
done

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

if [[ -n "$SCAN_DIR" ]]; then
    JSON_DIR="$TMP/json"
    mkdir -p "$JSON_DIR"
    # Prefer the versioned repository/ copy: it is the artifact the release pipeline
    # ships, whereas extension/ can be a stale intermediate.
    while IFS= read -r artifact; do
        name="$(basename "$artifact" .duckdb_extension)"
        [[ -f "$JSON_DIR/$name.json" ]] && continue
        echo "auditing $name ..." >&2
        "$HERE/audit-function-docs.sh" --extension "$name" --artifact "$artifact" \
            --duckdb "$DUCKDB_BIN" --json "$JSON_DIR/$name.json" >/dev/null 2>&1
    done < <(find "$SCAN_DIR" -path '*/build/release/*' -name '*.duckdb_extension' 2>/dev/null \
             | grep -vE '/(core_functions|parquet|json|httpfs|icu|tpch|tpcds|autocomplete|shell|jemalloc|fts|excel|inet|sqlsmith|visualizer)\.duckdb_extension$' \
             | sort)
fi

if [[ -z "$JSON_DIR" || ! -d "$JSON_DIR" ]]; then
    echo "fleet-doc-report: need --json-dir or --scan" >&2
    exit 0
fi

shopt -s nullglob
files=("$JSON_DIR"/*.json)
if ((${#files[@]} == 0)); then
    echo "fleet-doc-report: no JSON found in $JSON_DIR" >&2
    exit 0
fi

# DuckDB does the aggregation: it is already a dependency, and read_json over a glob
# handles the shape without any bash arithmetic.
"$DUCKDB_BIN" -noheader -markdown <<SQL
SELECT
    extension                                      AS "extension",
    eligible                                       AS "eligible",
    described                                      AS "described",
    CASE WHEN eligible = 0 THEN '-'
         ELSE (100 * described / eligible)::INT || '%' END AS "coverage",
    col0                                           AS "col0 params",
    gaps                                           AS "gaps",
    CASE WHEN aliases > 0 THEN aliases::VARCHAR ELSE '' END AS "aliases",
    CASE WHEN exempt  > 0 THEN exempt::VARCHAR  ELSE '' END AS "exempt",
    CASE WHEN examples_failed > 0
         THEN examples_failed || '/' || examples_checked ELSE '' END AS "bad examples"
FROM read_json('$JSON_DIR/*.json', union_by_name = true)
ORDER BY CASE WHEN eligible = 0 THEN 1 ELSE described::DOUBLE / eligible END ASC,
         gaps DESC,
         extension ASC;
SQL

"$DUCKDB_BIN" -noheader -list <<SQL
SELECT 'fleet: ' || sum(described) || '/' || sum(eligible) || ' eligible entries documented ('
    || (100 * sum(described) / nullif(sum(eligible), 0))::INT || '%), '
    || sum(col0) || ' still on col0, '
    || count(*) FILTER (described = eligible AND eligible > 0) || '/' || count(*) || ' extensions complete'
FROM read_json('$JSON_DIR/*.json', union_by_name = true);
SQL

exit 0
