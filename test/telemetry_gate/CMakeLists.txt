# A consumer that switches DATAZOO_BANNER_TELEMETRY on, the way every DataZoo
# extension does, and then adds posthog_telemetry only when its dependencies
# were found - *after* the banner, as the extensions do. Configured twice by
# the parent test suite: WITH_TELEMETRY_TARGET=OFF must compile without
# telemetry.hpp (the macro stays off), ON must compile the telemetry path.
cmake_minimum_required(VERSION 3.14)
project(datazoo_banner_telemetry_gate LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

option(WITH_TELEMETRY_TARGET "Declare a posthog_telemetry target" OFF)

set(DATAZOO_BANNER_TELEMETRY ON CACHE BOOL "" FORCE)
add_subdirectory(${BANNER_SOURCE_DIR} banner)

if(WITH_TELEMETRY_TARGET)
    add_library(posthog_telemetry INTERFACE)
    target_include_directories(posthog_telemetry INTERFACE ${BANNER_SOURCE_DIR}/test/telemetry_stub)
endif()

add_executable(gate gate.cpp)
target_link_libraries(gate PRIVATE datazoo_banner)
if(WITH_TELEMETRY_TARGET)
    target_link_libraries(gate PRIVATE posthog_telemetry)
    target_compile_definitions(gate PRIVATE EXPECT_TELEMETRY=1)
else()
    target_compile_definitions(gate PRIVATE EXPECT_TELEMETRY=0)
endif()
