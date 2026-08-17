add_library(storage STATIC
    analysis_repository.cpp
    analysis_repository.h
    database_manager.cpp
    database_manager.h
    migration_runner.cpp
    migration_runner.h
    puzzle_attempt_repository.cpp
    puzzle_attempt_repository.h
)

target_include_directories(storage
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(storage
    PUBLIC
        Qt6::Core
        Qt6::Sql
        domain
        puzzle_runner
)
