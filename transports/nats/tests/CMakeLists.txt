include(GoogleTest)

add_executable(conduit_nats_tests test_nats_smoke.cpp)

target_link_libraries(conduit_nats_tests
    PRIVATE
        conduit::transport_nats
        GTest::gtest_main
)

conduit_set_warnings(conduit_nats_tests PRIVATE)
conduit_enable_sanitizers(conduit_nats_tests)
conduit_enable_clang_tidy(conduit_nats_tests)
conduit_enable_coverage(conduit_nats_tests)

gtest_discover_tests(conduit_nats_tests)
