cmake_minimum_required(VERSION 3.20)

project(server VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

#option(ENABLE_TESTS "Build tests" ON)

include(GNUInstallDirs)

set(SERVER_BINARY_FOLDER "${CMAKE_BINARY_DIR}")

set(CMAKE_MODULE_PATH
        "${server_SOURCE_DIR}/cmake"
)
include(configuration)

add_subdirectory(src)
