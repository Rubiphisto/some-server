some_application(game LIBS loader framework common ipc)

add_custom_command(TARGET game POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:game>/conf
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/bin/conf/game.json
                $<TARGET_FILE_DIR:game>/conf/game.json
)
