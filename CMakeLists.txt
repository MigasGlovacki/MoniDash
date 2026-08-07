cmake_minimum_required(VERSION 3.20)
project(monidash_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(monidash_core
    src/core/schema.cpp
    src/core/session_store.cpp
    src/core/monidash_core.cpp
)
target_include_directories(monidash_core PUBLIC src)
if(MSVC)
    set(MONIDASH_WARNINGS /W4 /permissive-)
else()
    set(MONIDASH_WARNINGS -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()
target_compile_options(monidash_core PRIVATE ${MONIDASH_WARNINGS})

add_executable(monidash_smoke src/cli/smoke.cpp)
target_link_libraries(monidash_smoke PRIVATE monidash_core)
target_compile_options(monidash_smoke PRIVATE ${MONIDASH_WARNINGS})

add_executable(monidash_tests tests/monidash_tests.cpp)
target_link_libraries(monidash_tests PRIVATE monidash_core)
target_compile_options(monidash_tests PRIVATE ${MONIDASH_WARNINGS})

enable_testing()
add_test(NAME monidash_tests COMMAND monidash_tests)

add_test(
    NAME monidash_packaging_contract
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/packaging_contract.cmake
)
add_test(
    NAME monidash_geode_bootstrap_contract
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/geode_bootstrap_contract.cmake
)
add_test(
    NAME monidash_windows_nominmax_contract
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/windows_nominmax_contract.cmake
)

option(MONIDASH_BUILD_GEODE_MOD "Build the Geode mod package" OFF)
set(MONIDASH_GEODE_SDK "" CACHE PATH "Path to the Geode SDK source tree")
set(GEODE_CLI "GEODE_CLI-NOTFOUND" CACHE FILEPATH "Path to the Geode CLI executable")

if(MONIDASH_BUILD_GEODE_MOD)
    if(NOT MONIDASH_GEODE_SDK)
        message(FATAL_ERROR "MONIDASH_GEODE_SDK must be set when MONIDASH_BUILD_GEODE_MOD is ON")
    endif()
    if(NOT IS_DIRECTORY "${MONIDASH_GEODE_SDK}")
        message(FATAL_ERROR "MONIDASH_GEODE_SDK is not a directory: ${MONIDASH_GEODE_SDK}")
    endif()
    if(NOT GEODE_CLI STREQUAL "GEODE_CLI-NOTFOUND" AND NOT EXISTS "${GEODE_CLI}")
        message(FATAL_ERROR "GEODE_CLI does not exist: ${GEODE_CLI}")
    endif()

    add_subdirectory("${MONIDASH_GEODE_SDK}" "${CMAKE_BINARY_DIR}/geode-sdk")
    add_library(monidash_geode_mod SHARED src/main.cpp)
    target_link_libraries(monidash_geode_mod monidash_core)
    target_compile_features(monidash_geode_mod PRIVATE cxx_std_23)
    target_compile_options(monidash_geode_mod PRIVATE ${MONIDASH_WARNINGS})
    setup_geode_mod(monidash_geode_mod)
endif()
