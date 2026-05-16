# 玩家系统任务目标与实现进度

## 状态

- 持续维护中
- 作用：记录当前会话对应的总体目标、拆分步骤、当前实现进度、下一步动作

## 当前任务目标

围绕 `gate + game + sim_client` 建立一套可落地的玩家接入与运行体系，主要目标如下：

1. 建立 `gate` 与 `game` 多进程协同的完整玩家登录流程。
2. 基于现有 IPC 架构完成服务端内部通信，并明确 IPC 需要补强但不应过度下沉的部分。
3. 建立客户端协议与 IPC 协议彻底分离的 protobuf 工程结构。
4. 建立 `game` 侧玩家对象加载、暂留、释放、Redis 热存储、MariaDB 落盘方案。
5. 建立 `gate` 侧长连接、会话、顶号、断线重连、玩家路由方案。
6. 建立独立 `sim_client` 进程，用真实客户端协议联调整体链路。

## 步骤拆分与进度

### 阶段 1：设计文档拆分与边界收敛

目标：

- 把总体设计拆成专题文档
- 明确客户端协议与 IPC 协议隔离边界
- 明确 `gate`、`game`、`sim_client`、IPC 改进各自职责

当前进度：

- 已完成

产出：

- [overview.md](/home/dev/projects/some-server/docs/player-system/overview.md)
- [player-login-storage-design.md](/home/dev/projects/some-server/docs/player-system/player-login-storage-design.md)
- [gate-session-design.md](/home/dev/projects/some-server/docs/player-system/gate-session-design.md)
- [gate-game-protocol-design.md](/home/dev/projects/some-server/docs/player-system/gate-game-protocol-design.md)
- [game-player-storage-design.md](/home/dev/projects/some-server/docs/player-system/game-player-storage-design.md)
- [sim-client-design.md](/home/dev/projects/some-server/docs/player-system/sim-client-design.md)
- [ipc-improvement-design.md](/home/dev/projects/some-server/docs/ipc-improvements/ipc-improvement-design.md)
- [client-protocol-build-design.md](/home/dev/projects/some-server/docs/protocol/client-protocol-build-design.md)
  - 当前已由 [game-protocol-build-design.md](/home/dev/projects/some-server/docs/protocol/game-protocol-build-design.md) 取代

对应提交：

- `5c6f7f2` `Add player system and protocol design docs`

### 阶段 2：客户端 protobuf 独立工程落地

目标：

- 新建 `proto/client/`
- 新建 `src/protocol/`
- 建立独立 `client_proto` target
- 保证不并入现有 IPC proto 生成链

当前进度：

- 已完成最小骨架

已落地内容：

- [proto/client/common/v1/types.proto](/home/dev/projects/some-server/proto/client/common/v1/types.proto)
- [proto/client/login/v1/login.proto](/home/dev/projects/some-server/proto/client/login/v1/login.proto)
- [proto/client/game/v1/player.proto](/home/dev/projects/some-server/proto/client/game/v1/player.proto)
- [src/protocol/CMakeLists.txt](/home/dev/projects/some-server/src/protocol/CMakeLists.txt)
- [src/protocol/client_proto.cmake](/home/dev/projects/some-server/src/protocol/client_proto.cmake)
- [tools/client_proto/gen_client_proto.sh](/home/dev/projects/some-server/tools/client_proto/gen_client_proto.sh)

验证结果：

- 已执行 `cmake -S . -B build`
- 已执行 `cmake --build build --target client_proto -j 4`
- `client_proto` 构建通过

对应提交：

- `de513a0` `Add standalone client protobuf build pipeline`

### 阶段 3：IPC 上层 `gate_game` 业务协议落地

目标：

- 在 `proto/ipc/` 下新增 `gate_game/v1`
- 定义登录、转发、推送、断线、顶号、重连等最小消息集合
- 继续保持与客户端协议完全隔离

当前进度：

- 进行中
- 已建立独立 `gate_game_proto` 业务协议骨架
- 已接入 `gate` / `game` 进程级业务分发与最小登录请求/响应路径
- 已形成 `sim_client -> gate -> relay -> game -> relay -> gate` 的最小登录联调闭环
- 当前联调结果表明链路可达，失败点已收敛到 `game` 玩家仓储的 MariaDB 依赖

参考文档：

- [gate-game-protocol-design.md](/home/dev/projects/some-server/docs/player-system/gate-game-protocol-design.md)
- [ipc-improvement-design.md](/home/dev/projects/some-server/docs/ipc-improvements/ipc-improvement-design.md)

已落地内容：

- [proto/ipc/gate_game/v1/common.proto](/home/dev/projects/some-server/proto/ipc/gate_game/v1/common.proto)
- [proto/ipc/gate_game/v1/login.proto](/home/dev/projects/some-server/proto/ipc/gate_game/v1/login.proto)
- [proto/ipc/gate_game/v1/session.proto](/home/dev/projects/some-server/proto/ipc/gate_game/v1/session.proto)
- [proto/ipc/gate_game/v1/player_message.proto](/home/dev/projects/some-server/proto/ipc/gate_game/v1/player_message.proto)
- [proto/ipc/gate_game/v1/push.proto](/home/dev/projects/some-server/proto/ipc/gate_game/v1/push.proto)
- [src/protocol/gate_game_proto.cmake](/home/dev/projects/some-server/src/protocol/gate_game_proto.cmake)
- [src/protocol/CMakeLists.txt](/home/dev/projects/some-server/src/protocol/CMakeLists.txt)
- [src/gate/services/login_service.h](/home/dev/projects/some-server/src/gate/services/login_service.h)
- [src/gate/services/login_service.cpp](/home/dev/projects/some-server/src/gate/services/login_service.cpp)
- [src/game/services/player_login_service.h](/home/dev/projects/some-server/src/game/services/player_login_service.h)
- [src/game/services/player_login_service.cpp](/home/dev/projects/some-server/src/game/services/player_login_service.cpp)
- [src/gate/services/ipc_service.h](/home/dev/projects/some-server/src/gate/services/ipc_service.h)
- [src/gate/services/ipc_service.cpp](/home/dev/projects/some-server/src/gate/services/ipc_service.cpp)
- [src/game/services/ipc_client_service.h](/home/dev/projects/some-server/src/game/services/ipc_client_service.h)
- [src/game/services/ipc_client_service.cpp](/home/dev/projects/some-server/src/game/services/ipc_client_service.cpp)
- [src/gate/services/process_receiver_host.h](/home/dev/projects/some-server/src/gate/services/process_receiver_host.h)
- [src/gate/services/process_receiver_host.cpp](/home/dev/projects/some-server/src/gate/services/process_receiver_host.cpp)
- [src/game/services/process_receiver_host.h](/home/dev/projects/some-server/src/game/services/process_receiver_host.h)
- [src/game/services/process_receiver_host.cpp](/home/dev/projects/some-server/src/game/services/process_receiver_host.cpp)

