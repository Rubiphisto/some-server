# 玩家登录、长连接与数据存储设计

## 状态

- 草案
- 范围：`gate` 与 `game` 多进程场景下的账号验证、玩家登录、长连接转发、玩家对象生命周期、玩家数据加载与存储
- 参考项目：`/home/dev/projects/yhyh/server/src/game`

## 文档定位

本文档保留总体设计与关键边界，不再承载全部细节。

具体实现设计请分别查看：

- [overview.md](/home/dev/projects/some-server/docs/player-system/overview.md)
- [gate-session-design.md](/home/dev/projects/some-server/docs/player-system/gate-session-design.md)
- [gate-game-protocol-design.md](/home/dev/projects/some-server/docs/player-system/gate-game-protocol-design.md)
- [game-player-storage-design.md](/home/dev/projects/some-server/docs/player-system/game-player-storage-design.md)
- [sim-client-design.md](/home/dev/projects/some-server/docs/player-system/sim-client-design.md)
- [ipc-improvement-design.md](/home/dev/projects/some-server/docs/ipc-improvements/ipc-improvement-design.md)

## 目标

基于当前工程已有的 IPC 架构，设计一套完整的玩家登录、长连接、数据加载与存储流程，并满足以下原则：

- `gate` 与 `game` 都支持 `1..N` 个进程实例
- 客户端只与 `gate` 建立长连接
- `gate` 负责账号验证、连接管理、转发、踢线与会话归属
- `game` 负责玩家对象的创建、加载、存活、释放与数据持久化
- 玩家在 `game` 中的生命周期长于 `gate` 连接生命周期
- 服务端内部通信统一基于现有 IPC
- 客户端协议与玩家数据统一使用 protobuf
- 客户端 protobuf 协议与 IPC protobuf 协议必须彻底解耦，目录、生成、编译、脚本、命名空间都各自独立

## 强约束

下面这条是本设计的硬约束，不是建议项：

- 客户端 protobuf 协议体系与 IPC 协议体系不是同一个层级的事物，二者互相不应该有任何关系

这里的“没有关系”包括：

- 目录结构无依赖
- `.proto` 文件无 import 关系
- 生成脚本无复用关系
- CMake 编译目标无复用关系
- 产物输出目录无复用关系
- C++ 命名空间无耦合关系
- 协议演进节奏无绑定关系

也就是说：

- IPC 协议只服务进程间通信
- 客户端协议只服务客户端与 `gate` 的通信

二者都可以使用 protobuf，但这只是“同用一种序列化技术”，不是“属于同一套协议系统”。

## 参考项目分析

参考项目本质上拆成了三层：

### 1. 账号验证层

相关文件：

- `/home/dev/projects/yhyh/server/src/game/authentication/router.ts`
- `/home/dev/projects/yhyh/server/src/game/authentication/verifier.ts`

主要职责：

- 校验第三方账号
- 生成带平台、用户、区服、过期时间的登录令牌

这一层的设计思路仍然有效，但参考项目是短连接 HTTP，请求级鉴权后立即结束；我们这里需要改造成长连接接入模型。

### 2. 账号到玩家映射层

相关文件：

- `/home/dev/projects/yhyh/server/src/game/player/userManager.ts`

主要职责：

- `platform + userId (+ areaId) -> playerId`
- Redis 命中优先，MariaDB 兜底
- 首次登录时补建映射

这一层可以直接迁移思路，但应作为独立服务存在，不应耦合到 `gate` 连接管理或 `game` 玩家对象内部。

当前实现补充：

- 账号目录当前已优先持久化到 Redis
- 目录键空间已与玩家数据键空间分离
- 目录 dataset 当前使用独立前缀：`player_directory:`
- `PlayerDirectoryService` 当前应视为正式保留组件，而不是临时过渡方案
- 当前职责只包括：
  - `platform + account_id + area_id -> player_id`
  - `next_player_id` 持久化计数器
- 当前不应把玩家数据加载、lease、gate 路由或账号中心类职责继续并入 `PlayerDirectoryService`

### 3. 玩家数据加载与存储层

相关文件：

- `/home/dev/projects/yhyh/server/src/game/player/playerManager.ts`
- `/home/dev/projects/yhyh/server/src/game/player/player.ts`
- `/home/dev/projects/yhyh/server/src/game/storage/dataset.ts`
- `/home/dev/projects/yhyh/server/src/game/storage/datasetLander.ts`

主要职责：

