#pragma once

// Helpers for registering DuckDB functions that document themselves.
//
// Why this exists
// ---------------
// An agent connected to a DuckDB database can only learn what an extension does by
// querying duckdb_functions(). A README is not reachable from a SQL connection. The
// metadata that IS reachable lives in FunctionDescription, and the bare
// ExtensionLoader::RegisterFunction(fn) overload -- the one nearly everyone reaches
// for -- has nowhere to put it.
//
// These helpers make the documented form the path of least resistance, and cover the
// two cases where ExtensionLoader offers no info-taking overload at all: pragmas and
// macros.
//
// A NOTE ON C++17, WHICH YOU MUST NOT "FIX"
// -----------------------------------------
// This is a header with no translation unit, consumed through the datazoo_banner
// INTERFACE target, which deliberately does NOT declare
// target_compile_features(... INTERFACE cxx_std_17).
//
// Do not add one. It propagates through the consuming extension target into DuckDB's
// own tools/plan_serializer, which then compiles as C++17 while libduckdb_static stays
// C++11. In that split, BufferedFileWriter::DEFAULT_OPEN_FLAGS -- a static constexpr
// member with a deprecated out-of-line definition -- is COMDAT-weak on one side and
// strong on the other, and the link fails with a multiple-definition error.
// posthog-telemetry hit this, then anofox-statistics CI hit it again through this very
// library. Every consumer already builds at C++17, so stating the requirement buys
// nothing and costs a link error.

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/default/default_functions.hpp"
#include "duckdb/catalog/default/default_table_functions.hpp"
#include "duckdb/function/function_set.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/parser/parsed_data/create_aggregate_function_info.hpp"
#include "duckdb/parser/parsed_data/create_macro_info.hpp"
#include "duckdb/parser/parsed_data/create_pragma_function_info.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"

#include <utility>

namespace datazoo {
namespace doc {

using duckdb::FunctionDescription;
using duckdb::LogicalType;
using duckdb::string;
using duckdb::vector;

using Docs = vector<FunctionDescription>;

//===--------------------------------------------------------------------===//
// Doc: a builder for FunctionDescription
//===--------------------------------------------------------------------===//
//
// On Params() vs Types(), which is the one part of this API that can silently make
// things WORSE rather than better:
//
//   duckdb_functions() picks a description for an overload by matching
//   FunctionDescription::parameter_types against the overload's actual types
//   (see CalcDescriptionSpecificity). A concrete type that does not match
//   DISQUALIFIES the description entirely -- and the symptom is a function with no
//   description at all, indistinguishable from one you never documented.
//
//   So the default is Params() alone, with NO Types(). A single description with no
//   parameter_types matches every overload, which is what you want for the common case
//   of one function with optional trailing arguments.
//
//   Reach for Types() only when a function has overloads that genuinely mean different
//   things and each deserves its own sentence. Then give EVERY overload its own Doc
//   with its exact types, including the zero-argument one (Types({})).
//
// On Params() and named parameters -- the other trap:
//
//   Two lists are in play and they are NOT the same length. Overload MATCHING uses
//   GetParameterLogicalTypes, which is the positional arguments only. The parameter
//   NAME loop uses GetParameterTypes, which is positional arguments FOLLOWED BY named
//   parameters. When parameter_names is non-empty it replaces the WHOLE list, walking
//   the second one and filling any shortfall with "col1", "col2", ...
//
//   So naming only the positional arguments of a function that also has named
//   parameters OVERWRITES the named-parameter names DuckDB was already reporting
//   correctly -- scenario_create(col0, key_columns, base, from_scenario, mode) becomes
//   scenario_create(scenario, col1, col2, col3, col4), which is strictly worse.
//
//     - all arguments are named parameters -> do NOT set Params(); the fallback is right
//     - only positional arguments         -> set Params() normally
//     - both                              -> Params() must list the positional names
//                                            followed by EVERY named parameter, while
//                                            Types() matches the positional count only
//
//   That last case is order-sensitive: named parameters come out in
//   fun.named_parameters map order, and that map is a case_insensitive_map_t -- not
//   sorted, not guaranteed stable. Pin the rendered `parameters` list in a test rather
//   than trusting it, or a reordering silently misnames every argument.
class Doc {
public:
	Doc() = default;

	//! One sentence on what the function does. Infer it from the implementation, the
	//! tests, or an existing README -- never from the function's own name. If you
	//! cannot point at where the sentence came from, leave it out and file a TODO: a
	//! confidently wrong description is worse for an agent than a missing one.
	Doc &Describe(string one_sentence) {
		desc.description = std::move(one_sentence);
		return *this;
	}

	//! Real argument names, never col0/col1. See the class comment before using this
	//! on a function with named parameters.
	Doc &Params(vector<string> names) {
		desc.parameter_names = std::move(names);
		return *this;
	}

	//! The overload key. Omit unless you are documenting genuinely distinct overloads;
	//! see the class comment.
	Doc &Types(vector<LogicalType> types) {
		desc.parameter_types = std::move(types);
		return *this;
	}