验证结果：

- 已执行 `cmake -S . -B build`
- 已执行 `cmake --build build --target gate_game_proto -j 4`
- 已执行 `cmake --build build --target gate -j 4`
- 已执行 `cmake --build build --target game -j 4`
- `gate_game_proto` 构建通过
- 生成产物位于 `src/protocol/ipc/pb/`
- `gate` 构建通过
- `game` 构建通过

### 阶段 4：`gate` IPC 接入与长连接骨架

目标：

- 为 `gate` 接入 IPC runtime
- 建立 `gate` 会话与连接服务骨架
- 建立接收 `game` 推送和踢线消息的入口

当前进度：

- 进行中
- 已完成 `gate` IPC runtime 与业务接收入口最小骨架
- 已完成连接、会话、路由、协议、鉴权服务骨架
- 已接入最小真实 TCP 长连接监听、客户端收发帧、心跳响应、连接断开回调
- 已把真实 `connection_id` 贯穿到 `gate -> game -> gate` 登录响应回包路径
- 已拆分 `gate` 的客户端监听与 IPC 监听，避免同端口冲突
- 已接入 `gate -> game` 断线通知和 `gate` 本地 session/route 清理
- 已接入同一 `gate` 进程内的同账号新连接踢旧连接
- 已接入基于 Redis 账号目录 + `gate -> gate` IPC 的跨 gate 同账号踢线
- 已接入客户端玩家消息转发与 `game -> gate -> client` 主动推送最小闭环
- 已补齐 `gate` 消息服务编译清单

参考文档：

- [gate-session-design.md](/home/dev/projects/some-server/docs/player-system/gate-session-design.md)
- [ipc-improvement-design.md](/home/dev/projects/some-server/docs/ipc-improvements/ipc-improvement-design.md)

已落地内容：

- [src/gate/application.h](/home/dev/projects/some-server/src/gate/application.h)
- [src/gate/application.cpp](/home/dev/projects/some-server/src/gate/application.cpp)
- [src/gate/CMakeLists.txt](/home/dev/projects/some-server/src/gate/CMakeLists.txt)
- [src/gate/gate.cmake](/home/dev/projects/some-server/src/gate/gate.cmake)
- [src/gate/services/ipc_service.h](/home/dev/projects/some-server/src/gate/services/ipc_service.h)
- [src/gate/services/ipc_service.cpp](/home/dev/projects/some-server/src/gate/services/ipc_service.cpp)
- [src/gate/services/process_receiver_host.h](/home/dev/projects/some-server/src/gate/services/process_receiver_host.h)
- [src/gate/services/process_receiver_host.cpp](/home/dev/projects/some-server/src/gate/services/process_receiver_host.cpp)
- [src/gate/services/service_receiver_host.h](/home/dev/projects/some-server/src/gate/services/service_receiver_host.h)
- [src/gate/services/service_receiver_host.cpp](/home/dev/projects/some-server/src/gate/services/service_receiver_host.cpp)
- [src/gate/services/connection_service.h](/home/dev/projects/some-server/src/gate/services/connection_service.h)
- [src/gate/services/connection_service.cpp](/home/dev/projects/some-server/src/gate/services/connection_service.cpp)
- [src/gate/services/session_service.h](/home/dev/projects/some-server/src/gate/services/session_service.h)
- [src/gate/services/session_service.cpp](/home/dev/projects/some-server/src/gate/services/session_service.cpp)
- [src/gate/services/routing_service.h](/home/dev/projects/some-server/src/gate/services/routing_service.h)
- [src/gate/services/routing_service.cpp](/home/dev/projects/some-server/src/gate/services/routing_service.cpp)
- [src/gate/services/protocol_service.h](/home/dev/projects/some-server/src/gate/services/protocol_service.h)
- [src/gate/services/protocol_service.cpp](/home/dev/projects/some-server/src/gate/services/protocol_service.cpp)
- [src/gate/services/auth_service.h](/home/dev/projects/some-server/src/gate/services/auth_service.h)
- [src/gate/services/auth_service.cpp](/home/dev/projects/some-server/src/gate/services/auth_service.cpp)
- [src/gate/services/player_message_service.h](/home/dev/projects/some-server/src/gate/services/player_message_service.h)
- [src/gate/services/player_message_service.cpp](/home/dev/projects/some-server/src/gate/services/player_message_service.cpp)
- [bin/conf/gate.json](/home/dev/projects/some-server/bin/conf/gate.json)

验证结果：

- 已执行 `cmake -S . -B build`
- 已执行 `cmake --build build --target gate -j 4`
- 已执行 `cmake --build build --target gate game sim_client -j 4`
- `gate` 构建通过
- `game` 构建通过
- `sim_client` 构建通过
- 已执行最小真实联调：
  - `sim_client scenario_run login_smoke`
  - `sim_client scenario_run player_echo`
  - `game player_push 100000 4001 server-push-1`
- 联调结果：
  - `sim_client` 收到登录响应，`last_login_ok=true`
  - `sim_client` 收到玩家回包，`last_player_response_payload=sim-player-ping`
  - `sim_client` 收到服务端推送，`last_push_message_id=4001`，`last_push_payload=server-push-1`
  - `gate ipc_status` 中 `process_dispatch_count=3`，最后一条为 `PushPlayerMessage`
  - `game ipc_status` 中 `process_dispatch_count=2`，说明 `ForwardPlayerMessageRequest` 已进入业务路径

### 阶段 5：`game` 玩家加载与存储骨架

目标：

- 建立玩家目录服务
- 建立玩家仓储与 lease
- 建立玩家会话与暂留态
- 建立延迟落盘骨架

当前进度：

