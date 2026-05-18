find_package(PkgConfig REQUIRED)
pkg_check_modules(FRAMEWORK_STORAGE_DEPS REQUIRED IMPORTED_TARGET hiredis libmariadb)

set(FRAMEWORK_IPC_PROTO_ROOT ${CMAKE_SOURCE_DIR}/proto)
set(FRAMEWORK_IPC_PROTO_GEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ipc/pb)
set(FRAMEWORK_ETCD_PROTO_ROOT ${CMAKE_SOURCE_DIR}/third_party/etcd-proto)
set(FRAMEWORK_THIRD_PARTY_PROTO_ROOT ${CMAKE_SOURCE_DIR}/third_party)
set(FRAMEWORK_IPC_PROTO_FILES
        ${FRAMEWORK_IPC_PROTO_ROOT}/ipc/types.proto
        ${FRAMEWORK_IPC_PROTO_ROOT}/ipc/control.proto
        ${FRAMEWORK_IPC_PROTO_ROOT}/ipc/envelope.proto
)
set(FRAMEWORK_IPC_PROTO_SRCS
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}/ipc/types.pb.cc
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}/ipc/control.pb.cc
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}/ipc/envelope.pb.cc
)
set(FRAMEWORK_IPC_PROTO_HDRS
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}/ipc/types.pb.h
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}/ipc/control.pb.h
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}/ipc/envelope.pb.h
)
set(FRAMEWORK_ETCD_PROTO_FILES
        ${FRAMEWORK_THIRD_PARTY_PROTO_ROOT}/gogoproto/gogo.proto
        ${FRAMEWORK_ETCD_PROTO_ROOT}/etcd/api/authpb/auth.proto
        ${FRAMEWORK_ETCD_PROTO_ROOT}/etcd/api/mvccpb/kv.proto
        ${FRAMEWORK_ETCD_PROTO_ROOT}/etcd/api/versionpb/version.proto
        ${FRAMEWORK_ETCD_PROTO_ROOT}/etcd/api/etcdserverpb/rpc.proto
)
set(FRAMEWORK_ETCD_PROTO_SRCS
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}/gogoproto/gogo.pb.cc
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}/etcd/api/authpb/auth.pb.cc
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}/etcd/api/mvccpb/kv.pb.cc
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}/etcd/api/versionpb/version.pb.cc
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}/etcd/api/etcdserverpb/rpc.pb.cc
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}/etcd/api/etcdserverpb/rpc.grpc.pb.cc
)
set(FRAMEWORK_ETCD_PROTO_HDRS
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}/gogoproto/gogo.pb.h
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}/etcd/api/authpb/auth.pb.h
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}/etcd/api/mvccpb/kv.pb.h
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}/etcd/api/versionpb/version.pb.h
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}/etcd/api/etcdserverpb/rpc.pb.h
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}/etcd/api/etcdserverpb/rpc.grpc.pb.h
)

find_program(FRAMEWORK_GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)

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

add_custom_command(
        OUTPUT ${FRAMEWORK_ETCD_PROTO_SRCS} ${FRAMEWORK_ETCD_PROTO_HDRS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${FRAMEWORK_IPC_PROTO_GEN_DIR}
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
                --proto_path=${FRAMEWORK_ETCD_PROTO_ROOT}
                --proto_path=${FRAMEWORK_THIRD_PARTY_PROTO_ROOT}
                --proto_path=${Protobuf_INCLUDE_DIRS}
                --cpp_out=${FRAMEWORK_IPC_PROTO_GEN_DIR}
                --grpc_out=${FRAMEWORK_IPC_PROTO_GEN_DIR}
                --plugin=protoc-gen-grpc=${FRAMEWORK_GRPC_CPP_PLUGIN}
                ${FRAMEWORK_ETCD_PROTO_FILES}
        DEPENDS ${FRAMEWORK_ETCD_PROTO_FILES}
        COMMENT "Generating etcd discovery protobuf and gRPC sources"
        VERBATIM
)

some_library(framework LIBS glaze::glaze ${Protobuf_LIBRARIES} gRPC::grpc++)

target_link_libraries(framework PUBLIC PkgConfig::FRAMEWORK_STORAGE_DEPS)

target_sources(
        framework PRIVATE
        ${FRAMEWORK_IPC_PROTO_SRCS}
        ${FRAMEWORK_IPC_PROTO_HDRS}
        ${FRAMEWORK_ETCD_PROTO_SRCS}
        ${FRAMEWORK_ETCD_PROTO_HDRS}
)
target_include_directories(framework PUBLIC
        ${FRAMEWORK_IPC_PROTO_GEN_DIR}
        ${FRAMEWORK_ETCD_PROTO_ROOT}
        ${FRAMEWORK_THIRD_PARTY_PROTO_ROOT}
        ${Protobuf_INCLUDE_DIRS}
)
