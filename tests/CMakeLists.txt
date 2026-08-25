add_executable(test_unit_storage
    unit/test_unit_storage.cpp
    ../apps/desktop/resources.qrc
)
target_include_directories(test_unit_storage PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_unit_storage PRIVATE Qt6::Core Qt6::Sql Qt6::Test storage)
add_test(NAME test_unit_storage COMMAND test_unit_storage)

add_executable(test_unit_puzzle_attempt_repository
    unit/test_unit_puzzle_attempt_repository.cpp
    ../apps/desktop/resources.qrc
)
target_compile_definitions(test_unit_puzzle_attempt_repository PRIVATE PARLAWL_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(test_unit_puzzle_attempt_repository PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_unit_puzzle_attempt_repository PRIVATE Qt6::Core Qt6::Sql Qt6::Test storage)
add_test(NAME test_unit_puzzle_attempt_repository COMMAND test_unit_puzzle_attempt_repository)

add_executable(test_unit_analysis_row_mappers
    unit/test_unit_analysis_row_mappers.cpp
    ../apps/desktop/resources.qrc
)
target_include_directories(test_unit_analysis_row_mappers PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_unit_analysis_row_mappers PRIVATE Qt6::Core Qt6::Sql Qt6::Test orchestration storage)
add_test(NAME test_unit_analysis_row_mappers COMMAND test_unit_analysis_row_mappers)

add_executable(test_unit_analysis_insert_specs
    unit/test_unit_analysis_insert_specs.cpp
    ../apps/desktop/resources.qrc
)
target_include_directories(test_unit_analysis_insert_specs PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_unit_analysis_insert_specs PRIVATE Qt6::Core Qt6::Sql Qt6::Test orchestration storage)
add_test(NAME test_unit_analysis_insert_specs COMMAND test_unit_analysis_insert_specs)

add_executable(test_unit_worker_protocol
    unit/test_unit_worker_protocol.cpp
)
target_include_directories(test_unit_worker_protocol PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_unit_worker_protocol PRIVATE Qt6::Core Qt6::Test orchestration)
add_test(NAME test_unit_worker_protocol COMMAND test_unit_worker_protocol)

add_executable(test_unit_report_formatter
    unit/test_unit_report_formatter.cpp
)
target_include_directories(test_unit_report_formatter PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_unit_report_formatter PRIVATE Qt6::Core Qt6::Test reporting)
add_test(NAME test_unit_report_formatter COMMAND test_unit_report_formatter)

add_executable(test_unit_puzzle_info_summary_builder
    unit/test_unit_puzzle_info_summary_builder.cpp
)
target_include_directories(test_unit_puzzle_info_summary_builder PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_unit_puzzle_info_summary_builder PRIVATE Qt6::Core Qt6::Test reporting)
add_test(NAME test_unit_puzzle_info_summary_builder COMMAND test_unit_puzzle_info_summary_builder)

add_executable(test_unit_puzzle_runner
    unit/test_unit_puzzle_runner.cpp
    ../apps/desktop/resources.qrc
)
target_compile_definitions(test_unit_puzzle_runner PRIVATE PARLAWL_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(test_unit_puzzle_runner PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_unit_puzzle_runner PRIVATE Qt6::Core Qt6::Test puzzle_runner)
add_test(NAME test_unit_puzzle_runner COMMAND test_unit_puzzle_runner)

add_executable(test_unit_annotated_replay_pack
    unit/test_unit_annotated_replay_pack.cpp
)
target_include_directories(test_unit_annotated_replay_pack PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_unit_annotated_replay_pack PRIVATE Qt6::Core Qt6::Test puzzle_runner)
add_test(NAME test_unit_annotated_replay_pack COMMAND test_unit_annotated_replay_pack)

add_executable(test_unit_engine_validated_puzzle_pack
    unit/test_unit_engine_validated_puzzle_pack.cpp
)
target_compile_definitions(
    test_unit_engine_validated_puzzle_pack
    PRIVATE PARLAWL_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_include_directories(test_unit_engine_validated_puzzle_pack PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_unit_engine_validated_puzzle_pack PRIVATE Qt6::Core Qt6::Test puzzle_runner)
add_test(NAME test_unit_engine_validated_puzzle_pack COMMAND test_unit_engine_validated_puzzle_pack)

add_executable(test_unit_replay_session
    unit/test_unit_replay_session.cpp
    unit/test_unit_annotated_replay_pack.cpp
)
target_compile_definitions(test_unit_replay_session PRIVATE PARLAWL_REPLAY_FIXTURE_PROVIDER_ONLY)
target_include_directories(test_unit_replay_session PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_unit_replay_session PRIVATE Qt6::Core Qt6::Test puzzle_runner)
add_test(NAME test_unit_replay_session COMMAND test_unit_replay_session)

add_executable(test_unit_puzzle_panels
    unit/test_unit_puzzle_panels.cpp
    unit/test_unit_annotated_replay_pack.cpp
    ../apps/desktop/puzzle_panels.cpp
)
target_compile_definitions(test_unit_puzzle_panels PRIVATE PARLAWL_REPLAY_FIXTURE_PROVIDER_ONLY)
target_include_directories(test_unit_puzzle_panels PRIVATE ${CMAKE_BINARY_DIR}/generated ../apps/desktop)
target_link_libraries(test_unit_puzzle_panels PRIVATE Qt6::Core Qt6::Widgets Qt6::Test puzzle_runner reporting)
add_test(NAME test_unit_puzzle_panels COMMAND test_unit_puzzle_panels)

add_executable(test_unit_player_statistics_panel
    unit/test_unit_player_statistics_panel.cpp
    ../apps/desktop/player_analysis_catalog.cpp
    ../apps/desktop/player_statistics_panel.cpp
)
target_include_directories(test_unit_player_statistics_panel PRIVATE ../apps/desktop)
target_link_libraries(test_unit_player_statistics_panel PRIVATE Qt6::Core Qt6::Sql Qt6::Widgets Qt6::Test)
add_test(NAME test_unit_player_statistics_panel COMMAND test_unit_player_statistics_panel)

add_executable(test_unit_player_analysis_catalog
    unit/test_unit_player_analysis_catalog.cpp
    ../apps/desktop/player_analysis_catalog.cpp
)
target_include_directories(test_unit_player_analysis_catalog PRIVATE ../apps/desktop)
target_link_libraries(test_unit_player_analysis_catalog PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
add_test(NAME test_unit_player_analysis_catalog COMMAND test_unit_player_analysis_catalog)

add_executable(test_unit_puzzle_supply_coordinator
    unit/test_unit_puzzle_supply_coordinator.cpp
)
target_include_directories(test_unit_puzzle_supply_coordinator PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_unit_puzzle_supply_coordinator PRIVATE Qt6::Core Qt6::Test lichess)
add_test(NAME test_unit_puzzle_supply_coordinator COMMAND test_unit_puzzle_supply_coordinator)

add_executable(test_unit_puzzle_api_requests
    unit/test_unit_puzzle_api_requests.cpp
)
target_include_directories(test_unit_puzzle_api_requests PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_unit_puzzle_api_requests PRIVATE Qt6::Core Qt6::Test lichess)
add_test(NAME test_unit_puzzle_api_requests COMMAND test_unit_puzzle_api_requests)

add_executable(test_unit_pgn_utils
    unit/test_unit_pgn_utils.cpp
)
target_include_directories(test_unit_pgn_utils PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_unit_pgn_utils PRIVATE Qt6::Core Qt6::Test puzzle_runner)
add_test(NAME test_unit_pgn_utils COMMAND test_unit_pgn_utils)

add_executable(test_unit_source_game_pgn_cache
    unit/test_unit_source_game_pgn_cache.cpp
)
target_include_directories(test_unit_source_game_pgn_cache PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_unit_source_game_pgn_cache PRIVATE Qt6::Core Qt6::Test lichess)
add_test(NAME test_unit_source_game_pgn_cache COMMAND test_unit_source_game_pgn_cache)

add_executable(test_integration_worker_roundtrip
    integration/test_integration_worker_roundtrip.cpp
)
target_include_directories(test_integration_worker_roundtrip PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_integration_worker_roundtrip PRIVATE Qt6::Core Qt6::Test orchestration)
add_test(NAME test_integration_worker_roundtrip COMMAND test_integration_worker_roundtrip)

add_executable(test_integration_worker_invalid_engine
    integration/test_integration_worker_invalid_engine.cpp
)
target_include_directories(test_integration_worker_invalid_engine PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_integration_worker_invalid_engine PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_integration_worker_invalid_engine COMMAND test_integration_worker_invalid_engine)

add_executable(test_integration_worker_mapping
    integration/test_integration_worker_mapping.cpp
)
target_include_directories(test_integration_worker_mapping PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_integration_worker_mapping PRIVATE Qt6::Core Qt6::Test orchestration)
add_test(NAME test_integration_worker_mapping COMMAND test_integration_worker_mapping)

add_executable(test_integration_worker_multi_role
    integration/test_integration_worker_multi_role.cpp
)
target_include_directories(test_integration_worker_multi_role PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_integration_worker_multi_role PRIVATE Qt6::Core Qt6::Test orchestration)
add_test(NAME test_integration_worker_multi_role COMMAND test_integration_worker_multi_role)

add_executable(test_integration_worker_live_fixtures
    integration/test_integration_worker_live_fixtures.cpp
)
target_include_directories(test_integration_worker_live_fixtures PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_integration_worker_live_fixtures PRIVATE Qt6::Core Qt6::Test orchestration)
add_test(NAME test_integration_worker_live_fixtures COMMAND test_integration_worker_live_fixtures)

add_executable(test_integration_persistence
    integration/test_integration_persistence.cpp
    ../apps/desktop/resources.qrc
)
target_include_directories(test_integration_persistence PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_integration_persistence PRIVATE Qt6::Core Qt6::Sql Qt6::Test orchestration storage)
add_test(NAME test_integration_persistence COMMAND test_integration_persistence)

add_executable(test_integration_provided_puzzle_handoff
    integration/test_integration_provided_puzzle_handoff.cpp
    ../apps/desktop/resources.qrc
)
target_include_directories(test_integration_provided_puzzle_handoff PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_integration_provided_puzzle_handoff PRIVATE Qt6::Core Qt6::Sql Qt6::Test orchestration puzzle_runner storage reporting)
add_test(NAME test_integration_provided_puzzle_handoff COMMAND test_integration_provided_puzzle_handoff)

if(EXISTS "${CMAKE_SOURCE_DIR}/.venv/bin/python")
    add_test(
        NAME test_python_worker_logic
        COMMAND ${CMAKE_SOURCE_DIR}/.venv/bin/python -m unittest discover -s workers/analysis_py -p test_worker_logic.py
    )
    set_tests_properties(test_python_worker_logic PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
