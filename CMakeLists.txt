cmake_minimum_required(VERSION 3.24)

project(parlawl VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(PARLAWL_BUILD_TESTS "Build parlawl test targets" OFF)

# The market workspace draws OHLCV with the Qt Charts module
# (QCandlestickSeries plus a volume bar series). It is required, not optional:
# a chart drawn by hand in QPainter would be a second renderer to keep honest.
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Sql Network Test Charts)

qt_standard_project_setup()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/parlawl_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/parlawl_config.h
    @ONLY
)

add_subdirectory(libs/domain)
add_subdirectory(libs/strict_json)
add_subdirectory(libs/market)
add_subdirectory(libs/lichess)
add_subdirectory(libs/storage)
add_subdirectory(libs/reporting)
add_subdirectory(libs/orchestration)
add_subdirectory(libs/puzzle_runner)
add_subdirectory(apps/desktop)

if(PARLAWL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
