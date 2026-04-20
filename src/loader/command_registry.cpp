#include "command_registry.h"

#include <sstream>

bool CommandRegistry::RegisterCommand(std::string command_name,
                                      std::string description,
                                      CommandHandler handler)
{
    if (command_name.empty() || !handler)
    {
        return false;
    }

    std::lock_guard lock(mMutex);
    return mCommands.emplace(std::move(command_name),
                             CommandDefinition{std::move(description), std::move(handler)})
        .second;
}

CommandExecutionResult CommandRegistry::Execute(std::string_view input) const
{
    std::istringstream stream{std::string{input}};
    std::string command_name;
    if (!(stream >> command_name))
    {
        return {};
    }

    CommandArguments arguments;
    for (std::string argument; stream >> argument;)
    {
        arguments.push_back(std::move(argument));
    }

    CommandHandler handler;
    {
        std::lock_guard lock(mMutex);
        const auto command = mCommands.find(command_name);
        if (command == mCommands.end())
        {
            return {CommandExecutionStatus::unknown_command, std::move(command_name)};
        }
        handler = command->second.handler;
    }
    return {handler(arguments), std::move(command_name)};
}

void CommandRegistry::Clear()
{
    std::lock_guard lock(mMutex);
    mCommands.clear();
}
