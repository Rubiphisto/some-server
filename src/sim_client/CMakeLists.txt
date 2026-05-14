some_application(sim_client LIBS loader framework common client_proto)

add_custom_command(TARGET sim_client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:sim_client>/conf
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/bin/conf/sim_client.json
                $<TARGET_FILE_DIR:sim_client>/conf/sim_client.json
)
