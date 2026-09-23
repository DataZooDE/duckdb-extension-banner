# Writing self-documenting DuckDB functions

An agent connected to a DuckDB database learns what an extension does by querying
`duckdb_functions()`. Our READMEs are not reachable from a SQL connection, and the
`extended_description` in community-extensions is both unreachable and too large to fit
in a context window. Per-function metadata is reachable, and searchable one function at
a time. That is the whole reason this exists.

Use `include/datazoo_function_doc.hpp`. Audit with `./audit-function-docs.sh`.

## The shape

```cpp
#include "datazoo_function_doc.hpp"
namespace ddoc = datazoo::doc;

static void RegisterThings(ExtensionLoader &loader) {
    ddoc::Registrar reg(loader, {"tunnel"});   // default categories for this extension

    reg.Register(MakeMyScalar(),
        {ddoc::Doc()
            .Describe("Returns the great-circle distance between two points in kilometres.")
            .Params({"lat_a", "lon_a", "lat_b", "lon_b"})
            .Example("my_distance(52.52, 13.40, 48.86, 2.35)")});
}
```

`Registrar` is an object, not a global, because several of our extensions can be loaded
into one process and a process-wide default would be whatever loaded last.

## The four fields

**`Describe`** — one sentence, present tense, saying what the function *does* and what it
returns. Mention named parameters in prose; DuckDB has no slot for them.

**`Params`** — the real argument names. This is the highest-value field: it turns
`f(col0, col1)` into a signature a caller can reason about. **But read the two traps
below before using it.**

**`Example`** — one runnable call. A bare expression for scalar and aggregate functions
(`date_trunc('hour', TIMESTAMPTZ '1992-09-20 20:38:40')`); a full statement for table
functions and pragmas (`SELECT * FROM my_func(...)`), because a table function used as a
bare expression is a binder error.

**`Categories`** — a short tag or two. Set defaults once on the `Registrar`.

## Trap 1: `Types` can silently delete your description

`duckdb_functions()` selects a description for an overload by matching
`parameter_types` against the overload's actual types. A concrete type that does not
match **disqualifies the description entirely** — and the result is a function with no
description, indistinguishable from one you never documented.

- **Default: use `Params` alone, with no `Types`.** A single description with no
  `parameter_types` matches every overload. This is right for the common case of one
  function with optional trailing arguments.
- **Use `Types` only** when overloads genuinely mean different things and each deserves
  its own sentence. Then give *every* overload its own `Doc` with its exact types,
  including the zero-argument one (`Types({})`).

**Review check**: the increase in the audit's described-count must equal the number of
`Doc`s you added. Any shortfall is a `parameter_types` mismatch.

## Trap 2: `Params` on a function with named parameters

Two different lists are in play, and they are **not the same length**:

| used for | contents |
|---|---|
| `GetParameterLogicalTypes` → overload **matching** | positional arguments **only** |
| `GetParameterTypes` → the parameter-**name** loop | positional arguments **+ named parameters** |

When `parameter_names` is non-empty, `duckdb_functions()` replaces the **whole** parameter
list: it walks the *second* list and fills any shortfall with `col1`, `col2`, …

So naming only the positional arguments of a function that *also* has named parameters
**overwrites the named-parameter names DuckDB was already reporting correctly**. Naming
`scenario_create`'s one positional argument turns

```
scenario_create(col0, key_columns, base, from_scenario, mode)
```

into

```
scenario_create(scenario, col1, col2, col3, col4)
```

which is strictly worse. The rules:

- All arguments are **named parameters** → do **not** set `Params`. The fallback is
  already right. (`tunnel_import` takes six named parameters and sets no `Params`.)
- Only **positional** arguments → set `Params` normally. (`tunnel_close(tunnel_id)`.)
- **Both** → `Params` must list the positional names **followed by every named parameter**,
  and `Types` must match the *positional* count only (that is the list matching uses).

That last case is order-sensitive: named parameters are emitted in
`fun.named_parameters` map-iteration order, which is a `case_insensitive_map_t` and
therefore **not sorted and not guaranteed stable**. Never hand-maintain such a list
without pinning the rendered output in a test:

```
query I
SELECT parameters FROM duckdb_functions()
WHERE function_name = 'scenario_create' AND len(parameters) = 5;
----
[scenario, key_columns, base, from_scenario, mode]
```

If the map order ever shifts, that fails loudly instead of silently misnaming every
argument.

## Pragmas can be documented

Widely believed otherwise, because `ExtensionLoader` exposes only
`RegisterFunction(PragmaFunction)` with no info-taking overload. But
`CreatePragmaFunctionInfo` derives from `CreateFunctionInfo`, and `duckdb_functions()`
extracts `PRAGMA_FUNCTION_ENTRY` through the same generic `ExtractFunctionData` path as
everything else. `Registrar::RegisterPragma` goes through the system catalog directly.

## Macros can be documented

`DefaultFunctionGenerator::CreateInternalMacroInfo` builds the info but leaves
`descriptions` empty, which is why macros normally arrive undocumented.
`Registrar::RegisterMacro` / `RegisterTableMacro` fill it in.

Macros created by **executing a `CREATE MACRO` SQL string cannot carry metadata at
all** — convert them to `DefaultMacro` to document them. `COMMENT ON MACRO` is not a
substitute: it populates `duckdb_functions().comment`, a different column from
`.description`.

## Aliases

Both entries get the full description, and the alias also gets `alias_of`. An agent
handed the short name needs to find documentation *at* the short name; making it follow
a pointer to learn what a function does is a worse catalog.

Consumers that read the whole catalog at once and care about size should skip rows where
`alias_of IS NOT NULL` — that is the reader's job, not the catalog's.

`AliasDirection` says which name is canonical. Our extensions genuinely differ
(anofox-statistics/optimize/tabfm make the prefixed name canonical; anofox-forecast makes
the short one canonical), both are published, and flipping either would rewrite
`alias_of` on entries users can see.

## Honesty rules

**Never infer a description from the function's own name.** Infer it from the
implementation, the tests, or an existing README. If you cannot point at where the
sentence came from, do not write it — leave a `TODO` and list the function for whoever
owns that domain. A confidently wrong description is worse for an agent than a missing
one, because the agent will act on it.

**Run `--check-examples`.** A hallucinated example usually does not survive a parser, so
this is the only automated check we have on whether documentation is *true* rather than
merely *present*.

## What cannot be documented

These are legitimate exemptions; declare them in `function-docs-exemptions.json`:

| `kind` | why |
|---|---|
| `secret_function` | `CreateSecretFunction` is not a `CreateFunctionInfo` |
| `copy_function` | same |
| `attach_generated` | one catalog entry per remote object at ATTACH time; unbounded |
| `runtime_create_macro` | `CREATE MACRO` executed into a user-chosen schema at runtime |
| `internal_underscore` | internal helpers not part of the public surface |

```json
[
  {"match": "my_ext_*_secret",
   "kind": "secret_function",
   "reason": "CreateSecretFunction has no descriptions field."}
]
```

`match` is a glob. `kind` must come from the table above — the audit rejects anything
else, so "exempt" cannot quietly become a synonym for "hard". The audit also reports any
exemption matching **zero** live functions as `STALE`, so they cannot outlive the code
they described.

## Auditing

```bash
./datazoo-banner/audit-function-docs.sh \
    --extension my_ext \
    --artifact build/release/extension/my_ext/my_ext.duckdb_extension \
    --check-examples
```

Always pass `--artifact`. Without it the script loads the *installed* extension, which is
the previous release — silently auditing the wrong binary.

The script is report-only and always exits 0.
