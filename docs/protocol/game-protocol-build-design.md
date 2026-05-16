# 业务协议目录与构建规划

## 状态

- 当前有效
- 范围：业务层 protobuf 协议的目录结构、代码生成、CMake 编译目标、脚本组织、与 IPC 协议的隔离规则

## 目标

当前 `proto/` 顶层只按两类划分：

- `proto/ipc/`
- `proto/game/`

其中：

- `proto/ipc/` 属于进程间通信层
- `proto/game/` 属于业务层

业务层里既包含：

- 客户端 <-> `gate` 通讯协议
- 玩家数据定义结构

因此当前不再把它们拆成两个顶层目录 `proto/client/` 和 `proto/player/`。

## 强约束

### 1. 顶层目录只保留两类

```text
proto/
  game/
  ipc/
```

不再使用：

- `proto/client/`
- `proto/player/`

### 2. `proto/game/` 与 `proto/ipc/` 完全隔离

- `proto/game/*.proto` 不得 import `proto/ipc/...`
- `proto/ipc/*.proto` 不得 import `proto/game/...`
- 二者生成目录独立
- 二者 CMake target 独立
- 二者脚本独立

### 3. 业务层协议与玩家数据同属 `proto/game/`

这两类内容虽然用途不同，但都属于业务层：

- 网络通讯消息
- 玩家数据结构

因此当前应统一放在 `proto/game/` 下，而不是分成两个顶层体系。

## 当前推荐目录结构

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

说明：

- `common.proto`
  - 业务层通用错误码、header
- `login.proto`
  - 登录、心跳、踢线等网络消息
- `player.proto`
  - 玩家请求/响应/推送等网络消息
- `player_data.proto`
  - 玩家持久化数据结构

## 当前生成目录

建议保持：

```text
src/protocol/game/pb/
src/protocol/ipc/pb/
```

其中：

- `src/protocol/game/pb/` 只服务 `proto/game/*`
- `src/protocol/ipc/pb/` 只服务 `proto/ipc/*`

## 当前 CMake 结构

建议保持：

```text
src/protocol/
  CMakeLists.txt
  game_proto.cmake
  gate_game_proto.cmake
```

当前 target：

- `game_proto`
- `gate_game_proto`

依赖关系建议：

- `gate` -> `game_proto` + `gate_game_proto`
- `game` -> `game_proto` + `gate_game_proto`
- `sim_client` -> `game_proto`

不建议：

- `framework` -> `game_proto`

因为 `framework` 不应被业务层协议污染。

## 当前脚本结构

建议保持：

```text
tools/game_proto/
  gen_game_proto.sh
```

要求：

- 该目录只处理 `proto/game/*`
- 不处理 `proto/ipc/*`

如果后续 IPC 也需要独立脚本，应继续单独放在别的目录。

## 与 `sim_client` 的关系

`sim_client` 必须依赖 `game_proto`，这样才能：

- 真实编码登录与玩家请求
- 真实解析 `gate` 回包
- 真实验证顶号、重连、推送

`sim_client` 不应依赖 `ipc` 协议。

## 与当前实现的关系

当前这套规划对应的实现口径是：

- 客户端通讯协议与玩家数据定义都收进 `proto/game/`
- `client_proto` 与 `player_data_proto` 合并成 `game_proto`
- 仍然保持与 `proto/ipc/` 的彻底隔离

## 首版最小集合

当前业务层协议最小集合至少包括：

- `common.proto`
- `login.proto`
- `player.proto`
- `player_data.proto`

## KISS 复核

当前重点不是把 protobuf 体系拆得更细，而是把层级先摆正：

- 顶层只分 `game` 和 `ipc`
- 业务层消息与玩家数据同属 `proto/game/`
- IPC 单独保留在 `proto/ipc/`

这已经足够满足当前版本的主链路与高频问题处理，不需要再提前细分更多层级。
