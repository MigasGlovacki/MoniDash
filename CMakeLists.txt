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
target_compile_options(monidash_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

add_executable(monidash_smoke src/cli/smoke.cpp)
target_link_libraries(monidash_smoke PRIVATE monidash_core)
target_compile_options(monidash_smoke PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

add_executable(monidash_tests tests/monidash_tests.cpp)
target_link_libraries(monidash_tests PRIVATE monidash_core)
target_compile_options(monidash_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

enable_testing()
add_test(NAME monidash_tests COMMAND monidash_tests)
