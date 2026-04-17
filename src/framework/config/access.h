#pragma once

#include "value.h"

#include <cstddef>
#include <cctype>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>

namespace config_access
{
    inline std::string ToLower(std::string value)
    {
        for (char& ch : value)
        {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return value;
    }

    inline std::optional<std::size_t> ParseSize(std::string value)
    {
        if (value.empty())
        {
            return std::nullopt;
        }

        std::size_t split = value.size();
        while (split > 0 && std::isalpha(static_cast<unsigned char>(value[split - 1])))
        {
            --split;
        }

        try
        {
            const auto base = static_cast<std::size_t>(std::stoull(value.substr(0, split)));
            const std::string suffix = ToLower(value.substr(split));
            if (suffix.empty() || suffix == "b")
            {
                return base;
            }
            if (suffix == "k" || suffix == "kb")
            {
                return base * 1024;
            }
            if (suffix == "m" || suffix == "mb")
            {
                return base * 1024 * 1024;
            }
            if (suffix == "g" || suffix == "gb")
            {
                return base * 1024 * 1024 * 1024ULL;
            }
        }
        catch (const std::exception&)
        {
        }

        return std::nullopt;
    }

    inline bool ReadString(const ConfigValue& root,
                           const char* key,
                           std::string& target,
                           std::string& error,
                           const char* path)
    {
        const auto* value = root.Find(key);
        if (value == nullptr)
        {
            return true;
        }

        const auto* text = value->AsString();
        if (text == nullptr)
        {
            error = std::string(path) + " must be a string";
            return false;
        }

        target = *text;
        return true;
    }

    inline bool ReadBool(const ConfigValue& root,
                         const char* key,
                         bool& target,
                         std::string& error,
                         const char* path)
    {
        const auto* value = root.Find(key);
        if (value == nullptr)
        {
            return true;
        }

        const auto* flag = value->AsBool();
        if (flag == nullptr)
        {
            error = std::string(path) + " must be a bool";
            return false;
        }

        target = *flag;
        return true;
    }

    inline bool ReadUInt(const ConfigValue& root,
                         const char* key,
                         std::size_t& target,
                         std::string& error,
                         const char* path)
    {
        const auto* value = root.Find(key);
        if (value == nullptr)
        {
            return true;
        }

        const auto* integer = value->AsUInt();
        if (integer == nullptr)
        {
            error = std::string(path) + " must be an unsigned integer";
            return false;
        }

        target = static_cast<std::size_t>(*integer);
        return true;
    }

    inline bool ReadSize(const ConfigValue& root,
                         const char* key,
                         std::size_t& target,
                         std::string& error,
                         const char* path)
    {
        const auto* value = root.Find(key);
        if (value == nullptr)
        {
            return true;
        }

        if (const auto* integer = value->AsUInt(); integer != nullptr)
        {
            target = static_cast<std::size_t>(*integer);
            return true;
        }

        const auto* text = value->AsString();
        if (text == nullptr)
        {
            error = std::string(path) + " must be a size string or integer";
            return false;
        }

        const auto parsed = ParseSize(*text);
        if (!parsed.has_value())
        {
            error = std::string(path) + " has invalid size value";
            return false;
        }

        target = parsed.value();
        return true;
    }
}
