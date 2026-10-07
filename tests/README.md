# cloud-ws 回归测试

> 2026-10-07 起固化。原则：**测试只对隔离实例跑**，不碰生产（8788）/ 不碰
> 生产 recordings。写完新功能顺手把关键路径固化成测试，下面三个是第一批。

## 运行方式

### 单元测试（无需起云端）

```bash
node tests/unit-ecodes.mjs        # 错误码体系：ECODES 格式/映射/errorPayload/兼容
# 或
npm run test:unit
```

### 集成回归（需先起隔离云端）

```bash
# 1) 起隔离实例（端口、令牌随便给，别用生产 .env 的令牌）
CLOUD_WS_PORT=8095 CLOUD_WS_HOST=127.0.0.1 \
  CLOUD_WS_TOKEN=dev-cloud-token CLOUD_VIEWER_TOKEN=dev-viewer-token \
  node src/index.js

# 2) 另开一个终端跑回归（脚本会连 8095）
node tests/regression-confirm-receipt.cjs 8095   # 通知确认回执链路（8/8）
node tests/regression-recordings.cjs 8095         # 班级监控 recordings（6/6，自清理）
node tests/regression-broadcast.cjs 8095          # 屏幕广播链路（8/8）
```

`npm test` = 只跑单元测试（不依赖云端，最常用）。

## 覆盖清单

| 文件 | 类型 | 覆盖 | 断言数 |
|---|---|---|---|
| `unit-ecodes.mjs` | 单元 | ECODES 格式（模块2+4位数字、25键含 sem/msg）、关键映射（DV3001/DV3002/CM4001/AU1003）、errorPayload 归一化、未编号旧码兼容、errPayload 旧接口、双向自洽 | 14 |
| `regression-confirm-receipt.cjs` | 集成 | 被控端 register → 收 notify(confirm+replies) → 二次回执 confirmed → 云端 recordResult 放行（不记 failed）→ 管理端收转发 | 8 |
| `regression-recordings.cjs` | 集成 | 设备令牌上传 → 401 边界（无/错令牌）→ 管理端列表 → 静态下载 video/mp4 → 自清理 | 6 |
| `regression-broadcast.cjs` | 集成 | 管理端 broadcast.start → 云端进广播态 + 通知被控端 → JPEG 帧含 v1 帧头不截断 → stop 后帧丢弃 | 8 |

## 安全约定

- **永远不要**把 `CLOUD_WS_TOKEN` / `CLOUD_VIEWER_TOKEN` 指到生产（recordings 测试
  会往 recordings/<uid> 写文件，虽然结束会自清理，但写进生产目录就是事故）。
- 集成测试用随机 uid（如 TEST_CFMRX123 / REC229），避免多实例撞。
- 测完隔离实例记得停（`Stop-Process`），不要留着占 8095。