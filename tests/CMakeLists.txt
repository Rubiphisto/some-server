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
