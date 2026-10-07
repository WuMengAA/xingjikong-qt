# cloud-ws 回归测试

> 2026-10-07 起固化。原则：**测试只对隔离实例跑**，不碰生产（8788）/ 不碰
> 生产 recordings。写完新功能顺手把关键路径固化成测试。
> 2026-10-08 增补：设备报修 CRUD + 设备接入票（验票端安全边界）。

## 运行方式

### 单元测试（无需起云端）

```bash
node tests/unit-ecodes.mjs            # 错误码体系：ECODES 格式/映射/errorPayload/兼容
node tests/unit-agent-ticket.mjs      # 设备接入票：7 段格式/签名往返/篡改/过期/fp 强制对照
# 或
npm run test:unit
```

### 集成回归（需先起隔离云端）

```bash
# 1) 起隔离实例（端口、令牌随便给，别用生产 .env 的令牌）
#    ⚠️ agent-ticket 相关脚本（单测/WS 集成）需要 CLOUD_VIEWER_SECRET —— 票是 HMAC 签的；
#    config.js 在**模块加载时**把密钥固化成常量，所以必须启动前就设好，跑起来后再设无效。
CLOUD_WS_PORT=8095 CLOUD_WS_HOST=127.0.0.1 \
  CLOUD_WS_TOKEN=dev-cloud-token CLOUD_VIEWER_TOKEN=dev-viewer-token \
  CLOUD_VIEWER_SECRET=it-secret \
  node src/index.js

# 2) 另开一个终端跑回归（脚本会连 8095）
node tests/regression-confirm-receipt.cjs 8095   # 通知确认回执链路（8/8）
node tests/regression-recordings.cjs 8095         # 班级监控 recordings（6/6，自清理）
node tests/regression-broadcast.cjs 8095          # 屏幕广播链路（8/8）
node tests/regression-voice.cjs 8095               # 语音对讲链路（8/8）
node tests/regression-repairs.cjs 8095             # 设备报修 CRUD（37/37，自清理）
node tests/regression-agent-ticket.mjs 8095        # 设备接入票 WS 握手（12/12，自清理）
```

`npm test` = 只跑单元测试（不依赖云端，最常用）。

## 覆盖清单

| 文件 | 类型 | 覆盖 | 断言数 |
|---|---|---|---|
| `unit-ecodes.mjs` | 单元 | ECODES 格式（模块2+4位数字、25键含 sem/msg）、关键映射（DV3001/DV3002/CM4001/AU1003）、errorPayload 归一化、未编号旧码兼容、errPayload 旧接口、双向自洽 | 14 |
| `unit-agent-ticket.mjs` | 单元 | 设备接入票 7 段格式、签验往返（uid/owner 还原）、签名/载荷篡改、过期、fp 不匹配与大小写不敏感、**fp 强制对照**（票有 fp 却不报 → 拒）、无 fp 票兼容、5 段 viewer 票与 7 段设备票互不混用 | 21 |
| `regression-confirm-receipt.cjs` | 集成 | 被控端 register → 收 notify(confirm+replies) → 二次回执 confirmed → 云端 recordResult 放行（不记 failed）→ 管理端收转发 | 8 |
| `regression-recordings.cjs` | 集成 | 设备令牌上传 → 401 边界（无/错令牌）→ 管理端列表 → 静态下载 video/mp4 → 自清理 | 6 |
| `regression-broadcast.cjs` | 集成 | 管理端 broadcast.start → 云端进广播态 + 通知被控端 → JPEG 帧含 v1 帧头不截断 → stop 后帧丢弃 | 8 |
| `regression-voice.cjs` | 集成 | 管理端 audio.start → 云端进语音态 + 通知被控端 → PCM 帧含 v1 帧头不截断 → stop 后帧丢弃 | 8 |
| `regression-repairs.cjs` | 集成 | 设备报修 CRUD：鉴权 3、POST 校验 7、列表 4、PATCH 5、DELETE 3、审计 3、字段正确性 9、自清理 1 | 37 |
| `regression-agent-ticket.mjs` | 集成 | 设备票真实 WS 握手：票+fp 匹配注册、**持票冒名别的 uid → 拒**、未报 uid 取自票、持票不报 fp → 拒、票拷到别的机器 → 拒、静态令牌兜底、伪票（签名乱/夹带静态令牌）→ 拒、非 register 首帧 → AUTH_REQUIRED、不发首帧不假注册、在线表入表 | 12 |

## 安全约定

- **永远不要**把 `CLOUD_WS_TOKEN` / `CLOUD_VIEWER_TOKEN` 指到生产（recordings 测试
  会往 recordings/<uid> 写文件，虽然结束会自清理，但写进生产目录就是事故）。
- 集成测试用随机 uid（如 TEST_CFMRX123 / REC229），避免多实例撞。
- 测完隔离实例记得停（`Stop-Process`），不要留着占 8095。