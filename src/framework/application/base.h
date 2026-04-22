#pragma once

#include "interface.h"
#include "lifecycle_task.h"
#include "service.h"

#include <algorithm>
#include <cassert>
#include <exception>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

template <typename TConfiguration>
class ApplicationBase : public IApplication
{
public:
    std::unique_ptr<IApplicationConfiguration> CreateConfiguration() const override
    {
        return std::make_unique<TConfiguration>();
    }

    void Load() final override
    {
        EnsureServicesRegistered();
        EnsureRuntimeCommandsRegistered();
        OnLoad().Wait();

        mLoadedServices.clear();
        try
        {
            RunForwardBatches(
                [](IService& service) { service.Load().Wait(); },
                [this](IService& service) { mLoadedServices.push_back(&service); });
        }
        catch (...)
        {
            Rollback(mLoadedServices, &IService::Unload);
            mLoadedServices.clear();
            throw;
        }
    }

    void Start() final override
    {
        OnStart().Wait();

        mStartedServices.clear();
        try
        {
            RunForwardBatches(
                [](IService& service) { service.Start().Wait(); },
                [this](IService& service) { mStartedServices.push_back(&service); });
        }
        catch (...)
        {
            Rollback(mStartedServices, &IService::Stop);
            mStartedServices.clear();
            throw;
        }
    }

    void Stop() final override
    {
        std::exception_ptr first_error;

        RunReverseBatches(
            mStartedServices,
            [&first_error](IService& service) {
                CaptureFailure(first_error, [&]() { service.Stop().Wait(); });
            });
        mStartedServices.clear();

        CaptureFailure(first_error, [&]() { OnStop().Wait(); });

        if (first_error)
        {
            std::rethrow_exception(first_error);
        }
    }

    void Unload() final override
    {
        std::exception_ptr first_error;

        RunReverseBatches(
            mLoadedServices,
            [&first_error](IService& service) {
                CaptureFailure(first_error, [&]() { service.Unload().Wait(); });
            });
        mLoadedServices.clear();

        CaptureFailure(first_error, [&]() { OnUnload().Wait(); });

        if (first_error)
        {
            std::rethrow_exception(first_error);
        }
    }

    void SetRuntime(IApplicationRuntime& runtime) override
    {
        mRuntime = &runtime;
    }

    bool Configure(const CommonConfiguration& common_configuration,
                   const IApplicationConfiguration& application_configuration) override
    {
        const auto* typed = dynamic_cast<const TConfiguration*>(&application_configuration);
        if (typed == nullptr)
        {
            return false;
        }

        mCommonConfiguration = common_configuration;
        mApplicationConfiguration = *typed;
        return OnConfigure();
    }

protected:
    virtual bool OnConfigure() { return true; }
    virtual void RegisterServices() {}
    virtual void RegisterRuntimeCommands() {}
    virtual LifecycleTask OnLoad() { return LifecycleTask::Completed(); }
    virtual LifecycleTask OnStart() { return LifecycleTask::Completed(); }
    virtual LifecycleTask OnStop() { return LifecycleTask::Completed(); }
    virtual LifecycleTask OnUnload() { return LifecycleTask::Completed(); }

    void AddService(std::unique_ptr<IService> service)
    {
        if (!service)
        {
            throw std::invalid_argument("service must not be null");
        }

        const std::string name{service->GetName()};
        if (name.empty())
        {
            throw std::invalid_argument("service name must not be empty");
        }

        mServices.push_back(std::move(service));
    }

    const CommonConfiguration& CommonConfig() const { return mCommonConfiguration; }
    const TConfiguration& AppConfig() const { return mApplicationConfiguration; }
    IApplicationRuntime& Runtime() const
    {
        assert(mRuntime != nullptr && "Application runtime has not been set");
        return *mRuntime;
    }

private:
    using ServiceStage = LifecycleTask (IService::*)();

    void EnsureServicesRegistered()
    {
        if (mServicesRegistered)
        {
            return;
        }

        RegisterServices();
        ValidateServices();

        mServicesInOrder.clear();
        mServicesInOrder.reserve(mServices.size());
        for (const auto& service : mServices)
        {
            mServicesInOrder.push_back(service.get());
        }

        std::stable_sort(
            mServicesInOrder.begin(),
            mServicesInOrder.end(),
            [](const IService* lhs, const IService* rhs) { return lhs->GetBatch() < rhs->GetBatch(); });

        mServicesRegistered = true;
    }

    void EnsureRuntimeCommandsRegistered()
    {
        if (mRuntimeCommandsRegistered)
        {
            return;
        }

        RegisterRuntimeCommands();
        mRuntimeCommandsRegistered = true;
    }

    void ValidateServices() const
    {
        std::set<std::string, std::less<>> names;
        for (const auto& service : mServices)
        {
            const std::string name{service->GetName()};
            if (!names.insert(name).second)
            {
                throw std::invalid_argument("duplicate service name: " + name);
            }
        }
    }

    void Rollback(const std::vector<IService*>& services, ServiceStage stage)
    {
        std::exception_ptr rollback_error;
        for (auto it = services.rbegin(); it != services.rend(); ++it)
        {
            CaptureFailure(rollback_error, [&]() { ((*it)->*stage)().Wait(); });
        }

        if (rollback_error)
        {
            std::rethrow_exception(rollback_error);
        }
    }

    template <typename TOperation, typename TSuccess>
    void RunForwardBatches(TOperation&& operation, TSuccess&& on_success)
    {
        std::size_t index = 0;
        while (index < mServicesInOrder.size())
        {
            const std::int32_t batch = mServicesInOrder[index]->GetBatch();
            std::size_t batch_end = index;
            while (batch_end < mServicesInOrder.size() && mServicesInOrder[batch_end]->GetBatch() == batch)
            {
                ++batch_end;
            }

            for (std::size_t current = index; current < batch_end; ++current)
            {
                IService& service = *mServicesInOrder[current];
                std::forward<TOperation>(operation)(service);
                std::forward<TSuccess>(on_success)(service);
            }

            index = batch_end;
        }
    }

    template <typename TOperation>
    static void RunReverseBatches(const std::vector<IService*>& services, TOperation&& operation)
    {
        std::size_t index = services.size();
        while (index > 0)
        {
            const std::int32_t batch = services[index - 1]->GetBatch();
            std::size_t batch_begin = index;
            while (batch_begin > 0 && services[batch_begin - 1]->GetBatch() == batch)
            {
                --batch_begin;
            }

            for (std::size_t current = index; current > batch_begin; --current)
            {
                std::forward<TOperation>(operation)(*services[current - 1]);
            }

            index = batch_begin;
        }
    }

    template <typename TOperation>
    static void CaptureFailure(std::exception_ptr& error, TOperation&& operation)
    {
        try
        {
            std::forward<TOperation>(operation)();
        }
        catch (...)
        {
            if (!error)
            {
                error = std::current_exception();
            }
        }
    }

    IApplicationRuntime* mRuntime = nullptr;
    CommonConfiguration mCommonConfiguration;
    TConfiguration mApplicationConfiguration;
    std::vector<std::unique_ptr<IService>> mServices;
    std::vector<IService*> mServicesInOrder;
    std::vector<IService*> mLoadedServices;
    std::vector<IService*> mStartedServices;
    bool mServicesRegistered = false;
    bool mRuntimeCommandsRegistered = false;
};
