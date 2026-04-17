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

add_test(NAME loader_config_test COMMAND loader_config_test)
