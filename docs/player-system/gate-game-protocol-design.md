# Gate 与 Game 内部业务协议设计

## 状态

- 草案
- 范围：`gate <-> game` 之间用于登录、消息转发、推送、断线、顶号协同的内部业务协议

## 目标

本设计建立在现有 IPC 通道之上，但不把业务协议下沉进 IPC 基础层。

也就是说：

- IPC 负责送达
- 本文定义送什么

## 强约束

- 本文协议属于服务端内部业务协议
- 目录必须位于 `proto/ipc/...`
- 不得 import 客户端 `.proto`
- 不得复用客户端 message 作为 IPC message

## 协议分层

建议把内部业务协议按子域拆开：

```text
proto/ipc/
  gate_game/v1/
    login.proto
    session.proto
    player_message.proto
    push.proto
```

## 消息分类

### 登录类

- `LoginPlayerRequest`
- `LoginPlayerResponse`
- `ReconnectPlayerRequest`
- `ReconnectPlayerResponse`

### 会话类

- `KickAccountSession`
- `KickAccountSessionAck`
- `PlayerDisconnected`
- `BindPlayerSession`
- `UnbindPlayerSession`

### 玩家消息类

- `ForwardPlayerMessageRequest`
- `ForwardPlayerMessageResponse`

### 推送类

- `PushPlayerMessage`

## 登录协议建议

### `LoginPlayerRequest`

建议字段：

```text
request_id
gate_process_id
gate_session_id
platform
account_id
area_id
client_version
channel
login_token / auth_context
```

### `LoginPlayerResponse`

建议字段：

```text
request_id
result_code
player_id
game_process_id
is_reconnect
initial_player_snapshot
error_message
```

说明：

- `initial_player_snapshot` 是面向客户端初始化回包的数据载荷
- 这是内部协议自己的 message，不等于客户端登录响应 message

## 玩家消息转发协议

### `ForwardPlayerMessageRequest`

建议字段：

```text
request_id
gate_process_id
gate_session_id
player_id
message_id
payload_bytes
client_sequence
timestamp_ms
```

### `ForwardPlayerMessageResponse`

建议字段：

```text
request_id
player_id
message_id
result_code
response_payload_bytes
server_sequence
error_message
```

## 推送协议

### `PushPlayerMessage`

建议字段：

```text
player_id
gate_process_id
gate_session_id
message_id
payload_bytes
push_sequence
```

说明：

- `game` 推送时最好带上它认知的 gate 路由信息
- gate 若发现本地 session 已变更，可拒绝投递并记录

## 断线与重连协议

### `PlayerDisconnected`

用途：

- gate 告知 game 该玩家当前入口连接已断开

建议字段：

```text
player_id
gate_process_id
gate_session_id
reason
occurred_at_ms
```

### `ReconnectPlayerRequest`

用途：

- 新 gate 尝试把玩家重新挂接到原 game

建议字段：

```text
request_id
player_id
gate_process_id
gate_session_id
account_id
client_version
```

### `ReconnectPlayerResponse`

建议字段：

```text
request_id
result_code
player_id
game_process_id
player_snapshot
error_message
```

## 顶号协议

### `KickAccountSession`

用途：

- 新 gate 通知旧 gate 踢掉同账号连接

建议字段：

```text
request_id
account_id
old_gate_process_id
old_gate_session_id
new_gate_process_id
reason
```

### `KickAccountSessionAck`

建议字段：

```text
request_id
result_code
account_id
old_gate_session_id
```

## request-response 设计

当前不建议侵入 IPC 基础层做通用 RPC。

首版建议：

- 业务层显式带 `request_id`
- 请求方维护 pending map
- 响应方原样带回 `request_id`

这样足够支撑：

- 登录
- 顶号
- 玩家请求响应
- 重连

## 失败语义

每个 response 都应至少支持：

- 成功
- 参数非法
- 账号校验失败
- 玩家不存在
- 玩家正被其他 game 持有
- 会话已失效
- player/game 路由不一致
- 内部错误

## 与客户端协议的边界

这里再次强调：

- 内部协议只承载服务协作语义
- 客户端协议只承载网络接入语义

即便 `message_id` 一样，也不意味着 message 定义可复用。

内部协议里应使用：

- `payload_bytes`
- `message_id`

来承载客户端消息内容，而不是直接 import 客户端 message 类型。

## 首版实现建议

优先定义以下最小集合：

1. `LoginPlayerRequest/Response`
2. `ForwardPlayerMessageRequest/Response`
3. `PushPlayerMessage`
4. `PlayerDisconnected`
5. `ReconnectPlayerRequest/Response`
6. `KickAccountSession/KickAccountSessionAck`

这已经足够打通完整链路。