- 单玩家加载互斥
- Redis 热数据
- MariaDB 持久化
- 延迟落盘
- 玩家释放后，Redis 数据保留一段时间

这一部分是最值得参考的核心，因为它天然适合“`game` 生命周期长于 `gate` 连接”的目标。

## 对当前工程的判断

当前工程已经具备几项关键基础能力：

- `game` 已经接入 `StorageService`
- `game` 已经有 `GameIpcClientService`
- IPC 已经支持 `ProcessReceiver`、`ServiceReceiver`、`PlayerReceiver`
- `PlayerReceiver` 已具备“一个玩家由一个进程权威持有”的建模方向

相关文件：

- [src/game/application.cpp](/home/dev/projects/some-server/src/game/application.cpp)
- [src/game/services/ipc_client_service.cpp](/home/dev/projects/some-server/src/game/services/ipc_client_service.cpp)
- [src/game/services/player_receiver_host.cpp](/home/dev/projects/some-server/src/game/services/player_receiver_host.cpp)
- [src/framework/storage/service.cpp](/home/dev/projects/some-server/src/framework/storage/service.cpp)
- [docs/interprocess-communication-design.md](/home/dev/projects/some-server/docs/interprocess-communication-design.md)

但也存在明显空缺：

- `gate` 目前还没有任何长连接接入层
- IPC 目前只有通用消息能力，还没有“登录、踢线、会话绑定、重连接管”的业务协议
- 还没有账号验证服务
- 还没有账号到玩家的目录服务
- 还没有玩家仓储、玩家会话、玩家登录协调服务

## 这次设计的核心调整方向

以下方向采用你提出的 12 条要求，不再沿用之前仅围绕 `game` 的简化假设。

### 1. 完整链路必须覆盖 `gate + game`

最终方案必须覆盖：

- 客户端连接 `gate`
- `gate` 账号验证
- `gate` 选择或定位 `game`
- `game` 创建或加载玩家
- `gate` 与 `game` 建立玩家会话关联
- 后续客户端消息经 `gate` 转发到 `game`
- 客户端断线后，`game` 内玩家对象继续存活一段时间
- 新连接到来时，可重新绑定新的 `gate -> game` 关系

### 2. `gate` 和 `game` 都支持多进程

必须从一开始就按多实例设计：

- `gate` 为接入层集群
- `game` 为逻辑层集群
- 玩家任一时刻只能归属于一个权威 `game` 进程
- 同一账号任一时刻只能归属于一个权威 `gate` 连接

### 3. `gate` 负责连接与账号，不持有玩家业务对象

`gate` 负责：

- 长连接 accept/read/write
- protobuf 编解码
- 登录态管理
- 账号验证
- 会话心跳与超时
- 踢掉旧连接
- 记录玩家当前所属 `game`
- 将客户端消息通过 IPC 转发给目标 `game`

`gate` 不负责：

- 玩家对象创建
- 玩家数据加载
- 玩家业务逻辑执行
- 玩家持久化

### 4. `game` 负责玩家业务对象全生命周期

`game` 负责：

- `playerId` 的创建或解析
- 玩家对象加载
- 玩家对象在内存中的在线态与暂留态
- 玩家消息处理
- 玩家对象释放
- Redis / MariaDB 持久化

## 总体架构

建议将系统分成两条主线。

### A. 接入会话线

组件位于 `gate`：

- `GateConnectionService`
- `GateSessionService`
- `GateAuthService`
- `GateRoutingService`
- `GateIpcService`

### B. 玩家业务线

组件位于 `game`：

- `PlayerDirectoryService`
- `PlayerRepository`
- `PlayerSessionService`
- `PlayerLoginService`
- `PlayerCommandDispatcher`
- `PlayerPersistenceService`

### C. 模拟客户端线

组件位于独立新进程：

- `sim_client`
- `SimClientConnectionService`
- `SimClientProtocolService`
- `SimClientScenarioService`

该进程不参与正式线上业务，只用于开发、联调、回归测试、压测和协议回放。

## 建议的职责划分

### `gate` 侧

#### `GateConnectionService`

职责：

- 管理客户端 TCP 长连接
- 负责连接建立、断开、心跳、发送队列、背压
- 负责 protobuf 包解码与编码

#### `GateSessionService`

职责：

- 维护 `connection_id -> gate_session`
- 维护 `account_id -> connection_id`
- 维护 `player_id -> session route`
- 处理同账号新连接踢旧连接

#### `GateAuthService`

