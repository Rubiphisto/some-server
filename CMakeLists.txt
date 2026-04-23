cmake_minimum_required(VERSION 3.20)

project(server VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(ENABLE_TESTS "Build tests" ON)

include(GNUInstallDirs)
include(FetchContent)

set(SERVER_BINARY_FOLDER "${CMAKE_BINARY_DIR}")

option(USE_SYSTEM_SPDLOG "Use a preinstalled spdlog package" OFF)
option(USE_SYSTEM_GLAZE "Use a preinstalled glaze package" OFF)
option(USE_SYSTEM_PROTOBUF "Use a preinstalled protobuf package" ON)

if (USE_SYSTEM_SPDLOG)
    find_package(spdlog CONFIG REQUIRED)
else ()
    FetchContent_Declare(
            spdlog_proj
            QUIET
            GIT_REPOSITORY https://github.com/gabime/spdlog.git
            GIT_TAG v1.16.0
    )
    FetchContent_MakeAvailable(spdlog_proj)
endif ()

if (USE_SYSTEM_GLAZE)
    find_package(glaze CONFIG REQUIRED)
else ()
    FetchContent_Declare(
            glaze_proj
            QUIET
            GIT_REPOSITORY https://github.com/stephenberry/glaze.git
            GIT_TAG v6.0.3
            GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(glaze_proj)
endif ()

if (USE_SYSTEM_PROTOBUF)
    find_package(Protobuf REQUIRED)
else ()
    set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
            protobuf_proj
            QUIET
            GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
            GIT_TAG v3.21.12
            GIT_SHALLOW TRUE
            OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(protobuf_proj)
endif ()

set(CMAKE_MODULE_PATH
        "${server_SOURCE_DIR}/cmake"
)
include(configuration)

add_subdirectory(src)

if (ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()
