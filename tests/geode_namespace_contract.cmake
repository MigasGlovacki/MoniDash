set(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
file(READ "${REPOSITORY_ROOT}/src/main.cpp" MAIN_TEXT)

string(FIND "${MAIN_TEXT}" "#include <Geode/Geode.hpp>" GEODE_INCLUDE)
string(FIND "${MAIN_TEXT}" "using namespace geode::prelude;" NAMESPACE_IMPORT)
string(FIND "${MAIN_TEXT}" "$on_mod(Loaded)" LOADED_HOOK)

if(GEODE_INCLUDE EQUAL -1 OR NAMESPACE_IMPORT EQUAL -1 OR LOADED_HOOK EQUAL -1)
    message(FATAL_ERROR "src/main.cpp must contain the Geode include, prelude import, and Loaded hook")
endif()
if(NAMESPACE_IMPORT LESS GEODE_INCLUDE OR NAMESPACE_IMPORT GREATER LOADED_HOOK)
    message(FATAL_ERROR "using namespace geode::prelude; must follow the Geode include and precede $on_mod(Loaded)")
endif()

message(STATUS "MoniDash Geode namespace contract passed")
