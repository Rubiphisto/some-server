set(GAME_PROTO_FILES
        ${GAME_PROTO_ROOT}/common.proto
        ${GAME_PROTO_ROOT}/login.proto
        ${GAME_PROTO_ROOT}/player.proto
        ${GAME_PROTO_ROOT}/player_data.proto
)

set(GAME_PROTO_SRCS
        ${GAME_PROTO_GEN_DIR}/common.pb.cc
        ${GAME_PROTO_GEN_DIR}/login.pb.cc
        ${GAME_PROTO_GEN_DIR}/player.pb.cc
        ${GAME_PROTO_GEN_DIR}/player_data.pb.cc
)

set(GAME_PROTO_HDRS
        ${GAME_PROTO_GEN_DIR}/common.pb.h
        ${GAME_PROTO_GEN_DIR}/login.pb.h
        ${GAME_PROTO_GEN_DIR}/player.pb.h
        ${GAME_PROTO_GEN_DIR}/player_data.pb.h
)

add_custom_command(
        OUTPUT ${GAME_PROTO_SRCS} ${GAME_PROTO_HDRS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GAME_PROTO_GEN_DIR}
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
                --proto_path=${GAME_PROTO_ROOT}
                --cpp_out=${GAME_PROTO_GEN_DIR}
                ${GAME_PROTO_FILES}
        DEPENDS ${GAME_PROTO_FILES}
        COMMENT "Generating game protobuf sources"
        VERBATIM
)
