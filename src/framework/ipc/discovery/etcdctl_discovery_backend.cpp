#include "etcd_discovery_backend.h"

#include <glaze/glaze.hpp>

#include <array>
#include <cstdio>
#include <sstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <atomic>
#include <csignal>
#include <mutex>

namespace ipc::detail
{
struct BackendJsonLeaseGrantResponse
{
    std::uint64_t ID = 0;
};
}

namespace ipc
{
namespace
{
bool ExtractNextJsonObject(std::string& buffer, std::string& object)
{
    bool in_string = false;
    bool escape = false;
    int depth = 0;
    std::size_t start = std::string::npos;

    for (std::size_t index = 0; index < buffer.size(); ++index)
    {
        const char ch = buffer[index];
        if (start == std::string::npos)
        {
            if (ch == '{')
            {
                start = index;
                depth = 1;
            }
            continue;
        }

        if (in_string)
        {
            if (escape)
            {
                escape = false;
                continue;
            }
            if (ch == '\\')
            {
                escape = true;
                continue;
            }
            if (ch == '"')
            {
                in_string = false;
            }
            continue;
        }

        if (ch == '"')
        {
            in_string = true;
            continue;
        }
        if (ch == '{')
        {
            ++depth;
            continue;
        }
        if (ch == '}')
        {
            --depth;
            if (depth == 0)
            {
                object = buffer.substr(start, index - start + 1);
                buffer.erase(0, index + 1);
                return true;
            }
        }
    }

    if (start == std::string::npos && buffer.size() > 4096)
    {
        buffer.clear();
    }
    return false;
}

std::string ShellEscape(const std::string& value)
{
    std::string escaped = "'";
    for (char ch : value)
    {
        if (ch == '\'')
        {
            escaped += "'\"'\"'";
        }
        else
        {
            escaped += ch;
        }
    }
    escaped += "'";
    return escaped;
}

std::string LeaseIdArg(const std::uint64_t lease_id)
{
    std::ostringstream stream;
    stream << std::hex << std::nouppercase << lease_id;
    return stream.str();
}
} // namespace
} // namespace ipc

namespace glz
{
template <>
struct meta<ipc::detail::BackendJsonLeaseGrantResponse>
{
    using T = ipc::detail::BackendJsonLeaseGrantResponse;
    static constexpr auto value = glz::object("ID", &T::ID);
};
} // namespace glz

namespace ipc
{
namespace
{
class EtcdctlDiscoveryBackend final : public IEtcdDiscoveryBackend
{
public:
    explicit EtcdctlDiscoveryBackend(EtcdDiscoveryOptions options)
        : mOptions(std::move(options))
    {
    }

    ~EtcdctlDiscoveryBackend() override
    {
        StopWatch();
    }

    Result GrantLease(const std::uint32_t ttl_seconds, std::uint64_t& lease_id) override
    {
        std::string output;
        const std::string command =
            ShellEscape(mOptions.etcdctl_path) + " --endpoints=" + ShellEscape(EndpointsArg()) + " lease grant " +
            std::to_string(ttl_seconds) + " -w json 2>/dev/null";
        const Result run = RunCommand(command, output);
        if (!run.ok)
        {
            return run;
        }

        ipc::detail::BackendJsonLeaseGrantResponse response;
        if (auto result = glz::read<glz::opts{.error_on_unknown_keys = false}>(response, output))
        {
            return Result::Failure(glz::format_error(result, output));
        }
        if (response.ID == 0)
        {
            return Result::Failure("etcd lease grant returned empty lease id");
        }

        lease_id = response.ID;
        return Result::Success();
    }

    Result Put(const std::string& key, const std::string& value, const std::uint64_t lease_id) override
    {
        std::string output;
        std::string command =
            ShellEscape(mOptions.etcdctl_path) + " --endpoints=" + ShellEscape(EndpointsArg()) + " put " +
            ShellEscape(key) + " " + ShellEscape(value);
        if (lease_id != 0)
        {
            command += " --lease=" + LeaseIdArg(lease_id);
        }
        command += " -w json 2>/dev/null";
        return RunCommand(command, output);
    }

    Result KeepAliveOnce(const std::uint64_t lease_id) override
    {
        std::string output;
        const std::string command =
            ShellEscape(mOptions.etcdctl_path) + " --endpoints=" + ShellEscape(EndpointsArg()) +
            " lease keep-alive " + LeaseIdArg(lease_id) + " --once -w json 2>/dev/null";
        return RunCommand(command, output);
    }

    Result Delete(const std::string& key) override
    {
        std::string output;
        const std::string command =
            ShellEscape(mOptions.etcdctl_path) + " --endpoints=" + ShellEscape(EndpointsArg()) + " del " +
            ShellEscape(key) + " -w json 2>/dev/null";
        return RunCommand(command, output);
    }

