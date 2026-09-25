#!/usr/bin/env bash
# Report how much of an extension's catalog surface is self-documenting.
#
# An AI agent connected to a DuckDB database can only learn what an extension does by
# querying duckdb_functions(). A README is not reachable from a SQL connection. This
# script measures what that agent would actually see: description, examples, real
# parameter names (not col0/col1/...) and categories, per registered function.
#
# Placeholder detection matches ANY colN, not just col0. DuckDB fills parameter_names
# positionally and pads the shortfall, so a function whose names cover only its first
# three arguments renders as (a, b, c, col3, col4, ...) -- documented at the front,
# placeholders at the back. Matching col0 alone silently passes exactly that case,
# which is how anofox_tab_outlier_tree's 9-argument overload went unnoticed.
#
# Spelled as an explicit list rather than a regex over a lambda on purpose. The
# `->` lambda arrow is deprecated and DuckDB prints a warning for it on STDOUT,
# which lands in this report and cost the STATS row entirely; the replacement
# `lambda x:` syntax is not available on the 1.4.5 LTS CLI some of these repos
# audit against. A literal list works everywhere and says exactly what it means.
#
# It is REPORT-ONLY and always exits 0. Failing a build on missing prose would only
# teach people to write filler; the countermeasure to drift is that the numbers are
# visible (CI writes this table to the job summary) and ranked across repos.
#
# Usage:
#   ./audit-function-docs.sh --extension erpl_tunnel [options]
#   ./audit-function-docs.sh --extension erpl_tunnel --artifact build/release/extension/erpl_tunnel/erpl_tunnel.duckdb_extension
#
# Options:
#   --extension NAME     Extension to audit (required).
#   --artifact PATH      Load this built file instead of the installed extension.
#                        Always prefer this in CI: the installed copy is the PREVIOUS
#                        release, so auditing it silently measures the wrong binary.
#   --preload "a b"      Extra extensions to LOAD before the baseline snapshot, for
#                        dependencies beyond the common autoload set. If the report warns
#                        UNEXPECTED_AUTOLOAD, that names exactly what to pass here.
#   --exemptions PATH    Default: ./function-docs-exemptions.json
#   --check-examples     Validate every example (see "Checking examples" below).
#   --json PATH          Also write one JSON object for the fleet roll-up.
#   --duckdb PATH        duckdb CLI to use (default: duckdb on PATH).
#
# Checking examples
#   The point of --check-examples is that a hallucinated example usually does not
#   survive contact with a parser, which makes it the only automated check we have on
#   whether documentation is true rather than merely present.
#
#   Every example is PARSE-checked via json_serialize_sql(), which never executes.
#   Scalar, aggregate and macro examples are additionally BIND-checked via EXPLAIN,
#   which resolves the function name and arity without running anything -- that is what
#   catches a plausible-looking call to a function that does not exist.
#
#   Table-function and pragma examples are deliberately NOT bind-checked. Their bind
#   phase is where DuckDB does I/O, so EXPLAIN on `PRAGMA tunnel_import(...)` would open
#   a real SSH connection. An auditing tool must not have side effects.

set -uo pipefail

EXT=""
ARTIFACT=""
EXEMPTIONS="function-docs-exemptions.json"
EXTRA_PRELOAD=""
CHECK_EXAMPLES=0
JSON_OUT=""
DUCKDB_BIN="${DUCKDB_BIN:-duckdb}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --extension)   EXT="$2"; shift 2 ;;
        --artifact)    ARTIFACT="$2"; shift 2 ;;
        --preload)     EXTRA_PRELOAD="$2"; shift 2 ;;
        --exemptions)  EXEMPTIONS="$2"; shift 2 ;;
        --check-examples) CHECK_EXAMPLES=1; shift ;;
        --json)        JSON_OUT="$2"; shift 2 ;;
        --duckdb)      DUCKDB_BIN="$2"; shift 2 ;;
        -h|--help)     sed -n '2,40p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 0 ;;
    esac
done

if [[ -z "$EXT" ]]; then
    echo "audit-function-docs: --extension is required" >&2
    exit 0
fi

if ! command -v "$DUCKDB_BIN" >/dev/null 2>&1 && [[ ! -x "$DUCKDB_BIN" ]]; then
    echo "audit-function-docs: duckdb CLI not found (set --duckdb or DUCKDB_BIN)" >&2
    exit 0
fi

# An unsigned locally-built artifact needs -unsigned to load at all.
DUCKDB=("$DUCKDB_BIN" -unsigned -noheader -list)

