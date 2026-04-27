some_application(relay LIBS loader framework common)

add_custom_command(TARGET relay POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:relay>/conf
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/bin/conf/relay.json
                $<TARGET_FILE_DIR:relay>/conf/relay.json
)
