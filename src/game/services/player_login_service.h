#pragma once

#include "../../framework/application/service_base.h"
#include "../../framework/ipc/base/envelope.h"
#include "../../framework/ipc/base/result.h"

#include <ipc/login.pb.h>
#include <ipc/session.pb.h>

class GameIpcClientService;
class PlayerDirectoryService;
class PlayerSessionService;

class PlayerLoginService final : public ServiceBase
{
public:
    PlayerLoginService(
        PlayerDirectoryService* directory_service,
        PlayerSessionService* session_service,
        GameIpcClientService* ipc_service);

private:
    void RegisterProcessHandlers();
    ipc::DispatchResult HandleLoginRequest(
        const ipc::Envelope& envelope,
        const pb::ipc::LoginPlayerRequest& request);
    ipc::DispatchResult HandleReconnectRequest(
        const ipc::Envelope& envelope,
        const pb::ipc::ReconnectPlayerRequest& request);
    ipc::DispatchResult HandlePlayerDisconnected(
        const ipc::Envelope& envelope,
        const pb::ipc::PlayerDisconnected& request);

    PlayerDirectoryService* mDirectoryService = nullptr;
    PlayerSessionService* mSessionService = nullptr;
    GameIpcClientService* mIpcService = nullptr;
};
