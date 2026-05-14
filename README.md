# some-server

## Sim Client 回归入口

更完整的宿主环境 / nightly / CTest 接入建议见：

- [docs/player-system/host-integration-ci.md](/home/dev/projects/some-server/docs/player-system/host-integration-ci.md)
- [docs/player-system/current-scope-and-boundary.md](/home/dev/projects/some-server/docs/player-system/current-scope-and-boundary.md)

`sim_client` 相关的多进程回归场景已经统一收敛到：

- `tools/sim_client/run_regression.sh <scenario>`
- `tools/sim_client/run_all_regressions.sh`
- `tools/sim_client/run_host_ctest.sh`

当前支持的场景：

- `reconnect_after_disconnect`
- `same_account_kick`
- `migration_recover_after_kick`
- `default_player_recover_after_maria_restore`
- `player_directory_persist_across_restart`

场景含义：

- `reconnect_after_disconnect`
  - 验证显式断开后的重新连接与重新登录
- `same_account_kick`
  - 验证同账号新连接踢掉旧连接
- `migration_recover_after_kick`
  - 验证 owner 迁移后旧连接被踢、客户端显式恢复
- `default_player_recover_after_maria_restore`
  - 验证 Maria 不可用窗口下新玩家默认数据登录、Maria 恢复后补档成功
- `player_directory_persist_across_restart`
  - 验证目录映射与 `next_player_id` 计数器在 `game` 重启后保持一致

典型用法：

```bash
cmake --build build --target sim_client_regression_same_account_kick
cmake --build build --target sim_client_regression_migration_recover_after_kick
cmake --build build --target sim_client_regression
```

如果你希望把它注册进 `ctest`，可以显式开启：

```bash
cmake -S . -B build -DENABLE_HOST_INTEGRATION_TESTS=ON
ctest --test-dir build -R sim_client_regression_same_account_kick --output-on-failure
```

也可以直接走统一 runner 入口：

```bash
tools/sim_client/run_host_ctest.sh
```

如只想跑某一条 host-only 场景，可配合 `CTEST_REGEX`：

```bash
CTEST_REGEX=sim_client_regression_player_directory_persist_across_restart \
tools/sim_client/run_host_ctest.sh
```

或直接运行单场景：

```bash
cmake --build build --target sim_client_regression_same_account_kick
tools/sim_client/run_regression.sh same_account_kick
```

如果你希望保留成功场景的工作目录和日志：

```bash
KEEP_ARTIFACTS=1 tools/sim_client/run_regression.sh same_account_kick
```

查看最近一次保留产物的摘要：

```bash
tools/sim_client/summarize_artifact.sh latest-same_account_kick
```

说明：

- 这些脚本会独立拉起 `etcd + relay + game1 + game2 + gate + sim_client`
- 它们依赖宿主环境允许本地监听端口，不适合放进受限沙箱里的默认单元测试流
- 手工入口现在同时支持：
  - `cmake --build build --target sim_client_regression_<scenario>`
  - `cmake --build build --target sim_client_regression`
- 聚合 target 会串行执行全部场景，避免多场景并发时互抢端口与测试账号
- 失败场景的日志与配置快照会默认保留在 `build/test-artifacts/sim_client/`
- 可通过 `KEEP_ARTIFACTS=1` 保留成功场景产物，`latest-<scenario>` 会指向最近一次保留的目录
- 可通过 `tools/sim_client/summarize_artifact.sh latest-<scenario>` 直接查看最近一次保留产物的失败信号、关键状态和日志尾部
- `tools/sim_client/run_all_regressions.sh` 在某个场景失败时，会自动展开对应 `latest-<scenario>` 的摘要
- `tools/sim_client/run_host_ctest.sh` 会固定执行：
  - `cmake -S . -B build -DENABLE_HOST_INTEGRATION_TESTS=ON`
  - `cmake --build build --target relay gate game sim_client`
  - `ctest --test-dir build -R <regex> --output-on-failure`
- 默认不会自动注册进 `ctest`，只有开启 `ENABLE_HOST_INTEGRATION_TESTS=ON` 才会作为 host-only 集成测试出现
- 开启后会拆成多条独立测试，当前包括：
  - `sim_client_regression_reconnect_after_disconnect`
  - `sim_client_regression_same_account_kick`
  - `sim_client_regression_migration_recover_after_kick`
  - `sim_client_regression_default_player_recover_after_maria_restore`
  - `sim_client_regression_player_directory_persist_across_restart`
- 默认路线仍然是“显式断开 + 显式重连提示”，不做无感迁移