职责：

- 校验客户端登录请求
- 生成或验证服务器内部会话凭证
- 将账号身份传递给 `game`

#### `GateRoutingService`

职责：

- 管理 `player_id -> game_process`
- 管理 `account_id -> gate_process/connection`
- 支持断线重连后的重新挂接

这里建议 Redis 作为共享目录，IPC 作为实时通知通道。

#### `GateIpcService`

职责：

- 通过 IPC 向 `game` 发登录、登出、玩家消息、踢线、重绑等指令
- 接收 `game` 的登录完成、强制下线、推送消息、迁移通知等回调

### `game` 侧

#### `PlayerDirectoryService`

职责：

- 维护 `AccountIdentity -> playerId`
- 首次登录时创建映射
- 使用 Redis 做热点缓存，MariaDB 做最终存储
- 维护独立的目录持久化键空间与 `next_player_id` 计数器

当前阶段结论：

- `PlayerDirectoryService` 保留并继续使用
- 当前实现使用独立 `directory` dataset 即可
- 当前不再继续把它扩展成更复杂的账号中心或目录中心

#### `PlayerRepository`

职责：

- 玩家 protobuf 数据编解码
- 从 Redis 加载玩家数据
- Redis 不可用或数据不完整时，从 MariaDB 回源并重建 Redis
- 将变更后的玩家数据写回 Redis
- 触发延迟落盘到 MariaDB

#### `PlayerSessionService`

职责：

- 管理本进程内 `loading_players`
- 管理本进程内 `online_players`
- 管理本进程内 `detached_players`
- 与 `GameIpcClientService` 配合完成 `PlayerReceiver(player_id)` 的本地绑定和解绑

#### `PlayerLoginService`

职责：

- 协调整个登录流程
- 驱动账号转玩家
- 驱动玩家加载
- 处理重连复用、重复登录、旧 gate 替换

#### `PlayerCommandDispatcher`

职责：

- 将“协议号”映射到 protobuf 请求类型和处理函数
- 分发玩家命令
- 当前只服务 `game` 内玩家消息分发
- 若后续真的出现新的同类进程，再单独评估是否复用这套模型

#### `PlayerPersistenceService`

职责：

- 统一管理 Redis lease、热数据刷新、延迟落盘、释放超时

### `sim_client` 侧

#### 目标

建立一个独立的模拟客户端进程，用真实客户端协议直接连接 `gate`，用于验证完整链路：

- `sim_client -> gate` 长连接
- 登录鉴权
- protobuf 编解码
- 心跳
- 请求/响应
- 推送
- 断线重连
- 顶号
- 多客户端并发

#### 职责

- 发起与 `gate` 的 TCP 长连接
- 使用与正式客户端一致的 protobuf 协议
- 模拟登录、心跳、业务消息发送
- 模拟断线、重连、重复登录、顶号等行为
- 支持脚本化场景回放
- 支持批量虚拟客户端并发运行

#### 边界

`sim_client` 不应复用 `gate` 或 `game` 的业务实现逻辑，只应复用：

- 协议定义
- 必要的 protobuf 编解码工具
- 通用基础设施

这样才能真正测试到接入链路，而不是在进程内绕过它。

## 长连接模型

本项目与参考项目的关键差异在于：我们不是短连接，而是长连接。

因此必须明确三种生命周期：

### 1. 客户端连接生命周期

存在于 `gate`：

- 建立 TCP 连接
- 登录成功后成为带账号/玩家语义的会话
- 断线后会话立即失效

### 2. `gate` 会话生命周期

存在于 `gate`：

- 连接成功前只是匿名会话
- 登录成功后绑定账号、玩家、目标 `game`
- 被顶号、断线、超时、主动登出时结束

### 3. `game` 玩家实例生命周期

存在于 `game`：

- 登录或重连时加载/激活
- 断线后不会立刻销毁
- 在“暂留时间”内进入 `detached` 状态
- 若此时有新连接重新登录，则恢复绑定
- 仅在超时或明确登出后释放

这正是你提出的第 9 条要求。

### 4. `sim_client` 进程生命周期

存在于独立测试进程：

- 启动后建立一个或多个到 `gate` 的连接
- 可以长期保持在线，模拟真实客户端
- 可以按场景主动断开、重连、切换 gate
- 可以同时持有多个虚拟账号，模拟并发在线

它的价值在于把“账号验证、长连接、转发、玩家加载、断线重连、顶号”放进同一条真实链路中验证。

