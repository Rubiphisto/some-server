#include "framework/application/application.h"
#include "loader/config.h"
#include "loader/options.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    class TestApplication final : public IApplication
    {
    public:
        const char8_t* GetName() const override { return u8"gate"; }
        bool Configure(const ApplicationContext&) override { return true; }
        void Load() override {}
        void Start() override {}
        void Stop() override {}
        void Unload() override {}
    };

    struct TempFile
    {
        std::filesystem::path path;

        ~TempFile()
        {
            std::error_code ignored;
            if (!path.empty())
            {
                std::filesystem::remove(path, ignored);
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

    TempFile WriteTempYaml(const std::string& name, const std::string& content)
    {
        const auto path = std::filesystem::temp_directory_path() / name;
        std::ofstream out(path);
        out << content;
        out.close();
        return TempFile{path};
    }

    void TestDefaults()
    {
        TestApplication app;
        StartupOptions options;
        ApplicationContext context = BuildDefaultContext(options, app);

        Require(context.config_path == (std::filesystem::path("conf") / "gate.yaml").string(), "default config path");
        Require(context.listen.host == "127.0.0.1", "default listen host");
        Require(context.listen.port == 9000, "default listen port");
        Require(context.log.rotate.max_size == 10 * 1024 * 1024, "default log max size");
    }

    void TestYamlLoad()
    {
        auto temp = WriteTempYaml(
            "loader_config_test.yaml",
            "listen:\n"
            "  host: 0.0.0.0\n"
            "  port: 7001\n"
            "log:\n"
            "  level: debug\n"
            "  console: false\n"
            "  rotate:\n"
            "    mode: daily\n"
            "    max_size: 20MB\n"
            "    max_files: 7\n"
            "    daily_hour: 2\n"
            "    daily_minute: 30\n"
            "runtime:\n"
            "  daemon: true\n");

        ApplicationContext context;
        context.config_path = temp.path.string();
        Require(LoadYamlIntoContext(context, true, false), "yaml load should succeed");
        Require(context.listen.host == "0.0.0.0", "yaml listen host");
        Require(context.listen.port == 7001, "yaml listen port");
        Require(context.log.level == "debug", "yaml log level");
        Require(!context.log.console, "yaml console flag");
        Require(context.log.rotate.mode == "daily", "yaml rotation mode");
        Require(context.log.rotate.max_size == 20 * 1024 * 1024, "yaml size parsing");
        Require(context.runtime.daemon, "yaml daemon");
    }

    void TestCliOverrides()
    {
        ApplicationContext context;
        context.log.level = "info";
        context.log.console = true;
        context.log.rotate.max_files = 5;

        StartupOptions options;
        options.log_level = "warn";
        options.disable_console = true;
        options.log_max_files = 9;
        options.log_max_files_explicit = true;

        ApplyCliOverrides(context, options);

        Require(context.log.level == "warn", "cli log level");
        Require(!context.log.console, "cli console override");
        Require(context.log.rotate.max_files == 9, "cli max files override");
        Require(context.settings["log.level"] == "warn", "settings log level");
    }

    void TestValidation()
    {
        ApplicationContext context;
        context.listen.port = 0;
        Require(!ValidateContext(context), "port zero should fail validation");

        context.listen.port = 8080;
        context.log.rotate.max_files = 0;
        Require(!ValidateContext(context), "max files zero should fail validation");
    }
}

int main()
{
    try
    {
        TestDefaults();
        TestYamlLoad();
        TestCliOverrides();
        TestValidation();
        std::cout << "loader_config_test: ok" << std::endl;
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "loader_config_test: " << ex.what() << std::endl;
        return 1;
    }
}
