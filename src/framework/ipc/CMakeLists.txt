set(IPC_PROTO_ROOT ${CMAKE_SOURCE_DIR}/proto)
set(IPC_PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(IPC_PROTO_FILES
        ${IPC_PROTO_ROOT}/ipc/common/v1/types.proto
        ${IPC_PROTO_ROOT}/ipc/control/v1/control.proto
        ${IPC_PROTO_ROOT}/ipc/data/v1/envelope.proto
)
set(IPC_PROTO_SRCS
        ${IPC_PROTO_GEN_DIR}/ipc/common/v1/types.pb.cc
        ${IPC_PROTO_GEN_DIR}/ipc/control/v1/control.pb.cc
        ${IPC_PROTO_GEN_DIR}/ipc/data/v1/envelope.pb.cc
)
set(IPC_PROTO_HDRS
        ${IPC_PROTO_GEN_DIR}/ipc/common/v1/types.pb.h
        ${IPC_PROTO_GEN_DIR}/ipc/control/v1/control.pb.h
        ${IPC_PROTO_GEN_DIR}/ipc/data/v1/envelope.pb.h
)

add_custom_command(
        OUTPUT ${IPC_PROTO_SRCS} ${IPC_PROTO_HDRS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${IPC_PROTO_GEN_DIR}
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
                --proto_path=${IPC_PROTO_ROOT}
                --cpp_out=${IPC_PROTO_GEN_DIR}
                ${IPC_PROTO_FILES}
        DEPENDS ${IPC_PROTO_FILES}
        COMMENT "Generating IPC protobuf sources"
        VERBATIM
)

some_library(ipc LIBS common glaze::glaze)

target_sources(ipc PRIVATE ${IPC_PROTO_SRCS} ${IPC_PROTO_HDRS})
target_include_directories(ipc PUBLIC
        ${IPC_PROTO_GEN_DIR}
        ${Protobuf_INCLUDE_DIRS}
)
target_link_libraries(ipc PUBLIC ${Protobuf_LIBRARIES})