## 关键关系模型

建议明确维护四类关系。

### 1. 账号到 gate 连接

`account_id -> gate_process_id + connection_id`

用途：

- 顶号
- 查找当前活跃入口连接

### 2. 玩家到 game 进程

`player_id -> game_process_id`

用途：

- 玩家消息路由
- 重连时重新挂接

### 3. 玩家到 gate 会话

`player_id -> gate_process_id + gate_session_id`

用途：

- `game` 主动推送给客户端时找到正确 gate
- `game` 请求 gate 踢线时找到目标连接

### 4. 玩家在 game 中的实例状态

`player_id -> online | detached | unloading`

用途：

- 控制断线重连逻辑
- 控制延迟释放逻辑

## Redis 与 IPC 的分工

你倾向于 “IPC + Redis” 配合，我认为这个方向是对的。

建议分工如下：

### Redis 负责共享事实记录

适合放 Redis 的内容：

- `account_id -> gate connection`
- `player_id -> game process`
- `player_id -> gate session`
- 玩家数据热存储
- 玩家 lease 元数据
- 待落盘队列

这些数据的特点是：

- 需要跨进程共享
- 允许短时间最终一致
- 需要在进程崩溃后恢复

### IPC 负责实时协同

适合放 IPC 的内容：

- `gate -> game` 登录请求
- `game -> gate` 登录完成
- `gate -> game` 玩家消息转发
- `game -> gate` 玩家推送
- `game -> gate` 踢线请求
- `gate -> game` 断线通知
- `gate -> game` 重连绑定

这些消息的特点是：

- 实时性强
- 是操作，而不是状态
- 不应依赖轮询 Redis 来推动

## 登录流程设计

推荐登录时序如下。

### 阶段 1：客户端接入 gate

1. 客户端与某个 `gate` 进程建立 TCP 长连接
2. `gate` 完成 protobuf 握手、版本校验、心跳初始化
3. 客户端发起登录请求

这里的“客户端”在开发和联调环境中，可以由 `sim_client` 进程替代。

### 阶段 2：gate 完成账号验证

4. `gate` 调用 `GateAuthService` 校验账号
5. 生成标准化的 `AccountIdentity`
6. `GateSessionService` 检查该账号是否已有活跃连接
7. 若已有，则通过本地或 IPC 通知旧 `gate` 连接执行踢线
8. 踢线完成或旧连接失效后，新连接成为当前账号唯一入口

### 阶段 3：gate 引导 game 创建或加载玩家

9. `gate` 通过 IPC 向某个 `game` 发送 `LoginPlayerRequest`
10. `game` 调用 `PlayerDirectoryService` 解析或创建 `playerId`
11. `game` 查询本地是否已有该玩家实例
12. 若本地已有在线或暂留实例，则直接复用
13. 若本地没有，则由 `PlayerRepository` 获取玩家 lease 并执行加载
14. `game` 将玩家对象激活
15. `game` 本地绑定 `PlayerReceiver(player_id)`
16. `game` 更新 Redis 中的 `player_id -> game_process`
17. `game` 返回 `LoginPlayerResponse` 给 `gate`

### 阶段 4：建立 gate 与 game 的会话关联

18. `gate` 记录 `player_id -> game_process`
19. `gate` 记录 `player_id -> gate_session`
20. `gate` 向客户端返回登录成功与初始化数据
21. 后续客户端所有玩家协议都通过 `gate -> game` IPC 转发

## 断线与重连流程

### 客户端断线

1. `gate` 检测连接断开
2. `gate` 清理本地 `connection/session`
3. `gate` 通过 IPC 通知对应 `game`
4. `game` 将玩家从 `online` 转为 `detached`
5. `game` 保留玩家实例一段时间，不立即释放

### 客户端重连

1. 新客户端连接到任意一个 `gate`
2. `gate` 再次完成账号验证
3. 若旧连接仍存在，则先踢旧连接
4. `gate` 通过 Redis 或 IPC 查到 `player_id` 当前所属 `game`
5. `gate` 向该 `game` 发起 `ReconnectPlayerRequest`
6. `game` 若发现玩家仍在 `detached` 暂留期，则直接重新绑定
7. `game` 返回重连成功
8. `gate` 与 `game` 建立新的玩家会话关联

`sim_client` 应能够脚本化触发整个流程，用于反复验证：

- 短时断线重连
- 换 gate 重连
- 旧连接未彻底回收时的新连接顶替

## 顶号流程

