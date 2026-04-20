#include "loader.h"
#include "config.h"
#include "logging.h"

#include <spdlog/spdlog.h>

#include <array>
#include <condition_variable>
#include <exception>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>
#include <string>
#include <string_view>

namespace
{
    enum class ParseResult
    {
        ok,
        exit_success,
        exit_failure
    };

    struct OptionDefinition
    {
        std::string_view long_name;
        std::string_view short_name;
        std::string_view value_format;
        std::string_view description;
        bool expects_value = false;
    };

    constexpr std::array<OptionDefinition, 2> kOptionDefinitions{{
        {"help", "h", "", "Show this help message", false},
        {"config", "c", "<path>", "Load a JSON configuration overlay file", true},
    }};

    void PrintHelp(const std::string& application_name)
    {
        std::cout << application_name << " startup loader\n\n"
                  << "./" << application_name << " [OPTIONS]\n\n"
                  << "OPTIONS:\n";

        for (const auto& option : kOptionDefinitions)
        {
            std::string option_usage =
                "  -" + std::string{option.short_name} + ", --" + std::string{option.long_name};
            if (option.expects_value && !option.value_format.empty())
            {
                option_usage += ' ';
                option_usage += option.value_format;
            }

            std::cout << std::left << std::setw(30) << option_usage << option.description << '\n';
        }
    }

    ParseResult ParseArguments(int argc,
                               char* argv[],
                               const std::string& application_name,
                               std::map<std::string, std::string>& option_values)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view argument = argv[i];

            const OptionDefinition* matched_option = nullptr;
            if (argument.starts_with("--"))
            {
                const std::string_view long_name = argument.substr(2);
                for (const auto& option : kOptionDefinitions)
                {
                    if (option.long_name == long_name)
                    {
                        matched_option = &option;
                        break;
                    }
                }
            }
            else if (argument.starts_with('-'))
            {
                const std::string_view short_name = argument.substr(1);
                for (const auto& option : kOptionDefinitions)
                {
                    if (option.short_name == short_name)
                    {
                        matched_option = &option;
                        break;
                    }
                }
            }

            if (matched_option != nullptr)
            {
                if (!matched_option->expects_value)
                {
                    option_values.emplace(std::string{matched_option->long_name}, std::string{});
                    continue;
                }

                if (i + 1 >= argc)
                {
                    std::cerr << "missing value for " << argument << std::endl;
                    return ParseResult::exit_failure;
                }

                option_values[std::string{matched_option->long_name}] = argv[++i];
                continue;
            }

            if (argument.starts_with('-'))
            {
                std::cerr << "unknown option: " << argument << std::endl;
                return ParseResult::exit_failure;
            }
            std::cerr << "unexpected positional argument: " << argument << std::endl;
            return ParseResult::exit_failure;
        }

        if (option_values.contains("help"))
        {
            PrintHelp(application_name);
            return ParseResult::exit_success;
        }

        return ParseResult::ok;
    }
}

int Loader::Run(IApplication& app, int argc, char* argv[])
{
    const std::string application_name = app.GetName();
    std::map<std::string, std::string> option_values;
    const ParseResult parse_result = ParseArguments(argc, argv, application_name, option_values);
    if (ParseResult::ok != parse_result)
        return ParseResult::exit_success == parse_result ? 0 : 1;

    CommonConfiguration common_configuration = BuildDefaultCommonConfiguration(app);
    std::unique_ptr<IApplicationConfiguration> application_configuration;
    const auto config_option = option_values.find("config");
    const std::string override_path =
        config_option != option_values.end()
            ? config_option->second
            : "conf/" + application_name + "_my.json";
    const std::string main_path = "conf/" + application_name + ".json";

    if (!LoadConfiguration(common_configuration, main_path, override_path, application_configuration, app))
    {
        return 1;
    }

    if (!SetupLogging(application_name, common_configuration))
    {
        return 1;
    }

    if (!Initialize(app, common_configuration, *application_configuration))
    {
        return 1;
    }

    return 0;
}

bool Loader::Initialize(IApplication& app,
                        const CommonConfiguration& common_configuration,
                        const IApplicationConfiguration& application_configuration)
{
    mCommandRegistry.Clear();
    RegisterCommand(
        "exit",
        "Stop the application and exit the loader",
        [this](const CommandArguments&) {
            RequestStop();
            return CommandExecutionStatus::exit_requested;
        });

    app.SetRuntime(*this);
    if (!app.Configure(common_configuration, application_configuration))
    {
        spdlog::error("application configure failed");
        return false;
    }

    RuntimeState runtime_state;
    mRuntimeState = &runtime_state;

    std::thread application_thread([&app, &runtime_state]() {
        try
        {
            app.Load();
            app.Start();

            {
                std::lock_guard lock(runtime_state.mutex);
                runtime_state.start_completed = true;
            }
            runtime_state.condition.notify_all();

            std::unique_lock lock(runtime_state.mutex);
            runtime_state.condition.wait(lock, [&runtime_state] { return runtime_state.stop_requested; });
            lock.unlock();

            app.Stop();
            app.Unload();
        }
        catch (const std::exception& ex)
        {
            {
                std::lock_guard lock(runtime_state.mutex);
                runtime_state.start_completed = true;
                runtime_state.stop_requested = true;
                runtime_state.runtime_failed = true;
                runtime_state.error_message = ex.what();
            }
            runtime_state.condition.notify_all();
        }
        catch (...)
        {
            {
                std::lock_guard lock(runtime_state.mutex);
                runtime_state.start_completed = true;
                runtime_state.stop_requested = true;
                runtime_state.runtime_failed = true;
                runtime_state.error_message = "application lifecycle failed with unknown exception";
            }
            runtime_state.condition.notify_all();
        }
    });

    {
        std::unique_lock lock(runtime_state.mutex);
        runtime_state.condition.wait(lock, [&runtime_state] { return runtime_state.start_completed; });
        if (runtime_state.runtime_failed)
        {
            spdlog::error(runtime_state.error_message);
            lock.unlock();
            application_thread.join();
            mRuntimeState = nullptr;
            return false;
        }
    }

    for (std::string input; true;)
    {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, input))
        {
            RequestStop();
            break;
        }

        const CommandExecutionResult result = mCommandRegistry.Execute(input);
        if (result.status == CommandExecutionStatus::unknown_command)
        {
            std::cerr << "unknown command: " << result.command_name << std::endl;
            continue;
        }

        if (result.status == CommandExecutionStatus::exit_requested)
        {
            break;
        }
    }

    application_thread.join();
    mRuntimeState = nullptr;

    {
        std::lock_guard lock(runtime_state.mutex);
        if (runtime_state.runtime_failed)
        {
            spdlog::error(runtime_state.error_message);
            return false;
        }
    }

    return true;
}

bool Loader::RegisterCommand(std::string command_name,
                             std::string description,
                             CommandHandler handler)
{
    return mCommandRegistry.RegisterCommand(std::move(command_name), std::move(description), std::move(handler));
}

void Loader::RequestStop()
{
    if (mRuntimeState == nullptr)
    {
        return;
    }

    std::lock_guard lock(mRuntimeState->mutex);
    mRuntimeState->stop_requested = true;
    mRuntimeState->condition.notify_all();
}
