#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

class ConfigValue
{
public:
    using Object = std::unordered_map<std::string, ConfigValue>;
    using Array = std::vector<ConfigValue>;
    using Value = std::variant<std::monostate, bool, std::uint64_t, std::string, Object, Array>;

    ConfigValue() = default;
    ConfigValue(bool value)
        : mValue(value)
    {
    }
    ConfigValue(std::uint64_t value)
        : mValue(value)
    {
    }
    ConfigValue(std::string value)
        : mValue(std::move(value))
    {
    }
    ConfigValue(Object value)
        : mValue(std::move(value))
    {
    }
    ConfigValue(Array value)
        : mValue(std::move(value))
    {
    }

    bool IsNull() const { return std::holds_alternative<std::monostate>(mValue); }
    bool IsBool() const { return std::holds_alternative<bool>(mValue); }
    bool IsUInt() const { return std::holds_alternative<std::uint64_t>(mValue); }
    bool IsString() const { return std::holds_alternative<std::string>(mValue); }
    bool IsObject() const { return std::holds_alternative<Object>(mValue); }
    bool IsArray() const { return std::holds_alternative<Array>(mValue); }

    const bool* AsBool() const { return std::get_if<bool>(&mValue); }
    const std::uint64_t* AsUInt() const { return std::get_if<std::uint64_t>(&mValue); }
    const std::string* AsString() const { return std::get_if<std::string>(&mValue); }
    const Object* AsObject() const { return std::get_if<Object>(&mValue); }
    const Array* AsArray() const { return std::get_if<Array>(&mValue); }

    Object* AsObject() { return std::get_if<Object>(&mValue); }
    Array* AsArray() { return std::get_if<Array>(&mValue); }

    const ConfigValue* Find(std::string_view key) const
    {
        const auto* object = AsObject();
        if (object == nullptr)
        {
            return nullptr;
        }

        const auto it = object->find(std::string(key));
        return it == object->end() ? nullptr : &it->second;
    }

private:
    Value mValue;
};
