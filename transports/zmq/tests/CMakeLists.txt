include(GoogleTest)

add_executable(conduit_zmq_tests test_zmq_smoke.cpp)

target_link_libraries(conduit_zmq_tests
    PRIVATE
        conduit::transport_zmq
        GTest::gtest_main
)

conduit_set_warnings(conduit_zmq_tests PRIVATE)
conduit_enable_sanitizers(conduit_zmq_tests)
conduit_enable_clang_tidy(conduit_zmq_tests)
conduit_enable_coverage(conduit_zmq_tests)

gtest_discover_tests(conduit_zmq_tests)