你提出“同账号新连接需要踢掉先前 gate 连接”，这是必须具备的。

建议规则：

1. 账号唯一入口以 `account_id` 为准，而不是 `player_id`
2. 新登录成功前，必须先让旧连接失效
3. 若旧连接在本 gate，本地直接关闭
4. 若旧连接在其他 gate，通过 IPC 向目标 gate 发送 `KickAccountSession`
5. 目标 gate 关闭连接后，回执结果
6. 新 gate 再继续完成登录

Redis 可作为共享索引，但“踢线动作”应通过 IPC 驱动，不建议只写 Redis 再等对方轮询。

`sim_client` 需要支持至少两种顶号测试模式：

- 同一账号两个连接同时登录
- 旧连接保持在线时，新连接跨 gate 登录

## 玩家对象生命周期设计

建议在 `game` 里将玩家对象状态分成：

- `loading`
- `online`
- `detached`
- `unloading`

### `online`

- 玩家当前有 gate 会话
- 能正常收发协议

### `detached`

- 玩家对象仍在内存中
- 当前无 gate 会话
- 保留一段可重连窗口

### `unloading`

- 玩家准备释放
- 做最终 flush、落盘或延迟落盘登记
- 解绑 `PlayerReceiver`

这样可以自然满足：

- `game` 生命周期长于 `gate`
- 支持断线短时间重连
- 避免每次断线都重新加载玩家

## 玩家数据设计

第 11 条要求是关键点：玩家数据必须用 protobuf 定义，并归入业务层 `proto/game/`。

我建议：

### 1. 内存主模型使用 protobuf

例如：

```text
proto/game/player_data.proto
```

定义：

- `PlayerData`
- `PlayerBaseData`
- `PlayerInventoryData`
- `PlayerQuestData`

### 2. Redis 存储策略

首版建议优先简单，不要过度拆分。

可选方案有两个：

#### 方案 A：整包 protobuf 二进制存一份

Redis key 示例：

- `player:{player_id}:blob`
- `player:{player_id}:meta`

优点：

- 实现最简单
- 编解码边界清晰

缺点：

- 局部字段更新不方便
- 不利于调试

#### 方案 B：按大模块拆分 protobuf blob

Redis key 示例：

- `player:{player_id}:base`
- `player:{player_id}:inventory`
- `player:{player_id}:quest`
- `player:{player_id}:meta`

优点：

- 更新粒度更合理
- 更适合未来脏数据分段保存

缺点：

- 实现复杂度稍高

### 首版建议

首版建议采用“少量大块”的方案，而不是字段级拆分：

- `base`
- `core`
- `extensions`

这样仍然符合 KISS，不会把存储层复杂度拉太高。

### 3. MariaDB 存储策略

首版建议不要立即做高度范式化。

建议先用通用表存 protobuf blob：

- `player_entries`
- `entry_key = player_id`
- `entry_value = protobuf 二进制或其安全编码形式`

如果当前 `StorageService` 更适合文本字段，可先做二进制安全编码，例如 `BLOB` 或可控的字节存储形式；不建议为了首版可用性过早拆成很多业务表。

### 4. Redis 与 MariaDB 的关系

- Redis 是热数据
- MariaDB 是耐久数据
- 当前实现的 `DatasetPut()` 以 Maria 写成功为准
- Redis 回写当前是 best-effort 热态更新
- 后台持久化服务负责择机触发 flush
- 玩家释放前若仍为 `dirty`，必须先 flush 成功

说明：

- “先写 Redis、再延迟写 Maria” 仍然可以作为后续演进方向
- 但当前已落地口径应以实际实现为准，不再假设已有独立落盘队列

## 玩家数据加载与存储流程

### 加载流程

1. `game` 解析出 `playerId`
2. 检查本地是否已有实例
3. 若无，则尝试获取该玩家 Redis lease
4. 若 Redis 中已有热数据，直接反序列化
5. 若 Redis 数据不存在或不完整，则从 MariaDB 加载
6. 从 MariaDB 数据重建 Redis 热数据
7. 创建内存玩家对象
8. 绑定 `PlayerReceiver(player_id)`

### 保存流程

1. 玩家逻辑修改内存 protobuf 数据
2. 标记整玩家 `dirty`
3. 后台持久化服务扫描 `dirty` / `detached` / `pending_initial_persist`
4. 调用 `FlushPlayer()`
5. `FlushPlayer()` 再通过 `DatasetPut()` 写入 Maria，并 best-effort 回写 Redis

