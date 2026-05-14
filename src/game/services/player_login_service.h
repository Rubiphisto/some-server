#pragma once

#include "../../framework/application/service_base.h"
#include "../../framework/ipc/base/envelope.h"
#include "../../framework/ipc/base/result.h"

class GameIpcClientService;
class PlayerDirectoryService;
class PlayerSessionService;

class PlayerLoginService final : public ServiceBase
{
public:
    PlayerLoginService(
        PlayerDirectoryService* directory_service,
        PlayerSessionService* session_service,
        GameIpcClientService* ipc_service)
        : ServiceBase("player_login", 40)
        , mDirectoryService(directory_service)
        , mSessionService(session_service)
        , mIpcService(ipc_service)
    {
    }

    ipc::DispatchResult HandleProcessEnvelope(const ipc::ReceiverAddress& target, const ipc::Envelope& envelope);

private:
    ipc::DispatchResult HandleLoginRequest(const ipc::Envelope& envelope);
    ipc::DispatchResult HandleReconnectRequest(const ipc::Envelope& envelope);
    ipc::DispatchResult HandlePlayerDisconnected(const ipc::Envelope& envelope);

    PlayerDirectoryService* mDirectoryService = nullptr;
    PlayerSessionService* mSessionService = nullptr;
    GameIpcClientService* mIpcService = nullptr;
};
