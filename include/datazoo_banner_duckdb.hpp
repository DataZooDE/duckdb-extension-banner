// datazoo_banner_duckdb.hpp — the DuckDB-facing half of the banner library.
//
// Include this from a loadable extension; include only datazoo_banner.hpp from
// CLIs and servers, which must not drag in duckdb.hpp.
//
// Two things live here:
//   * RegisterBannerOption — exposes `SET datazoo_banner = false`.
//   * DATAZOO_GUARD        — wraps a registered bind/init/execute function so
//                            every error it raises carries an issue link.

#pragma once

#include "datazoo_banner.hpp"

#include "duckdb.hpp"
#include "duckdb/common/error_data.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include <type_traits>
#include <utility>

namespace datazoo {

namespace banner_detail {

inline void OnBannerOptionSet(duckdb::ClientContext &, duckdb::SetScope, duckdb::Value &parameter) {
	BannerEnabledFlag() = parameter.GetValue<bool>();
}

// Errors we must not touch. INTERRUPT is a user pressing Ctrl-C, FATAL means
// the connection is already being torn down, and OUT_OF_MEMORY should not be
// made longer by string concatenation on the way out.
inline bool ShouldAnnotate(duckdb::ExceptionType type) {
	switch (type) {
	case duckdb::ExceptionType::INTERRUPT:
	case duckdb::ExceptionType::FATAL:
	case duckdb::ExceptionType::OUT_OF_MEMORY:
		return false;
	default:
		return true;
	}
}

[[noreturn]] inline void RethrowWithHint(const std::exception &caught, const BannerInfo &info) {
	duckdb::ErrorData error(caught);
	if (!error.HasError() || !ShouldAnnotate(error.Type())) {
		throw;
	}
	// Rebuild rather than wrap: ErrorData::Throw only prepends, and the hint
	// belongs at the end, after the real diagnostic. Reusing Type() keeps the
	// original exception class, which callers and clients switch on.
	duckdb::ErrorData(error.Type(), WithIssueHint(error.RawMessage(), info)).Throw();
}

// Turns a free function into an identical one that annotates any error it
// raises. Specialised on the function's signature so the result is still a
// plain function pointer — DuckDB's TableFunction/ScalarFunction slots take
// pointers, not std::function, so a capturing lambda would not fit.
template <class SIGNATURE, SIGNATURE *FN, const BannerInfo *INFO>
struct GuardedFunction;

template <class RETURN, class... ARGS, RETURN (*FN)(ARGS...), const BannerInfo *INFO>
struct GuardedFunction<RETURN(ARGS...), FN, INFO> {
	static RETURN Call(ARGS... args) {
		try {
			return FN(std::forward<ARGS>(args)...);
		} catch (const std::exception &caught) {
			RethrowWithHint(caught, *INFO);
		}
	}
};

} // namespace banner_detail

// Registers `SET datazoo_banner = false`. Tolerates being called by several
// DataZoo extensions in one database — the first one wins, the rest are no-ops.
inline void RegisterBannerOption(duckdb::ExtensionLoader &loader) {
	auto &instance = loader.GetDatabaseInstance();
	auto &config = duckdb::DBConfig::GetConfig(instance);
	if (config.HasExtensionOption("datazoo_banner")) {
		return;
	}
	config.AddExtensionOption("datazoo_banner",
	                          "Show the DataZoo feedback banner when an extension is loaded in an "
	                          "interactive terminal (at most once a day per extension).",
	                          duckdb::LogicalType::BOOLEAN, duckdb::Value(true),
	                          banner_detail::OnBannerOptionSet);
}

// Wraps a registered function so its errors carry an issue link:
//   loader.RegisterFunction(TableFunction("x", {}, DATAZOO_GUARD(BANNER, XExecute),
//                                         DATAZOO_GUARD(BANNER, XBind)));
// INFO must be a namespace-scope object; a `static constexpr BannerInfo` next to
// the extension's version constant is the intended shape.
#define DATAZOO_GUARD(INFO, FN)                                                                    \
	(&::datazoo::banner_detail::GuardedFunction<::std::remove_pointer<decltype(&FN)>::type, &FN,   \
	                                            &INFO>::Call)

// Statement form for code that throws inline rather than through a registered
// function pointer — a bind lambda, or a helper deep in the call tree.
#define DATAZOO_GUARDED_BLOCK(INFO, ...)                                                           \
	do {                                                                                           \
		try {                                                                                      \
			__VA_ARGS__                                                                            \
		} catch (const ::std::exception &_datazoo_caught) {                                        \
			::datazoo::banner_detail::RethrowWithHint(_datazoo_caught, INFO);                      \
		}                                                                                          \
	} while (false)

} // namespace datazoo
