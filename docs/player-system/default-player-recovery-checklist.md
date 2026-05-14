# 默认数据玩家补档检查清单

## 状态

- 草案
- 作用：把“先服务、后补档”的策略落成可执行的运维与回归检查项

## 适用场景

这份清单只适用于以下情况：

- 新玩家首次登录时，MariaDB 一度不可用
- 玩家已用默认数据进入游戏
- 后续需要确认该玩家是否已经真正补档成功

它不适用于：

- 旧玩家正常读档
- 普通在线脏数据 flush
- 单纯 Redis 热数据命中场景

## 核心判定原则

必须同时满足下面两点，才能认为“补档完成”：

1. 玩家不再处于：
   - `pending_initial_persist=true`
   - `created_without_maria=true`

2. 成功判定以 Maria 持久化成功为准
   - 不能只看 Redis

## 检查顺序

### 1. 先确认 Maria 已恢复

在 `game` 进程执行：

```text
storage_probe
```

期望：

- `storage maria probe ... reachable=true`

如果这里仍是 `reachable=false`，后面的补档检查没有意义。

### 2. 看玩家当前是否仍处于待补档状态

在 `game` 进程执行：

```text
player_status <player_id>
player_persistence_player_status <player_id>
```

重点字段：

- `dirty`
- `pending_initial_persist`
- `created_without_maria`
- `flush_count`
- `last_flush_ms`

若仍然看到：

- `pending_initial_persist=true`
- 或 `created_without_maria=true`

说明补档尚未完成。

### 3. 主动触发一次持久化扫描

```text
player_persistence_flush_due
player_persistence_status
player_persistence_player_status <player_id>
```

期望：

- `flush_success_count` 增长
- 玩家单体视图里：
  - `pending_initial_persist=false`
  - `created_without_maria=false`
  - `flush_count` 增长

### 4. 必要时走显式恢复命令

如果之前已经进入 backoff，可执行：

```text
player_persistence_recover_flush
```

然后再次查看：

```text
player_persistence_player_status <player_id>
```

期望：

- retry/backoff 状态被清掉
- 玩家补档状态被清掉

### 5. 最终用 Maria 回读确认

若要做强确认，建议：

1. 删除该玩家 Redis 热数据
2. 再执行 `storage_dataset_get player <player_id>`

期望：

- 返回来源为 `maria`

这一步的意义是确认：

- 不是只写进了 Redis
- Maria 中已经存在可回源的数据

## 不允许放行的状态

只要出现下面任一项，都不应把该玩家视为“补档已完成”：

- `pending_initial_persist=true`
- `created_without_maria=true`
- `dirty=true`
- `storage_probe` 显示 Maria 仍不可达
- `player_persistence_player_status` 仍在持续 backoff
- 只能从 Redis 取到数据，无法确认 Maria 回读

## 释放前检查

若玩家准备进入释放路径，必须额外确认：

- `player_release` 没有被 flush 保护拦住
- 玩家不再是 `pending_initial_persist`
- 玩家不再是 `created_without_maria`

如果 `player_release` 失败，应直接看失败日志中的这些字段：

- `loaded`
- `dirty`
- `pending_initial_persist`
- `created_without_maria`
- `flush_count`
- `last_dirty_ms`
- `last_flush_ms`

## 回归建议

建议至少保留一条专门场景，覆盖：

1. Maria 不可用
2. 新玩家首次登录成功
3. 玩家以默认数据进入游戏
4. Maria 恢复
5. 主动触发或等待补档成功
6. 确认 Maria 回读成功

当前已落地一条对应的 host-only 回归场景：

- `default_player_recover_after_maria_restore`

如果以后要把这条链路接进 nightly，可以直接用这份清单作为验收标准。

## KISS 复核

这份清单只依赖现有命令与现有观测：

- `storage_probe`
- `player_status`
- `player_persistence_status`
- `player_persistence_player_status`
- `player_persistence_flush_due`
- `player_persistence_recover_flush`
- `storage_dataset_get`

它没有引入新的业务状态，也没有新增专门的“补档流程服务”。

如果后续为了检查补档，又新增一套并行状态机或专用命令体系，就已经偏离 KISS 了。
