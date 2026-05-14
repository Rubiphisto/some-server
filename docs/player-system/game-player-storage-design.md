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

## 当前落地口径

以下内容不是未来想象，而是当前实现已经收敛出的主口径：

- 玩家持久化 protobuf 当前使用单个 `PlayerData`
- MariaDB 当前使用一个通用 `player_entries` 表保存二进制 blob
- `StorageService::DatasetPut()` 当前是先写 Maria，再 best-effort 回写 Redis
- 当前没有独立的 Redis 落盘队列服务
- 当前落盘驱动来自 `PlayerRepository dirty + PlayerPersistenceService` 后台扫描
- 允许“首登新玩家 + Maria 不可用”时受控创建默认数据，但会显式标记为待首次落盘

因此本文档里凡是提到“落盘队列”的部分，都应理解为后续可选演进方向，不是当前已经落地的事实。

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

说明：

- 这是设计上的可选演进方向
- 当前实现尚未引入独立落盘队列
- 当前落盘调度依赖本地 `dirty` 状态与后台扫描

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

当前实现补充：

- `entry_value` 已明确使用二进制列，不再使用文本列
- 运行时轻量 ensure 当前只负责 `CREATE TABLE IF NOT EXISTS`
- 列类型迁移应通过显式入口执行，而不是夹在每次读写里
- 当前 `DatasetPut()` 成功条件以 Maria 写成功为准
- Redis 回写失败不会让 `DatasetPut()` 整体失败，但会在返回信息中保留状态
- `DatasetGet()` 仍保持 Redis 优先、Maria 回源、回源后回填 Redis

### 当前操作建议

当前实现建议把“建表”和“迁移”分开操作：

- `storage_dataset_init <dataset>`
  - 只负责轻量建表
  - 适合首次初始化或确认表存在
- `storage_dataset_migrate_binary <dataset>`
  - 显式把 `entry_value` 迁移为 `LONGBLOB`
  - 适合历史环境修表
  - 不应夹在日常运行时读写路径中

推荐顺序：

1. 先执行 `storage_dataset_init player`
2. 若是历史环境，再执行 `storage_dataset_migrate_binary player`
3. 再进行正常 `DatasetGet / DatasetPut / FlushPlayer` 路径验证

### 目录命名空间

当前实现已把玩家目录键从玩家数据键里解耦出来：

- 目录 dataset：`directory`
- Redis 前缀：`player_directory:`
- 玩家数据 dataset：`player`
- Redis 前缀：`player:`

这意味着：

- `platform + account_id + area_id -> player_id` 的目录信息
- 不再借用玩家数据的 Redis 前缀
- 后续若继续独立目录服务或迁移目录存储，不需要碰玩家数据热态键空间

当前阶段结论：

- `PlayerDirectoryService` 继续保留
- 当前 `directory` dataset 方案已经满足主链路与高频问题处理
- 当前不为低频场景继续扩展额外目录子系统

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
3. 由后台持久化服务判断是否应 flush
4. flush 时调用 `StorageService::DatasetPut()`
5. 由 `DatasetPut()` 完成 Maria 持久化与 Redis best-effort 回写

当前实现补充：

- 仓储当前不是“分块脏标记”，而是整玩家 `dirty`
- `PlayerPersistenceService` 每秒扫描一次
- `dirty + detached` 会优先 flush
- `dirty` 超过 `landing_min_time_seconds` 会尝试 flush
- flush 失败会进入 retry/backoff，而不是每秒重试

## 延迟落盘设计

### 目标

- 避免每次玩家请求都同步写 MariaDB

### 机制

- 当前实现以后台扫描为主：
  - 读本地仓储状态
  - 判断 `dirty` / `detached` / `pending_initial_persist`
  - 直接执行 `FlushPlayer()`
- flush 失败后记录 retry/backoff
- Maria 恢复后可通过显式恢复命令触发重试

### 规则

- 玩家释放前若仍为 `dirty`，必须先 flush 成功
- 若释放前 flush 失败，则撤回 `unloading` 状态并拒绝释放
- `pending_initial_persist` 玩家会被优先 flush
- 落盘失败时要保留可观测状态，不能静默丢玩家

### 首登新玩家与 Maria 不可用

当前实现已明确支持一条受控回退路径：

