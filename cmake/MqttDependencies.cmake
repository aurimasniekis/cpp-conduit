include_guard(GLOBAL)
include(FetchContent)

# Paho's bundled CMakeLists target a CMake older than CMake 4 supports. The
# policy-min injection below lets modern CMake configure the legacy script.
set(CMAKE_POLICY_VERSION_MINIMUM "3.5" CACHE STRING "" FORCE)

# Paho MQTT C++ builds its bundled C library when PAHO_WITH_MQTT_C is ON.
set(PAHO_BUILD_STATIC          ON  CACHE INTERNAL "")
set(PAHO_BUILD_SHARED          OFF CACHE INTERNAL "")
set(PAHO_BUILD_SAMPLES         OFF CACHE INTERNAL "")
set(PAHO_BUILD_DOCUMENTATION   OFF CACHE INTERNAL "")
set(PAHO_WITH_SSL              OFF CACHE INTERNAL "")
set(PAHO_HIGH_PERFORMANCE      ON  CACHE INTERNAL "")
set(PAHO_ENABLE_TESTING        OFF CACHE INTERNAL "")
set(PAHO_WITH_MQTT_C           ON  CACHE INTERNAL "")
set(PAHO_BUILD_DEB_PACKAGE     OFF CACHE INTERNAL "")

FetchContent_Declare(
    paho_mqtt_cpp
    GIT_REPOSITORY https://github.com/eclipse/paho.mqtt.cpp.git
    GIT_TAG        v1.4.0
    FIND_PACKAGE_ARGS NAMES PahoMqttCpp
)
FetchContent_MakeAvailable(paho_mqtt_cpp)

# Mark paho's include dirs as SYSTEM on each known target so consumers built
# with -Werror don't inherit -Wold-style-cast etc. from third-party headers.
foreach(_paho_target
    paho-mqttpp3-static paho-mqttpp3
    paho-mqtt3a-static paho-mqtt3a paho-mqtt3c-static paho-mqtt3c)
    if(TARGET ${_paho_target})
        get_target_property(_paho_inc ${_paho_target} INTERFACE_INCLUDE_DIRECTORIES)
        if(_paho_inc)
            set_target_properties(${_paho_target} PROPERTIES
                INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_paho_inc}")
        endif()
    endif()
endforeach()
