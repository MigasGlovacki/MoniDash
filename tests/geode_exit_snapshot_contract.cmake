set(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
file(READ "${REPOSITORY_ROOT}/src/main.cpp" MAIN_TEXT)

foreach(REQUIRED
    "#include <Geode/loader/GameEvent.hpp>"
    "#include <Geode/loader/Loader.hpp>"
    "\\$on_game\\(Exiting\\)"
    "getLoadedMod"
    "elohmrow.death_tracker"
    "-local"
    "-gauntlet"
    "metadata"
    "general.dt"
    "std::filesystem::copy_file"
    "session_path()"
    "death_tracker_snapshot"
    "core->shutdown()"
    "core.reset()"
    "store.reset()"
)
    if(NOT MAIN_TEXT MATCHES "${REQUIRED}")
        message(FATAL_ERROR "exit snapshot contract is missing: ${REQUIRED}")
    endif()
endforeach()

if(NOT MAIN_TEXT MATCHES "exiting_handled" OR NOT MAIN_TEXT MATCHES "exiting_handled[ ]*=[ ]*false")
    message(FATAL_ERROR "exit hook must have a once-only guard")
endif()
if(NOT MAIN_TEXT MATCHES "exiting_handled[ ]*=[ ]*true")
    message(FATAL_ERROR "exit hook must mark its once-only guard")
endif()
string(FIND "${MAIN_TEXT}" "$on_game(Exiting)" EXIT_HOOK_POSITION)
string(SUBSTRING "${MAIN_TEXT}" ${EXIT_HOOK_POSITION} -1 EXIT_TEXT)
string(FIND "${EXIT_TEXT}" "if (exiting_handled)" EXIT_GUARD_POSITION)
string(FIND "${EXIT_TEXT}" "return;" EXIT_RETURN_POSITION)
if(EXIT_GUARD_POSITION LESS 0 OR EXIT_RETURN_POSITION LESS 0 OR EXIT_RETURN_POSITION LESS EXIT_GUARD_POSITION)
    message(FATAL_ERROR "exit hook must return after the first invocation")
endif()

string(FIND "${MAIN_TEXT}" "snapshot" SNAPSHOT_POSITION)
string(FIND "${MAIN_TEXT}" "core->shutdown()" SHUTDOWN_POSITION)
if(SNAPSHOT_POSITION LESS 0 OR SHUTDOWN_POSITION LESS 0 OR SNAPSHOT_POSITION GREATER SHUTDOWN_POSITION)
    message(FATAL_ERROR "exit snapshot must happen before core finalization")
endif()

if(NOT MAIN_TEXT MATCHES "is_directory" OR NOT MAIN_TEXT MATCHES "directory_count")
    message(FATAL_ERROR "exit snapshot must require exactly one existing candidate directory")
endif()

string(FIND "${MAIN_TEXT}" "void snapshot_death_tracker" SNAPSHOT_FUNCTION_POSITION)
if(SNAPSHOT_FUNCTION_POSITION LESS 0)
    message(FATAL_ERROR "exit snapshot helper is missing")
endif()
string(SUBSTRING "${MAIN_TEXT}" ${SNAPSHOT_FUNCTION_POSITION} -1 SNAPSHOT_TEXT)
if(NOT SNAPSHOT_TEXT MATCHES "active_level_id")
    message(FATAL_ERROR "exit snapshot must use the verified active level ID")
endif()
if(NOT SNAPSHOT_TEXT MATCHES "!active_level_id")
    message(FATAL_ERROR "exit snapshot must omit safely when no active level exists")
endif()
string(FIND "${SNAPSHOT_TEXT}" "getSaveDir() / \"levels\"" LEVELS_ROOT_POSITION)
if(LEVELS_ROOT_POSITION LESS 0)
    message(FATAL_ERROR "exit snapshot must use the Death Tracker levels root")
endif()
if(SNAPSHOT_TEXT MATCHES "keys[^\n]*mod_id")
    message(FATAL_ERROR "exit snapshot candidate keys must not be based on mod_id")
endif()
if(NOT SNAPSHOT_TEXT MATCHES "active_level_id[^\n]*-local" OR NOT SNAPSHOT_TEXT MATCHES "active_level_id[^\n]*-gauntlet")
    message(FATAL_ERROR "exit snapshot candidate keys must use the active level ID")
endif()
if(MAIN_TEXT MATCHES "death_tracker[^\n]*(remove|rename|ofstream|create_directories)" OR MAIN_TEXT MATCHES "death_tracker[^\n]*copy_options")
    message(FATAL_ERROR "exit snapshot must not mutate Death Tracker data")
endif()
if(MAIN_TEXT MATCHES "(sessions|backups|saved|settings)[^\n]*(remove|rename|ofstream|create_directories)")
    message(FATAL_ERROR "exit snapshot must not mutate unrelated data")
endif()

message(STATUS "MoniDash Geode exit snapshot contract passed")
