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
        LoaderConfig config = BuildDefaultConfig(options, app);

        Require(config.config_path == (std::filesystem::path("conf") / "gate.yaml").string(), "default config path");
        Require(config.listen.host == "127.0.0.1", "default listen host");
        Require(config.listen.port == 9000, "default listen port");
        Require(config.log.max_size == 10 * 1024 * 1024, "default log max size");
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

        LoaderConfig config;
        config.config_path = temp.path.string();
        Require(LoadYamlConfig(config, true, false), "yaml load should succeed");
        Require(config.listen.host == "0.0.0.0", "yaml listen host");
        Require(config.listen.port == 7001, "yaml listen port");
        Require(config.log.level == "debug", "yaml log level");
        Require(!config.log.console, "yaml console flag");
        Require(config.log.rotation_mode == "daily", "yaml rotation mode");
        Require(config.log.max_size == 20 * 1024 * 1024, "yaml size parsing");
        Require(config.runtime.daemon, "yaml daemon");
    }

    void TestCliOverrides()
    {
        LoaderConfig config;
        config.log.level = "info";
        config.log.console = true;
        config.log.max_files = 5;

        StartupOptions options;
        options.log_level = "warn";
        options.disable_console = true;
        options.log_max_files = 9;
        options.log_max_files_explicit = true;

        ApplyCliOverrides(config, options);

        Require(config.log.level == "warn", "cli log level");
        Require(!config.log.console, "cli console override");
        Require(config.log.max_files == 9, "cli max files override");
        Require(config.settings["log.level"] == "warn", "settings log level");
    }

    void TestValidation()
    {
        LoaderConfig config;
        config.listen.port = 0;
        Require(!ValidateConfig(config), "port zero should fail validation");

        config.listen.port = 8080;
        config.log.max_files = 0;
        Require(!ValidateConfig(config), "max files zero should fail validation");
    }

    void TestCopyToContext()
    {
        LoaderConfig config;
        config.listen.host = "192.168.0.10";
        config.listen.port = 9100;
        config.log.level = "trace";
        config.runtime.daemon = true;

        ApplicationContext context;
        CopyToContext(config, context);

        Require(context.listen_host == "192.168.0.10", "context listen host");
        Require(context.listen_port == 9100, "context listen port");
        Require(context.log_level == "trace", "context log level");
        Require(context.daemon, "context daemon");
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
        TestCopyToContext();
        std::cout << "loader_config_test: ok" << std::endl;
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "loader_config_test: " << ex.what() << std::endl;
        return 1;
    }
}
