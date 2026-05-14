# Host-Only 集成回归接入说明

## 状态

- 草案
- 作用：说明 `sim_client` 多进程回归如何在宿主环境、本地联调机、nightly runner 或手工流水线中接入

## 适用范围

这份文档只覆盖以下 host-only 回归入口：

- `tools/sim_client/run_regression.sh <scenario>`
- `tools/sim_client/run_all_regressions.sh`
- `tools/sim_client/run_host_ctest.sh`
- `cmake --build build --target sim_client_regression_<scenario>`
- `cmake --build build --target sim_client_regression`
- `ctest --test-dir build -R sim_client_regression_...`

它不覆盖普通单元测试，也不覆盖受限沙箱中的默认测试流。

## 为什么是 host-only

这些回归场景会真实拉起：

- `etcd`
- `relay`
- `game1`
- `game2`
- `gate`
- `sim_client`

并依赖宿主环境允许：

- 本地监听多个 TCP 端口
- 启动多个长期存活的进程
- 访问宿主 Redis
- 访问宿主 MariaDB

因此它们不应默认并入普通 `ctest` 或受限沙箱测试。

## 前置条件

宿主 runner 或本地联调机需要满足：

1. 已完成工程构建
   - `cmake -S . -B build`
   - `cmake --build build --target relay gate game sim_client -j 4`

2. 可执行依赖存在
   - `etcd`
   - `etcdctl`
   - `redis-cli`

3. 宿主 Redis 可用
   - `127.0.0.1:6379`

4. 宿主 MariaDB 可用
   - `127.0.0.1:3306`
   - database: `some_game`
   - username: `game`
   - password: `game`

5. 若是历史环境，建议先确认 dataset 表结构
   - 可通过 `game` 运行时命令：
     - `storage_dataset_init player`
     - `storage_dataset_migrate_binary player`

## 推荐接入层级

### 1. 本地手工单场景

适用：

- 开发中针对某个问题快速回归
- 先看单场景是否稳定

推荐命令：

```bash
cmake --build build --target sim_client_regression_same_account_kick
```

或：

```bash
tools/sim_client/run_regression.sh same_account_kick
```

### 2. 本地手工全场景

适用：

- 合并前人工检查
- 调整了 `gate/game/sim_client` 主链路后做一次全量核对

推荐命令：

```bash
cmake --build build --target sim_client_regression
```

或：

```bash
tools/sim_client/run_all_regressions.sh
```

### 3. 宿主 runner 上的 host-only CTest

适用：

- nightly
- 手工触发流水线
- 需要场景粒度测试报告

配置：

```bash
cmake -S . -B build -DENABLE_HOST_INTEGRATION_TESTS=ON
```

执行：

```bash
ctest --test-dir build -R sim_client_regression_ --output-on-failure
```

也可以直接使用统一脚本：

```bash
tools/sim_client/run_host_ctest.sh
```

如只想在 runner 上执行单场景，可配合 `CTEST_REGEX`：

```bash
CTEST_REGEX=sim_client_regression_same_account_kick \
tools/sim_client/run_host_ctest.sh
```

当前会注册 5 条独立测试：

- `sim_client_regression_reconnect_after_disconnect`
- `sim_client_regression_same_account_kick`
- `sim_client_regression_migration_recover_after_kick`
- `sim_client_regression_default_player_recover_after_maria_restore`
- `sim_client_regression_player_directory_persist_across_restart`

建议理解为 5 类覆盖点：

- `reconnect_after_disconnect`
  - 显式断线与重连复用
- `same_account_kick`
  - 同账号顶号踢线
- `migration_recover_after_kick`
  - owner 迁移后旧连接收敛与恢复
- `default_player_recover_after_maria_restore`
  - Maria 不可用窗口下的新玩家默认数据补档
- `player_directory_persist_across_restart`
  - 目录映射与 `next_player_id` 计数器跨 `game` 重启保持一致

## Nightly 建议

推荐把这组回归作为：

- nightly 定时任务
- 或手工触发的 host-only integration job

不建议：

- 每次普通 PR 都默认跑这组场景

原因：

- 它们依赖宿主 Redis/Maria/etcd
- 单次执行成本明显高于普通单元测试
- 失败诊断更偏基础设施与多进程协同

如果后续需要把它接进更频繁的流水线，建议先确保：

- 宿主 runner 长期稳定
- Maria/Redis 的测试态隔离策略足够清楚
- 失败产物可以被流水线归档

## 失败排障顺序

当某个场景失败时，建议按下面顺序看：

1. 先看聚合脚本是否已经自动打印摘要
   - `run_all_regressions.sh` 失败时会自动展开 `latest-<scenario>` 摘要

2. 单独查看最近一次产物

```bash
tools/sim_client/summarize_artifact.sh latest-same_account_kick
```

3. 再进入产物目录查看详细日志
   - `build/test-artifacts/sim_client/`

4. 最后才回看完整 stdout / app log / error log

若失败点涉及：

- Maria 不可用窗口下的新玩家默认数据
- 补档成功与否

建议同时参考：

- [default-player-recovery-checklist.md](/home/dev/projects/some-server/docs/player-system/default-player-recovery-checklist.md)

## 产物策略

默认：

- 失败场景自动保留产物

可选：

- 成功场景可通过 `KEEP_ARTIFACTS=1` 保留

示例：

```bash
KEEP_ARTIFACTS=1 tools/sim_client/run_regression.sh migration_recover_after_kick
```

保留后可通过：

- `latest-reconnect_after_disconnect`
- `latest-same_account_kick`
- `latest-migration_recover_after_kick`
- `latest-default_player_recover_after_maria_restore`
- `latest-player_directory_persist_across_restart`

快速定位最近一次产物。

## KISS 复核

当前接入策略刻意保持简单：

- 业务主链路不感知 CI
- `gate/game/sim_client` 不额外引入测试专用分支
- 回归编排和归档都留在 `tools/sim_client/`
- 是否注册到 `ctest` 由 `ENABLE_HOST_INTEGRATION_TESTS` 显式控制

如果后续出现下面这些倾向，应重新审视：

- 为了 CI 去改业务协议或主逻辑
- 在应用代码里加入大量仅测试使用的分支
- 把 host-only 场景强塞进默认单元测试流
- 为了流水线方便而让多场景重新并发执行

出现这些情况，说明当前低耦合边界开始被破坏。
