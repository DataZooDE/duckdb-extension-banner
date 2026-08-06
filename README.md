# duckdb-extension-banner

A one-per-day, terminal-only feedback nudge for DataZoo DuckDB extensions and CLIs.

When someone loads one of our extensions in an interactive DuckDB shell, they see this once a day:

```
┌──────────────────────────────────────────────────────────────────┐
│ erpl_tunnel 2026.07.24                                           │
│                                                                  │
│ Hit a bug or something unexpected? Please tell us --             │
│ every issue makes the next release better:                       │
│   https://github.com/DataZooDE/erpl-tunnel/issues                │
│ ★ If it saved you time, a star helps others find it:             │
│   https://github.com/DataZooDE/erpl-tunnel                       │
│                                                                  │
│ (silence this: SET datazoo_banner=false, or DATAZOO_NO_BANNER=1) │
└──────────────────────────────────────────────────────────────────┘
```

And when the extension raises an error, the message ends with:

```
-> Unexpected? Please report it: https://github.com/DataZooDE/erpl-tunnel/issues
```

## Why it is quiet by default

A banner that shows up in a pipeline, a notebook or a CI log is a bug, not marketing. DuckDB has no
notice channel, so the only way to reach a user at load time is stderr — which means the gating
matters more than the copy. A banner prints only when **all** of these hold:

- stderr is a terminal (`isatty`), and for CLIs stdout is a terminal too;
- `DATAZOO_NO_BANNER` / `NO_BANNER` is unset or explicitly off;
- no CI marker is set (`CI`, `GITHUB_ACTIONS`, `GITLAB_CI`, `BUILDKITE`, `JENKINS_URL`,
  `TEAMCITY_VERSION`, `DUCKDB_TEST_RUNNER`, `DATAZOO_TEST`);
- `SET datazoo_banner = false` has not been issued in this process;
- `~/.duckdb/datazoo_banner/<extension>` is older than 24 hours.

The consequence worth stating plainly: **no existing test suite changes**, because under a test
runner stderr is not a tty and nothing is written. `test/test_banner.cpp` asserts exactly that.

If `HOME` is unwritable the stamp cannot be kept, and the banner degrades to once per process rather
than failing or spamming.

## Use it from a C++ extension

```cpp
#include "datazoo_banner_duckdb.hpp"

static constexpr datazoo::BannerInfo BANNER {
    "erpl_tunnel", ERPL_TUNNEL_VERSION, "https://github.com/DataZooDE/erpl-tunnel"};

static void LoadInternal(ExtensionLoader &loader) {
    // ...
    datazoo::RegisterBannerOption(loader);
    datazoo::ShowBanner(BANNER);

    loader.RegisterFunction(TableFunction("tunnels", {},
                                          DATAZOO_GUARD(BANNER, TunnelsExecute),
                                          DATAZOO_GUARD(BANNER, TunnelsBind)));
}
```

`DATAZOO_GUARD` yields a plain function pointer with the same signature, so it drops into DuckDB's
function slots directly. It rethrows with the original `ExceptionType` preserved and the issue link
appended, and it leaves `INTERRUPT`, `FATAL` and `OUT_OF_MEMORY` untouched. Nested guards append the
footer once.

For code that throws inline rather than through a registered function pointer, use
`DATAZOO_GUARDED_BLOCK(BANNER, { ... })`.

## Use it from a CLI or server

Include only `datazoo_banner.hpp`; it has no DuckDB dependency.

```cpp
datazoo::ShowBannerStandalone(BANNER, /* machine_readable = */ args.json || args.quiet);
```

For daemons that rarely run on a terminal (erpl-rev), the useful surface is the log rather than the
banner — `datazoo::FeedbackLine(BANNER)` returns a single unrated line suitable for an INFO log entry
or `--help` output.

## Telemetry

Building with `-DDATAZOO_BANNER_TELEMETRY=ON` emits a
`banner_shown` event when a banner actually prints, so the event count is an impression count. It
rides the extension's existing telemetry consent: if the user disabled telemetry, nothing is sent.
Without the flag the code is compiled out entirely.

`telemetry.hpp` has to be reachable from the translation units that include the banner header. Many
repos link `posthog_telemetry` as `PRIVATE`, in which case its include path does *not* reach them —
point `DATAZOO_BANNER_TELEMETRY_INCLUDE_DIR` at the directory instead:

```cmake
set(DATAZOO_BANNER_TELEMETRY ON CACHE BOOL "" FORCE)
set(DATAZOO_BANNER_TELEMETRY_INCLUDE_DIR
    ${CMAKE_SOURCE_DIR}/third_party/posthog-telemetry/include CACHE PATH "" FORCE)
add_subdirectory(third_party/datazoo-banner)
```

## Build and test

```bash
cmake -B build -DDATAZOO_BANNER_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure

cd rust/datazoo-banner && cargo test
```

The Rust crate is footer-only — the banner itself is emitted from the C++ entry point even in the
Rust-based extensions. Its `hint_matches_the_cpp_wording` test is what catches the two halves
drifting apart.

## Consuming it

Added as a submodule, exactly like `posthog-telemetry`:

```bash
git submodule add https://github.com/DataZooDE/duckdb-extension-banner.git third_party/datazoo-banner
```

## Why there is no `INTERFACE cxx_std_17`

The headers need C++17, and stating that with `target_compile_features(... INTERFACE cxx_std_17)`
is the obvious thing to do. It breaks the build.

The requirement propagates through the extension target into DuckDB's own
`tools/plan_serializer`, which then compiles as C++17 while `libduckdb_static` stays C++11. In that
split `BufferedFileWriter::DEFAULT_OPEN_FLAGS` — a `static constexpr` member with a deprecated
out-of-line definition — is COMDAT-weak on one side and a strong symbol on the other, and the link
dies with `multiple definition`. posthog-telemetry hit this; this library then hit it again in
anofox-statistics CI.

Every consumer already builds at C++17, so the declaration buys nothing. Leave it out.

## Keeping consumers in sync

A fix here only helps the repos that bump their submodule. When some do and some do not, the stale
ones fail in CI for a reason that looks like a bug in their own code — that is exactly how
`DBConfig::HasExtensionOption` (absent from DuckDB's 1.4 LTS line) took down erpl-idoc's LTS jobs
after erpl-tunnel had already been fixed.

```bash
./check-consumer-pins.sh            # scans sibling repos, flags stale pins
```

Run it after landing anything here, and bump the flagged repos before assuming the fix is live.

## License

MIT — this is call-to-action copy, not product IP, and it should be trivially vendorable.
