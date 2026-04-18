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
        LoaderConfiguration configuration = BuildDefaultLoaderConfiguration(app);

        Require(configuration.config_path ==
                    (std::filesystem::current_path() / "conf" / "gate.json").lexically_normal().string(),
                "default config path");
        Require(configuration.log.rotate.max_size == 10 * 1024 * 1024, "default log max size");
    }

    void TestJsonLoad()
    {
        auto main_config = WriteTempJson(
            "loader_config_main_test.json",
            "{\n"
            "  \"loader\": {\n"
            "    \"log\": {\n"
            "      \"level\": \"debug\",\n"
            "      \"console\": true,\n"
            "      \"rotate\": {\n"
            "        \"mode\": \"size\",\n"
            "        \"max_size\": 20971520,\n"
            "        \"max_files\": 7,\n"
            "        \"daily_hour\": 2,\n"
            "        \"daily_minute\": 30\n"
            "      }\n"
            "    }\n"
            "  },\n"
            "  \"application\": {\n"
            "    \"listen\": {\n"
            "      \"host\": \"0.0.0.0\",\n"
            "      \"port\": 7000\n"
            "    }\n"
            "  }\n"
            "}\n");
        auto override_config = WriteTempJson(
            "loader_config_override_test.json",
            "{\n"
            "  \"loader\": {\n"
            "    \"log\": {\n"
            "      \"console\": false,\n"
            "      \"rotate\": {\n"
            "        \"mode\": \"daily\",\n"
            "        \"max_files\": 9\n"
            "      }\n"
            "    }\n"
            "  },\n"
            "  \"application\": {\n"
            "    \"listen\": {\n"
            "      \"port\": 7001\n"
            "    }\n"
            "  }\n"
            "}\n");

        std::string error;
        Application app;
        LoaderConfiguration loader = BuildDefaultLoaderConfiguration(app);
        GateConfiguration application;
        Require(ApplyConfigurationDocument(loader, application, main_config.path.string(), error),
                "main config should apply");
        Require(ApplyConfigurationDocument(loader, application, override_config.path.string(), error),
                "override config should apply");
        Require(loader.log.level == "debug", "json log level");
        Require(!loader.log.console, "override console flag");
        Require(loader.log.rotate.mode == "daily", "json rotation mode");
        Require(loader.log.rotate.max_size == 20 * 1024 * 1024, "json size parsing");
        Require(loader.log.rotate.max_files == 9, "override max files");
        Require(application.listen.host == "0.0.0.0", "main listen host should remain");
        Require(application.listen.port == 7001, "override listen port");
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
