cmake_minimum_required(VERSION 3.21)

project(MoniDash VERSION 0.2.0)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# Keeps MSVC and CMake 4 compatible with Geode's transitive dependencies.
if (MSVC AND NOT CMAKE_CXX_FLAGS)
    set(CMAKE_CXX_FLAGS "/utf-8" CACHE STRING "MSVC compiler flags" FORCE)
endif()

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "GEODE_SDK is not defined. Install the Geode SDK and run geode sdk install.")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
setup_geode_mod(${PROJECT_NAME})
