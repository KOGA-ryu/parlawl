add_library(lichess STATIC
    lichess_client.cpp
    lichess_client.h
    puzzle_api_requests.cpp
    puzzle_api_requests.h
    puzzle_api_types.cpp
    puzzle_api_types.h
    puzzle_supply_coordinator.cpp
    puzzle_supply_coordinator.h
    puzzle_supply_cache.cpp
    puzzle_supply_cache.h
    source_game_pgn_cache.cpp
    source_game_pgn_cache.h
)

target_include_directories(lichess
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(lichess
    PUBLIC
        Qt6::Core
        Qt6::Network
        domain
        puzzle_runner
)
