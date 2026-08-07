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

if(NOT MAIN_TEXT MATCHES "record_death")
    message(FATAL_ERROR "death bridge must record deaths through core")
endif()

if(MAIN_TEXT MATCHES "[Dd]eath[ \t_-]*[Tt]racker|DeathTracker|death_tracker")
    message(FATAL_ERROR "death bridge must not reference Death Tracker")
endif()

if(MAIN_TEXT MATCHES "player[ \t\n]*->[ \t\n]*[A-Za-z_]")
    message(FATAL_ERROR "death bridge must not read PlayerObject state")
endif()

if(MAIN_TEXT MATCHES "raw_x|percentage|recent_input|input")
    message(FATAL_ERROR "death bridge must not read raw X, percentage, or input telemetry")
endif()

if(MAIN_TEXT MATCHES "resetLevel")
    message(FATAL_ERROR "death bridge must not hook resetLevel")
endif()
