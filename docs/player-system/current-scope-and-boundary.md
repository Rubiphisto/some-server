# 当前版本范围与完成线

## 状态

- 当前有效
- 作用：明确当前版本已经收口到哪里，以及哪些事项明确不在当前范围内

## 当前版本目标

当前版本只覆盖：

- 玩家登录
- `gate` 长连接接入
- `gate -> game` 转发
- 玩家实例加载、暂留、重连复用
- 同账号顶号
- 常见断线重连
- Redis 热态与 Maria 基本持久化
- `PlayerDirectoryService` 目录映射与计数器持久化

目标原则：

- 先保证主链路完整
- 先解决基础功能与常见高频问题
- 不为低频边角问题提前堆复杂度

## 当前完成线

当前版本以以下 5 条 host-only 回归场景作为完成线：

1. `reconnect_after_disconnect`
2. `same_account_kick`
3. `migration_recover_after_kick`
4. `default_player_recover_after_maria_restore`
5. `player_directory_persist_across_restart`

只要这 5 条场景持续通过，就认为当前版本的“基本功能 + 高频问题处理”仍然成立。

## 当前正式保留的组件

### `PlayerDirectoryService`

当前作为正式组件保留并继续使用。

当前职责只有两类：

- `platform + account_id + area_id -> player_id`
- `next_player_id` 持久化计数器

当前不承担：

- 玩家数据加载
- lease
- `gate` 路由
- 账号中心类聚合职责

当前实现边界：

- 使用独立 `directory` dataset
- 使用独立 Redis 前缀：`player_directory:`
- 不借用玩家数据 dataset

## 当前明确不做的事项

以下事项明确不属于当前版本范围：

- `social`
- 无感迁移
- CI 接入
- nightly 流水线
- 离线迁移流程
- 低频释放异常治理
- 低频补档异常治理
- 低频 lease 丢失异常治理
- 更复杂的目录中心化设计

## 当前设计约束

当前仍然保持以下边界：

- `gate` 不持有玩家业务对象
- `game` 不直接操作客户端连接对象
- 客户端协议与 IPC 协议彻底分离
- `PlayerDirectoryService` 只做目录，不做玩家业务
- 回归脚本和宿主测试入口只放在 `tools/sim_client/`

## 何时才应扩大范围

只有出现下面两类情况，才建议继续扩设计：

1. 当前 5 条场景之外，出现了真实高频问题
2. 当前边界已经阻碍主链路继续使用

在此之前，默认不扩大范围。