- 进行中
- 已完成目录服务、仓储骨架、玩家会话状态机与基础运行时命令
- 已接入独立 `proto/player/` 领域模型和真实 `StorageService` 序列化存取
- 已修复 `game` 侧 `gate_game` payload 未注册问题
- 已修复 IPC data frame 持锁 dispatch 导致的同步回调死锁问题
- 已为“新创建玩家”接入 `Redis miss + Maria unavailable` 的默认数据回退加载
- 已验证在 Maria 不可用时首登新玩家仍可完成登录闭环
- 已接入玩家 `online -> detached -> online` 的最小重连状态流转
- 已验证重连时复用既有玩家实例，`load_count` 不增加
- 已验证同账号新连接会踢掉旧 gate 连接，并把玩家路由切到新 session
- 已验证跨 gate 同账号新连接会踢掉旧 gate 连接，并把玩家路由切到新 gate
- 已接入 `ForwardPlayerMessageRequest -> ForwardPlayerMessageResponse` 的最小 echo 业务处理
- 已接入 `PushPlayerMessage` 到活跃 `gate session` 的最小主动推送
- 已把 `game_player_message_service` 从原始 echo 路径升级为“消息号 -> protobuf 请求/响应 -> handler”的最小注册表
- 已落地两个内建 handler：
  - `3101` 玩家 echo
  - `3102` 玩家改名
- 已落地一个 typed push：
  - `5101` 玩家资料推送
- 已新增最小 `PlayerRuntimeService + PlayerInstance` 实例层
- 已把 `echo` / `rename` / `profile push` 从直接操作仓储改为调用玩家实例方法
- 已让玩家实例生命周期跟随 `ActivatePlayer` / `ReleasePlayer`，并在 `detached` 期间继续保留
- 已新增最小 `PlayerPersistenceService`
- 已接入后台周期扫描与手动 `flush_due` 入口
- 当前 flush 策略：
  - `dirty` 且超过 `landing_min_time_seconds` 时尝试 flush
  - `detached` 且 `dirty` 时优先尝试 flush
- 已接入最小失败退避：
  - flush 失败后记录 `consecutive_failure_count`
  - 按 `landing_min_time_seconds` 起步做指数退避
  - 最大退避不超过 `landing_time_seconds`
- 已新增显式恢复命令链：
  - `storage_probe`
  - `player_persistence_recover_flush`
- 语义：
  - 先探测 Maria 是否恢复可用
  - 若已恢复，则清空 persistence backoff
  - 然后立即重跑一次 `flush_due`
- 已新增最小 `PlayerLeaseService`
- 已接入：
  - 新加载前先 `Acquire`
  - 活跃/暂留期间后台续约
  - 释放时按 token 归还 lease
- 已把“lease 被其它 game 持有”映射为 `RESULT_CODE_PLAYER_HELD_BY_OTHER_GAME`
- 已接入本地 owner 丢失后的最小防护：
  - 续约失败后清除本地 tracked lease
  - 后续玩家消息与主动推送都要求 `lease` 仍由本地持有
  - `game` 停止时会主动批量释放本地 lease
- 已修复“本地实例仍在，但 lease 已丢失”时的重连恢复缺口：
  - `PlayerSessionService::ActivatePlayer()` 在复用 `online/detached` 玩家前，会先确认本地仍持有 lease
  - 若 lease 已丢失，则先重新 `Acquire`
  - 重新拿回 lease 后，复用已有仓储与 runtime，不重新加载玩家数据
  - 若重新 `Acquire` 失败，则直接返回，不再出现“假重连成功、后续消息又被 owner 校验拒绝”的状态
- 已补最小跨 `game` owner 迁移降级逻辑：
  - `PlayerSessionService` 新增后台 reconcile 线程
  - 周期检查 `online/detached` 玩家是否仍持有本地 lease
  - 旧 owner 一旦不再持有 lease，就自动：
    - 解除本地 player receiver 绑定
    - 把 `online` 状态降为 `detached`
    - 清空旧 `gate` 关联
    - 向旧 `gate` 发送 `UnbindPlayerSession`
  - 保留仓储与实例数据，等待同 game 快速恢复或 detached TTL 到期释放
  - 同时新增运行时命令 `player_reconcile_lease_loss` 方便人工触发与观测
- 已修复通用 Maria dataset 表对 protobuf 二进制 payload 的存储类型错误：
  - `entry_value` 从 `LONGTEXT` 调整为 `LONGBLOB`
  - `StorageService` 在首次 ensure dataset 表时会执行一次值列二进制化修正
- 已完成 Maria 可用后的成功落盘联调：
  - `storage_probe` 显示 Maria `reachable=true`
  - `storage_dataset_init player` 可创建并校正 `player_entries`
  - 玩家改名后进入 `dirty=true`
  - 断线进入 `detached` 后，后台 persistence scheduler 成功 flush
  - `player_status 100000` 显示 `dirty=false`、`flush_count=1`
  - `player_persistence_status` 显示 `flush_success_count=1`、`flush_failure_count=0`
  - 删除 Redis 热数据后执行 `storage_dataset_get player 100000`，`source=maria`
  - 说明玩家 protobuf 数据已成功落入 Maria，并可经 Redis miss 回读
- 尚未接入更完整的 owner 接管与 lease 恢复策略

参考文档：

- [game-player-storage-design.md](/home/dev/projects/some-server/docs/player-system/game-player-storage-design.md)

已落地内容：

- [src/game/application.h](/home/dev/projects/some-server/src/game/application.h)
- [src/game/application.cpp](/home/dev/projects/some-server/src/game/application.cpp)
- [src/game/game.cmake](/home/dev/projects/some-server/src/game/game.cmake)
- [proto/player/v1/player.proto](/home/dev/projects/some-server/proto/player/v1/player.proto)
- [src/protocol/CMakeLists.txt](/home/dev/projects/some-server/src/protocol/CMakeLists.txt)
- [src/protocol/player_data_proto.cmake](/home/dev/projects/some-server/src/protocol/player_data_proto.cmake)
- [src/game/services/player_directory_service.h](/home/dev/projects/some-server/src/game/services/player_directory_service.h)
- [src/game/services/player_directory_service.cpp](/home/dev/projects/some-server/src/game/services/player_directory_service.cpp)
- [src/game/services/player_message_service.h](/home/dev/projects/some-server/src/game/services/player_message_service.h)
- [src/game/services/player_message_service.cpp](/home/dev/projects/some-server/src/game/services/player_message_service.cpp)
- [src/game/services/player_lease_service.h](/home/dev/projects/some-server/src/game/services/player_lease_service.h)
- [src/game/services/player_lease_service.cpp](/home/dev/projects/some-server/src/game/services/player_lease_service.cpp)
- [src/game/services/player_persistence_service.h](/home/dev/projects/some-server/src/game/services/player_persistence_service.h)
- [src/game/services/player_persistence_service.cpp](/home/dev/projects/some-server/src/game/services/player_persistence_service.cpp)
- [src/game/services/player_repository.h](/home/dev/projects/some-server/src/game/services/player_repository.h)
- [src/game/services/player_repository.cpp](/home/dev/projects/some-server/src/game/services/player_repository.cpp)
- [src/game/services/player_runtime_service.h](/home/dev/projects/some-server/src/game/services/player_runtime_service.h)
- [src/game/services/player_runtime_service.cpp](/home/dev/projects/some-server/src/game/services/player_runtime_service.cpp)
- [src/game/services/player_session_service.h](/home/dev/projects/some-server/src/game/services/player_session_service.h)
- [src/game/services/player_session_service.cpp](/home/dev/projects/some-server/src/game/services/player_session_service.cpp)
- [src/game/services/ipc_client_service.h](/home/dev/projects/some-server/src/game/services/ipc_client_service.h)
- [src/game/services/ipc_client_service.cpp](/home/dev/projects/some-server/src/game/services/ipc_client_service.cpp)
- [src/framework/storage/service.h](/home/dev/projects/some-server/src/framework/storage/service.h)
- [src/framework/storage/service.cpp](/home/dev/projects/some-server/src/framework/storage/service.cpp)

