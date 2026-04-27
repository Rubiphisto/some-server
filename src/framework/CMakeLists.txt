set(FRAMEWORK_IPC_PROTO_ROOT ${CMAKE_SOURCE_DIR}/proto)
set(FRAMEWORK_IPC_PROTO_GEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ipc/pb)
set(FRAMEWORK_IPC_PROTO_FILES
        ${FRAMEWORK_IPC_PROTO_ROOT}/ipc/common/v1/types.proto
        ${FRAMEWORK_IPC_PROTO_ROOT}/ipc/control/v1/control.proto
        ${FRAMEWORK_IPC_PROTO_ROOT}/ipc/data/v1/envelope.proto
)
set(FRAMEWORK_IPC_PROTO_SRCS
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}/ipc/common/v1/types.pb.cc
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}/ipc/control/v1/control.pb.cc
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}/ipc/data/v1/envelope.pb.cc
)
set(FRAMEWORK_IPC_PROTO_HDRS
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}/ipc/common/v1/types.pb.h
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}/ipc/control/v1/control.pb.h
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}/ipc/data/v1/envelope.pb.h
)

add_custom_command(
        OUTPUT ${FRAMEWORK_IPC_PROTO_SRCS} ${FRAMEWORK_IPC_PROTO_HDRS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${FRAMEWORK_IPC_PROTO_GEN_DIR}
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
                --proto_path=${FRAMEWORK_IPC_PROTO_ROOT}
                --cpp_out=${FRAMEWORK_IPC_PROTO_GEN_DIR}
                ${FRAMEWORK_IPC_PROTO_FILES}
        DEPENDS ${FRAMEWORK_IPC_PROTO_FILES}
        COMMENT "Generating IPC protobuf sources"
        VERBATIM
)

some_library(framework LIBS common glaze::glaze ${Protobuf_LIBRARIES})

target_sources(framework PRIVATE ${FRAMEWORK_IPC_PROTO_SRCS} ${FRAMEWORK_IPC_PROTO_HDRS})
target_include_directories(framework PUBLIC
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}
        ${Protobuf_INCLUDE_DIRS}
)