    Result GetPrefix(const std::string& key_prefix, std::string& output) override
    {
        const std::string command =
            ShellEscape(mOptions.etcdctl_path) + " --endpoints=" + ShellEscape(EndpointsArg()) + " get " +
            ShellEscape(key_prefix) + " --prefix -w json 2>/dev/null";
        return RunCommand(command, output);
    }

    Result StartWatchPrefix(const std::string& key_prefix) override
    {
        std::scoped_lock lock(mMutex);
        if (mWatchPipeFd != -1 || mWatchPid > 0)
        {
            return Result::Success();
        }

        int fds[2];
        if (pipe(fds) != 0)
        {
            return Result::Failure("failed to create watch pipe");
        }

        const pid_t pid = fork();
        if (pid == -1)
        {
            close(fds[0]);
            close(fds[1]);
            return Result::Failure("failed to fork watch process");
        }

        if (pid == 0)
        {
            dup2(fds[1], STDOUT_FILENO);
            dup2(fds[1], STDERR_FILENO);
            close(fds[0]);
            close(fds[1]);
            const std::string endpoints_arg = "--endpoints=" + EndpointsArg();
            std::vector<char*> argv;
            argv.push_back(const_cast<char*>(mOptions.etcdctl_path.c_str()));
            argv.push_back(const_cast<char*>(endpoints_arg.c_str()));
            argv.push_back(const_cast<char*>("watch"));
            argv.push_back(const_cast<char*>(key_prefix.c_str()));
            argv.push_back(const_cast<char*>("--prefix"));
            argv.push_back(const_cast<char*>("-w"));
            argv.push_back(const_cast<char*>("json"));
            argv.push_back(nullptr);
            execvp(mOptions.etcdctl_path.c_str(), argv.data());
            _exit(127);
        }

        close(fds[1]);
        mWatchPipeFd = fds[0];
        mWatchPid = pid;
        mWatchBuffer.clear();
        mWatchRunning.store(true);
        return Result::Success();
    }

    WatchPollResult WaitForWatchEvent() override
    {
        while (true)
        {
            int watch_pipe_fd = -1;
            {
                std::scoped_lock lock(mMutex);
                if (mWatchPipeFd == -1)
                {
                    return WatchPollResult{WatchPollKind::stopped, {}};
                }
                if (ExtractNextJsonObject(mWatchBuffer, mScratchObject))
                {
                    return WatchPollResult{WatchPollKind::event, {}};
                }
                watch_pipe_fd = mWatchPipeFd;
            }

            char chunk[256];
            const ssize_t read_count = read(watch_pipe_fd, chunk, sizeof(chunk));
            if (read_count <= 0)
            {
                std::scoped_lock lock(mMutex);
                StopWatchUnlocked();
                return WatchPollResult{WatchPollKind::stream_closed, {}};
            }

            std::scoped_lock lock(mMutex);
            mWatchBuffer.append(chunk, static_cast<std::size_t>(read_count));
        }
    }

    void StopWatch() override
    {
        std::scoped_lock lock(mMutex);
        StopWatchUnlocked();
    }

    bool WatchRunning() const override
    {
        return mWatchRunning.load();
    }

private:
    std::string EndpointsArg() const
    {
        std::ostringstream stream;
        for (std::size_t index = 0; index < mOptions.endpoints.size(); ++index)
        {
            if (index > 0)
            {
                stream << ",";
            }
            stream << mOptions.endpoints[index];
        }
        return stream.str();
    }

    Result RunCommand(const std::string& command, std::string& output) const
    {
        output.clear();
        FILE* pipe = popen(command.c_str(), "r");
        if (pipe == nullptr)
        {
            return Result::Failure("failed to start command");
        }

        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
        {
            output.append(buffer);
        }

        const int rc = pclose(pipe);
        if (rc != 0)
        {
            return Result::Failure("command failed: " + command + "\n" + output);
        }
        return Result::Success();
    }

    void StopWatchUnlocked()
    {
        if (mWatchPid > 0)
        {
            kill(mWatchPid, SIGTERM);
            waitpid(mWatchPid, nullptr, 0);
            mWatchPid = -1;
        }
        if (mWatchPipeFd != -1)
        {
            close(mWatchPipeFd);
            mWatchPipeFd = -1;
        }
        mWatchBuffer.clear();
        mScratchObject.clear();
        mWatchRunning.store(false);
    }

    EtcdDiscoveryOptions mOptions;
    mutable std::mutex mMutex;
    int mWatchPipeFd = -1;
    pid_t mWatchPid = -1;
    std::string mWatchBuffer;
    std::string mScratchObject;
    std::atomic<bool> mWatchRunning = false;
};
} // namespace

std::unique_ptr<IEtcdDiscoveryBackend> CreateEtcdctlDiscoveryBackend(const EtcdDiscoveryOptions& options)
{
    return std::make_unique<EtcdctlDiscoveryBackend>(options);
}
} // namespace ipc
