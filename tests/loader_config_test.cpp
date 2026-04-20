#include "gate/application.h"
#include "loader/command_registry.h"
#include "loader/config.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <chrono>

namespace
{
    struct ScopedCurrentPath
    {
        std::filesystem::path original_path = std::filesystem::current_path();

        explicit ScopedCurrentPath(const std::filesystem::path& path)
        {
            std::filesystem::current_path(path);
        }

        ~ScopedCurrentPath()
        {
            std::error_code ignored;
            std::filesystem::current_path(original_path, ignored);
        }
    };

    struct TempDirectory
    {
        std::filesystem::path path;

        ~TempDirectory()
        {
            std::error_code ignored;
            if (!path.empty())
            {
                std::filesystem::remove_all(path, ignored);
            }
        }
    };

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    TempDirectory CreateTempDirectory(const std::string& name)
    {
        const auto suffix = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        const auto path = std::filesystem::temp_directory_path() / (name + "_" + suffix);
        std::filesystem::create_directories(path);
        return TempDirectory{path};
    }

    void WriteJson(const std::filesystem::path& path, const std::string& content)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path);
        out << content;
    }

    void TestDefaults()
    {
        Application app;
        CommonConfiguration configuration = BuildDefaultCommonConfiguration(app);

        Require(std::string{"conf/"} + app.GetName() + ".json" == "conf/gate.json",
                "main config path");
        Require(configuration.log.rotate.max_size == 10 * 1024 * 1024, "default log max size");
    }

    void TestJsonLoad()
    {
        auto temp_directory = CreateTempDirectory("loader_config_test");
        WriteJson(
            temp_directory.path / "conf" / "gate.json",
            "{\n"
            "  \"log\": {\n"
            "    \"level\": \"debug\",\n"
            "    \"console\": true,\n"
            "    \"rotate\": {\n"
            "      \"mode\": \"size\",\n"
            "      \"max_size\": 20971520,\n"
            "      \"max_files\": 7,\n"
            "      \"daily_hour\": 2,\n"
            "      \"daily_minute\": 30\n"
            "    }\n"
            "  },\n"
            "  \"gate\": {\n"
            "    \"listen\": {\n"
            "      \"host\": \"0.0.0.0\",\n"
            "      \"port\": 7000\n"
            "    }\n"
            "  }\n"
            "}\n");
        const auto override_path = temp_directory.path / "override.json";
        WriteJson(
            override_path,
            "{\n"
            "  \"log\": {\n"
            "    \"console\": false,\n"
            "    \"rotate\": {\n"
            "      \"mode\": \"daily\",\n"
            "      \"max_files\": 9\n"
            "    }\n"
            "  },\n"
            "  \"gate\": {\n"
            "    \"listen\": {\n"
            "      \"port\": 7001\n"
            "    }\n"
            "  }\n"
            "}\n");

        Application app;
        CommonConfiguration common_configuration = BuildDefaultCommonConfiguration(app);
        std::unique_ptr<IApplicationConfiguration> application;
        const ScopedCurrentPath scoped_current_path(temp_directory.path);
        const std::string main_path = "conf/" + app.GetName() + ".json";
        Require(LoadConfiguration(common_configuration, main_path, override_path.string(), application, app),
                "configs should load");
        auto* gate_configuration = dynamic_cast<GateConfiguration*>(application.get());
        Require(gate_configuration != nullptr, "gate configuration type");
        Require(common_configuration.log.level == "debug", "json log level");
        Require(!common_configuration.log.console, "override console flag");
        Require(common_configuration.log.rotate.mode == "daily", "json rotation mode");
        Require(common_configuration.log.rotate.max_size == 20 * 1024 * 1024, "json size parsing");
        Require(common_configuration.log.rotate.max_files == 9, "override max files");
        Require(gate_configuration->listen.host == "0.0.0.0", "main listen host should remain");
        Require(gate_configuration->listen.port == 7001, "override listen port");
    }

    void TestDefaultOverridePath()
    {
        auto temp_directory = CreateTempDirectory("loader_config_default_override_test");
        WriteJson(
            temp_directory.path / "conf" / "gate.json",
            "{\n"
            "  \"log\": {\n"
            "    \"level\": \"warn\",\n"
            "    \"console\": true\n"
            "  },\n"
            "  \"gate\": {\n"
            "    \"listen\": {\n"
            "      \"port\": 7000\n"
            "    }\n"
            "  }\n"
            "}\n");
        WriteJson(
            temp_directory.path / "conf" / "gate_my.json",
            "{\n"
            "  \"log\": {\n"
            "    \"console\": false\n"
            "  },\n"
            "  \"gate\": {\n"
            "    \"listen\": {\n"
            "      \"port\": 7002\n"
            "    }\n"
            "  }\n"
            "}\n");

        Application app;
        CommonConfiguration common_configuration = BuildDefaultCommonConfiguration(app);
        std::unique_ptr<IApplicationConfiguration> application;
        const ScopedCurrentPath scoped_current_path(temp_directory.path);
        const std::string main_path = "conf/" + app.GetName() + ".json";
        const std::string default_override_path = "conf/" + app.GetName() + "_my.json";
        Require(LoadConfiguration(common_configuration, main_path, default_override_path, application, app),
                "default override should be applied");
        Require(common_configuration.log.level == "warn", "main config should still load");
        Require(!common_configuration.log.console, "default override should update common config");
        auto* gate_configuration = dynamic_cast<GateConfiguration*>(application.get());
        Require(gate_configuration != nullptr, "gate configuration type");
        Require(gate_configuration->listen.port == 7002, "default override should update application config");
    }

    void TestMissingOverrideIsIgnored()
    {
        auto temp_directory = CreateTempDirectory("loader_config_missing_override_test");
        WriteJson(
            temp_directory.path / "conf" / "gate.json",
            "{\n"
            "  \"log\": {\n"
            "    \"level\": \"warn\"\n"
            "  }\n"
            "}\n");

        Application app;
        CommonConfiguration common_configuration = BuildDefaultCommonConfiguration(app);
        std::unique_ptr<IApplicationConfiguration> application;
        const ScopedCurrentPath scoped_current_path(temp_directory.path);
        const std::string main_path = "conf/" + app.GetName() + ".json";
        Require(LoadConfiguration(common_configuration, main_path, "missing.json", application, app),
                "missing override should be ignored");
        Require(common_configuration.log.level == "warn", "main config should still load");
    }

    void TestValidation()
    {
        CommonConfiguration context;
        std::string error;
        context.log.rotate.max_files = 0;
        Require(!ValidateCommonConfiguration(context, error), "max files zero should fail validation");
    }

    void TestCommandRegistry()
    {
        CommandRegistry command_registry;
        Require(command_registry.RegisterCommand(
                    "exit",
                    "Stop the application and exit the loader",
                    [](const CommandArguments&) { return CommandExecutionStatus::exit_requested; }),
                "register exit command");
        Require(!command_registry.RegisterCommand(
                    "exit",
                    "duplicate",
                    [](const CommandArguments&) { return CommandExecutionStatus::handled; }),
                "duplicate command should fail");

        const CommandExecutionResult empty_result = command_registry.Execute("   ");
        Require(empty_result.status == CommandExecutionStatus::handled, "empty command should be ignored");

        const CommandExecutionResult unknown_result = command_registry.Execute("status");
        Require(unknown_result.status == CommandExecutionStatus::unknown_command, "unknown command");
        Require(unknown_result.command_name == "status", "unknown command name");

        const CommandExecutionResult exit_result = command_registry.Execute("exit now");
        Require(exit_result.status == CommandExecutionStatus::exit_requested, "exit command");
        Require(exit_result.command_name == "exit", "exit command name");
    }
}

int main()
{
    try
    {
        TestDefaults();
        TestJsonLoad();
        TestDefaultOverridePath();
        TestMissingOverrideIsIgnored();
        TestValidation();
        TestCommandRegistry();
        std::cout << "loader_config_test: ok" << std::endl;
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "loader_config_test: " << ex.what() << std::endl;
        return 1;
    }
}
