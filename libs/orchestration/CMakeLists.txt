add_library(orchestration STATIC
    analysis_orchestrator.cpp
    analysis_orchestrator.h
    worker_protocol.h
)

target_include_directories(orchestration
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(orchestration
    PUBLIC
        Qt6::Core
        Qt6::Sql
        lichess
        reporting
        storage
)
