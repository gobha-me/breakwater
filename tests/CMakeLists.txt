include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.0
    GIT_SHALLOW TRUE
    SYSTEM
)
FetchContent_MakeAvailable(Catch2)

add_executable(breakwater_tests test_rng.cpp test_board.cpp test_mission.cpp test_intents.cpp test_turn.cpp test_rammer.cpp test_outcome.cpp test_log.cpp test_soak.cpp test_solver.cpp)
target_link_libraries(breakwater_tests PRIVATE breakwater::core breakwater_warnings Catch2::Catch2WithMain)
target_compile_definitions(breakwater_tests PRIVATE BREAKWATER_LOG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/logs")

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(breakwater_tests)

# bw_replay, end to end: a good log replays, a refused command and a malformed
# log each fail, with the reason on stderr.
if(BREAKWATER_BUILD_TOOLS)
    set(_logs ${CMAKE_CURRENT_SOURCE_DIR}/logs)
    add_test(NAME bw_replay.win COMMAND bw_replay ${_logs}/win-seed20.log)
    set_tests_properties(bw_replay.win PROPERTIES PASS_REGULAR_EXPRESSION "outcome won")
    foreach(_case refused malformed)
        add_test(NAME bw_replay.${_case}.fails COMMAND bw_replay ${_logs}/invalid/${_case}.log)
        set_tests_properties(bw_replay.${_case}.fails PROPERTIES WILL_FAIL TRUE)
    endforeach()
    add_test(NAME bw_replay.refused.reason COMMAND bw_replay ${_logs}/invalid/refused.log)
    set_tests_properties(bw_replay.refused.reason PROPERTIES PASS_REGULAR_EXPRESSION
                                                             "command 1 refused: destination is out of reach")
    add_test(NAME bw_replay.malformed.reason COMMAND bw_replay ${_logs}/invalid/malformed.log)
    set_tests_properties(bw_replay.malformed.reason PROPERTIES PASS_REGULAR_EXPRESSION
                                                               "malformed.log:4: 'shove' direction must be N, E, S or W")
endif()
