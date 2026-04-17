#include "gate/application.h"
#include "loader/config.h"
#include "loader/options.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
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

    TempFile WriteTempJson(const std::string& name, const std::string& content)
    {
        const auto path = std::filesystem::temp_directory_path() / name;
        std::ofstream out(path);
        out << content;
        out.close();
        return TempFile{path};
    }

    void TestDefaults()
    {
        Application app;
        StartupOptions options;
        LoaderConfiguration configuration = BuildDefaultLoaderConfiguration(options, app);

        Require(configuration.config_path == (std::filesystem::path("conf") / "gate.json").string(), "default config path");
        Require(configuration.log.rotate.max_size == 10 * 1024 * 1024, "default log max size");
    }

    void TestJsonLoad()
    {
        auto temp = WriteTempJson(
            "loader_config_test.json",
            "{\n"
            "  \"loader\": {\n"
            "    \"log\": {\n"
            "      \"level\": \"debug\",\n"
            "      \"console\": false,\n"
            "      \"rotate\": {\n"
            "        \"mode\": \"daily\",\n"
            "        \"max_size\": 20971520,\n"
            "        \"max_files\": 7,\n"
            "        \"daily_hour\": 2,\n"
            "        \"daily_minute\": 30\n"
            "      }\n"
            "    },\n"
            "    \"runtime\": {\n"
            "      \"daemon\": true\n"
            "    }\n"
            "  },\n"
            "  \"application\": {\n"
            "    \"listen\": {\n"
            "      \"host\": \"0.0.0.0\",\n"
            "      \"port\": 7001\n"
            "    }\n"
            "  }\n"
            "}\n");

        std::string document;
        std::string error;
        Require(LoadConfigurationDocument(temp.path.string(), document, error), "document load should succeed");

        LoaderConfiguration loader;
        Require(ApplyLoaderConfiguration(loader, document, error), "loader config should apply");
        Require(loader.log.level == "debug", "json log level");
        Require(!loader.log.console, "json console flag");
        Require(loader.log.rotate.mode == "daily", "json rotation mode");
        Require(loader.log.rotate.max_size == 20 * 1024 * 1024, "json size parsing");
        Require(loader.runtime.daemon, "json daemon");

        GateConfiguration application;
        Require(application.LoadFromJson(document, error), "application config should apply");
        Require(application.listen.host == "0.0.0.0", "json listen host");
        Require(application.listen.port == 7001, "json listen port");
    }

    void TestCliOverrides()
    {
        LoaderConfiguration context;
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
    }

    void TestValidation()
    {
        LoaderConfiguration context;
        std::string error;
        context.log.rotate.max_files = 0;
        Require(!ValidateLoaderConfiguration(context, error), "max files zero should fail validation");
    }
}

int main()
{
    try
    {
        TestDefaults();
        TestJsonLoad();
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
