set(GATE_GAME_PROTO_FILES
        ${GATE_GAME_PROTO_ROOT}/ipc/common/v1/types.proto
        ${GATE_GAME_PROTO_ROOT}/ipc/gate_game/v1/common.proto
        ${GATE_GAME_PROTO_ROOT}/ipc/gate_game/v1/login.proto
        ${GATE_GAME_PROTO_ROOT}/ipc/gate_game/v1/session.proto
        ${GATE_GAME_PROTO_ROOT}/ipc/gate_game/v1/player_message.proto
        ${GATE_GAME_PROTO_ROOT}/ipc/gate_game/v1/push.proto
)

set(GATE_GAME_PROTO_SRCS
        ${GATE_GAME_PROTO_GEN_DIR}/ipc/common/v1/types.pb.cc
        ${GATE_GAME_PROTO_GEN_DIR}/ipc/gate_game/v1/common.pb.cc
        ${GATE_GAME_PROTO_GEN_DIR}/ipc/gate_game/v1/login.pb.cc
        ${GATE_GAME_PROTO_GEN_DIR}/ipc/gate_game/v1/session.pb.cc
        ${GATE_GAME_PROTO_GEN_DIR}/ipc/gate_game/v1/player_message.pb.cc
        ${GATE_GAME_PROTO_GEN_DIR}/ipc/gate_game/v1/push.pb.cc
)

set(GATE_GAME_PROTO_HDRS
        ${GATE_GAME_PROTO_GEN_DIR}/ipc/common/v1/types.pb.h
        ${GATE_GAME_PROTO_GEN_DIR}/ipc/gate_game/v1/common.pb.h
        ${GATE_GAME_PROTO_GEN_DIR}/ipc/gate_game/v1/login.pb.h
        ${GATE_GAME_PROTO_GEN_DIR}/ipc/gate_game/v1/session.pb.h
        ${GATE_GAME_PROTO_GEN_DIR}/ipc/gate_game/v1/player_message.pb.h
        ${GATE_GAME_PROTO_GEN_DIR}/ipc/gate_game/v1/push.pb.h
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

