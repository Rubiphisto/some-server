add_executable(loader_config_test
        loader_config_test.cpp
        ${CMAKE_SOURCE_DIR}/src/gate/application.cpp
)

target_include_directories(loader_config_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(loader_config_test PRIVATE
        loader
)

add_custom_command(TARGET loader_config_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SERVER_BINARY_FOLDER}/bin/conf
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/bin/conf/gate.json
                ${SERVER_BINARY_FOLDER}/bin/conf/gate.json
)

add_test(NAME loader_config_test COMMAND $<TARGET_FILE:loader_config_test>)
set_tests_properties(loader_config_test PROPERTIES
        WORKING_DIRECTORY ${SERVER_BINARY_FOLDER}/bin
)

add_executable(application_service_lifecycle_test
        application_service_lifecycle_test.cpp
)

target_include_directories(application_service_lifecycle_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(application_service_lifecycle_test PRIVATE
        framework
)

add_test(NAME application_service_lifecycle_test COMMAND $<TARGET_FILE:application_service_lifecycle_test>)
set_tests_properties(application_service_lifecycle_test PROPERTIES
        WORKING_DIRECTORY ${SERVER_BINARY_FOLDER}/bin
)

add_executable(game_configuration_test
        game_configuration_test.cpp
)

target_include_directories(game_configuration_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(game_configuration_test PRIVATE
        framework
)

add_test(NAME game_configuration_test COMMAND $<TARGET_FILE:game_configuration_test>)
set_tests_properties(game_configuration_test PROPERTIES
        WORKING_DIRECTORY ${SERVER_BINARY_FOLDER}/bin
)

add_executable(ipc_transport_link_test
        ipc_transport_link_test.cpp
)

target_include_directories(ipc_transport_link_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(ipc_transport_link_test PRIVATE
        framework
)

add_test(NAME ipc_transport_link_test COMMAND $<TARGET_FILE:ipc_transport_link_test>)
set_tests_properties(ipc_transport_link_test PROPERTIES
        WORKING_DIRECTORY ${SERVER_BINARY_FOLDER}/bin
)

add_executable(ipc_discovery_routing_test
        ipc_discovery_routing_test.cpp
)

target_include_directories(ipc_discovery_routing_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(ipc_discovery_routing_test PRIVATE
        framework
)

add_test(NAME ipc_discovery_routing_test COMMAND $<TARGET_FILE:ipc_discovery_routing_test>)
set_tests_properties(ipc_discovery_routing_test PROPERTIES
        WORKING_DIRECTORY ${SERVER_BINARY_FOLDER}/bin
)

add_executable(ipc_etcd_discovery_test
        ipc_etcd_discovery_test.cpp
)

target_include_directories(ipc_etcd_discovery_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(ipc_etcd_discovery_test PRIVATE
        framework
)

add_test(NAME ipc_etcd_discovery_test COMMAND $<TARGET_FILE:ipc_etcd_discovery_test>)
set_tests_properties(ipc_etcd_discovery_test PROPERTIES
        WORKING_DIRECTORY ${SERVER_BINARY_FOLDER}/bin
)

add_executable(ipc_receiver_messaging_test
        ipc_receiver_messaging_test.cpp
)

target_include_directories(ipc_receiver_messaging_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(ipc_receiver_messaging_test PRIVATE
        framework
)

add_test(NAME ipc_receiver_messaging_test COMMAND $<TARGET_FILE:ipc_receiver_messaging_test>)
set_tests_properties(ipc_receiver_messaging_test PROPERTIES
        WORKING_DIRECTORY ${SERVER_BINARY_FOLDER}/bin
)

add_executable(ipc_multi_process_integration_test
        ipc_multi_process_integration_test.cpp
)

target_include_directories(ipc_multi_process_integration_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(ipc_multi_process_integration_test PRIVATE
        framework
)

add_test(NAME ipc_multi_process_integration_test COMMAND $<TARGET_FILE:ipc_multi_process_integration_test>)
set_tests_properties(ipc_multi_process_integration_test PROPERTIES
        WORKING_DIRECTORY ${SERVER_BINARY_FOLDER}/bin
)

foreach (scenario
        reconnect_after_disconnect
        same_account_kick
        migration_recover_after_kick
        default_player_recover_after_maria_restore
        player_directory_persist_across_restart)
    add_custom_target(sim_client_regression_${scenario}
            COMMAND ${CMAKE_COMMAND} -E env
                    BUILD_DIR=${CMAKE_BINARY_DIR}
                    ${CMAKE_SOURCE_DIR}/tools/sim_client/run_regression.sh
                    ${scenario}
            DEPENDS relay gate game sim_client
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Run sim_client regression scenario: ${scenario}"
            VERBATIM
    )
endforeach ()

add_custom_target(sim_client_regression
        COMMAND ${CMAKE_COMMAND} -E env
                BUILD_DIR=${CMAKE_BINARY_DIR}
                ${CMAKE_SOURCE_DIR}/tools/sim_client/run_all_regressions.sh
        DEPENDS
                relay gate game sim_client
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Run all sim_client regression scenarios serially"
        VERBATIM
)

if (ENABLE_HOST_INTEGRATION_TESTS)
    foreach (scenario
            reconnect_after_disconnect
            same_account_kick
            migration_recover_after_kick
            default_player_recover_after_maria_restore
            player_directory_persist_across_restart)
        add_test(
                NAME sim_client_regression_${scenario}
                COMMAND ${CMAKE_COMMAND} -E env
                        BUILD_DIR=${CMAKE_BINARY_DIR}
                        ${CMAKE_SOURCE_DIR}/tools/sim_client/run_regression.sh
                        ${scenario}
        )
        set_tests_properties(sim_client_regression_${scenario} PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                RUN_SERIAL TRUE
                LABELS "host_integration;sim_client"
        )
    endforeach ()
endif ()
