add_library(reporting STATIC
    opening_resolution.cpp
    opening_resolution.h
    puzzle_info_summary_builder.cpp
    puzzle_info_summary_builder.h
    report_formatter.cpp
    report_formatter.h
)

target_include_directories(reporting
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(reporting
    PUBLIC
        Qt6::Core
        domain
        puzzle_runner
)
