cmake_minimum_required(VERSION 3.20)

project(server VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(ENABLE_TESTS "Build tests" ON)

include(GNUInstallDirs)
include(FetchContent)

set(SERVER_BINARY_FOLDER "${CMAKE_BINARY_DIR}")

option(USE_SYSTEM_CLI11 "Use a preinstalled CLI11 package" OFF)
option(USE_SYSTEM_SPDLOG "Use a preinstalled spdlog package" OFF)
option(USE_SYSTEM_YAML_CPP "Use a preinstalled yaml-cpp package" OFF)

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

if (USE_SYSTEM_YAML_CPP)
    find_package(yaml-cpp CONFIG REQUIRED)
else ()
    FetchContent_Declare(
            yaml_cpp_proj
            QUIET
            GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
            GIT_TAG 0.8.0
    )
    FetchContent_MakeAvailable(yaml_cpp_proj)
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
