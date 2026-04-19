some_application(gate LIBS loader framework common)

add_custom_command(TARGET gate POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:gate>/conf
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/bin/conf/gate.json
                $<TARGET_FILE_DIR:gate>/conf/gate.json
)
