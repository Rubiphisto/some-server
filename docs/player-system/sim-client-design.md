# Sim Client 设计

## 状态

- 草案
- 范围：独立模拟客户端进程 `sim_client` 的职责、结构、协议依赖与测试场景

## 目标

建立一个独立新进程，以真实客户端协议连接 `gate`，用于：

- 联调
- 回归测试
- 压测
- 顶号测试
- 重连测试
- 协议回放

## 强约束

- `sim_client` 只依赖客户端协议
- `sim_client` 不依赖 IPC 协议
- `sim_client` 不调用服务端内部业务逻辑
- `sim_client` 必须通过真实网络路径连接 `gate`

## 建议目录

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
    protocol_service.h
    protocol_service.cpp
    scenario_service.h
    scenario_service.cpp
```

## 服务划分

### `connection_service`

职责：

- 与 `gate` 建立 TCP 连接
- 管理断开、重连
- 管理发送与接收缓冲
- 管理心跳

### `protocol_service`

职责：

- 按客户端协议打包与解包
- 协议号分发
- 响应结果解析
- 推送解析

### `scenario_service`

职责：

- 驱动测试场景
- 调度多个虚拟客户端
- 记录行为结果

## 虚拟客户端模型

建议一个 `sim_client` 进程内支持多个虚拟用户：

```text
virtual_client_id
account_id
connection_state
login_state
bound_player_id
target_gate
scenario_context
```

## 核心场景

至少支持：

### 1. 基础登录场景

- 建立连接
- 登录
- 心跳
- 发一条玩家消息
- 收响应

### 2. 断线重连场景

- 登录
- 主动断开
- 在暂留窗口内重新连接
- 验证是否恢复成功

### 3. 顶号场景

- 同一账号两个连接登录
- 验证旧连接被踢
- 验证新连接接管成功

### 4. 跨 gate 重连场景

- 先连 gate A
- 断开
- 再连 gate B
- 验证 `game` 中玩家实例被复用

### 5. 多账号并发场景

- 多连接同时登录
- 并发发消息
- 收集延迟与错误

## 输出与观测

建议支持：

- 控制台日志
- 场景结果汇总
- 失败统计
- 协议收发统计
- 可选导出为文本或 JSON 报告

## 与正式客户端的关系

`sim_client` 不需要完整复制正式客户端逻辑，但需要尽可能复用：

- 客户端协议号
- 客户端 protobuf message
- 编解码规则

不能复用：

- UI
- 游戏表现层
- 服务端内部协议

## 实现优先级

首版先做：

1. 单连接登录
2. 心跳
3. 玩家消息请求/响应
4. 断线重连
5. 顶号

后续再做：

6. 多账号并发
7. 场景脚本化
8. 协议回放
9. 压测模式

## 为什么必须单独做进程

因为只有单独进程才能真实验证：

- TCP 接入
- 编解码
- 长连接管理
- 顶号
- 断线重连
- gate 与 game 协同

如果只是进程内 mock，会漏掉最关键的接入问题。
