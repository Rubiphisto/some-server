#pragma once

#include <functional>
#include <string>
#include <vector>

enum class CommandExecutionStatus
{
    handled,
    exit_requested,
    unknown_command
};

using CommandArguments = std::vector<std::string>;
using CommandHandler = std::function<CommandExecutionStatus(const CommandArguments&)>;

class IApplicationRuntime
{
public:
    virtual ~IApplicationRuntime() = default;
    virtual bool RegisterCommand(std::string command_name,
                                 std::string description,
                                 CommandHandler handler) = 0;
    virtual void RequestStop() = 0;
};
