include(GoogleTest)

set(_conduit_test_sources
    test_flags.cpp
    test_glob.cpp
    test_metadata.cpp
    test_event.cpp
    test_envelope.cpp
    test_builder.cpp
    test_serialization_json.cpp
    test_serialization_cbor.cpp
    test_middleware.cpp
    test_listener.cpp
    test_subscriber.cpp
    test_bus_direct.cpp
    test_bus_queue.cpp
    test_bus_threadpool.cpp
    test_relay_transport.cpp
    test_scope_filtering.cpp
    test_filtered_transport.cpp
    test_event_registry.cpp
    test_event_type_registry.cpp
    test_priority.cpp
)

add_executable(conduit_tests ${_conduit_test_sources})

target_link_libraries(conduit_tests
    PRIVATE
        conduit::conduit
        GTest::gtest_main
)

conduit_set_warnings(conduit_tests PRIVATE)
conduit_enable_sanitizers(conduit_tests)
conduit_enable_coverage(conduit_tests)
conduit_enable_clang_tidy(conduit_tests)

gtest_discover_tests(conduit_tests)
