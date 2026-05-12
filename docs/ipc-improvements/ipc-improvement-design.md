# IPC 模块改进设计

## 状态

- 草案
- 范围：在不破坏现有 IPC 基础层职责的前提下，评估并定义为支撑 `gate + game + sim_client` 所需的改进点

## 目标

本设计不是重写 IPC。

本设计要回答两个问题：

1. 当前 IPC 哪些能力已经足够
2. 哪些能力需要补，但应该补在什么层级

## 现有 IPC 可直接复用的能力

当前 IPC 已具备：

- discovery
- transport
- link
- router
- messenger
- receiver directory
- `ProcessReceiver`
- `ServiceReceiver`
- `PlayerReceiver`

这些能力已经足以作为：

- `gate <-> game`
- `game <-> game`
- 后续 `game <-> social`

之间的基础消息通道。

## 当前 IPC 不足的地方

不足主要不在“底层消息送达”，而在“业务协作协议与接收分发模式”。

### 1. 缺少 `gate` 侧 IPC 服务接入

当前 `game` 已经有较完整的 IPC 集成，`gate` 基本没有。

需要补：

- `gate` 进程的 IPC runtime
- `gate` 的 service/process receiver
- `gate` 的业务消息接收入口

### 2. `PlayerReceiver` 还没有真正落到玩家实例分发

当前更像“绑定检查”，还不是完整业务分发。

需要补：

- `PlayerReceiverHost` 与 `PlayerSessionService` 对接
- 让玩家消息真正分发到本地玩家实例

### 3. 缺少业务层 request-response 约定

登录、顶号、消息转发等场景都需要请求/响应。

不建议改 IPC 基础 API 去做重型 RPC。

建议：

- 在业务协议层显式带 `request_id`
- 调用方维护 pending 请求表

### 4. 缺少 gate 会话接收模型

`game -> gate` 推送或踢线，需要 gate 有稳定的业务接收入口。

建议首版：

- 不新增通用 `SessionReceiverType`
- 使用 `ServiceReceiver(gate)` 或 `ProcessReceiver(gate)` 收业务消息
- gate 进程内再根据 `gate_session_id` 路由到本地连接

这个方案更简单，也更符合 KISS。

## 建议改进点

### 改进 1：为 `gate` 建立与 `game` 对等的 IPC 服务

建议新增：

- `GateIpcService`
- 本地 service receiver host
- 本地 process receiver host

用于承接：

- `KickAccountSession`
- `PushPlayerMessage`
- 登录回包
- 重连回包

### 改进 2：补齐 IPC 上层业务协议，而不是改底层 IPC 消息模型

建议在 `proto/ipc/` 下新增业务协议子目录，例如：

```text
proto/ipc/gate_game/v1/
```

注意：

- 这属于 IPC 上层业务协议
- 不属于客户端协议
- 也不属于 IPC transport/control 的基础协议扩展

### 改进 3：为 `gate` 和 `game` 增加统一的业务消息分发层

底层 messenger 负责送达 protobuf message。

上层还需要：

- 业务消息注册
- `request_id` 跟踪
- 响应回调
- 失败处理

建议这层放在 `gate/services`、`game/services` 或共享业务中间层，不放进 `framework/ipc` 基础层。

### 改进 4：明确 `PlayerReceiver` owner 的生效点

建议规则：

1. `game` 成功获取 lease
2. `game` 完成玩家对象加载
3. `game` 本地绑定 `PlayerReceiver(player_id)`
4. `game` 更新 Redis 的 `player -> game`
5. 才对外宣布该玩家可服务

这样可以减少 owner 与共享路由状态不一致的窗口。

### 改进 5：增加必要的运维观察命令

建议为 `gate` 和 `game` 增加最小运行时命令：

`gate`：

- `gate_ipc_status`
- `gate_sessions`
- `gate_session <account|player>`

`game`：

- `player_owner_status <player_id>`
- `player_receiver_status <player_id>`
- `player_route_refresh <player_id>`

## 不建议做的 IPC 改动

为了保持 KISS，首版不建议做以下改动：

### 1. 不新增通用 `SessionReceiverType`

原因：

- 会把接入层会话语义过早下沉到通用 IPC 抽象
- 当前 `ServiceReceiver + gate session id` 已足够使用

### 2. 不在 IPC 基础层做通用 RPC 框架

原因：

- 需求目前主要是少量 request-response
- 业务层自己带 `request_id` 即可

### 3. 不把玩家 owner 目录做成第二套复杂分布式目录系统

原因：

- 当前已有 discovery 和 receiver 机制
- 再做一套强一致远程目录会明显提高复杂度

### 4. 不把客户端协议并入 IPC 协议体系

原因：

- 这会直接破坏客户端协议与 IPC 协议完全分离的硬约束

## 分层建议

建议明确三层：

### 1. IPC 基础层

位置：

- `src/framework/ipc/`

职责：

- transport
- discovery
- routing
- receiver
- messenger

### 2. IPC 上层业务协议层

位置建议：

- `proto/ipc/gate_game/...`
- `proto/ipc/game_social/...`

职责：

- 定义服务间业务消息

### 3. 服务业务接收与分发层

位置建议：

- `src/gate/services/`
- `src/game/services/`

职责：

- protobuf 解包
- request/response 对应
- 调用具体业务服务

## 推荐落地顺序

1. 先给 `gate` 接入 IPC 运行时
2. 定义 `gate_game` 业务协议
3. 建立 `gate` 业务接收入口
4. 建立 `game` 玩家消息分发入口
5. 补 request-response 跟踪

## 结论

当前 IPC 架构总体方向是对的，足以承接这套系统。

真正需要做的不是“重写 IPC”，而是：

- 补 `gate` 的 IPC 集成
- 补 IPC 上层业务协议
- 补业务接收与 request-response 机制
- 让 `PlayerReceiver` 真正落到玩家实例分发

只要守住这些边界，就能在不破坏现有架构的前提下完成扩展。
