include(GoogleTest)

add_executable(conduit_redis_tests test_redis_smoke.cpp)

target_link_libraries(conduit_redis_tests
    PRIVATE
        conduit::transport_redis
        GTest::gtest_main
)

conduit_set_warnings(conduit_redis_tests PRIVATE)
conduit_enable_sanitizers(conduit_redis_tests)
conduit_enable_clang_tidy(conduit_redis_tests)
conduit_enable_coverage(conduit_redis_tests)

gtest_discover_tests(conduit_redis_tests)