当前补充：

- flush 失败会进入 retry/backoff
- `pending_initial_persist` 玩家会被优先 flush
- 在“新玩家首次创建 + Maria 不可用”时，允许受控创建默认数据，但会显式标记为待首次落盘
- 只有真正完成 Maria 成功写入后，才清除“首次待落盘”语义

### 释放流程

1. 玩家进入 `unloading`
2. 若仓储仍为 `dirty`，执行最后一次 `FlushPlayer()`
3. 若 flush 失败，则撤回 `unloading` 并拒绝释放
4. 解绑 `PlayerReceiver`
5. 释放本地实例
6. 释放 lease

当前补充：

- 这样可以避免 Maria 异常时把脏玩家静默释放掉
- 对“首次待落盘”玩家同样适用

### 默认数据玩家策略结论

当前实现的明确结论是：

- 允许新玩家在 Maria 不可用窗口先以默认数据进入游戏
- 但必须显式保留：
  - `pending_initial_persist`
  - `created_without_maria`
- 后续只有在 Maria 真正写成功后，才允许把它当作已完成建档的正式玩家
- 在此之前：
  - 后台持久化会优先处理它
  - 释放路径会保护它，避免静默丢档

## 协议系统设计

第 10 条和第 12 条要求合起来，意味着我们需要两层协议：

### 1. 业务层网络协议

特点：

- 客户端 <-> gate
- protobuf
- 有协议号
- 登录、心跳、玩家请求、服务器推送都在这里
- 目录、生成、编译、脚本都归入业务层 `proto/game/`
- 但仍然与 `proto/ipc/` 完全独立

建议目录：

```text
proto/game/
  common.proto
  login.proto
  player.proto
  player_data.proto
```

建议生成目录：

```text
src/protocol/game/pb/
```

建议构建入口：

- 独立 `game_proto.cmake`
- 独立生成命令
- 独立 library target，例如 `game_proto`

### 2. 内部 IPC 协议

特点：

- gate <-> game
- protobuf
- 不直接复用客户端协议
- 目录、生成、编译、脚本完全独立于业务层 `proto/game/`

建议目录：

```text
proto/ipc/
  common/v1/
  control/v1/
  gate_game/v1/
```

建议生成目录：

```text
src/framework/ipc/pb/
```

建议构建入口：

- 维持 IPC 自己的生成规则
- 独立 library target，例如当前 `framework` 内部的 IPC proto 产物
- 不允许把客户端 proto 编进 IPC 目标

这里不是“尽量不要”，而是“明确禁止”：

- `proto/game/` 面向业务层协议与玩家数据结构
- IPC 协议面向服务协作

禁止事项：

- `proto/game/*.proto` import `proto/ipc/...`
- IPC `.proto` import `proto/game/...`
- 业务层 protobuf 代码生成到 `src/framework/ipc/pb/`
- IPC protobuf 代码生成到客户端协议目录
- 在同一个 CMake custom command 里同时生成业务层协议与 IPC 协议
- 用同一个脚本同时承担两套协议的生成职责

允许的唯一共性只有：

- 都依赖 protobuf 编译器
- 都遵循各自独立的版本管理和兼容性规则

## 协议号与 protobuf 绑定机制

建议设计一套统一的消息注册表，当前先应用于 `game`。

### 目标

支持以下绑定关系：

- `协议号 -> 请求 protobuf 类型`
- `协议号 -> 响应 protobuf 类型`
- `协议号 -> 处理函数`

### 推荐抽象

```cpp
struct MessageDescriptor {
    std::uint32_t message_id = 0;
    std::string_view name;
    std::string_view request_type;
    std::string_view response_type;
};

class IMessageDispatcher {
public:
    virtual bool Register(const MessageDescriptor& descriptor, Handler handler) = 0;
    virtual DispatchResult Dispatch(PlayerContext& ctx, std::uint32_t message_id, std::span<const std::byte> payload) = 0;
};
```

### 处理流程

1. `gate` 收到客户端包
2. 解出 `message_id + protobuf payload`
3. 封装成内部 `PlayerMessageRequest`
4. 通过 IPC 发给目标 `game`
5. `game` 根据 `message_id` 查注册表
6. 创建对应 protobuf request 对象
7. 反序列化
8. 调用 handler
9. 生成 protobuf response
10. 经 IPC 回给 `gate`
11. `gate` 回包给客户端

这样做的好处是：

