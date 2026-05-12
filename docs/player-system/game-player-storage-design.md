# Game 玩家数据与存储设计

## 状态

- 草案
- 范围：`game` 内玩家 protobuf 数据结构、Redis 热存储、MariaDB 持久化、lease、延迟落盘、暂留与释放

## 目标

`game` 是玩家对象唯一权威持有者。

本设计要解决：

- 玩家 protobuf 数据如何组织
- Redis 如何存
- MariaDB 如何存
- 如何避免同一玩家被多个 game 同时加载
- 如何支持断线后玩家暂留
- 如何支持异步落盘

## 进程内服务划分

建议至少拆成：

- `player_directory_service`
- `player_repository`
- `player_session_service`
- `player_persistence_service`
- `player_command_dispatcher`

## 玩家对象状态

建议状态：

- `loading`
- `online`
- `detached`
- `unloading`

### `loading`

- 正在获取 lease、读 Redis、回源 MariaDB、构建对象

### `online`

- 有 gate 会话，能正常处理消息

### `detached`

- 当前无 gate 会话
- 玩家对象暂留在内存
- 可在暂留期内被重连复用

### `unloading`

- 准备释放
- 做最后 flush
- 解绑 `PlayerReceiver`

## 玩家 protobuf 数据组织

建议客户端可见玩家数据与服务端持久化玩家数据共用领域模型思路，但不等于共用所有 message。

建议目录：

```text
proto/player/
  v1/
    player_base.proto
    player_inventory.proto
    player_progress.proto
    player_blob.proto
```

建议根对象：

- `PlayerData`

建议先按少量大块拆分：

- `base`
- `core`
- `extensions`

不要一开始拆成非常多的小块。

## Redis 键设计

建议首版按“少量大块 blob + metadata”设计。

### 元数据

```text
player:{player_id}:meta
```

建议字段：

- `owner_token`
- `owner_process_id`
- `lease_expire_at_ms`
- `state`
- `data_version`
- `last_land_at_ms`

### 数据块

```text
player:{player_id}:base
player:{player_id}:core
player:{player_id}:extensions
```

### 落盘队列

```text
player:landing
```

## MariaDB 存储设计

首版建议保持简单。

### 建议表

- `player_entries`

字段建议：

- `entry_key`
- `entry_value`
- `updated_at`

其中：

- `entry_key = player_id`
- `entry_value = PlayerData` 的序列化结果

若后续确认需要模块化存储，再拆表；首版不建议过早做高范式化。

## lease 设计

### 目标

- 防止多个 game 同时加载同一玩家
- 支持 owner 崩溃后的恢复

### lease 元数据建议

- `owner_token`
- `owner_process_id`
- `lease_expire_at_ms`

### 规则

1. 无 owner 时，可获取 lease
2. owner 过期时，可接管 lease
3. 非 owner 不能写玩家 Redis 热数据
4. 非 owner 不能释放玩家实例元数据

### 与 `PlayerReceiver` 的关系

- lease 获取成功后，才能继续加载玩家
- 玩家对象真正可服务后，才绑定 `PlayerReceiver(player_id)`
- 卸载时先停止接收消息，再解绑 receiver，再释放 lease

## 加载流程

1. 解析 `player_id`
2. 查看本地是否已有实例
3. 若无本地实例，则尝试获取 lease
4. 先查 Redis metadata
5. 若 Redis 数据完整，则加载 Redis
6. 若 Redis 数据不存在或损坏，则从 MariaDB 回源
7. 回源后重建 Redis 热数据
8. 构建内存玩家对象
9. 执行登录时初始化逻辑
10. 绑定 `PlayerReceiver`

## 保存流程

1. 业务逻辑修改内存 protobuf 数据
2. 标记脏块
3. 请求结束或 tick 结束时 flush 到 Redis
4. 更新 metadata 状态
5. 将玩家加入落盘队列

## 延迟落盘设计

### 目标

- 避免每次玩家请求都同步写 MariaDB

### 机制

- Redis 写热数据
- `player:landing` 记录待落盘 player
- 后台落盘服务按时间窗口取出
- 从 Redis 重建待落盘快照
- 写入 MariaDB

### 规则

- 玩家释放前必须至少登记过待落盘
- 落盘成功后才更新 `last_land_at_ms`

## 暂留与释放

### 暂留

客户端断线时：

1. 玩家从 `online` 转 `detached`
2. 保留内存实例
3. 保留 Redis 热数据
4. 在超时时间内允许重连复用

### 释放

暂留超时或明确登出时：

1. 进入 `unloading`
2. flush 脏数据到 Redis
3. 确保落盘任务已登记
4. 解绑 `PlayerReceiver`
5. 销毁内存实例
6. 释放 lease

## 与 Redis/MariaDB 的职责关系

Redis：

- 热数据
- lease
- 落盘队列
- 快速恢复

MariaDB：

- 最终持久化
- 冷启动回源

## 首版实现建议

先实现最小可用模型：

1. 一个 `PlayerData`
2. 三个 Redis 大块
3. 一个 MariaDB 通用表
4. 一个 lease 机制
5. 一个落盘队列
6. `online/detached/unloading` 三态

这足够支撑长连接、断线重连、异步存储。
