set(GATE_GAME_PROTO_FILES
        ${GATE_GAME_PROTO_ROOT}/ipc/types.proto
        ${GATE_GAME_PROTO_ROOT}/ipc/common.proto
        ${GATE_GAME_PROTO_ROOT}/ipc/login.proto
        ${GATE_GAME_PROTO_ROOT}/ipc/session.proto
        ${GATE_GAME_PROTO_ROOT}/ipc/player_message.proto
        ${GATE_GAME_PROTO_ROOT}/ipc/push.proto
)

set(GATE_GAME_PROTO_SRCS
        ${GATE_GAME_PROTO_GEN_DIR}/ipc/types.pb.cc
        ${GATE_GAME_PROTO_GEN_DIR}/ipc/common.pb.cc
        ${GATE_GAME_PROTO_GEN_DIR}/ipc/login.pb.cc
        ${GATE_GAME_PROTO_GEN_DIR}/ipc/session.pb.cc
        ${GATE_GAME_PROTO_GEN_DIR}/ipc/player_message.pb.cc
        ${GATE_GAME_PROTO_GEN_DIR}/ipc/push.pb.cc
)

set(GATE_GAME_PROTO_HDRS
        ${GATE_GAME_PROTO_GEN_DIR}/ipc/types.pb.h
        ${GATE_GAME_PROTO_GEN_DIR}/ipc/common.pb.h
        ${GATE_GAME_PROTO_GEN_DIR}/ipc/login.pb.h
        ${GATE_GAME_PROTO_GEN_DIR}/ipc/session.pb.h
        ${GATE_GAME_PROTO_GEN_DIR}/ipc/player_message.pb.h
        ${GATE_GAME_PROTO_GEN_DIR}/ipc/push.pb.h
)

add_custom_command(
        OUTPUT ${GATE_GAME_PROTO_SRCS} ${GATE_GAME_PROTO_HDRS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GATE_GAME_PROTO_GEN_DIR}
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
                --proto_path=${GATE_GAME_PROTO_ROOT}
                --cpp_out=${GATE_GAME_PROTO_GEN_DIR}
                ${GATE_GAME_PROTO_FILES}
        DEPENDS ${GATE_GAME_PROTO_FILES}
        COMMENT "Generating gate_game IPC business protobuf sources"
        VERBATIM
)