验证结果：

- 已执行 `cmake -S . -B build`
- 已执行 `cmake --build build --target player_data_proto -j 4`
- 已执行 `cmake --build build --target game -j 4`
- 已执行 `cmake --build build --target client_proto game sim_client -j 4`
- `player_data_proto` 构建通过
- `game` 构建通过
- `client_proto` 构建通过
- 已完成真实 typed 分发联调：
  - `sim_client scenario_run login_smoke`
  - `sim_client scenario_run player_echo`
  - `sim_client scenario_run player_rename`
  - `game player_push_profile 100000`
- 联调结果：
  - `sim_client` 收到 `3101` echo 响应，`last_echo_text=sim-player-ping`
  - `sim_client` 收到 `3102` rename 响应，`last_rename_display_name=sim-player-renamed`
  - `game player_status 100000` 显示玩家名已变为 `sim-player-renamed`
  - `sim_client` 收到 `5101` typed profile push，`last_profile_push_display_name=sim-player-renamed`
- 已完成实例层联调：
  - `game player_runtime_status 100000` 在登录后显示 `present=true`
  - `sim_client player_echo instance-ping` 后 `command_count=1`
  - `sim_client player_rename runtime-name-1` 后 `command_count=2`
  - 客户端断开后 `player_status 100000` 进入 `state=detached`
  - 同时 `player_runtime_status 100000` 仍为 `present=true`，说明实例在暂留期内未被释放
- 已完成 persistence scheduler 联调：
  - `player_persistence_status` 显示后台线程 `running=true`
  - 在线状态下立即执行 `player_persistence_flush_due`，因未到 `landing_min_time_seconds`，`flushed=0`
  - `detached` 后执行 `player_persistence_flush_due`，已触发 flush 尝试
  - 当前环境下 Maria 仍不可用，因此 `flush_failure_count` 增长，`last_error=maria connection is unavailable`
  - 说明“后台择机 flush”路径已生效，但成功落盘仍受 Maria 环境限制
- 已完成 persistence backoff 联调：
  - `dirty + detached` 后第一次 `player_persistence_flush_due` 触发失败
  - `player_persistence_player_status 100000` 显示 `consecutive_failure_count=1`
  - 立刻再次执行 `player_persistence_flush_due`，`flush_attempt_count` 未继续增长
  - 说明失败退避已阻止“每秒重复打 Maria”的无效重试
- 已完成恢复命令联调：
  - `storage_probe` 明确显示当前 Maria 状态
  - 当前环境返回的是 `Access denied for user 'game'@'localhost'`
  - `player_persistence_recover_flush` 在 Maria 未恢复时不会清空 backoff，也不会触发 flush
  - 这条链路已经就绪，后续一旦 Maria 凭据或连接恢复，可直接用同一命令验证成功路径
- 已完成 Maria 恢复后的成功路径联调：
  - 已在宿主 Maria 中补齐 `some_game`、`game@localhost` 与 `game@%`
  - `mysql --protocol=TCP -h 127.0.0.1 -P 3306 -ugame -pgame some_game` 可直接登录
  - 真实联调中发现旧 `player_entries.entry_value` 为 `LONGTEXT`，导致 protobuf 二进制落盘失败
  - 已把通用 dataset 值列修正为 `LONGBLOB`
  - 重新联调后，玩家改名数据在 `detached` 后成功 flush 到 Maria
  - 手动删除 Redis `some_server:player:entries:100000` 后，`storage_dataset_get player 100000` 返回 `source=maria`
  - 宿主 Maria 查询 `some_game.player_entries` 可见 `entry_key=100000` 已持久化
- 已完成双 `game` lease 联调：
  - 通过真实登录把 `player_id=100000` 挂到 `game1`
  - `game1 player_lease_status 100000` 显示本地与 Redis owner 都是 `10:1`
  - `game2 player_activate 100000` 失败，错误为 `player lease is already held by another game`
  - `game2 player_lease_status 100000` 显示本地未持有，但远端 owner 为 `10:1`
  - 说明“同一玩家不能被第二个 game 同时激活”的最小 owner 约束已生效
- 已完成“外部删掉 Redis lease key -> 本地 owner 丢失”联调：
  - 手动删除 `some_server:game:player:lease:100000`
  - 续约线程下一轮后 `player_lease_service_status` 显示 `renew_failure_count=1`、`local_loss_count=1`
  - `player_lease_status 100000` 变为 `local_tracked=false`
  - `game player_push_profile 100000` 被拒绝，错误为 `player lease is not held locally`
  - 客户端继续发 `player_echo` 时收到失败回包，`last_player_response_error_message=player lease is not held locally`
  - 说明 lease 丢失后，本地实例虽然还在，但已经不会继续当作 owner 对外服务
- 已完成“lease 丢失后的同 game 重连恢复”联调：
  - 登录成功后手动删除 `some_server:game:player:lease:100000`
  - 续约线程确认 `local_loss_count=1`，同时玩家进入 `detached`
  - 同一 `sim_client` 重新连接并再次登录后，`player_lease_status 100000` 恢复为 `local_tracked=true`
  - `player_status 100000` 显示 `activate_count=2`、`load_count=1`
  - 说明恢复路径复用了已有玩家实例，没有重新加载
  - 客户端随后执行 `player_echo reacquired-owner` 成功，`last_player_response_error_message=none`
  - 说明“丢 lease -> 重连 -> 重新拿回 owner -> 恢复服务”闭环已成立
