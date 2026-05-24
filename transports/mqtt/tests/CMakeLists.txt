include(GoogleTest)

add_executable(conduit_mqtt_tests test_mqtt_smoke.cpp)

target_link_libraries(conduit_mqtt_tests
    PRIVATE
        conduit::transport_mqtt
        GTest::gtest_main
)

conduit_set_warnings(conduit_mqtt_tests PRIVATE)
conduit_enable_sanitizers(conduit_mqtt_tests)
conduit_enable_clang_tidy(conduit_mqtt_tests)
conduit_enable_coverage(conduit_mqtt_tests)

gtest_discover_tests(conduit_mqtt_tests)
