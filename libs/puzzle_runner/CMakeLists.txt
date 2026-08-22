add_library(puzzle_runner STATIC
    annotated_replay_pack.cpp
    annotated_replay_pack.h
    chess_position.cpp
    chess_position.h
    engine_validated_puzzle_pack.cpp
    engine_validated_puzzle_pack.h
    fixture_puzzle_source.cpp
    fixture_puzzle_source.h
    game_state_store.cpp
    game_state_store.h
    pgn_utils.cpp
    pgn_utils.h
    puzzle_engine.cpp
    puzzle_engine.h
    puzzle_source.h
    puzzle_types.h
    review_engine_adapter.h
    replay_session.cpp
    replay_session.h
    session_controller.cpp
    session_controller.h
)

target_include_directories(puzzle_runner
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(puzzle_runner
    PUBLIC
        Qt6::Core
        domain
        strict_json
)
