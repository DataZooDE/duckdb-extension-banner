cmake_minimum_required(VERSION 3.14)

# Version tracks the banner copy, not an ABI: bumping it means the wording or
# the gating rules changed and consumers should re-pin the submodule.
project(datazoo_banner VERSION 1.0.0 LANGUAGES CXX)

# INTERFACE, not STATIC: the whole library is two headers with no state that
# needs a translation unit. Consumers get the include path and nothing to link,
# which keeps this out of the link order of 19 different builds.
add_library(datazoo_banner INTERFACE)

target_include_directories(datazoo_banner INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Deliberately NOT target_compile_features(... INTERFACE cxx_std_17).
#
# The header needs C++17, but every consumer already builds at C++17, so stating
# the requirement buys nothing and costs a link error. An INTERFACE cxx_std_17
# propagates through the extension target into DuckDB's own tools/plan_serializer,
# which then compiles as C++17 while libduckdb_static stays C++11. In that split,
# BufferedFileWriter::DEFAULT_OPEN_FLAGS -- a static constexpr member with a
# deprecated out-of-line definition -- is COMDAT-weak on one side and strong on
# the other, and the link fails with a multiple definition error. posthog-telemetry
# hit exactly this; anofox-statistics CI hit it again through this library.

# Opt-in: when set, the banner reports a `banner_shown` event through the
# consumer's posthog-telemetry instance. Requires telemetry.hpp on the include
# path, which every DataZoo repo already has via its own submodule.
option(DATAZOO_BANNER_TELEMETRY "Emit a banner_shown event via posthog-telemetry" OFF)

# Where telemetry.hpp lives. Consumers link posthog_telemetry PRIVATE often
# enough that its include path does not reach the translation units including
# this header -- erpl-adt hit exactly that. Setting this carries the path on the
# banner's own INTERFACE, so switching telemetry on stays a one-liner per repo.
set(DATAZOO_BANNER_TELEMETRY_INCLUDE_DIR "" CACHE PATH
    "Directory containing telemetry.hpp; needed only with DATAZOO_BANNER_TELEMETRY=ON")

if(DATAZOO_BANNER_TELEMETRY)
    target_compile_definitions(datazoo_banner INTERFACE DATAZOO_BANNER_TELEMETRY)
    if(DATAZOO_BANNER_TELEMETRY_INCLUDE_DIR)
        target_include_directories(datazoo_banner INTERFACE
            ${DATAZOO_BANNER_TELEMETRY_INCLUDE_DIR})
    endif()
endif()

# DuckDB installs its extension targets through an export set, and CMake refuses
# to export a target that links something outside that set — even a header-only
# INTERFACE library that contributes nothing but an include path. Joining the
# set is what lets a consumer just link `datazoo_banner` like any other target.
# Same treatment posthog-telemetry gives itself.
if(DEFINED DUCKDB_EXPORT_SET)
    install(TARGETS datazoo_banner EXPORT "${DUCKDB_EXPORT_SET}")
endif()

option(DATAZOO_BANNER_BUILD_TESTS "Build the banner gating tests" OFF)
if(DATAZOO_BANNER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
