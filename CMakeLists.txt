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

target_compile_features(datazoo_banner INTERFACE cxx_std_17)

# Opt-in: when set, the banner reports a `banner_shown` event through the
# consumer's posthog-telemetry instance. Requires telemetry.hpp on the include
# path, which every DataZoo repo already has via its own submodule.
option(DATAZOO_BANNER_TELEMETRY "Emit a banner_shown event via posthog-telemetry" OFF)
if(DATAZOO_BANNER_TELEMETRY)
    target_compile_definitions(datazoo_banner INTERFACE DATAZOO_BANNER_TELEMETRY)
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
