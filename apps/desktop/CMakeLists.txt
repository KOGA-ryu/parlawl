qt_add_executable(parlawl_desktop MACOSX_BUNDLE
    board_widget.cpp
    board_widget.h
    evaluation_bar_widget.cpp
    evaluation_bar_widget.h
    main.cpp
    puzzle_panels.cpp
    puzzle_panels.h
    puzzle_runner_window.cpp
    puzzle_runner_window.h
    resources.qrc
    stockfish_review_controller.cpp
    stockfish_review_controller.h
)

target_include_directories(parlawl_desktop
    PRIVATE
        ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(parlawl_desktop
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        domain
        lichess
        orchestration
        puzzle_runner
        reporting
        storage
)

set_target_properties(parlawl_desktop PROPERTIES
    OUTPUT_NAME parlawl
)
