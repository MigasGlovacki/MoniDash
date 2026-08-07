set(MAIN_TEXT "")
file(READ "${CMAKE_CURRENT_LIST_DIR}/../src/main.cpp" MAIN_TEXT)

if(NOT MAIN_TEXT MATCHES "class[ \t\n]+\\$modify\\(PlayLayer\\)[ \t\n]*\\{")
    message(FATAL_ERROR "death bridge must use the class PlayLayer modification declaration")
endif()

if(NOT MAIN_TEXT MATCHES "void[ \t\n]+destroyPlayer[ \t\n]*\\([ \t\n]*PlayerObject[ \t\\n]*\\*[ \t\n]*player[ \t\n]*,[ \t\n]*GameObject[ \t\n]*\\*[ \t\n]*object[ \t\n]*\\)")
    message(FATAL_ERROR "death bridge must use the exact destroyPlayer signature")
endif()

if(NOT MAIN_TEXT MATCHES "PlayLayer::destroyPlayer[ \t\n]*\\([ \t\n]*player[ \t\n]*,[ \t\n]*object[ \t\n]*\\)")
    message(FATAL_ERROR "death bridge must call the original destroyPlayer exactly")
endif()

string(REGEX MATCHALL "PlayLayer::destroyPlayer[ \t\n]*\\([ \t\n]*player[ \t\n]*,[ \t\n]*object[ \t\n]*\\)" ORIGINAL_CALLS "${MAIN_TEXT}")
list(LENGTH ORIGINAL_CALLS ORIGINAL_CALL_COUNT)
if(NOT ORIGINAL_CALL_COUNT EQUAL 1)
    message(FATAL_ERROR "death bridge must route repeat callbacks through one original destroyPlayer call")
endif()

if(NOT MAIN_TEXT MATCHES "const[ \t\n]+auto[ \t\n]+player_was_alive[ \t\n]*=[ \t\n]*player[ \t\n]*\\?[ \t\n]*![ \t\n]*player->m_isDead[ \t\n]*:[ \t\n]*false[ \t\n]*;")
    message(FATAL_ERROR "death bridge must capture the player's pre-call alive state")
endif()

string(FIND "${MAIN_TEXT}" "void destroyPlayer" DESTROY_START)
string(SUBSTRING "${MAIN_TEXT}" ${DESTROY_START} -1 DESTROY_TEXT)
if(DESTROY_TEXT MATCHES "\\b(sleep|debounce|rate|threshold|milliseconds|chrono)\\b")
    message(FATAL_ERROR "death bridge must not use timing, debounce, or rate-limit logic")
endif()

string(FIND "${MAIN_TEXT}" "PlayLayer::destroyPlayer(player, object);" ORIGINAL_CALL_POSITION)
string(FIND "${MAIN_TEXT}" "if (!core || !active_level_id || !player || !player_was_alive)" GUARD_POSITION)
string(FIND "${MAIN_TEXT}" "core->record_death(death);" RECORD_POSITION)
if(ORIGINAL_CALL_POSITION LESS 0 OR GUARD_POSITION LESS 0 OR RECORD_POSITION LESS 0 OR GUARD_POSITION LESS ORIGINAL_CALL_POSITION OR RECORD_POSITION LESS GUARD_POSITION)
    message(FATAL_ERROR "death must be recorded after the original call with all required guards")
endif()

if(NOT MAIN_TEXT MATCHES "record_death")
    message(FATAL_ERROR "death bridge must record deaths through core")
endif()

if(MAIN_TEXT MATCHES "[Dd]eath[ \t_-]*[Tt]racker|DeathTracker|death_tracker")
    message(FATAL_ERROR "death bridge must not reference Death Tracker")
endif()

string(REPLACE "player->m_isDead" "" PLAYER_READ_TEXT "${MAIN_TEXT}")
if(PLAYER_READ_TEXT MATCHES "player[ \t\n]*->[ \t\n]*[A-Za-z_]")
    message(FATAL_ERROR "death bridge must not read PlayerObject state")
endif()

if(MAIN_TEXT MATCHES "raw_x|percentage|recent_input|input")
    message(FATAL_ERROR "death bridge must not read raw X, percentage, or input telemetry")
endif()

if(MAIN_TEXT MATCHES "resetLevel")
    message(FATAL_ERROR "death bridge must not hook resetLevel")
endif()