- 协议号与业务处理逻辑解耦
- 可以逐步沉淀统一中间件层

`sim_client` 应直接复用同一套业务层协议号定义和 protobuf 消息定义，以保证联调路径真实。

注意：

- `sim_client` 只能依赖 `proto/game/` 下的业务层协议定义
- `sim_client` 不应依赖 IPC 协议定义
- `sim_client` 不应把 IPC 消息作为自己的网络协议
- `sim_client` 的价值就是模拟真实客户端，而不感知服务端内部 IPC 协议

## 协议目录与构建隔离

这一节用于把前面的边界落实到目录、生成、编译、脚本。

### 目录隔离

建议最终结构类似：

```text
proto/
  game/
    common.proto
    login.proto
    player.proto
    player_data.proto
  ipc/
    common/v1/
    control/v1/
    gate_game/v1/
```

### 生成产物隔离

建议：

```text
src/protocol/game/pb/
src/framework/ipc/pb/
```

二者不能共用同一个输出目录。

### CMake 隔离

建议：

- 客户端协议单独定义生成变量、输出变量、目标变量
- IPC 协议单独定义生成变量、输出变量、目标变量
- 不共享同一组 `PROTO_FILES`、`PROTO_SRCS`、`PROTO_HDRS`

按当前仓库状态，`src/framework/CMakeLists.txt` 中现有的 proto 生成链应继续只服务 `proto/ipc/*`，不应扩展成“顺手也生成业务层协议”。

也就是说，未来应新增一套独立入口，例如：

```text
src/protocol/CMakeLists.txt
src/protocol/game_proto.cmake
```

而不是把业务层协议继续塞进 `framework`。

### 脚本隔离

若后续加入 proto 生成脚本，建议分成：

- `tools/game_proto/gen_game_proto.sh`
- `tools/proto/gen_ipc_proto.sh`

或者更明确地拆到不同子目录：

```text
tools/game_proto/
tools/ipc_proto/
```

要求：

- 一个脚本只负责一套协议
- 不允许一个脚本同时承担客户端协议与 IPC 协议生成

### 命名空间隔离

建议：

- 业务层 `proto/game/*` 使用统一 package / namespace
- IPC 协议使用独立 package / namespace

例如：

```text
pb
ipc.control.v1
ipc.gate_game.v1
```

这不是命名风格问题，而是为了防止后续类型、生成代码、依赖关系混在一起。

## 模拟客户端进程设计

建议新增一个独立进程，例如：

```text
src/sim_client/
  CMakeLists.txt
  sim_client.cmake
  main.cpp
  application.h
  application.cpp
  services/
    connection_service.h
    connection_service.cpp
    client_protocol_service.h
    client_protocol_service.cpp
    scenario_service.h
    scenario_service.cpp
```

### 建议职责拆分

#### `connection_service`

- TCP 连接建立、断开、重连
- 收发缓冲
- 心跳

#### `protocol_service`

- 客户端 protobuf 消息封包与解包
- 协议号分发
- 登录响应、推送、错误码解析

#### `scenario_service`

- 登录场景
- 心跳场景
- 玩家协议压测场景
- 断线重连场景
- 顶号场景
- 多账号并发场景

### 为什么要单独做新进程

原因很直接：

- 可以验证真实网络接入路径
- 可以验证 `gate` 长连接管理是否正确
- 可以验证 protobuf 协议封包是否正确
- 可以验证跨 `gate`、跨 `game` 的协同行为
- 可以作为自动化联调与回归测试工具长期保留

如果只做进程内 mock，就测不到真正的问题。

## 对当前 IPC 架构的建议改进

你要求“如果现有 IPC 架构需要改进，要先提出来”。基于当前代码和设计文档，我认为有以下几项值得讨论。

### 1. 需要补一层“会话级业务协议”

现有 IPC 更偏底层消息通道，已经有：

- 发现
- 路由
- receiver
- 消息发送

但还缺：

- `gate -> game` 登录请求协议
- `game -> gate` 登录完成协议
- 踢线协议
- 断线通知协议
- 重连绑定协议
- 玩家推送协议

这个不是底层 IPC 的问题，而是应该在现有 IPC 之上加一层标准业务消息协议。

建议：

- 不改动 IPC 基础层抽象
- 在 `proto/` 下新增内部业务协议

### 2. `gate` 也需要接入 IPC client/service

当前只有 `game` 已接入相对完整的 IPC 运行时。

建议：

