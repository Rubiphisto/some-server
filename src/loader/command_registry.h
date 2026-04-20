#pragma once

#include "framework/application/runtime.h"

#include <map>
#include <mutex>
#include <string>
#include <string_view>

struct CommandExecutionResult
{
    CommandExecutionStatus status = CommandExecutionStatus::handled;
    std::string command_name;
};

class CommandRegistry
{
public:
    bool RegisterCommand(std::string command_name, std::string description, CommandHandler handler);
    CommandExecutionResult Execute(std::string_view input) const;
    void Clear();

private:
    struct CommandDefinition
    {
        std::string description;
        CommandHandler handler;
    };

    mutable std::mutex mMutex;
    std::map<std::string, CommandDefinition, std::less<>> mCommands;
};
