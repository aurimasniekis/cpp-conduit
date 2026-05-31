include_guard(GLOBAL)
include(FetchContent)

# conduit pulls in threadman (threading) and prom (metrics), which together drag
# in a deep transitive graph: spdlog, logman, dimval, prom, commons. conduit's
# own `cmake/` is first on CMAKE_MODULE_PATH and this file carries
# `include_guard(GLOBAL)`, so when threadman's / prom's top-level CMakeLists call
# `include(Dependencies)` it resolves back to *this* file and short-circuits.
# That means conduit must declare every transitive dependency itself, exactly as
# ../threadman/cmake/Dependencies.cmake and ../prom/cmake/Dependencies.cmake do.
# First FetchContent_Declare wins, so the ORDER below is load-bearing.

# 1. nlohmann/json — declared FIRST so commons / logman / dimval / parcel see
#    <nlohmann/json.hpp> on the include path and self-enable their JSON hooks
#    (COMMONS_WITH_NLOHMANN_JSON, etc. auto-detect via __has_include).
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install    OFF CACHE INTERNAL "")
FetchContent_Declare(
    nlohmann_json
    URL      https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.tar.gz
    URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    FIND_PACKAGE_ARGS 3.12.0 NAMES nlohmann_json
)
FetchContent_MakeAvailable(nlohmann_json)

# 2. ulid — must precede commons so COMMONS_WITH_ULID self-enables via
#    __has_include(<ulid/ulid.h>). Version unchanged; kept as a tag clone.
set(ULID_BUILD_TESTS      OFF CACHE INTERNAL "")
set(ULID_BUILD_BENCHMARKS OFF CACHE INTERNAL "")
set(ULID_BUILD_DOCS       OFF CACHE INTERNAL "")
FetchContent_Declare(
    ulid
    GIT_REPOSITORY https://github.com/aurimasniekis/cpp-ulid.git
    GIT_TAG        v1.0.0
    FIND_PACKAGE_ARGS 1.0.0 NAMES ulid
)
FetchContent_MakeAvailable(ulid)

