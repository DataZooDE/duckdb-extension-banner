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

# DATAZOO_BANNER_TELEMETRY=ON must only take effect when the consumer actually
# builds posthog_telemetry: extensions switch the flag on unconditionally but
# add the telemetry target only when OpenSSL is found, so a build without it
# used to fail to compile or link (anofox-scenario#33). Each case configures and
# builds a small consumer project.
foreach(with_target OFF ON)
    set(gate_dir ${CMAKE_CURRENT_BINARY_DIR}/telemetry_gate_${with_target})
    add_test(NAME telemetry_gate_${with_target}
        COMMAND ${CMAKE_CTEST_COMMAND}
            --build-and-test ${CMAKE_CURRENT_SOURCE_DIR}/telemetry_gate ${gate_dir}
            --build-generator ${CMAKE_GENERATOR}
            --build-config $<CONFIG>
            --build-options -DBANNER_SOURCE_DIR=${PROJECT_SOURCE_DIR} -DWITH_TELEMETRY_TARGET=${with_target})
endforeach()