if [[ -n "$ARTIFACT" ]]; then
    if [[ ! -f "$ARTIFACT" ]]; then
        echo "audit-function-docs: artifact not found: $ARTIFACT" >&2
        exit 0
    fi
    LOAD_STMT="LOAD '$(realpath "$ARTIFACT")';"
else
    LOAD_STMT="LOAD ${EXT};"
fi

# ---------------------------------------------------------------------------
# Baseline
#
# Loading an extension autoloads its dependencies -- json, parquet and
# core_functions at least -- and a naive before/after delta counts THEIR functions
# as ours. That is not a rounding error: it read anofox_visualization as 128
# functions when the real number is 7.
#
# So preload the known autoload set explicitly before the snapshot, then check
# afterwards whether anything else appeared. An unexpected autoload is reported as a
# stale preload list rather than silently inflating the total.
# ---------------------------------------------------------------------------
PRELOAD="INSTALL json; LOAD json; INSTALL parquet; LOAD parquet; LOAD core_functions;"
for _ext in $EXTRA_PRELOAD; do
    PRELOAD="$PRELOAD INSTALL ${_ext}; LOAD ${_ext};"
done

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# Normalise the exemptions into a table the SQL below can anti-join against.
# JSON rather than YAML purely so DuckDB can read it natively; parsing YAML in bash
# is exactly the kind of thing that fails quietly on the one file that matters.
#
# kind is a CLOSED vocabulary. An exemption whose kind is not in this list is
# reported as invalid, because "exempt" must not become a synonym for "hard".
VALID_KINDS="secret_function,copy_function,attach_generated,runtime_create_macro,internal_underscore"

if [[ -f "$EXEMPTIONS" ]]; then
    EXEMPT_SQL="CREATE TEMP TABLE _exempt AS
        SELECT match, kind, reason FROM read_json('$(realpath "$EXEMPTIONS")',
            columns = {match: 'VARCHAR', kind: 'VARCHAR', reason: 'VARCHAR'});"
else
    EXEMPT_SQL="CREATE TEMP TABLE _exempt(match VARCHAR, kind VARCHAR, reason VARCHAR);"
fi

REPORT="$TMP/report.txt"

"${DUCKDB[@]}" <<SQL > "$REPORT" 2>"$TMP/err.txt"
$PRELOAD
CREATE TEMP TABLE _pre_ext AS SELECT extension_name FROM duckdb_extensions() WHERE loaded;
CREATE TEMP TABLE _before AS SELECT function_name, function_type FROM duckdb_functions();

$LOAD_STMT

$EXEMPT_SQL

-- Every catalog entry this extension added.
CREATE TEMP TABLE _own AS
SELECT function_name, function_type, alias_of, description, examples, parameters, categories
FROM duckdb_functions()
WHERE (function_name, function_type) NOT IN (SELECT * FROM _before);

-- Exemption matching. 'match' is a glob so a family can be covered by one entry.
CREATE TEMP TABLE _classified AS
SELECT o.*,
       (SELECT min(e.kind) FROM _exempt e WHERE o.function_name GLOB e.match) AS exempt_kind
FROM _own o;

-- An unexpected autoload means the preload list above is out of date and the
-- totals below are inflated. Say so loudly rather than reporting a wrong number.
SELECT '::UNEXPECTED_AUTOLOAD::' || string_agg(extension_name, ', ')
FROM duckdb_extensions()
WHERE loaded
  AND extension_name NOT IN (SELECT * FROM _pre_ext)
  AND extension_name <> '${EXT}'
HAVING count(*) > 0;

-- Exemptions that match nothing are stale: the code they described is gone.
SELECT '::STALE_EXEMPTION::' || e.match
FROM _exempt e
WHERE NOT EXISTS (SELECT 1 FROM _own o WHERE o.function_name GLOB e.match);

SELECT '::INVALID_KIND::' || e.match || ' (' || coalesce(e.kind, '<null>') || ')'
FROM _exempt e
WHERE e.kind IS NULL OR NOT list_contains(string_split('$VALID_KINDS', ','), e.kind);

