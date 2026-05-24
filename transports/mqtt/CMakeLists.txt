# conduit::transport_mqtt — opt-in MQTT transport adapter.
# Only included from transports/CMakeLists.txt when CONDUIT_TRANSPORT_MQTT=ON.

include("${CMAKE_SOURCE_DIR}/cmake/MqttDependencies.cmake")

add_library(conduit_transport_mqtt STATIC
    src/transport.cpp
)
add_library(conduit::transport_mqtt ALIAS conduit_transport_mqtt)

target_compile_features(conduit_transport_mqtt PUBLIC cxx_std_23)
target_include_directories(conduit_transport_mqtt
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# paho.mqtt.cpp exposes the static target as `paho-mqttpp3-static` when used
# via FetchContent and as `PahoMqttCpp::paho-mqttpp3-static` when installed.
if(TARGET PahoMqttCpp::paho-mqttpp3-static)
    set(_conduit_mqtt_lib PahoMqttCpp::paho-mqttpp3-static)
elseif(TARGET paho-mqttpp3-static)
    set(_conduit_mqtt_lib paho-mqttpp3-static)
elseif(TARGET PahoMqttCpp::paho-mqttpp3)
    set(_conduit_mqtt_lib PahoMqttCpp::paho-mqttpp3)
elseif(TARGET paho-mqttpp3)
    set(_conduit_mqtt_lib paho-mqttpp3)
else()
    message(FATAL_ERROR "conduit: no Paho MQTT C++ target found")
endif()

target_link_libraries(conduit_transport_mqtt
    PUBLIC  conduit::conduit
    PRIVATE ${_conduit_mqtt_lib}
)

conduit_set_warnings(conduit_transport_mqtt PRIVATE)
conduit_enable_sanitizers(conduit_transport_mqtt)
# Intentionally NOT running clang-tidy on the library TU: paho's
# `mqtt/ssl_options.h` declares `std::basic_string<unsigned char>`, which
# modern libc++ (>= 17) rejects because `std::char_traits<unsigned char>`
# was removed. Our regular compile uses AppleClang's older libc++ so it
# still builds — but Homebrew clang-tidy (used in CI's macos-latest job
# and locally) parses with libc++ 22 and hard-errors on paho's header.
# Public consumers (tests/examples) only see our pimpl'd header and tidy
# fine; only `transport.cpp` (this lib) is excluded.
conduit_enable_coverage(conduit_transport_mqtt)

if(CONDUIT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(CONDUIT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(CONDUIT_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS conduit_transport_mqtt
        EXPORT  conduit-transport-mqttTargets
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(DIRECTORY include/conduit/mqtt
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/conduit
        FILES_MATCHING PATTERN "*.hpp"
    )

    install(EXPORT conduit-transport-mqttTargets
        FILE        conduit-transport-mqttTargets.cmake
        NAMESPACE   conduit::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/conduit-transport-mqtt
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/conduit-transport-mqttConfigVersion.cmake"
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
        ARCH_INDEPENDENT
    )

    configure_package_config_file(
        "${CMAKE_SOURCE_DIR}/cmake/conduit-transport-mqttConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/conduit-transport-mqttConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/conduit-transport-mqtt
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/conduit-transport-mqttConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/conduit-transport-mqttConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/conduit-transport-mqtt
    )
endif()
