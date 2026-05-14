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

set(GATE_GAME_PROTO_ROOT ${CMAKE_SOURCE_DIR}/proto)
set(GATE_GAME_PROTO_GEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ipc/pb)

include(${CMAKE_CURRENT_SOURCE_DIR}/gate_game_proto.cmake)

add_library(gate_game_proto STATIC "")
some_configure_common(gate_game_proto)
set_target_properties(gate_game_proto PROPERTIES LINKER_LANGUAGE CXX)

target_sources(gate_game_proto PRIVATE ${GATE_GAME_PROTO_SRCS} ${GATE_GAME_PROTO_HDRS})
target_include_directories(gate_game_proto PUBLIC
        ${GATE_GAME_PROTO_GEN_DIR}
        ${Protobuf_INCLUDE_DIRS}
)
target_link_libraries(gate_game_proto PUBLIC ${Protobuf_LIBRARIES})

set(PLAYER_DATA_PROTO_ROOT ${CMAKE_SOURCE_DIR}/proto)
set(PLAYER_DATA_PROTO_GEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/player/pb)

include(${CMAKE_CURRENT_SOURCE_DIR}/player_data_proto.cmake)

add_library(player_data_proto STATIC "")
some_configure_common(player_data_proto)
set_target_properties(player_data_proto PROPERTIES LINKER_LANGUAGE CXX)

target_sources(player_data_proto PRIVATE ${PLAYER_DATA_PROTO_SRCS} ${PLAYER_DATA_PROTO_HDRS})
target_include_directories(player_data_proto PUBLIC
        ${PLAYER_DATA_PROTO_GEN_DIR}
        ${Protobuf_INCLUDE_DIRS}
)
target_link_libraries(player_data_proto PUBLIC ${Protobuf_LIBRARIES})
