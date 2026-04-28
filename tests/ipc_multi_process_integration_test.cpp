#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace
{
using Clock = std::chrono::steady_clock;

void Require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

class ScopedEtcd
{
public:
    explicit ScopedEtcd(std::filesystem::path data_dir)
        : mDataDir(std::move(data_dir))
    {
    }

    ~ScopedEtcd()
    {
        Stop();
        std::error_code ignored;
        std::filesystem::remove_all(mDataDir, ignored);
    }

    void Start()
    {
        const std::string client_url = "http://127.0.0.1:32379";
        const std::string peer_url = "http://127.0.0.1:32380";

        const pid_t pid = fork();
        if (pid == -1)
        {
            throw std::runtime_error("fork failed for etcd");
        }
        if (pid == 0)
        {
            execlp(
                "etcd",
                "etcd",
                "--name",
                "ipc-test",
                "--data-dir",
                mDataDir.c_str(),
                "--listen-client-urls",
                client_url.c_str(),
                "--advertise-client-urls",
                client_url.c_str(),
                "--listen-peer-urls",
                peer_url.c_str(),
                "--initial-advertise-peer-urls",
                peer_url.c_str(),
                "--initial-cluster",
                "ipc-test=http://127.0.0.1:32380",
                "--initial-cluster-state",
                "new",
                static_cast<char*>(nullptr));
            _exit(127);
        }

        mPid = pid;

        for (int attempt = 0; attempt < 50; ++attempt)
        {
            const int rc = std::system("etcdctl --endpoints=127.0.0.1:32379 endpoint health >/dev/null 2>&1");
            if (rc == 0)
            {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        throw std::runtime_error("etcd did not become healthy");
    }

    void Stop()
    {
        if (mPid > 0)
        {
            kill(mPid, SIGTERM);
            waitpid(mPid, nullptr, 0);
            mPid = -1;
        }
    }

private:
    pid_t mPid = -1;
    std::filesystem::path mDataDir;
};

class ChildProcess
{
public:
    ChildProcess(std::string executable, std::filesystem::path config_path)
        : mExecutable(std::move(executable))
        , mConfigPath(std::move(config_path))
    {
    }

    ~ChildProcess()
    {
        Stop();
    }

    void Start()
    {
        int stdin_pipe[2];
        int stdout_pipe[2];
        if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0)
        {
            throw std::runtime_error("pipe creation failed");
        }

        const pid_t pid = fork();
        if (pid == -1)
        {
            throw std::runtime_error("fork failed");
        }
        if (pid == 0)
        {
            dup2(stdin_pipe[0], STDIN_FILENO);
            dup2(stdout_pipe[1], STDOUT_FILENO);
            dup2(stdout_pipe[1], STDERR_FILENO);
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);

            execl(
                mExecutable.c_str(),
                mExecutable.c_str(),
                "-c",
                mConfigPath.c_str(),
                static_cast<char*>(nullptr));
            _exit(127);
        }

        mPid = pid;
        mStdinFd = stdin_pipe[1];
        mStdoutFd = stdout_pipe[0];
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
    }

    void Stop()
    {
        if (mPid <= 0)
        {
            return;
        }

        if (mStdinFd >= 0)
        {
            Send("exit");
            close(mStdinFd);
            mStdinFd = -1;
        }

        waitpid(mPid, nullptr, 0);
        if (mStdoutFd >= 0)
        {
            close(mStdoutFd);
            mStdoutFd = -1;
        }
        mPid = -1;
    }

    void Send(const std::string& command) const
    {
        const std::string line = command + "\n";
        if (write(mStdinFd, line.data(), line.size()) < 0)
        {
            throw std::runtime_error("failed to write command");
        }
    }

    bool WaitFor(std::string_view needle, const std::chrono::milliseconds timeout)
    {
        const auto deadline = Clock::now() + timeout;
        while (Clock::now() < deadline)
        {
            if (mOutput.find(needle) != std::string::npos)
            {
                return true;
            }

            pollfd fd{mStdoutFd, POLLIN, 0};
            const int rc = poll(&fd, 1, 100);
            if (rc > 0 && (fd.revents & POLLIN))
            {
                char buffer[4096];
                const ssize_t bytes = read(mStdoutFd, buffer, sizeof(buffer));
                if (bytes > 0)
                {
                    mOutput.append(buffer, static_cast<std::size_t>(bytes));
                    continue;
                }
            }
        }

        return mOutput.find(needle) != std::string::npos;
    }

    const std::string& Output() const
    {
        return mOutput;
    }

private:
    std::string mExecutable;
    std::filesystem::path mConfigPath;
    pid_t mPid = -1;
    int mStdinFd = -1;
    int mStdoutFd = -1;
    std::string mOutput;
};

std::filesystem::path WriteRelayConfig(
    const std::filesystem::path& dir,
    const std::string& prefix)
{
    const auto path = dir / "relay.json";
    std::ofstream out(path);
    out << "{\n"
           "  \"log\": {\n"
           "    \"file\": \"" << (dir / "relay.log").string() << "\",\n"
           "    \"error_file\": \"" << (dir / "relay.error.log").string() << "\",\n"
           "    \"console\": true\n"
           "  },\n"
           "  \"relay\": {\n"
           "    \"instance_id\": 1,\n"
           "    \"listen\": { \"host\": \"127.0.0.1\", \"port\": 9200 },\n"
           "    \"discovery\": {\n"
           "      \"endpoints\": [\"127.0.0.1:32379\"],\n"
           "      \"prefix\": \"" << prefix << "\",\n"
           "      \"lease_ttl_seconds\": 4\n"
           "    }\n"
           "  }\n"
           "}\n";
    return path;
}

std::filesystem::path WriteGameConfig(
    const std::filesystem::path& dir,
    const std::string& file_name,
    const std::string& prefix,
    const std::uint32_t instance_id,
    const std::uint16_t port)
{
    const auto path = dir / file_name;
    std::ofstream out(path);
    out << "{\n"
           "  \"log\": {\n"
           "    \"file\": \"" << (dir / (file_name + ".log")).string() << "\",\n"
           "    \"error_file\": \"" << (dir / (file_name + ".error.log")).string() << "\",\n"
           "    \"console\": true\n"
           "  },\n"
           "  \"game\": {\n"
           "    \"instance_id\": " << instance_id << ",\n"
           "    \"listen\": { \"host\": \"127.0.0.1\", \"port\": " << port << " },\n"
           "    \"discovery\": {\n"
           "      \"endpoints\": [\"127.0.0.1:32379\"],\n"
           "      \"prefix\": \"" << prefix << "\",\n"
           "      \"lease_ttl_seconds\": 4\n"
           "    }\n"
           "  }\n"
           "}\n";
    return path;
}

void WaitOrThrow(ChildProcess& process, std::string_view needle, const std::string& message)
{
    if (!process.WaitFor(needle, std::chrono::seconds(10)))
    {
        throw std::runtime_error(message + "\noutput:\n" + process.Output());
    }
}

void PollCommandUntil(
    ChildProcess& process,
    const std::string& command,
    std::string_view needle,
    const std::string& message)
{
    for (int attempt = 0; attempt < 20; ++attempt)
    {
        process.Send(command);
        if (process.WaitFor(needle, std::chrono::milliseconds(500)))
        {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    throw std::runtime_error(message + "\noutput:\n" + process.Output());
}

void TestRelayFirstMessaging()
{
    const auto suffix = std::to_string(Clock::now().time_since_epoch().count());
    const auto temp_dir = std::filesystem::temp_directory_path() / ("ipc_multi_" + suffix);
    std::filesystem::create_directories(temp_dir);
    const auto etcd_dir = temp_dir / "etcd";
    std::filesystem::create_directories(etcd_dir);

    ScopedEtcd etcd(etcd_dir);
    etcd.Start();

    const std::string prefix = "/some_server/ipc/test/multi/" + suffix;
    const auto relay_config = WriteRelayConfig(temp_dir, prefix);
    const auto game1_config = WriteGameConfig(temp_dir, "game1.json", prefix, 1, 9100);
    const auto game2_config = WriteGameConfig(temp_dir, "game2.json", prefix, 2, 9101);

    ChildProcess relay("./relay", relay_config);
    ChildProcess game1("./game", game1_config);
    ChildProcess game2("./game", game2_config);

    relay.Start();
    game1.Start();
    game2.Start();

    WaitOrThrow(relay, "> ", "relay did not reach command prompt");
    WaitOrThrow(game1, "> ", "game1 did not reach command prompt");
    WaitOrThrow(game2, "> ", "game2 did not reach command prompt");

    relay.Send("ipc_refresh");
    game1.Send("ipc_refresh");
    game2.Send("ipc_refresh");
    WaitOrThrow(relay, "relay ipc refresh: members=3", "relay refresh failed");
    WaitOrThrow(game1, "game ipc refresh: members=3", "game1 refresh failed");
    WaitOrThrow(game2, "game ipc refresh: members=3", "game2 refresh failed");

    relay.Send("ipc_connect 10 1");
    relay.Send("ipc_connect 10 2");
    WaitOrThrow(relay, "relay ipc connect: ok", "relay connect failed");
    std::this_thread::sleep_for(std::chrono::seconds(1));

    PollCommandUntil(relay, "ipc_links", "relay ipc links: count=2", "relay links not healthy");
    PollCommandUntil(game1, "ipc_links", "game ipc links: count=1", "game1 relay link not healthy");
    PollCommandUntil(game2, "ipc_links", "game ipc links: count=1", "game2 relay link not healthy");

    game1.Send("ipc_send_process 2 process-test");
    WaitOrThrow(game1, "game ipc process send: ok", "process send failed");
    PollCommandUntil(game2, "ipc_status", "process_dispatch_count=1", "process dispatch did not arrive");

    game2.Send("ipc_bind_player_local 1001");
    game1.Send("ipc_bind_player_remote 1001 2");
    WaitOrThrow(game2, "game ipc bind player local: ok", "local player bind failed");
    WaitOrThrow(game1, "game ipc bind player remote: ok", "remote player bind failed");

    game1.Send("ipc_send_player 1001 player-test");
    WaitOrThrow(game1, "game ipc player send: ok", "player send failed");
    PollCommandUntil(game2, "ipc_status", "player_dispatch_count=1", "player dispatch did not arrive");
    WaitOrThrow(game2, "last_player_id=1001", "player dispatch target mismatch");

    game1.Send("ipc_broadcast_service broadcast-test 1");
    WaitOrThrow(game1, "game ipc broadcast service: ok", "service broadcast failed");
    PollCommandUntil(game1, "ipc_status", "local_service_dispatch_count=1", "game1 service broadcast local dispatch missing");
    PollCommandUntil(game2, "ipc_status", "local_service_dispatch_count=1", "game2 service broadcast dispatch missing");
}
} // namespace

int main()
{
    try
    {
        TestRelayFirstMessaging();
        std::cout << "ipc_multi_process_integration_test: ok" << std::endl;
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "ipc_multi_process_integration_test: " << ex.what() << std::endl;
        return 1;
    }
}