- 已完成跨 `game` owner 迁移联调：
  - `game1` 先持有 `player_id=100000` 的 lease 并处于 `online`
  - 手动删除 `some_server:game:player:lease:100000` 后，`game2 player_activate 100000 ...` 可成功接管
  - `game2 player_lease_status 100000` 显示新 owner 为 `10:2`
  - `game1 player_reconcile_lease_loss` 可立即把旧状态降为 `detached`，并清掉本地 player receiver
  - 进一步用 `player_id=100001` 验证后台线程自动路径：
    - `game1` 持有并在线
    - 删除 lease 后由 `game2` 接管
    - 不手动执行任何命令，只等待后台 reconcile
    - `game1 player_status 100001` 自动变为 `state=detached`
    - `game1 ipc_receivers` 显示 `local_players=0`
  - 说明“新 owner 接管成功，旧 owner 自动降级并退出本地接收者集合”的最小迁移闭环已成立
- 已完成“旧 game 降级后主动收敛旧 gate 会话”联调：
  - `sim_client` 先通过 `gate1 -> game1` 登录到 `player_id=100000`
  - 手动删除 lease 后由 `game2 player_activate 100000 ...` 成功接管
  - 旧 `game1` 后台 reconcile 触发 `UnbindPlayerSession` 给旧 `gate1`
  - `sim_client status` 显示：
    - `connected=false`
    - `last_kick_reason=player session ownership moved`
  - `gate1 session_status 1` 显示 `present=false`
  - `gate1 route_status 100000` 显示 `present=false`
  - 说明旧 `gate` 不再需要等待客户端自然断开，stale session / route 已能被主动收敛

### 阶段 6：`sim_client` 独立进程骨架

目标：

- 新建 `src/sim_client/`
- 依赖 `client_proto`
- 支持最小登录、心跳、收发消息测试

当前进度：

- 进行中
- 已完成独立进程、服务拆分和客户端 proto 依赖接入骨架
- 已接入最小真实 TCP 客户端连接、帧发送、接收线程与登录/心跳帧收发骨架
- 已修正断连与停止阶段的线程回收问题
- 已完成与 `gate + relay + game` 的最小登录联调验证
- 已验证首登新玩家在 Maria 不可用时的成功登录路径
- 已完成“登录 -> 断线 -> detached -> 重连复用”的最小联调验证
- 已完成“同账号双连接 -> 旧连接被踢 -> 新连接接管”的最小联调验证
- 已完成“跨 gate 同账号双连接 -> gate1 被 gate2 踢下线 -> 新 gate 接管”的最小联调验证
- 已完成“玩家消息请求 -> gate 转发 -> game 回包 -> gate 回传”的最小联调验证
- 已完成“game 主动推送 -> gate -> sim_client”的最小联调验证
- 已补齐玩家消息响应与推送结果的状态输出
- 已支持 typed echo、typed rename、typed profile push 的 protobuf 编解码与状态输出
- 已补最小交互命令：
  - `player_echo`
  - `player_rename`
  - `scenario_run player_rename`
- 已补显式登录身份与回归脚本能力：
  - `use_account <account_id>`
  - `login [account_id]`
  - `heartbeat`
  - `reset_status`
  - `scenario_run reconnect_after_disconnect [account_id]`
  - `scenario_run recover_after_kick [account_id]`
  - `scenario_run login_then_echo [account_id]`
- 已新增多进程回归编排脚本：
  - [run_regression.sh](/home/dev/projects/some-server/tools/sim_client/run_regression.sh)
  - [run_all_regressions.sh](/home/dev/projects/some-server/tools/sim_client/run_all_regressions.sh)
  - 支持场景：
    - `reconnect_after_disconnect`
    - `same_account_kick`
    - `migration_recover_after_kick`
  - 脚本会独立拉起：
    - `etcd`
    - `relay`
    - `game1`
    - `game2`
    - `gate`
    - `sim1`
    - `sim2`
- 已接入统一手工测试入口：
  - `cmake --build build --target sim_client_regression`
  - 对应定义位置：[tests/CMakeLists.txt](/home/dev/projects/some-server/tests/CMakeLists.txt)
  - 使用说明已写入 [README.md](/home/dev/projects/some-server/README.md)
- 已新增显式 CTest 开关：
  - [CMakeLists.txt](/home/dev/projects/some-server/CMakeLists.txt) 中新增 `ENABLE_HOST_INTEGRATION_TESTS`
  - 默认 `OFF`
  - 开启后会把 `sim_client` 回归场景注册成 host-only 集成测试
  - 测试标签：
    - `host_integration`
    - `sim_client`
  - 当前会拆成 3 条独立测试：
    - `sim_client_regression_reconnect_after_disconnect`
    - `sim_client_regression_same_account_kick`
    - `sim_client_regression_migration_recover_after_kick`
- `sim_client status` 现在可观察：
  - `configured_account_id`
  - `last_login_error_code`
  - `last_login_error_message`
- 已修复 `sim_client` 在“被远端断开后再次 connect”时的线程生命周期崩溃：
  - 旧 reader 线程退出但仍 `joinable` 时，新的 `Connect()` 先 join 旧线程
  - 避免二次 `connect` 触发 `std::terminate`
- 已补远端断开时的 `disconnect_count` 统计

参考文档：

- [sim-client-design.md](/home/dev/projects/some-server/docs/player-system/sim-client-design.md)

已落地内容：

- [src/sim_client/CMakeLists.txt](/home/dev/projects/some-server/src/sim_client/CMakeLists.txt)
- [src/sim_client/sim_client.cmake](/home/dev/projects/some-server/src/sim_client/sim_client.cmake)
- [src/sim_client/main.cpp](/home/dev/projects/some-server/src/sim_client/main.cpp)
- [src/sim_client/application.h](/home/dev/projects/some-server/src/sim_client/application.h)
- [src/sim_client/application.cpp](/home/dev/projects/some-server/src/sim_client/application.cpp)
- [src/sim_client/services/connection_service.h](/home/dev/projects/some-server/src/sim_client/services/connection_service.h)
- [src/sim_client/services/connection_service.cpp](/home/dev/projects/some-server/src/sim_client/services/connection_service.cpp)
- [src/sim_client/services/protocol_service.h](/home/dev/projects/some-server/src/sim_client/services/protocol_service.h)
- [src/sim_client/services/protocol_service.cpp](/home/dev/projects/some-server/src/sim_client/services/protocol_service.cpp)
- [src/sim_client/services/scenario_service.h](/home/dev/projects/some-server/src/sim_client/services/scenario_service.h)
- [src/sim_client/services/scenario_service.cpp](/home/dev/projects/some-server/src/sim_client/services/scenario_service.cpp)
- [bin/conf/sim_client.json](/home/dev/projects/some-server/bin/conf/sim_client.json)
- [src/CMakeLists.txt](/home/dev/projects/some-server/src/CMakeLists.txt)
- [tools/sim_client/run_regression.sh](/home/dev/projects/some-server/tools/sim_client/run_regression.sh)
- [tools/sim_client/run_all_regressions.sh](/home/dev/projects/some-server/tools/sim_client/run_all_regressions.sh)
- [tests/CMakeLists.txt](/home/dev/projects/some-server/tests/CMakeLists.txt)
- [README.md](/home/dev/projects/some-server/README.md)

