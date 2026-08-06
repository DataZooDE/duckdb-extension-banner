add_executable(test_banner test_banner.cpp)
target_link_libraries(test_banner PRIVATE datazoo_banner)

# The gating tests assert on "no output when stderr is not a tty", which is
# exactly the situation ctest runs them in.
add_test(NAME banner_gating COMMAND test_banner)

# The telemetry path is compiled out unless a consumer switches it on, so this
# repo would otherwise never build it. The stub reproduces just enough of
# posthog-telemetry's surface -- crucially its namespace -- to catch that.
add_executable(test_banner_telemetry test_banner_telemetry.cpp)
target_link_libraries(test_banner_telemetry PRIVATE datazoo_banner)
target_include_directories(test_banner_telemetry PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/telemetry_stub)
target_compile_definitions(test_banner_telemetry PRIVATE DATAZOO_BANNER_TELEMETRY)

add_test(NAME banner_telemetry COMMAND test_banner_telemetry)