-- Headline numbers. 'raw' counts every entry, which is what the ecosystem-wide
-- surveys measure, so it stays comparable. 'eligible' removes exemptions and
-- counts aliases separately, which is what we actually steer on.
SELECT '::STATS::'
    || count(*)
    || '|' || count(*) FILTER (alias_of IS NOT NULL)
    || '|' || count(*) FILTER (exempt_kind IS NOT NULL)
    || '|' || count(*) FILTER (description IS NOT NULL AND description <> '')
    || '|' || count(*) FILTER (len(examples) > 0)
    || '|' || count(*) FILTER (len(categories) > 0)
    || '|' || count(*) FILTER (list_has_any(parameters, ['col0', 'col1', 'col2', 'col3', 'col4', 'col5', 'col6', 'col7', 'col8', 'col9', 'col10', 'col11', 'col12', 'col13', 'col14', 'col15', 'col16', 'col17', 'col18', 'col19', 'col20', 'col21', 'col22', 'col23', 'col24', 'col25', 'col26', 'col27', 'col28', 'col29', 'col30', 'col31', 'col32', 'col33', 'col34', 'col35', 'col36', 'col37', 'col38', 'col39']))
    || '|' || count(*) FILTER (exempt_kind IS NULL
                               AND (description IS NULL OR description = ''))
    || '|' || count(*) FILTER (exempt_kind IS NULL)
FROM _classified;

-- The worklist: what is actually undocumented, most useful line of the report.
SELECT '::GAP::' || function_type || '|' || function_name
    || '|' || CASE WHEN description IS NULL OR description = '' THEN 'no-description' ELSE 'ok' END
    || '|' || CASE WHEN len(examples) = 0 THEN 'no-example' ELSE 'ok' END
    || '|' || CASE WHEN list_has_any(parameters, ['col0', 'col1', 'col2', 'col3', 'col4', 'col5', 'col6', 'col7', 'col8', 'col9', 'col10', 'col11', 'col12', 'col13', 'col14', 'col15', 'col16', 'col17', 'col18', 'col19', 'col20', 'col21', 'col22', 'col23', 'col24', 'col25', 'col26', 'col27', 'col28', 'col29', 'col30', 'col31', 'col32', 'col33', 'col34', 'col35', 'col36', 'col37', 'col38', 'col39']) THEN 'colN-params' ELSE 'ok' END
    || '|' || CASE WHEN len(categories) = 0 THEN 'no-categories' ELSE 'ok' END
FROM _classified
WHERE exempt_kind IS NULL
  AND (description IS NULL OR description = ''
       OR len(examples) = 0
       OR list_has_any(parameters, ['col0', 'col1', 'col2', 'col3', 'col4', 'col5', 'col6', 'col7', 'col8', 'col9', 'col10', 'col11', 'col12', 'col13', 'col14', 'col15', 'col16', 'col17', 'col18', 'col19', 'col20', 'col21', 'col22', 'col23', 'col24', 'col25', 'col26', 'col27', 'col28', 'col29', 'col30', 'col31', 'col32', 'col33', 'col34', 'col35', 'col36', 'col37', 'col38', 'col39'])
       OR len(categories) = 0)
ORDER BY function_name;

SELECT '::EXAMPLE::' || function_type || '|' || function_name || '|' || unnest(examples)
FROM _classified WHERE len(examples) > 0;
SQL

if [[ -s "$TMP/err.txt" ]] && ! grep -q '::STATS::' "$REPORT"; then
    echo "audit-function-docs: could not load '${EXT}'" >&2
    sed 's/^/  /' "$TMP/err.txt" >&2
    exit 0
fi

# ---------------------------------------------------------------------------
# Example validation
# ---------------------------------------------------------------------------
EX_TOTAL=0
EX_BAD=0
declare -a EX_FAILURES=()

if [[ "$CHECK_EXAMPLES" == "1" ]]; then
    while IFS='|' read -r ftype fname example; do
        [[ -z "$example" ]] && continue
        EX_TOTAL=$((EX_TOTAL + 1))

        # Scalar, aggregate and scalar-macro examples are conventionally bare
        # expressions; wrap them so they are parseable statements. A table macro's
        # example is already a full SELECT, which the guard below detects.
        stmt="$example"
        case "$ftype" in
            scalar|aggregate|macro)
                [[ "$stmt" =~ ^[[:space:]]*(SELECT|WITH|PRAGMA|CALL|EXPLAIN) ]] || stmt="SELECT $stmt"
                ;;
        esac

        esc="${stmt//\'/\'\'}"
        if ! "${DUCKDB[@]}" -c "SELECT json_serialize_sql('$esc');" >/dev/null 2>&1; then
            EX_BAD=$((EX_BAD + 1))
            EX_FAILURES+=("parse|$fname|$example")
            continue
        fi

        # Bind-check only where bind is pure. A table function's or pragma's bind is
        # where it opens sockets, and an audit must not have side effects.
        case "$ftype" in
            scalar|aggregate|macro)
                if ! "${DUCKDB[@]}" -c "$PRELOAD $LOAD_STMT EXPLAIN $stmt;" >/dev/null 2>&1; then
                    EX_BAD=$((EX_BAD + 1))
                    EX_FAILURES+=("bind|$fname|$example")
                fi
                ;;
        esac
    done < <(grep '^::EXAMPLE::' "$REPORT" | sed 's/^::EXAMPLE:://')