验证结果：

- 已执行 `cmake -S . -B build`
- 已执行 `cmake --build build --target sim_client -j 4`
- 已执行 `cmake --build build --target sim_client gate game relay -j 4`
- `sim_client` 构建通过
- 已执行 `scenario_run login_smoke`
- 已执行 `scenario_run player_echo`
- 已执行 `scenario_run player_rename`
- `sim_client status` 可观察 `last_player_response_payload` 与 `last_push_payload`
- `sim_client status` 可观察 `last_echo_text`、`last_rename_display_name`、`last_profile_push_display_name`
- 已完成新命令最小验证：
  - `use_account sim-regr-1`
  - `reset_status`
  - `login`
  - `status`
  - 状态中可正确显示 `configured_account_id=sim-regr-1`
  - 状态中可正确显示 `last_login_error_code` / `last_login_error_message`
- 已完成回归脚本最小验证：
  - `scenario_run reconnect_after_disconnect sim-regr-2`
  - `status` 显示脚本执行后 `connected=true`
  - `connect_attempts` / `disconnect_count` 有递增
  - `last_scenario=reconnect_after_disconnect`
  - `last_login_ok=true`
- 已在宿主环境完成多进程脚本实测：
  - `tools/sim_client/run_regression.sh reconnect_after_disconnect`
  - `tools/sim_client/run_regression.sh same_account_kick`
  - `tools/sim_client/run_regression.sh migration_recover_after_kick`
  - 三个场景均返回 `ok`
- 已完成统一入口实测：
  - `cmake -S . -B build`
  - `cmake --build build --target sim_client_regression -j 4`
  - 统一入口会顺序执行：
    - `reconnect_after_disconnect`
    - `same_account_kick`
    - `migration_recover_after_kick`
  - 结果为 `sim_client regressions: all ok`
- 统一入口当前默认保持“手工 target”模式，不自动进入普通 `ctest`
- 但已经具备显式接入 `ctest` 的条件，后续只需要在宿主环境配置里打开 `ENABLE_HOST_INTEGRATION_TESTS=ON`
- 已进一步把 host-only `ctest` 注册从“单总测试”收敛成“场景级独立测试”，便于 CI 细粒度报错定位
- 已确认 `recover_after_kick` 脚本不只是命令路径存在，而是能和真实 owner 迁移断开场景组合完成恢复闭环
- 已进一步把手工构建目标也收敛成“场景级独立 target + 聚合 target”：
  - `sim_client_regression_reconnect_after_disconnect`
  - `sim_client_regression_same_account_kick`
  - `sim_client_regression_migration_recover_after_kick`
  - `sim_client_regression`
- 这使 `cmake --build` 与 host-only `ctest` 在场景粒度上保持一致，便于本地和 CI 对齐排障
- 已确认聚合 target 不能直接依赖三个场景 target 再并行构建，否则会并发抢占端口与测试账号
- 当前聚合 target 已收敛为串行执行 `run_all_regressions.sh`，单场景 target 保持独立可单跑
- 已为 `tools/sim_client/run_regression.sh` 补充失败快照归档：
  - 默认失败时保留到 `build/test-artifacts/sim_client/`
  - 可通过 `KEEP_ARTIFACTS=1` 保留成功场景产物
  - `latest-<scenario>` 符号链接指向最近一次保留目录
- 归档内容沿用原工作目录结构，包含进程日志、配置文件、fifo、etcd 数据目录和 `artifact_reason.txt`
- 已新增 [summarize_artifact.sh](/home/dev/projects/some-server/tools/sim_client/summarize_artifact.sh)
  - 可读取具体产物目录或 `latest-<scenario>` 链接
  - 会先提取 `last_login_ok`、`last_kick_reason`、`player lease`、`maria` 等关键信号
  - 然后再输出配置文件列表与 `etcd/relay/game/gate/sim_client` 的详细日志尾部
- `run_regression.sh` 保留产物后会直接打印摘要脚本调用方式，减少二次查找成本
- `run_all_regressions.sh` 现在会在某个场景失败后，自动尝试展开对应 `latest-<scenario>` 摘要
- 这样聚合入口失败时不需要再手动定位产物目录，就能先看到关键失败信号
- 已新增 [host-integration-ci.md](/home/dev/projects/some-server/docs/player-system/host-integration-ci.md)
  - 收敛 host-only 回归的本地、nightly、手工流水线接入方式
  - 明确 `ENABLE_HOST_INTEGRATION_TESTS=ON` 的使用边界
  - 明确失败摘要、产物目录和排障顺序
- 已收敛“默认数据玩家后续落盘”状态语义：
  - `PlayerRepositoryRecord` 新增 `pending_initial_persist`
  - `PlayerRepositoryRecord` 新增 `created_without_maria`
  - 新玩家首次创建后会显式标记“首次待落盘”
  - 若是 `maria connection is unavailable` 下的受控创建，则会显式标记“因 Maria 不可用创建”
- `PlayerPersistenceService` 现在会优先 flush `pending_initial_persist` 玩家，不再要求它先等到常规最小落盘时间
- `player_status` 已补充输出：
  - `pending_initial_persist`
  - `created_without_maria`
- 已完成最小运行验证：
  - 在 `build/bin` 启动单进程 `game`
  - `player_activate 910000123`
  - `player_status 910000123`
  - 验证首次加载时 `pending_initial_persist=true`
  - `player_flush 910000123`
  - 再次 `player_status 910000123`
  - 验证 flush 后 `pending_initial_persist=false`
- 已补 `ReleasePlayer()` 的释放前 flush 保护：
  - 若仓储仍为 `dirty`，释放前会先要求 `FlushPlayer()` 成功
  - 若 flush 失败，会撤回 `unloading` 状态并返回错误
  - 避免 Maria 异常时把“首次待落盘”或其他脏玩家静默释放掉
- 已增强 `player_release` 命令失败时的可观测性：
  - 失败日志会附带 `session_present/state`
  - 失败日志会附带 `loaded/dirty/pending_initial_persist/created_without_maria`
  - 失败日志会附带 `flush_count/last_dirty_ms/last_flush_ms`
  - 这样可以直接判断是否被“释放前 flush 保护”拦住
