set(GAME_PROTO_ROOT ${CMAKE_SOURCE_DIR}/proto/game)
set(GAME_PROTO_GEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/game/pb)

include(${CMAKE_CURRENT_SOURCE_DIR}/game_proto.cmake)

add_library(game_proto STATIC "")
some_configure_common(game_proto)
set_target_properties(game_proto PROPERTIES LINKER_LANGUAGE CXX)

target_sources(game_proto PRIVATE ${GAME_PROTO_SRCS} ${GAME_PROTO_HDRS})
target_include_directories(game_proto PUBLIC
        ${GAME_PROTO_GEN_DIR}
        ${Protobuf_INCLUDE_DIRS}
)
target_link_libraries(game_proto PUBLIC ${Protobuf_LIBRARIES})

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
