add_library(strict_json STATIC
    strict_json.cpp
    strict_json.h
)

target_include_directories(strict_json
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(strict_json
    PUBLIC
        Qt6::Core
)