- 已完成最小正常路径验证：
  - 在 `build/bin` 启动单进程 `game`
  - `player_activate 910000124`
  - `player_release 910000124`
  - `player_status 910000124`
  - 验证正常释放后 `session_present=false` 且 `repository_present=false`
- 已再次完成正常路径回归：
  - `player_activate 910000125`
  - `player_release 910000125`
  - 确认新增失败观测代码未影响成功释放路径
- 已增强 `player_persistence_status` 聚合观测：
  - 新增 `tracked_player_count`
  - 新增 `pending_initial_persist_count`
  - 新增 `created_without_maria_count`
- 已修正聚合计数口径：
  - 同一轮扫描里若首次待落盘玩家已经 flush 成功
  - `pending_initial_persist_count` / `created_without_maria_count` 会按扫描结束后的结果回落
- 已完成最小运行验证：
  - 在 `build/bin` 启动单进程 `game`
  - `player_activate 910000128`
  - `player_persistence_flush_due`
  - `player_persistence_status`
  - 验证扫描后 `tracked_player_count=1`
  - 验证成功 flush 后 `pending_initial_persist_count=0`
- 已增强 `player_persistence_player_status` 单玩家视图：
  - 新增 `repository_present`
  - 新增 `loaded/dirty`
  - 新增 `pending_initial_persist/created_without_maria`
  - 新增 `flush_count/last_dirty_ms/last_flush_ms`
  - 这样单玩家视图会同时给出“仓储当前状态 + retry/backoff 状态”
- 已完成最小运行验证：
  - 在 `build/bin` 启动单进程 `game`
  - `player_activate 910000129`
  - `player_persistence_player_status 910000129`
  - `player_persistence_flush_due`
  - 再次 `player_persistence_player_status 910000129`
  - 验证新视图可直接显示 `repository_present=true`
  - 验证成功落盘后可直接显示 `flush_count=1` 且 `pending_initial_persist=false`
- 已把最近落地的存储策略补回设计文档：
  - [game-player-storage-design.md](/home/dev/projects/some-server/docs/player-system/game-player-storage-design.md)
  - [player-login-storage-design.md](/home/dev/projects/some-server/docs/player-system/player-login-storage-design.md)
- 文档已明确当前实现口径：
  - `DatasetPut()` 当前以 Maria 写成功为准，Redis 回写为 best-effort
  - 当前没有独立落盘队列，而是本地 `dirty + persistence scheduler`
  - 已明确 `pending_initial_persist / created_without_maria`
  - 已明确释放前 flush 保护与失败拒绝释放
- 已收敛 `StorageService` 的 schema ensure 语义：
  - `EnsureMariaEntriesTable()` 现在只负责轻量 `CREATE TABLE IF NOT EXISTS`
  - 运行时读写路径不再隐式执行 `ALTER TABLE`
  - 新增显式迁移入口 `MigrateMariaEntriesTableToBinary()`
  - `game` 新增运行时命令 `storage_dataset_migrate_binary <dataset>`
- 已收敛 `StorageService` 的本地 Maria 故障注入能力：
  - 新增 `DisableMariaTarget()` / `EnableMariaTarget()`
  - `game` 新增运行时命令：
    - `storage_disable_maria <name>`
    - `storage_enable_maria <name>`
  - 仅影响本地进程视角，用于回归和运维演练，不改业务协议
- 已修复 `PlayerDirectoryService` 的纯内存分配缺口：
  - 账号到 `player_id` 映射当前优先持久化到 Redis
  - `next_player_id` 计数器当前持久化到 Redis
  - 重启后不会再默认从本地 `100000` 重新开始分配
  - 已完成最小验证：
    - 首次 `player_directory_resolve dev persist-check-3 1` 返回 `player_id=100000 created=true`
    - 重启后再次 resolve 同账号返回 `player_id=100000 created=false`
- 已进一步把目录命名空间从玩家数据命名空间解耦：
  - 新增 `directory` dataset
  - Redis 前缀：`player_directory:`
  - `PlayerDirectoryService` 不再借用 `player` dataset
  - 已完成最小验证：
    - `storage_status` 显示 `datasets=2`
    - `directory` dataset 的 `full_redis_prefix=some_server:player_directory:`
- 已把 5 条 host-only 回归场景的覆盖点补回文档：
  - [README.md](/home/dev/projects/some-server/README.md)
  - [host-integration-ci.md](/home/dev/projects/some-server/docs/player-system/host-integration-ci.md)
  - 文档不再只列场景名，也明确每条场景在验证什么
- 已把这两个运行时入口补回文档操作建议：
  - [game-player-storage-design.md](/home/dev/projects/some-server/docs/player-system/game-player-storage-design.md)
  - [host-integration-ci.md](/home/dev/projects/some-server/docs/player-system/host-integration-ci.md)
  - 已明确 `storage_dataset_init` 与 `storage_dataset_migrate_binary` 的推荐顺序
- 已把“默认数据玩家后续落盘与 Maria 恢复策略”补成文档化结论：
  - [game-player-storage-design.md](/home/dev/projects/some-server/docs/player-system/game-player-storage-design.md)
  - [player-login-storage-design.md](/home/dev/projects/some-server/docs/player-system/player-login-storage-design.md)
  - 已明确“允许先服务、后补档，但未补档前不能当作已落盘”
- 已新增 [default-player-recovery-checklist.md](/home/dev/projects/some-server/docs/player-system/default-player-recovery-checklist.md)
  - 将“先服务、后补档”策略收敛成运维检查项
  - 明确补档完成判定条件
  - 明确 Maria 恢复后的检查顺序
  - 明确何种状态不能放行
- 已新增自动化 host-only 场景：
  - `default_player_recover_after_maria_restore`
  - 覆盖链路：
    - 本地禁用 Maria
    - 新玩家首登走默认数据
    - `pending_initial_persist / created_without_maria` 生效
    - 恢复 Maria
    - `player_persistence_recover_flush`
    - 补档成功后状态清零
- 已完成真实回归验证：
  - `cmake --build build --target sim_client_regression_default_player_recover_after_maria_restore -j 4`
  - 场景返回 `ok`
- 已补目录服务运行时观测命令：
  - `player_directory_lookup <platform> <account_id> <area_id>`
  - `player_directory_counter`
  - 可直接查看目录映射是否已存在，以及 Redis 中持久化的 `next_player_id` 计数器
  - 最小验证已通过：
    - `player_directory_lookup dev namespace-observe-1 1` -> `present=false`
    - `player_directory_resolve dev namespace-observe-1 1` -> `player_id=100001 created=true`
    - 再次 `player_directory_lookup` -> `present=true player_id=100001`
    - `player_directory_counter` -> `present=true next_player_id=100001`