1. 目录服务确认这是新创建的 `player_id`
2. `DatasetGet()` Redis miss 后若 Maria 不可用
3. 允许初始化默认 `PlayerData`
4. 仓储显式标记：
   - `pending_initial_persist=true`
   - `created_without_maria=true`
5. 后续只要 Maria 恢复，后台持久化会优先尝试把这类玩家落盘

这条路径只适用于“新玩家首次创建”。

旧玩家读档失败时，不应静默降级成默认新号。

### 默认数据玩家后续落盘与 Maria 恢复策略

当前实现口径已经明确如下：

1. 只要玩家仍处于：
   - `pending_initial_persist=true`
   - 或 `dirty=true`
   就不能被当作“已安全持久化玩家”对待

2. `pending_initial_persist` 的优先级高于普通脏数据
   - 后台持久化服务会优先尝试 flush
   - 不需要再等常规 `landing_min_time_seconds`

3. Maria 不可用时：
   - 允许新玩家先登录并进入游戏
   - 但该玩家必须一直保留“待首次落盘”语义
   - 直到真正完成一次 Maria 成功写入

4. Maria 恢复后：
   - 可以通过后台 scheduler 自然重试
   - 也可以通过显式恢复命令主动触发重试
   - 只有 `FlushPlayer()` 真正成功后，才清除：
     - `pending_initial_persist`
     - `created_without_maria`

5. 释放保护：
   - 这类玩家在释放前仍必须满足 flush 成功
   - 若 flush 失败，则拒绝释放并恢复原状态

6. 成功判定：
   - 不能只看 Redis
   - 必须以 Maria 持久化成功为准

这条策略的本质是：

- 允许“先服务、后补档”
- 但不允许“未补档就当作已落盘”

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
2. 若仓储仍为 `dirty`，先要求 `FlushPlayer()` 成功
3. flush 成功后解绑 `PlayerReceiver`
4. 销毁内存实例
5. 释放 lease

当前实现补充：

- `ReleasePlayer()` 不再忽略 flush 失败
- flush 失败时会恢复原玩家状态并返回错误
- 这样可以避免“首次待落盘”或其他脏玩家在 Maria 异常时被静默释放

## 与 Redis/MariaDB 的职责关系

Redis：

- 热数据
- lease
- 落盘队列
- 快速恢复

MariaDB：

- 最终持久化
- 冷启动回源

当前口径再强调一次：

- 当前真正的持久化成功条件以 Maria 为准
- Redis 更偏热态缓存与回读加速
- 因此“Maria 恢复后是否真正补落盘成功”必须有显式观测，而不能只看 Redis

## 首版实现建议

先实现最小可用模型：

1. 一个 `PlayerData`
2. Redis 热态缓存
3. 一个 MariaDB 通用表
4. 一个 lease 机制
5. 一个后台持久化扫描器
6. `online/detached/unloading` 三态

这足够支撑长连接、断线重连、异步存储。

## 当前实现中的关键状态位

当前仓储和持久化层已经形成几类关键状态，后续设计与实现都应沿用：

- `dirty`
  - 玩家内存数据已变更，尚未完成 flush
- `pending_initial_persist`
  - 玩家是新创建的默认数据，仍等待首次成功落盘
- `created_without_maria`
  - 玩家是在 Maria 不可用时受控创建出来的

这几个状态的意义不同：

- `dirty` 代表一般脏数据
- `pending_initial_persist` 代表“这不是普通更新，而是首次真正建档”
- `created_without_maria` 代表“这次建档发生在 Maria 不可用窗口”

## 当前可观测性要求

当前实现已经补了这些直接观测入口：

- `player_status`
  - 可看 `dirty / pending_initial_persist / created_without_maria`
- `player_persistence_status`
  - 可看聚合数量：
    - `tracked_player_count`
    - `pending_initial_persist_count`
    - `created_without_maria_count`
- `player_persistence_player_status`
  - 可看单玩家：
    - `repository_present`
    - `loaded / dirty`
    - `pending_initial_persist / created_without_maria`
    - `flush_count / last_dirty_ms / last_flush_ms`
    - retry/backoff 状态
- `player_release` 失败日志
  - 会直接带出上述关键状态

这部分不是可选项，而是当前这条存储策略能否排障的必要条件。