fi

# ---------------------------------------------------------------------------
# Render
# ---------------------------------------------------------------------------
stats_line="$(grep '^::STATS::' "$REPORT" | head -1 | sed 's/^::STATS:://')"
IFS='|' read -r TOTAL ALIASES EXEMPT DESCRIBED EXAMPLES CATEGORIES COL0 UNDOC ELIGIBLE <<< "$stats_line"
TOTAL=${TOTAL:-0}; ALIASES=${ALIASES:-0}; EXEMPT=${EXEMPT:-0}; DESCRIBED=${DESCRIBED:-0}
EXAMPLES=${EXAMPLES:-0}; CATEGORIES=${CATEGORIES:-0}; COL0=${COL0:-0}; ELIGIBLE=${ELIGIBLE:-0}

pct() { # pct <num> <denom>
    if [[ "${2:-0}" -eq 0 ]]; then echo "n/a"; else echo "$(( 100 * $1 / $2 ))%"; fi
}

echo "## Function documentation — \`${EXT}\`"
echo
echo "| metric | count | of eligible |"
echo "|---|---|---|"
echo "| registered | ${TOTAL} | |"
echo "| aliases (\`alias_of\`) | ${ALIASES} | |"
echo "| exempt | ${EXEMPT} | |"
echo "| **eligible** | **${ELIGIBLE}** | |"
echo "| with description | ${DESCRIBED} | $(pct "$DESCRIBED" "$ELIGIBLE") |"
echo "| with examples | ${EXAMPLES} | $(pct "$EXAMPLES" "$ELIGIBLE") |"
echo "| with categories | ${CATEGORIES} | $(pct "$CATEGORIES" "$ELIGIBLE") |"
echo "| placeholder \`colN\` params | ${COL0} | |"

if [[ "$CHECK_EXAMPLES" == "1" ]]; then
    echo "| examples checked | ${EX_TOTAL} | ${EX_BAD} failed |"
fi
echo

for marker in UNEXPECTED_AUTOLOAD STALE_EXEMPTION INVALID_KIND; do
    if grep -q "^::${marker}::" "$REPORT"; then
        echo "> **${marker}**"
        grep "^::${marker}::" "$REPORT" | sed "s/^::${marker}:://; s/^/> - /"
        echo
    fi
done

if ((${#EX_FAILURES[@]} > 0)); then
    echo "### Examples that do not hold up"
    echo
    echo "| check | function | example |"
    echo "|---|---|---|"
    for f in "${EX_FAILURES[@]}"; do
        IFS='|' read -r kind fname ex <<< "$f"
        echo "| ${kind} | \`${fname}\` | \`${ex}\` |"
    done
    echo
fi

gap_count="$(grep -c '^::GAP::' "$REPORT" || true)"
if [[ "${gap_count:-0}" -gt 0 ]]; then
    echo "### Gaps (${gap_count})"
    echo
    echo "| type | function | description | example | params | categories |"
    echo "|---|---|---|---|---|---|"
    grep '^::GAP::' "$REPORT" | sed 's/^::GAP:://' | while IFS='|' read -r t n d e p c; do
        echo "| ${t} | \`${n}\` | ${d} | ${e} | ${p} | ${c} |"
    done
    echo
else
    echo "No gaps: every eligible function carries a description, an example, real parameter names and categories."
    echo
fi

if [[ -n "$JSON_OUT" ]]; then
    cat > "$JSON_OUT" <<JSON
{"extension":"${EXT}","total":${TOTAL},"aliases":${ALIASES},"exempt":${EXEMPT},"eligible":${ELIGIBLE},"described":${DESCRIBED},"examples":${EXAMPLES},"categories":${CATEGORIES},"col0":${COL0},"gaps":${gap_count:-0},"examples_checked":${EX_TOTAL},"examples_failed":${EX_BAD}}
JSON
fi

exit 0
