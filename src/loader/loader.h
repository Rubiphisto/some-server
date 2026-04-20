#pragma once

#include "command_registry.h"
#include "framework/application/application.h"

#include <condition_variable>
#include <mutex>
#include <string>

class Loader : public IApplicationRuntime
{
public:
    int Run(IApplication& app, int argc, char* argv[]);
    bool RegisterCommand(std::string command_name,
                         std::string description,
                         CommandHandler handler) override;
    void RequestStop() override;

private:
    struct RuntimeState
    {
        std::mutex mutex;
        std::condition_variable condition;
        bool start_completed = false;
        bool stop_requested = false;
        bool runtime_failed = false;
        std::string error_message;
    };

    bool Initialize(IApplication& app,
                    const CommonConfiguration& common_configuration,
                    const IApplicationConfiguration& application_configuration);

    CommandRegistry mCommandRegistry;
    RuntimeState* mRuntimeState = nullptr;
};