- 为 `gate` 增加与 `game` 对等的 IPC 服务
- 支持 `ProcessReceiver`、`ServiceReceiver`
- 必要时支持“gate 会话接收器”或通过 `ServiceReceiver + session_id` 分发

### 3. 当前 `PlayerReceiver` 只有“是否本地绑定”，缺少玩家业务分发

当前 `PlayerReceiverHost` 只验证玩家是否绑定本地，还没有真正把消息路由到玩家实例。

建议：

- 后续让 `PlayerSessionService` 持有玩家对象表
- `PlayerReceiverHost::Dispatch()` 调用玩家命令分发器

这是必补项，但不一定需要改 IPC 基础层接口。

### 4. 需要明确 `gate` 到 `game` 的响应路径

当前 IPC 更偏单向发送抽象。

登录和请求转发会天然需要“请求-响应”。

可选方案：

- 方案 A：在业务层自己带 `request_id/correlation_id`
- 方案 B：在 IPC 之上封一层轻量 request-response helper

建议首版：

- 先用方案 A
- 不立即侵入 IPC 基础层

这样更符合 KISS。

### 5. 需要增加会话路由与 owner 信息的共享记录

当前 IPC 设计文档里强调 receiver owner，但对 `gate session` 这类接入态对象没有统一模型。

建议：

- `player -> game` 仍然走 `PlayerReceiver`
- `player -> gate session` 不强行建成新的底层 receiver 类型
- 首版先用 Redis 维护 `player -> gate_process + session_id`
- `game -> gate` 主动推送时，先按 `player` 查路由，再通过 `ServiceReceiver(gate)` 或 `ProcessReceiver(gate)` 投递

也就是说，我暂时不建议为了 `gate session` 引入新的通用 `SessionReceiverType`。

### 6. 需要评估 receiver directory 的远程一致性策略

当前设计对 `PlayerReceiver` 的 owner 绑定是合理的，但真正进入“玩家可能换 game 进程”时，需要更明确：

- 谁写 owner
- 谁清理 owner
- owner 与 Redis 中 `player -> game` 的一致性如何保证

建议首版规则：

- 以 `game` 本地成功加载并绑定 `PlayerReceiver` 作为 owner 生效点
- 紧接着写 Redis 的 `player -> game`
- 失败时全部回滚

这可以先满足需求，不必先把 IPC 底层改成复杂的分布式目录。

## 推荐的首版边界

为了避免系统一开始就过重，首版建议只做到：

- `gate` TCP 长连接
- 登录/心跳/踢线/重连
- `gate -> game` 玩家协议转发
- `game` 单玩家单 owner
- protobuf 玩家数据
- Redis 热数据
- MariaDB 延迟落盘
- 玩家断线暂留
- 独立 `sim_client` 进程用于完整链路联调

首版不建议立即做：

- 玩家跨 game 迁移
- 多 owner 协同
- 通用 RPC 框架化
- gate session 作为新 receiver 类型
- 玩家数据按业务模块高度拆表

## KISS 复核

这版方案在全局上仍然保持了 KISS：

- `gate` 只做连接和转发，不承载玩家业务对象
- `game` 只做玩家业务与存储
- IPC 基础层尽量不改，只在其上加业务协议
- Redis 负责共享状态，IPC 负责实时动作
- 玩家数据首版只做少量大块 protobuf 存储，不做细粒度拆分

如果一开始就把“连接管理、玩家归属、协议分发、持久化格式、跨进程迁移”全部揉进 IPC 基础层，耦合会明显上升，这不值得。

## 低耦合复核

建议保持下面这组边界：

- `gate` 不知道玩家数据结构内部细节
- `game` 不知道客户端连接对象细节
- `PlayerRepository` 不关心连接和协议
- `PlayerCommandDispatcher` 不关心 Redis/MariaDB 细节
- `GateRoutingService` 不关心玩家对象内部逻辑
- IPC 基础层不感知账号、session、player 业务规则

这个边界能保证当前 `gate/game` 私有逻辑不会反向污染框架层。

## 下一步建议

如果按这个方向继续，我建议下一阶段先把文档再往下落一层，补三份更细的设计：

1. `gate` 长连接与会话模型设计
2. `gate <-> game` 内部 protobuf 协议设计
3. `game` 玩家 protobuf 数据与 Redis/MariaDB 存储格式设计

加入 `sim_client` 需求后，建议再补一份：

4. `sim_client` 进程与场景脚本设计

这三份定下来之后，再开始落代码，风险会低很多。
