add_library(market STATIC
    market_continuation.h
    market_grader.cpp
    market_grader.h
    market_hud.cpp
    market_hud.h
    market_puzzle_pack.cpp
    market_puzzle_pack.h
    market_rating.cpp
    market_rating.h
    market_reveal.h
    market_score_card.h
    market_scoring.cpp
    market_scoring.h
    market_session_controller.cpp
    market_session_controller.h
    market_types.cpp
    market_types.h
    reveal_ticket.h
    sealed_continuation_vault.cpp
    sealed_continuation_vault.h
    sealed_continuation_vault_p.h
)

target_include_directories(market
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(market
    PUBLIC
        Qt6::Core
        domain
        strict_json
)
