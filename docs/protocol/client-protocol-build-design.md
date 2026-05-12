# 客户端协议目录与构建规划

## 状态

- 草案
- 范围：客户端 protobuf 协议的目录结构、代码生成、CMake 编译目标、脚本组织、与 IPC 协议的隔离规则

## 目标

建立一套独立的客户端协议体系，用于：

- 客户端 <-> gate 通信
- `sim_client` 真实协议联调
- 客户端协议号与 protobuf message 绑定

并严格满足：

- 客户端协议与 IPC 协议彻底分离
- 二者可以都使用 protobuf，但不能共享协议工程边界

## 强约束

以下规则是硬约束：

### 1. 目录隔离

- 客户端协议放在 `proto/client/`
- IPC 协议放在 `proto/ipc/`
- 二者不得混放

### 2. import 隔离

- 客户端 `.proto` 不得 import `proto/ipc/...`
- IPC `.proto` 不得 import `proto/client/...`

### 3. 生成隔离

- 客户端协议生成目录独立
- IPC 协议生成目录独立
- 不得写入同一个输出目录

### 4. 构建隔离

- 客户端协议独立 CMake 变量
- 客户端协议独立 custom command
- 客户端协议独立 library target
- 不并入 `framework` 的 IPC proto 生成链

### 5. 脚本隔离

- 客户端协议独立生成脚本
- IPC 协议独立生成脚本
- 不允许一个脚本同时管理两套协议

## 推荐目录结构

建议最终结构：

```text
proto/
  client/
    common/v1/
    login/v1/
    game/v1/
    social/v1/
  ipc/
    common/v1/
    control/v1/
    gate_game/v1/
    game_social/v1/
```

说明：

- `proto/client/` 面向网络接入协议
- `proto/ipc/` 面向服务间业务协议与 IPC 基础协议

## 客户端协议分层建议

### `common/v1`

放公共 message，例如：

- 通用错误码
- 通用 header
- 分页/时间戳等基础结构

### `login/v1`

放登录相关协议，例如：

- 登录请求
- 登录响应
- 心跳请求
- 心跳响应
- 踢线通知

### `game/v1`

放游戏主业务协议，例如：

- 玩家初始化
- 玩家请求
- 玩家响应
- 推送通知

### `social/v1`

先预留，不必立即实现。

## 生成产物目录

建议客户端协议生成到：

```text
src/protocol/client/pb/
```

不要生成到：

- `src/framework/ipc/pb/`
- `src/framework/...`
- `src/gate/...`
- `src/game/...`

原因：

- 生成产物属于协议层，不属于 framework/ipc 层
- `gate`、`game`、`sim_client` 都可能依赖客户端协议
- 放在独立目录更利于 target 复用

## CMake 结构建议

建议新增独立协议模块：

```text
src/protocol/
  CMakeLists.txt
  client_proto.cmake
```

### `src/protocol/CMakeLists.txt`

职责：

- 定义客户端协议生成链
- 生成 `client_proto` 目标
- 暴露 include 目录

### `client_proto.cmake`

职责：

- 枚举客户端 `.proto` 文件
- 定义生成输出文件列表
- 组织 `protoc` 命令

## 推荐 target 设计

建议至少有一个独立 target：

- `client_proto`

职责：

- 编译 `proto/client/*` 的生成产物
- 提供头文件与链接依赖给 `gate`、`sim_client` 等模块

依赖关系建议：

- `gate` -> `client_proto`
- `sim_client` -> `client_proto`
- `game` 按需依赖 `client_proto`

不建议：

- `framework` -> `client_proto`

因为 `framework` 不应被客户端协议污染。

## 变量命名建议

为避免和 IPC proto 混淆，客户端协议建议使用单独变量前缀：

```text
CLIENT_PROTO_ROOT
CLIENT_PROTO_GEN_DIR
CLIENT_PROTO_FILES
CLIENT_PROTO_SRCS
CLIENT_PROTO_HDRS
```

不要复用：

- `FRAMEWORK_IPC_PROTO_ROOT`
- `FRAMEWORK_IPC_PROTO_GEN_DIR`
- `FRAMEWORK_IPC_PROTO_FILES`

## 生成命令建议

建议客户端协议使用独立 `add_custom_command(...)`：

- 只依赖 `proto/client/*`
- 只输出到 `src/protocol/client/pb/`
- 只服务 `client_proto`

不要做：

- 在 `src/framework/CMakeLists.txt` 的 proto 生成命令里顺带生成客户端协议

这会直接破坏边界。

## 脚本组织建议

建议新增独立脚本目录：

```text
tools/client_proto/
  gen_client_proto.sh
```

可选配套：

- `check_client_proto.sh`
- `fmt_client_proto.sh`

要求：

- 该目录只处理客户端协议
- 不处理 IPC 协议

IPC 协议如果也需要脚本，应单独放：

```text
tools/ipc_proto/
```

## 命名空间与 package 建议

客户端协议建议使用：

```text
client.common.v1
client.login.v1
client.game.v1
client.social.v1
```

IPC 协议建议使用：

```text
ipc.common.v1
ipc.control.v1
ipc.gate_game.v1
ipc.game_social.v1
```

二者不要共享 package 前缀。

## 与协议号分发机制的关系

客户端协议层只定义：

- protobuf message
- 协议号约定

消息分发机制本身不应写在 `.proto` 目录里，而应由代码层实现。

例如：

- `gate` 的网络协议分发器
- `sim_client` 的协议分发器
- `game` 中面向客户端消息的处理分发器

也就是说：

- 协议目录定义“消息长什么样”
- 分发器定义“消息如何被处理”

## 与 `sim_client` 的关系

`sim_client` 必须依赖 `client_proto`，这样才能：

- 真实编码客户端请求
- 真实解析 gate 回包
- 真实验证顶号、重连、推送

`sim_client` 不应依赖 IPC proto。

## 与现有工程的关系

当前仓库中，IPC proto 生成链位于：

- [src/framework/CMakeLists.txt](/home/dev/projects/some-server/src/framework/CMakeLists.txt)

该文件中的 proto 生成逻辑应继续只服务：

- `proto/ipc/*`
- etcd / gRPC 相关 proto

客户端协议不应并入这里。

## 建议落地步骤

1. 新建 `proto/client/`
2. 新建 `src/protocol/`
3. 新建 `src/protocol/client/pb/` 生成目录
4. 新建 `src/protocol/CMakeLists.txt`
5. 新建 `src/protocol/client_proto.cmake`
6. 新建 `tools/client_proto/`
7. 建立 `client_proto` target
8. 让 `gate` 与 `sim_client` 依赖 `client_proto`

## 首版最小集合

首版客户端协议建议至少包含：

- `client.login.v1.LoginRequest`
- `client.login.v1.LoginResponse`
- `client.login.v1.HeartbeatRequest`
- `client.login.v1.HeartbeatResponse`
- `client.game.v1.PlayerMessageRequest`
- `client.game.v1.PlayerMessageResponse`
- `client.game.v1.PlayerPushMessage`
- `client.login.v1.KickNotification`

## KISS 复核

这份规划的核心是把“客户端协议工程”独立出来。

首版只需要做到：

- 独立目录
- 独立生成
- 独立 target
- 独立脚本

不需要一开始就做：

- 复杂协议代码生成框架
- 自定义 protoc 插件
- 客户端协议与业务处理自动绑定生成

先把边界搭正确，比自动化做满更重要。
