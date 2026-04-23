#pragma once

#include <string>
#include <utility>

namespace ipc
{
struct Result
{
    bool ok = false;
    std::string message;

    static Result Success() { return Result{true, {}}; }
    static Result Failure(std::string error) { return Result{false, std::move(error)}; }
};

using SendResult = Result;
using DispatchResult = Result;
} // namespace ipc
