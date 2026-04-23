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

add_executable(ipc_transport_link_test
        ipc_transport_link_test.cpp
)

target_include_directories(ipc_transport_link_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(ipc_transport_link_test PRIVATE
        ipc
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
        ipc
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
        ipc
)

add_test(NAME ipc_etcd_discovery_test COMMAND $<TARGET_FILE:ipc_etcd_discovery_test>)
set_tests_properties(ipc_etcd_discovery_test PROPERTIES
        WORKING_DIRECTORY ${SERVER_BINARY_FOLDER}/bin
)
