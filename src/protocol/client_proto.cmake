set(CLIENT_PROTO_FILES
        ${CLIENT_PROTO_ROOT}/common/v1/types.proto
        ${CLIENT_PROTO_ROOT}/login/v1/login.proto
        ${CLIENT_PROTO_ROOT}/game/v1/player.proto
)

set(CLIENT_PROTO_SRCS
        ${CLIENT_PROTO_GEN_DIR}/common/v1/types.pb.cc
        ${CLIENT_PROTO_GEN_DIR}/login/v1/login.pb.cc
        ${CLIENT_PROTO_GEN_DIR}/game/v1/player.pb.cc
)

set(CLIENT_PROTO_HDRS
        ${CLIENT_PROTO_GEN_DIR}/common/v1/types.pb.h
        ${CLIENT_PROTO_GEN_DIR}/login/v1/login.pb.h
        ${CLIENT_PROTO_GEN_DIR}/game/v1/player.pb.h
)

add_custom_command(
        OUTPUT ${CLIENT_PROTO_SRCS} ${CLIENT_PROTO_HDRS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CLIENT_PROTO_GEN_DIR}
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
                --proto_path=${CLIENT_PROTO_ROOT}
                --cpp_out=${CLIENT_PROTO_GEN_DIR}
                ${CLIENT_PROTO_FILES}
        DEPENDS ${CLIENT_PROTO_FILES}
        COMMENT "Generating client protobuf sources"
        VERBATIM
)

