include(GoogleTest)

add_executable(conduit_amqp_tests test_amqp_smoke.cpp)

target_link_libraries(conduit_amqp_tests
    PRIVATE
        conduit::transport_amqp
        GTest::gtest_main
)

conduit_set_warnings(conduit_amqp_tests PRIVATE)
conduit_enable_sanitizers(conduit_amqp_tests)
conduit_enable_clang_tidy(conduit_amqp_tests)
conduit_enable_coverage(conduit_amqp_tests)

gtest_discover_tests(conduit_amqp_tests)
