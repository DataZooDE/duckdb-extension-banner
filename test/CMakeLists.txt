add_executable(test_banner test_banner.cpp)
target_link_libraries(test_banner PRIVATE datazoo_banner)

# The gating tests assert on "no output when stderr is not a tty", which is
# exactly the situation ctest runs them in.
add_test(NAME banner_gating COMMAND test_banner)
