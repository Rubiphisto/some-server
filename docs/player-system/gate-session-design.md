# Gate 长连接与会话设计

## 状态

- 草案
- 范围：`gate` 进程中的客户端长连接、登录会话、顶号、断线重连、共享路由状态

## 目标

`gate` 是接入层，不持有玩家业务对象，但必须可靠管理客户端连接和玩家会话。

本设计覆盖：

- TCP 长连接
- 登录前匿名连接
- 登录后账号会话
- 同账号顶号
- 客户端断线
- 客户端重连
- `gate -> game` 的玩家路由维护

## 职责边界

`gate` 负责：

- accept/read/write
- 心跳与超时
- 客户端协议解包与封包
- 登录请求受理
- 会话建立与销毁
- 顶号
- 路由 `player_id -> game`
- 将客户端请求转发给 `game`
- 将 `game` 推送转发给客户端

`gate` 不负责：

- 玩家对象创建
- 玩家数据加载
- 玩家逻辑执行
- 玩家数据存储

## 进程内服务划分

建议在 `src/gate/services/` 下逐步形成以下服务：

- `connection_service`
- `session_service`
- `auth_service`
- `routing_service`
- `ipc_service`
- `protocol_service`

## 连接模型

### 连接状态

建议状态：

- `accepted`
- `handshaking`
- `anonymous`
- `authenticating`
- `bound`
- `closing`
- `closed`

### 连接对象建议字段

```text
connection_id
socket_fd / transport_handle
remote_endpoint
last_recv_time
last_send_time
heartbeat_deadline
recv_buffer
send_queue
state
```

### 基本规则

- 每个 TCP 连接由一个唯一 `connection_id` 标识
- 未登录前不允许进入玩家协议转发
- 编解码错误、心跳超时、背压失控应触发主动断开

## 会话模型

### 会话状态

建议状态：

- `anonymous`
- `logging_in`
- `active`
- `kicked`
- `reconnecting`
- `closing`

### 会话对象建议字段

```text
gate_session_id
connection_id
account_id
player_id
game_process_id
login_version
session_epoch
last_active_time
```

### 关键关系

本地需要维护：

- `connection_id -> gate_session`
- `account_id -> connection_id`
- `player_id -> gate_session`

Redis 需要维护共享关系：

- `account_id -> gate_process_id + connection_id`
- `player_id -> gate_process_id + gate_session_id`
- `player_id -> game_process_id`

## 登录流程

### 本地流程

1. 连接建立
2. 协议握手、版本检查
3. 客户端发送登录请求
4. `auth_service` 完成账号验证
5. `session_service` 检查是否已有旧会话
6. 若有旧会话，先执行顶号
7. `ipc_service` 向目标 `game` 发送登录请求
8. 收到 `game` 登录成功回包
9. 本地建立 `active` 会话
10. 回客户端登录成功

### 与 `game` 的边界

`gate` 不创建 `player_id`，只接收 `game` 返回结果。

## 顶号设计

### 规则

- 唯一入口按 `account_id` 控制
- 新连接优先，旧连接必须失效

### 同 gate 顶号

1. 找到旧 `connection_id`
2. 标记旧会话 `kicked`
3. 发送踢线通知
4. 主动关闭旧连接
5. 释放旧本地索引

### 跨 gate 顶号

1. Redis 查到旧连接所在 gate
2. 通过 IPC 发送 `KickAccountSession`
3. 目标 gate 关闭旧连接并回执
4. 新 gate 再继续登录流程

### 一致性原则

- 顶号动作以 IPC 通知驱动
- Redis 只作为共享定位信息
- 不应依赖轮询 Redis 完成踢线

## 断线与重连

### 断线处理

1. `connection_service` 感知 socket 断开
2. `session_service` 将会话转为失效
3. 清理本地 `connection -> session`
4. 保留必要路由信息直到通知 `game`
5. `ipc_service` 向对应 `game` 发送 `PlayerDisconnected`

### 重连处理

1. 新连接登录成功
2. Redis 或 `game` 返回现有 `player -> game`
3. `gate` 向原 `game` 发起 `ReconnectPlayerRequest`
4. 若 `game` 中玩家仍处于暂留态，则恢复绑定
5. 新 `gate` 替换旧 `player -> gate session`

## 心跳与超时

建议至少区分两层：

- TCP/连接层心跳
- 业务层会话活跃时间

连接超时建议触发：

- 关闭连接
- 回收本地会话
- 通知 `game`

## 推送路径

### `game -> gate -> client`

1. `game` 确认 `player_id`
2. 根据 Redis 或缓存路由找到 `gate_process_id + gate_session_id`
3. 通过 IPC 将推送发给目标 gate
4. gate 定位本地连接
5. 编码客户端协议并下发

## 与 Redis 的关系

Redis 存的是共享事实，不存瞬时动作。

适合放 Redis 的内容：

- `account -> gate connection`
- `player -> gate session`
- `player -> game process`

不适合放 Redis 的内容：

- “请立即踢掉连接”
- “请立即转发一条消息”
- “请立即重绑会话”

这些都是 IPC 动作。

## 首版实现建议

首版优先支持：

- 单连接单账号登录
- 顶号
- 断线通知
- 重连恢复
- 玩家消息转发
- 推送下发

首版不建议支持：

- 连接迁移
- 多路复用会话
- 客户端直连多个逻辑进程

## 实现入口建议

后续编码时，建议先实现：

1. `connection_service`
2. `protocol_service`
3. `session_service`
4. `ipc_service`
5. `routing_service`

只有这几块成形后，`gate` 才真正具备接入层能力。
