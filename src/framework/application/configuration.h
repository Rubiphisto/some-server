#pragma once

#include <glaze/glaze.hpp>

#include <cstdint>
#include <string>
#include <utility>

struct ListenConfiguration
{
    std::string host = "127.0.0.1";
    std::uint16_t port = 9000;
};

struct BaseApplicationConfiguration
{
    ListenConfiguration listen;
};

class IApplicationConfiguration
{
public:
    virtual ~IApplicationConfiguration() = default;
    virtual bool LoadFromGeneric(const glz::generic& value, std::string& error) = 0;
};

template <typename TConfiguration>
class JsonApplicationConfiguration : public IApplicationConfiguration
{
public:
    bool LoadFromGeneric(const glz::generic& value, std::string& error) override
    {
        TConfiguration configuration = static_cast<const TConfiguration&>(*this);
        const auto document = value.dump();
        if (!document)
        {
            error = glz::format_error(document.error());
            return false;
        }

        if (auto result = glz::read<glz::opts{.error_on_unknown_keys = false}>(configuration, *document))
        {
            error = glz::format_error(result, *document);
            return false;
        }

        static_cast<TConfiguration&>(*this) = std::move(configuration);
        return true;
    }
};

template <typename TConfiguration, typename... TArgs>
constexpr auto MakeApplicationConfigObject(TArgs&&... args)
{
    return glz::object("listen", &TConfiguration::listen, std::forward<TArgs>(args)...);
}

template <>
struct glz::meta<ListenConfiguration>
{
    using T = ListenConfiguration;
    static constexpr auto value = glz::object("host", &T::host, "port", &T::port);
};

template <>
struct glz::meta<BaseApplicationConfiguration>
{
    using T = BaseApplicationConfiguration;
    static constexpr auto value = glz::object("listen", &T::listen);
};

#define SOME_SERVER_APPLICATION_CONFIG(Type, ...)        \
    template <>                                          \
    struct glz::meta<Type>                               \
    {                                                    \
        using T = Type;                                  \
        static constexpr auto value =                    \
            MakeApplicationConfigObject<T>(__VA_ARGS__); \
    }
