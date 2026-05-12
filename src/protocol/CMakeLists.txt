set(CLIENT_PROTO_ROOT ${CMAKE_SOURCE_DIR}/proto/client)
set(CLIENT_PROTO_GEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/client/pb)

include(${CMAKE_CURRENT_SOURCE_DIR}/client_proto.cmake)

add_library(client_proto STATIC "")
some_configure_common(client_proto)
set_target_properties(client_proto PROPERTIES LINKER_LANGUAGE CXX)

target_sources(client_proto PRIVATE ${CLIENT_PROTO_SRCS} ${CLIENT_PROTO_HDRS})
target_include_directories(client_proto PUBLIC
        ${CLIENT_PROTO_GEN_DIR}
        ${Protobuf_INCLUDE_DIRS}
)
target_link_libraries(client_proto PUBLIC ${Protobuf_LIBRARIES})

