#pragma once

#include <glaze/glaze.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
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
    virtual bool LoadFromJson(std::string_view document, std::string& error) = 0;
};

template <typename TConfiguration>
struct ApplicationConfigurationDocument
{
    TConfiguration application;
};

template <typename TConfiguration>
class JsonApplicationConfiguration : public IApplicationConfiguration
{
public:
    bool LoadFromJson(std::string_view document, std::string& error) override
    {
        static_assert(std::is_base_of_v<BaseApplicationConfiguration, TConfiguration>,
                      "Application configurations must derive from BaseApplicationConfiguration");

        ApplicationConfigurationDocument<TConfiguration> root{};
        root.application = static_cast<const TConfiguration&>(*this);

        if (auto result = glz::read<glz::opts{.error_on_unknown_keys = false}>(root, document))
        {
            error = glz::format_error(result, document);
            return false;
        }

        static_cast<TConfiguration&>(*this) = std::move(root.application);
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