# 3. commons — declared BEFORE metadata / parcel / logman / dimval / prom /
#    threadman so conduit's pinned 0.1.5 wins over their pinned 0.1.4 (their
#    `FIND_PACKAGE_ARGS 0.1` is satisfied by 0.1.5). ULID support is enabled by
#    `__has_include` now that ulid is on the include path above.
set(COMMONS_BUILD_TESTS    OFF CACHE INTERNAL "")
set(COMMONS_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(COMMONS_BUILD_DOCS     OFF CACHE INTERNAL "")
FetchContent_Declare(
    commons
    URL      https://github.com/aurimasniekis/cpp-commons/archive/refs/tags/v0.1.5.tar.gz
    URL_HASH SHA256=1c98ee66a8ac5bfd8b8580ddf775e5d34018ce21670c9e14f9cc2179901f521f
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    FIND_PACKAGE_ARGS 0.1.5 NAMES commons
)
FetchContent_MakeAvailable(commons)

# 4. spdlog — required transitively by logman; declared ahead of it. (Hash from
#    threadman's / prom's Dependencies.cmake.)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE INTERNAL "")
set(SPDLOG_BUILD_TESTS   OFF CACHE INTERNAL "")
set(SPDLOG_INSTALL       OFF CACHE INTERNAL "")
set(SPDLOG_FMT_EXTERNAL  OFF CACHE INTERNAL "")
FetchContent_Declare(
    spdlog
    URL      https://github.com/gabime/spdlog/archive/refs/tags/v1.17.0.tar.gz
    URL_HASH SHA256=d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    FIND_PACKAGE_ARGS 1.17.0
)
FetchContent_MakeAvailable(spdlog)

# 5. logman — central spdlog channel manager; required transitively by threadman
#    and prom. Declared before both.
set(LOGMAN_BUILD_TESTS    OFF CACHE INTERNAL "")
set(LOGMAN_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(LOGMAN_INSTALL        OFF CACHE INTERNAL "")
FetchContent_Declare(
    logman
    URL      https://github.com/aurimasniekis/cpp-logman/archive/refs/tags/v0.1.0.tar.gz
    URL_HASH SHA256=443dd2a0928d4bfc24281ad8e9035b302bf68f15e666ce4d6ffbf6ec25c761ed
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    FIND_PACKAGE_ARGS 0.1.0 NAMES logman
)
FetchContent_MakeAvailable(logman)

# 6. dimval — dimensional-value vocabulary required transitively by prom. Kept
#    lean: the metrics layer only matches it structurally, so no JSON / parcel.
set(DIMVAL_BUILD_TESTS        OFF CACHE INTERNAL "")
set(DIMVAL_BUILD_EXAMPLES     OFF CACHE INTERNAL "")
set(DIMVAL_INSTALL            OFF CACHE INTERNAL "")
set(DIMVAL_WITH_NLOHMANN_JSON OFF CACHE BOOL "" FORCE)
set(DIMVAL_WITH_PARCEL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    dimval
    URL      https://github.com/aurimasniekis/cpp-dimval/archive/refs/tags/v0.2.0.tar.gz
    URL_HASH SHA256=7ec1fa93abefc0d56d8ffbffadaecc06f9e2705e7b6aee57befa9c87f73149c1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    FIND_PACKAGE_ARGS 0.2 NAMES dimval
)
FetchContent_MakeAvailable(dimval)

# 7. prom — client-independent Prometheus/OpenMetrics abstraction. Always
#    required (metrics are always-on). Declared before threadman, which depends
#    on it. No prometheus-cpp backend by default — NullAdapter until a consumer
#    installs one.
set(PROM_BUILD_TESTS         OFF CACHE INTERNAL "")
set(PROM_BUILD_EXAMPLES      OFF CACHE INTERNAL "")
set(PROM_INSTALL             OFF CACHE INTERNAL "")
set(PROM_WITH_PROMETHEUS_CPP OFF CACHE INTERNAL "")
FetchContent_Declare(
    prom
    URL      https://github.com/aurimasniekis/cpp-prom/archive/refs/tags/v0.1.0.tar.gz
    URL_HASH SHA256=7a2cb15120646c6cb004df38911ef275a2c90f68b0ad2484ed434c3c8478a0e7
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    FIND_PACKAGE_ARGS 0.1.0 NAMES prom
)
FetchContent_MakeAvailable(prom)

# 8. metadata — envelope metadata vocabulary. Version unchanged; kept as a tag
#    clone. Declared after commons so it picks up conduit's 0.1.5.
set(METADATA_BUILD_TESTS    OFF CACHE INTERNAL "")
set(METADATA_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(METADATA_BUILD_DOCS     OFF CACHE INTERNAL "")
FetchContent_Declare(
    metadata
    GIT_REPOSITORY https://github.com/aurimasniekis/cpp-metadata.git
    GIT_TAG        v0.2.0
    FIND_PACKAGE_ARGS 0.2.0 NAMES metadata
)
FetchContent_MakeAvailable(metadata)

# 9. parcel — cell registry / serialization. 0.2.2 pre-registers a ULID cell in
#    its ParcelRegistry builtins, so conduit no longer ships its own UlidCell.
set(PARCEL_BUILD_TESTS    OFF CACHE INTERNAL "")
set(PARCEL_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(PARCEL_BUILD_DOCS     OFF CACHE INTERNAL "")
FetchContent_Declare(
    parcel
    URL      https://github.com/aurimasniekis/cpp-parcel/archive/refs/tags/v0.2.2.tar.gz
    URL_HASH SHA256=7e4277f9f57b5bbea815f029c086e194d6ceedb6fa370a358ef0c9e22e1d30e5
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    FIND_PACKAGE_ARGS 0.2.2 NAMES parcel
)
FetchContent_MakeAvailable(parcel)

# 10. threadman — threading (local executor pool + transport background threads).
#     Declared LAST; every transitive dependency it needs is already declared
#     above, so its own `include(Dependencies)` short-circuits cleanly. JSON and
#     parcel hooks are left at their defaults (both are present on the include
#     path).
set(THREADMAN_BUILD_TESTS    OFF CACHE INTERNAL "")
set(THREADMAN_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(THREADMAN_INSTALL        OFF CACHE INTERNAL "")
FetchContent_Declare(
    threadman
    URL      https://github.com/aurimasniekis/cpp-threadman/archive/refs/tags/v0.1.0.tar.gz
    URL_HASH SHA256=194e96ba5dcbd328986f9e321c14e9d6364f9a47542a68d559a94f1ffeb62aad
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    FIND_PACKAGE_ARGS 0.1.0 NAMES threadman
)
FetchContent_MakeAvailable(threadman)

# 11. googletest — tests only.
if(CONDUIT_BUILD_TESTS)
    set(INSTALL_GTEST OFF CACHE INTERNAL "")
    set(BUILD_GMOCK   OFF CACHE INTERNAL "")
    FetchContent_Declare(
        googletest
        URL      https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
        URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        FIND_PACKAGE_ARGS NAMES GTest
    )
    FetchContent_MakeAvailable(googletest)
endif()