	//! One runnable call. Repeatable. A bare expression for scalar and aggregate
	//! functions; a full statement for table functions and pragmas, since a table
	//! function used as a bare expression is a binder error.
	Doc &Example(string runnable_call) {
		desc.examples.push_back(std::move(runnable_call));
		return *this;
	}

	Doc &Categories(vector<string> cats) {
		desc.categories = std::move(cats);
		return *this;
	}

	const FunctionDescription &Get() const {
		return desc;
	}
	operator FunctionDescription() const { // NOLINT: implicit by design, so Docs{Doc()...} works
		return desc;
	}

private:
	FunctionDescription desc;
};

//===--------------------------------------------------------------------===//
// Plumbing: map a function type to its Create*Info, and rename it
//===--------------------------------------------------------------------===//

template <class FUNC>
struct InfoFor;
template <>
struct InfoFor<duckdb::ScalarFunction> {
	using type = duckdb::CreateScalarFunctionInfo;
};
template <>
struct InfoFor<duckdb::ScalarFunctionSet> {
	using type = duckdb::CreateScalarFunctionInfo;
};
template <>
struct InfoFor<duckdb::AggregateFunction> {
	using type = duckdb::CreateAggregateFunctionInfo;
};
template <>
struct InfoFor<duckdb::AggregateFunctionSet> {
	using type = duckdb::CreateAggregateFunctionInfo;
};
template <>
struct InfoFor<duckdb::TableFunction> {
	using type = duckdb::CreateTableFunctionInfo;
};
template <>
struct InfoFor<duckdb::TableFunctionSet> {
	using type = duckdb::CreateTableFunctionInfo;
};

inline void RenameTo(duckdb::ScalarFunction &f, const string &n) {
	f.name = n;
}
inline void RenameTo(duckdb::AggregateFunction &f, const string &n) {
	f.name = n;
}
inline void RenameTo(duckdb::TableFunction &f, const string &n) {
	f.name = n;
}

//! A set carries the name twice -- on the set and on every overload inside it -- and
//! both have to move, or the catalog entry and its functions disagree.
template <class SET>
inline void RenameSetTo(SET &s, const string &n) {
	s.name = n;
	for (auto &f : s.functions) {
		f.name = n;
	}
}
inline void RenameTo(duckdb::ScalarFunctionSet &s, const string &n) {
	RenameSetTo(s, n);
}
inline void RenameTo(duckdb::AggregateFunctionSet &s, const string &n) {
	RenameSetTo(s, n);
}
inline void RenameTo(duckdb::TableFunctionSet &s, const string &n) {
	RenameSetTo(s, n);
}

//===--------------------------------------------------------------------===//
// Registrar
//===--------------------------------------------------------------------===//

//! Which of the two names is the documented original and which points at it.
//!
//! This is a parameter rather than a convention because our extensions genuinely
//! differ: anofox-statistics, anofox-optimize and anofox-tabfm make the prefixed name
//! canonical, while anofox-forecast makes the short one canonical. Both are already
//! published, and flipping either would rewrite alias_of on entries users can see.
enum class AliasDirection {
	//! The function passed in is canonical; `alias_name` points at it.
	kPrimaryIsCanonical,
	//! `alias_name` is canonical; the function passed in points at IT.
	kAliasIsCanonical
};

//! Holds the loader and this extension's default categories.
//!
//! Deliberately an object rather than a global: several DataZoo extensions can be
//! loaded into one process, and a process-wide "default categories" would be whatever
//! the last extension to load happened to set.
class Registrar {
public:
	explicit Registrar(duckdb::ExtensionLoader &loader, vector<string> default_categories = {})
	    : loader(loader), default_categories(std::move(default_categories)) {
	}

	//===------------------------------------------------------------------===//
	// Scalar / aggregate / table functions
	//===------------------------------------------------------------------===//

	template <class FUNC>
	void Register(FUNC fn, Docs docs,
	              duckdb::OnCreateConflict on_conflict = duckdb::OnCreateConflict::ERROR_ON_CONFLICT) {
		typename InfoFor<FUNC>::type info(std::move(fn));
		info.descriptions = ApplyDefaults(std::move(docs));
		info.on_conflict = on_conflict;
		loader.RegisterFunction(std::move(info));
	}

