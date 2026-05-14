set(PLAYER_DATA_PROTO_FILES
        ${PLAYER_DATA_PROTO_ROOT}/player/v1/player.proto
)

set(PLAYER_DATA_PROTO_SRCS
        ${PLAYER_DATA_PROTO_GEN_DIR}/player/v1/player.pb.cc
)

set(PLAYER_DATA_PROTO_HDRS
        ${PLAYER_DATA_PROTO_GEN_DIR}/player/v1/player.pb.h
)

add_custom_command(
        OUTPUT ${PLAYER_DATA_PROTO_SRCS} ${PLAYER_DATA_PROTO_HDRS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PLAYER_DATA_PROTO_GEN_DIR}
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
                --proto_path=${PLAYER_DATA_PROTO_ROOT}
                --cpp_out=${PLAYER_DATA_PROTO_GEN_DIR}
                ${PLAYER_DATA_PROTO_FILES}
        DEPENDS ${PLAYER_DATA_PROTO_FILES}
        COMMENT "Generating player data protobuf sources"
        VERBATIM
)
