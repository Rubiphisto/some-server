cmake_minimum_required(VERSION 3.20)

project(server VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

#option(ENABLE_TESTS "Build tests" ON)

include(GNUInstallDirs)
include(FetchContent)

set(SERVER_BINARY_FOLDER "${CMAKE_BINARY_DIR}")

option(USE_SYSTEM_CLI11 "Use a preinstalled CLI11 package" OFF)

if (USE_SYSTEM_CLI11)
    find_package(CLI11 CONFIG REQUIRED)
else ()
    FetchContent_Declare(
            cli11_proj
            QUIET
            GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
            GIT_TAG v2.6.2
    )
    FetchContent_MakeAvailable(cli11_proj)
endif ()

set(CMAKE_MODULE_PATH
        "${server_SOURCE_DIR}/cmake"
)
include(configuration)

add_subdirectory(src)
