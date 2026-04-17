#pragma once

#include <glaze/glaze.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <cstdint>

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

class IApplication
{
public:
    virtual ~IApplication() = default;
    virtual const char8_t* GetName() const = 0;
    virtual std::unique_ptr<IApplicationConfiguration> CreateConfiguration() const = 0;
    virtual bool Configure(const IApplicationConfiguration& configuration) = 0;
    virtual void Load() = 0;
    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual void Unload() = 0;
};

template <typename TConfiguration>
class ApplicationBase : public IApplication
{
public:
    std::unique_ptr<IApplicationConfiguration> CreateConfiguration() const override
    {
        return std::make_unique<TConfiguration>();
    }

    bool Configure(const IApplicationConfiguration& configuration) override
    {
        const auto* typed = dynamic_cast<const TConfiguration*>(&configuration);
        if (typed == nullptr)
        {
            return false;
        }

        mConfiguration = *typed;
        return OnConfigure();
    }

protected:
    virtual bool OnConfigure() { return true; }
    const TConfiguration& AppConfig() const { return mConfiguration; }

private:
    TConfiguration mConfiguration;
};

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

#define SOME_SERVER_APPLICATION_CONFIG(Type, ...)     \
    template <>                                       \
    struct glz::meta<Type>                            \
    {                                                 \
        using T = Type;                               \
        static constexpr auto value =                 \
            MakeApplicationConfigObject<T>(__VA_ARGS__); \
    }
