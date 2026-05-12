# 玩家系统设计总览

## 状态

- 草案
- 作用：作为玩家接入、登录、长连接、存储、模拟客户端、IPC 改进等设计文档的总索引

## 设计目标

本项目玩家系统设计基于以下固定前提：

- `gate` 与 `game` 都支持 `1..N` 个进程实例
- 客户端只连接 `gate`
- `gate` 负责长连接、账号验证、会话管理、转发、踢线
- `game` 负责玩家对象加载、存活、释放、存储
- 玩家实例在 `game` 中的生命周期长于 `gate` 连接生命周期
- 服务端内部通信统一基于现有 IPC 架构扩展
- 客户端协议与 IPC 协议彻底解耦
- 需要独立的 `sim_client` 新进程用于真实链路联调

## 强约束

以下约束适用于所有后续设计文档：

### 1. 客户端协议与 IPC 协议完全分离

- 客户端 protobuf 与 IPC protobuf 不是同一个层级的事物
- 二者目录、生成、编译、脚本、命名空间都必须独立
- `.proto` 文件之间不得互相 import
- `sim_client` 只能依赖客户端协议，不能依赖 IPC 协议

### 2. 权威玩家 owner 只在 `game`

- 任意时刻一个玩家只能由一个权威 `game` 进程持有
- `gate` 不持有玩家业务对象
- `PlayerReceiver(player_id)` 仍然是玩家在服务端内部的权威路由标识

### 3. `gate` 连接生命周期短于 `game` 玩家实例生命周期

- 客户端断线不等于玩家实例立刻销毁
- 玩家实例需要支持 `detached` 暂留态
- 新连接重来后应重新建立 `gate -> game` 绑定

## 文档拆分

后续实现应以专题文档为准，而不是继续扩写一份总文档。

### 总体设计

- [player-login-storage-design.md](/home/dev/projects/some-server/docs/player-system/player-login-storage-design.md)

说明：

- 保留整体架构、职责边界、主流程
- 作为总述，不再承担所有细节

### `gate` 长连接与会话设计

- [gate-session-design.md](/home/dev/projects/some-server/docs/player-system/gate-session-design.md)

说明：

- 连接模型
- 会话模型
- 顶号
- 断线与重连
- `gate` 本地状态与 Redis 共享状态

### `gate <-> game` 内部业务协议设计

- [gate-game-protocol-design.md](/home/dev/projects/some-server/docs/player-system/gate-game-protocol-design.md)

说明：

- 登录请求与响应
- 玩家消息转发
- 推送
- 踢线
- 断线通知
- 重连绑定
- request/response 相关 id 设计

### `game` 玩家数据与存储设计

- [game-player-storage-design.md](/home/dev/projects/some-server/docs/player-system/game-player-storage-design.md)

说明：

- 玩家 protobuf 数据组织
- Redis 键设计
- MariaDB 存储设计
- lease
- 延迟落盘
- 暂留与释放

### `sim_client` 设计

- [sim-client-design.md](/home/dev/projects/some-server/docs/player-system/sim-client-design.md)

说明：

- 独立新进程职责
- 真实协议接入
- 联调、回归、压测场景
- 多账号、多连接、顶号、重连脚本

### IPC 改进设计

- [ipc-improvement-design.md](/home/dev/projects/some-server/docs/ipc-improvements/ipc-improvement-design.md)

说明：

- 当前 IPC 可以直接复用的部分
- 需要补充但不应过度侵入的部分
- 哪些属于 IPC 上层业务协议
- 哪些不应下沉到 IPC 基础层

### 客户端协议目录与构建设计

- [client-protocol-build-design.md](/home/dev/projects/some-server/docs/protocol/client-protocol-build-design.md)

说明：

- 客户端 protobuf 目录规划
- 客户端协议生成目录
- CMake target 与脚本拆分
- 与 IPC 协议彻底隔离的工程边界

## 推荐实现顺序

建议按下面顺序落地：

1. `gate-session-design.md`
2. `gate-game-protocol-design.md`
3. `game-player-storage-design.md`
4. `ipc-improvement-design.md`
5. `sim-client-design.md`
6. `client-protocol-build-design.md`

实际编码时，建议再反过来做一次交叉复核：

1. 先核对 IPC 是否足以承接业务协议
2. 再实现 `gate` 会话与 `game` 玩家加载
3. 最后补 `sim_client`

## 实现协作方式

后续落地建议采用以下方式之一：

### 方式 A：新开会话按文档逐份实现

适用场景：

- 你希望手动控制每个阶段
- 每一轮只实现一份文档覆盖的内容

### 方式 B：将单份文档作为 subagent 的明确输入

适用场景：

- 需要把范围压缩到一个明确子任务
- 避免在同一轮里同时改动 `gate`、`game`、协议、存储

建议规则：

- 一个子任务只对应一份专题文档
- 一个 subagent 只负责一个明确写入边界
- 先看文档再编码，避免现场重新发散设计

## KISS 复核

这份总览的意义就是防止设计重新耦合回一处。

如果后续任何专题文档开始出现下面问题，就应回退审视：

- `gate` 开始持有玩家业务对象
- `game` 开始直接操作连接对象
- IPC 基础层被塞入大量账号或会话语义
- 客户端协议与 IPC 协议开始共目录、共脚本、共目标
- `sim_client` 直接调用服务端内部业务逻辑而绕过网络接入

出现这些情况，说明设计边界开始被破坏。