	//! Register a function under two names.
	//!
	//! The alias is produced by COPY-THEN-RENAME, never by rebuilding the function from
	//! its parts. Reconstruction silently drops bind_replace, statistics propagation,
	//! pushdown flags, serialization hooks, null handling and error modes -- the alias
	//! has to be behaviourally identical, not merely similarly named.
	//!
	//! Both entries receive the full description. An agent handed the short name needs
	//! to find documentation AT the short name; making it follow alias_of to find out
	//! what a function does is a worse catalog. Consumers that read the whole catalog at
	//! once and care about size should skip rows where alias_of IS NOT NULL.
	template <class FUNC>
	void RegisterWithAlias(FUNC fn, const string &alias_name, Docs docs,
	                       AliasDirection direction = AliasDirection::kPrimaryIsCanonical,
	                       duckdb::OnCreateConflict on_conflict = duckdb::OnCreateConflict::ERROR_ON_CONFLICT) {
		auto resolved = ApplyDefaults(std::move(docs));
		const string primary_name = fn.name;

		FUNC alias_fn = fn; // copy first: `fn` is still the canonical spelling
		RenameTo(alias_fn, alias_name);

		const bool primary_is_canonical = direction == AliasDirection::kPrimaryIsCanonical;
		const string &canonical = primary_is_canonical ? primary_name : alias_name;

		{
			typename InfoFor<FUNC>::type info(std::move(fn));
			info.descriptions = resolved;
			info.on_conflict = on_conflict;
			if (!primary_is_canonical) {
				info.alias_of = canonical;
			}
			loader.RegisterFunction(std::move(info));
		}
		{
			typename InfoFor<FUNC>::type info(std::move(alias_fn));
			info.descriptions = std::move(resolved);
			info.on_conflict = on_conflict;
			if (primary_is_canonical) {
				info.alias_of = canonical;
			}
			loader.RegisterFunction(std::move(info));
		}
	}

	//===------------------------------------------------------------------===//
	// Pragmas
	//===------------------------------------------------------------------===//

	//! Register a pragma WITH documentation.
	//!
	//! Extensions widely believe pragmas cannot be documented, because ExtensionLoader
	//! exposes only RegisterFunction(PragmaFunction) with no info-taking overload. But
	//! CreatePragmaFunctionInfo derives from CreateFunctionInfo, and duckdb_functions()
	//! extracts PRAGMA_FUNCTION_ENTRY through the same generic ExtractFunctionData path
	//! as every other function type. Going through the system catalog directly is all
	//! that is needed.
	//!
	//! Re-read Doc's comment on Params() before documenting a pragma: most of our
	//! pragmas take named parameters only, and those must NOT set Params().
	void RegisterPragma(duckdb::PragmaFunction pragma, Docs docs,
	                    duckdb::OnCreateConflict on_conflict = duckdb::OnCreateConflict::ERROR_ON_CONFLICT) {
		auto name = pragma.name;
		duckdb::PragmaFunctionSet set(name);
		set.AddFunction(std::move(pragma));
		RegisterPragmaSet(std::move(name), std::move(set), std::move(docs), on_conflict);
	}

	void RegisterPragmaSet(string name, duckdb::PragmaFunctionSet set, Docs docs,
	                       duckdb::OnCreateConflict on_conflict = duckdb::OnCreateConflict::ERROR_ON_CONFLICT) {
		duckdb::CreatePragmaFunctionInfo info(std::move(name), std::move(set));
		info.descriptions = ApplyDefaults(std::move(docs));
		info.on_conflict = on_conflict;

		auto &db = loader.GetDatabaseInstance();
		auto &system_catalog = duckdb::Catalog::GetSystemCatalog(db);
		auto transaction = duckdb::CatalogTransaction::GetSystemTransaction(db);
		system_catalog.CreatePragmaFunction(transaction, info);
	}

	//===------------------------------------------------------------------===//
	// Macros
	//===------------------------------------------------------------------===//

	//! Register a macro WITH documentation.
	//!
	//! DefaultFunctionGenerator::CreateInternalMacroInfo builds the info but leaves
	//! `descriptions` empty, so macros registered the usual way arrive undocumented.
	//! Pushing the description onto the returned info before registering is all that is
	//! required -- CreateMacroInfo derives from CreateFunctionInfo like everything else.
	//!
	//! Macros created by EXECUTING a "CREATE MACRO ..." SQL string cannot carry
	//! metadata at all; convert those to DefaultMacro to document them. COMMENT ON
	//! MACRO is not a substitute -- it populates duckdb_functions().comment, which is a
	//! different column from .description.
	void RegisterMacro(const duckdb::DefaultMacro &macro, Docs docs) {
		auto info = duckdb::DefaultFunctionGenerator::CreateInternalMacroInfo(macro);
		info->descriptions = ApplyDefaults(std::move(docs));
		loader.RegisterFunction(*info);
	}

	void RegisterTableMacro(const duckdb::DefaultTableMacro &macro, Docs docs) {
		auto info = duckdb::DefaultTableFunctionGenerator::CreateTableMacroInfo(macro);
		info->descriptions = ApplyDefaults(std::move(docs));
		loader.RegisterFunction(*info);
	}

private:
	//! Fill in this extension's categories for any Doc that did not set its own.
	Docs ApplyDefaults(Docs docs) const {
		if (!default_categories.empty()) {
			for (auto &d : docs) {
				if (d.categories.empty()) {
					d.categories = default_categories;
				}
			}
		}
		return docs;
	}

	duckdb::ExtensionLoader &loader;
	vector<string> default_categories;
};

} // namespace doc
} // namespace datazoo
