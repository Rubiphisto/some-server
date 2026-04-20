#pragma once

#include <glaze/glaze.hpp>

#include <cstddef>
#include <string>

struct LoaderLogRotationConfiguration
{
    std::string mode = "size";
    std::size_t max_size = 10 * 1024 * 1024;
    std::size_t max_files = 5;
    std::size_t daily_hour = 0;
    std::size_t daily_minute = 0;
};

struct LoaderLogConfiguration
{
    std::string file;
    std::string error_file;
    std::string level = "info";
    bool console = true;
    bool syslog = false;
    LoaderLogRotationConfiguration rotate;
};

struct LoaderConfiguration
{
    LoaderLogConfiguration log;
};

template <>
struct glz::meta<LoaderLogRotationConfiguration>
{
    using T = LoaderLogRotationConfiguration;
    static constexpr auto value = glz::object(
        "mode",
        &T::mode,
        "max_size",
        &T::max_size,
        "max_files",
        &T::max_files,
        "daily_hour",
        &T::daily_hour,
        "daily_minute",
        &T::daily_minute);
};

template <>
struct glz::meta<LoaderLogConfiguration>
{
    using T = LoaderLogConfiguration;
    static constexpr auto value = glz::object(
        "file",
        &T::file,
        "error_file",
        &T::error_file,
        "level",
        &T::level,
        "console",
        &T::console,
        "syslog",
        &T::syslog,
        "rotate",
        &T::rotate);
};

template <>
struct glz::meta<LoaderConfiguration>
{
    using T = LoaderConfiguration;
    static constexpr auto value = glz::object("log", &T::log);
};