- 已新增第 5 条 host-only 回归场景：
  - `player_directory_persist_across_restart`
  - 覆盖链路：
    - `player_directory_lookup` 初始 miss
    - `player_directory_resolve` 创建目录映射
    - `player_directory_counter` 记录当前持久化计数器
    - 重启 `game1`
    - `player_directory_lookup` 命中同一 `player_id`
    - `player_directory_counter` 保持不变
  - 已完成真实验证：
    - `tools/sim_client/run_regression.sh player_directory_persist_across_restart` -> `ok`
    - `cmake --build build --target sim_client_regression_player_directory_persist_across_restart -j 4` -> `ok`
  - 已顺手修正回归脚本两处问题：
    - 日志断言统一改为 `grep -a`，避免控制字符导致 binary log 误判
    - `game1` 重启验证改用新的监听端口，避免旧端口短暂占用导致脚本误报
- 已完成 5 条 host-only 场景的全量串行回归：
  - `tools/sim_client/run_all_regressions.sh` -> `sim_client regressions: all ok`
  - 当前通过场景：
    - `reconnect_after_disconnect`
    - `same_account_kick`
    - `migration_recover_after_kick`
    - `default_player_recover_after_maria_restore`
    - `player_directory_persist_across_restart`
  - 本轮顺手修正了两个旧脚本硬编码前提：
    - `migration_recover_after_kick` 不再硬编码 `player_id=100000`，改为使用实际登录出来的 `player_id`
    - `default_player_recover_after_maria_restore` 不再写旧的 `player` dataset 目录计数器 key，改为当前 `directory` dataset 的 Redis key
- 已新增 CI / nightly 统一入口脚本：
  - `tools/sim_client/run_host_ctest.sh`
  - 固定流程：
    - `cmake -S . -B build -DENABLE_HOST_INTEGRATION_TESTS=ON`
    - `cmake --build build --target relay gate game sim_client`
    - `ctest --test-dir build -R sim_client_regression_ --output-on-failure`
  - 可通过 `CTEST_REGEX` 收窄到单场景
  - 已完成最小验证：
    - `CTEST_REGEX=sim_client_regression_player_directory_persist_across_restart tools/sim_client/run_host_ctest.sh`
    - 结果：`1/1 Test #13: sim_client_regression_player_directory_persist_across_restart ... Passed`
- 已按当前目标继续收口设计边界：
  - 文档已明确当前不提前考虑 `social`
  - 文档已明确当前不继续展开 CI / nightly 与离线迁移流程
  - 文档已明确 `PlayerDirectoryService` 作为正式组件保留并继续使用
  - 文档已明确现有 5 条 host-only 回归场景就是当前测试完成线
- 已新增 [current-scope-and-boundary.md](/home/dev/projects/some-server/docs/player-system/current-scope-and-boundary.md)
  - 单独记录当前版本范围
  - 单独记录当前完成线
  - 单独记录当前明确不做的事项
- 已完成业务层 proto 目录重构：
  - `proto/` 顶层当前只保留：
    - `proto/ipc/`
    - `proto/game/`
  - 原 `proto/client/` 与 `proto/player/` 已合并进 `proto/game/`
  - 原 `client_proto` 与 `player_data_proto` 已合并为 `game_proto`
  - 业务层生成目录已统一到 `src/protocol/game/pb/`
  - 对应生成脚本已统一到 `tools/game_proto/gen_game_proto.sh`
  - `proto/game/` 当前已进一步收平为单目录：
    - `common.proto`
    - `login.proto`
    - `player.proto`
    - `player_data.proto`
  - `src/protocol/game/pb/` 当前也已收平为单目录生成产物
  - 已完成验证：
    - `cmake --build build --target game_proto gate game sim_client -j 4`
    - `tools/sim_client/run_regression.sh reconnect_after_disconnect` -> `ok`

## 当前总体进度判断

## 当前协议目录补充说明

当前协议目录已进一步收口为：

- `proto/ipc/`
- `proto/game/`

其中：

- 原 `proto/client/` 与 `proto/player/` 已合并进 `proto/game/`
- 原 `client_proto` 与 `player_data_proto` 已合并为 `game_proto`
- 业务层生成目录已统一到 `src/protocol/game/pb/`

按阶段看：

- 阶段 1：已完成
- 阶段 2：已完成最小骨架
- 阶段 3：进行中
- 阶段 4：进行中
- 阶段 5：进行中
- 阶段 6：已完成第一轮

按整体工程看：

- 设计阶段：已完成第一轮
- 协议工程基础阶段：已完成客户端侧第一步
- 业务实现阶段：已进入真实接入骨架、最小登录闭环与依赖收口阶段

## 辅助产物

- 已新增全局 skill：`player-system-integration`
- 安装位置：`/home/dev/.codex/skills/player-system-integration`
- 作用：
  - 收敛本仓库 `relay + game + gate + sim_client` 联调的固定命令顺序
  - 收敛登录、持久化、same-game lease 恢复、cross-game owner takeover 的标准检查点
  - 提供 Redis 测试态清理脚本 `scripts/reset_player_state.sh`
- 说明：
  - 该 skill 不在仓库内，因为当前仓库 `.agents/` 为只读
  - `quick_validate.py` 依赖 `PyYAML`，当前环境缺少该模块，因此未完成官方校验脚本验证
  - 已完成基础可读性检查，清理脚本用法验证正常

## 下一步建议

优先顺序建议：

1. 继续维持“显式断开 + 显式重连提示”路线，不做无感迁移。
2. 将当前 5 条 host-only 回归场景视为“基本功能 + 常见高频问题”的当前测试完成线：
   - `reconnect_after_disconnect`
   - `same_account_kick`
   - `migration_recover_after_kick`
   - `default_player_recover_after_maria_restore`
   - `player_directory_persist_across_restart`
3. 保留并继续使用 `PlayerDirectoryService`，当前职责保持在：
   - 账号身份到 `player_id` 的目录映射
   - `next_player_id` 持久化计数器
4. 当前不继续展开：
   - `social`
   - CI / nightly
   - 离线迁移流程
   - 低频释放、补档、lease 丢失等边角问题

## 维护规则

从本次开始，后续每推进一步都同步更新本文件，至少维护这些内容：

- 当前新增或完成了哪个阶段
- 哪些文件已落地
- 做了什么验证
- 是否形成新提交
- 下一步计划是否变化
