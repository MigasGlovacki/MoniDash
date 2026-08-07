set(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
file(READ "${REPOSITORY_ROOT}/src/core/session_store.cpp" SESSION_STORE_TEXT)

if(NOT SESSION_STORE_TEXT MATCHES "#ifdef _WIN32[\r\n \t]+#ifndef NOMINMAX[\r\n \t]+#define NOMINMAX[\r\n \t]+#endif[\r\n \t]+#include[ \t]+<windows\\.h>")
    message(FATAL_ERROR "session_store.cpp must define NOMINMAX before including windows.h under _WIN32")
endif()

message(STATUS "MoniDash Windows NOMINMAX contract passed")
