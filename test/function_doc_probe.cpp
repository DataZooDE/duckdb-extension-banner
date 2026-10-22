// Compile probe: datazoo_function_doc.hpp against the real DuckDB headers of each line
// (CI: 1.4.5, 1.5.6, 2.0.0). Run with -fsyntax-only; nothing is linked or executed.
#include "datazoo_function_doc.hpp"

namespace {
void ProbeRename(duckdb::ScalarFunction &sf, duckdb::AggregateFunction &af, duckdb::TableFunction &tf,
                 duckdb::ScalarFunctionSet &sfs, duckdb::AggregateFunctionSet &afs, duckdb::TableFunctionSet &tfs) {
	datazoo::doc::RenameTo(sf, "alias");
	datazoo::doc::RenameTo(af, "alias");
	datazoo::doc::RenameTo(tf, "alias");
	datazoo::doc::RenameTo(sfs, "alias");
	datazoo::doc::RenameTo(afs, "alias");
	datazoo::doc::RenameTo(tfs, "alias");
}
} // namespace
