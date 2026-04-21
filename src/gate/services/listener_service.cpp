#include "listener_service.h"

#include <spdlog/spdlog.h>

namespace
{
    constexpr std::int32_t kListenerBatch = 20;
}

ListenerService::ListenerService(const GateConfiguration& configuration)
    : ServiceBase("gate.listener", kListenerBatch), mConfiguration(configuration)
{
}

LifecycleTask ListenerService::Load()
{
    spdlog::info("ListenerService::Load(listen={}:{})",
                 mConfiguration.listen.host,
                 mConfiguration.listen.port);
    return LifecycleTask::Completed();
}

LifecycleTask ListenerService::Start()
{
    spdlog::info("ListenerService::Start()");
    return LifecycleTask::Completed();
}

LifecycleTask ListenerService::Stop()
{
    spdlog::info("ListenerService::Stop()");
    return LifecycleTask::Completed();
}

LifecycleTask ListenerService::Unload()
{
    spdlog::info("ListenerService::Unload()");
    return LifecycleTask::Completed();
}
