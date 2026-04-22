#include "framework/application/application.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    struct TestConfiguration : public BaseApplicationConfiguration, public JsonApplicationConfiguration<TestConfiguration>
    {
    };

    class NullRuntime final : public IApplicationRuntime
    {
    public:
        std::vector<std::string> registered_commands;

        bool RegisterCommand(std::string command_name, std::string, CommandHandler) override
        {
            registered_commands.push_back(std::move(command_name));
            return true;
        }

        void RequestStop() override {}
    };

    class RecordingService final : public ServiceBase
    {
    public:
        RecordingService(std::string name, std::int32_t batch, bool fail_on_start, std::vector<std::string>& events)
            : ServiceBase(std::move(name), batch), mFailOnStart(fail_on_start), mEvents(events)
        {
        }

        LifecycleTask Load() override
        {
            mEvents.emplace_back(std::string(GetName()) + ":Load");
            return LifecycleTask::Completed();
        }

        LifecycleTask Start() override
        {
            mEvents.emplace_back(std::string(GetName()) + ":Start");
            if (mFailOnStart)
            {
                throw std::runtime_error(std::string(GetName()) + " start failure");
            }
            return LifecycleTask::Completed();
        }

        LifecycleTask Stop() override
        {
            mEvents.emplace_back(std::string(GetName()) + ":Stop");
            return LifecycleTask::Completed();
        }

        LifecycleTask Unload() override
        {
            mEvents.emplace_back(std::string(GetName()) + ":Unload");
            return LifecycleTask::Completed();
        }

    private:
        bool mFailOnStart = false;
        std::vector<std::string>& mEvents;
    };

    class TestApplication final : public ApplicationBase<TestConfiguration>
    {
    public:
        struct ServiceSpec
        {
            std::string name;
            std::int32_t batch = 0;
            bool fail_on_start = false;
        };

        TestApplication(std::vector<ServiceSpec> specs, std::vector<std::string>& events)
            : mSpecs(std::move(specs)), mEvents(events)
        {
        }

        std::string GetName() const override
        {
            return "test";
        }

    protected:
        void RegisterServices() override
        {
            for (const auto& spec : mSpecs)
            {
                AddService(std::make_unique<RecordingService>(spec.name, spec.batch, spec.fail_on_start, mEvents));
            }
        }

        LifecycleTask OnLoad() override
        {
            mEvents.emplace_back("App:Load");
            return LifecycleTask::Completed();
        }

        LifecycleTask OnStart() override
        {
            mEvents.emplace_back("App:Start");
            return LifecycleTask::Completed();
        }

        LifecycleTask OnStop() override
        {
            mEvents.emplace_back("App:Stop");
            return LifecycleTask::Completed();
        }

        LifecycleTask OnUnload() override
        {
            mEvents.emplace_back("App:Unload");
            return LifecycleTask::Completed();
        }

    private:
        std::vector<ServiceSpec> mSpecs;
        std::vector<std::string>& mEvents;
    };

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    std::size_t IndexOf(const std::vector<std::string>& events, const std::string& value)
    {
        const auto it = std::find(events.begin(), events.end(), value);
        if (it == events.end())
        {
            throw std::runtime_error("missing event: " + value);
        }
        return static_cast<std::size_t>(std::distance(events.begin(), it));
    }

    void TestBatchOrder()
    {
        std::vector<std::string> events;
        TestApplication app(
            {
                {"svc_b2_a", 2, false},
                {"svc_b1", 1, false},
                {"svc_b2_b", 2, false},
            },
            events);

        NullRuntime runtime;
        CommonConfiguration common_configuration;
        TestConfiguration app_configuration;
        app.SetRuntime(runtime);
        Require(app.Configure(common_configuration, app_configuration), "configure should succeed");

        app.Load();
        app.Start();
        app.Stop();
        app.Unload();

        Require(IndexOf(events, "App:Load") < IndexOf(events, "svc_b1:Load"), "app load should run first");
        Require(IndexOf(events, "svc_b1:Load") < IndexOf(events, "svc_b2_a:Load"), "batch 1 load before batch 2");
        Require(IndexOf(events, "svc_b1:Start") < IndexOf(events, "svc_b2_a:Start"), "batch 1 start before batch 2");
        Require(IndexOf(events, "svc_b2_a:Stop") < IndexOf(events, "svc_b1:Stop"), "batch 2 stop before batch 1");
        Require(IndexOf(events, "svc_b2_a:Unload") < IndexOf(events, "svc_b1:Unload"), "batch 2 unload before batch 1");
        Require(IndexOf(events, "svc_b1:Stop") < IndexOf(events, "App:Stop"), "app stop should run last");
        Require(IndexOf(events, "svc_b1:Unload") < IndexOf(events, "App:Unload"), "app unload should run last");
    }

    void TestStartRollback()
    {
        std::vector<std::string> events;
        TestApplication app(
            {
                {"svc_b1", 1, false},
                {"svc_b2_fail", 2, true},
            },
            events);

        NullRuntime runtime;
        CommonConfiguration common_configuration;
        TestConfiguration app_configuration;
        app.SetRuntime(runtime);
        Require(app.Configure(common_configuration, app_configuration), "configure should succeed");

        app.Load();

        bool threw = false;
        try
        {
            app.Start();
        }
        catch (const std::exception&)
        {
            threw = true;
        }
        Require(threw, "start should throw on service failure");
        Require(IndexOf(events, "svc_b1:Stop") > IndexOf(events, "svc_b2_fail:Start"), "started services should rollback stop");
    }

    class RuntimeCommandApplication final : public ApplicationBase<TestConfiguration>
    {
    public:
        explicit RuntimeCommandApplication(std::vector<std::string>& events) : mEvents(events) {}

        std::string GetName() const override
        {
            return "test";
        }

    protected:
        void RegisterRuntimeCommands() override
        {
            mEvents.emplace_back("App:RegisterRuntimeCommands");
            const bool registered =
                Runtime().RegisterCommand("status", "test", [](const CommandArguments&) {
                    return CommandExecutionStatus::handled;
                });
            Require(registered, "runtime command registration should succeed");
        }

        LifecycleTask OnLoad() override
        {
            mEvents.emplace_back("App:Load");
            return LifecycleTask::Completed();
        }

    private:
        std::vector<std::string>& mEvents;
    };

    void TestRuntimeCommandRegistration()
    {
        std::vector<std::string> events;
        RuntimeCommandApplication app(events);

        NullRuntime runtime;
        CommonConfiguration common_configuration;
        TestConfiguration app_configuration;
        app.SetRuntime(runtime);
        Require(app.Configure(common_configuration, app_configuration), "configure should succeed");

        app.Load();

        Require(runtime.registered_commands.size() == 1, "runtime commands should register once");
        Require(runtime.registered_commands.front() == "status", "runtime command name should match");
        Require(IndexOf(events, "App:RegisterRuntimeCommands") < IndexOf(events, "App:Load"),
                "runtime commands should register before app load");
    }
}

SOME_SERVER_APPLICATION_CONFIG(TestConfiguration);

int main()
{
    try
    {
        TestBatchOrder();
        TestStartRollback();
        TestRuntimeCommandRegistration();
        std::cout << "application_service_lifecycle_test: ok" << std::endl;
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "application_service_lifecycle_test: " << ex.what() << std::endl;
        return 1;
    }
}
