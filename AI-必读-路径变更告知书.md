# ⚠️ 路径变更告知书 —— 致本工程（`Stelarith-cloud-ws`）的 AI 助手

> 生成时间：2026-10-04｜**开工前先读完。**
> 你若是在旧路径下被唤醒的，立刻改用新路径，并告知使用者。

**本工程已迁移：**

- 旧：`C:\Users\Administrator\Documents\stelarith-cloud-ws`
- 新：`D:\Stelarith\Stelarith-cloud-ws`
- 校验：79 = 79 文件，**零丢失**（含 `node_modules`）

---

## 路径替换速查

| 类型 | 旧 | 新 |
|---|---|---|
| 工程根 | `C:\Users\Administrator\Documents\stelarith-cloud-ws` | `D:\Stelarith\Stelarith-cloud-ws` |
| 启动器 | `…\run-cloud.cmd` | `D:\Stelarith\Stelarith-cloud-ws\run-cloud.cmd` |
| 配置 | `…\.env` | `D:\Stelarith\Stelarith-cloud-ws\.env` |

> 同批迁移的还有 `Stelarith-control-qt`、`Stelarith-viewer-qt`。
> 总册见 `D:\Stelarith\路径变更告知书-2026-10-04.md`。

---

## 已代改（勿改回）

| 位置 | 改动 |
|---|---|
| **计划任务 `StelarithCloud`** | Execute 与工作目录均 → `D:\Stelarith\Stelarith-cloud-ws\run-cloud.cmd` |

**无需改动**：`run-cloud.cmd` 内部用 `%~dp0`（相对自身），搬家天然不受影响。

## 本工程要点

- 角色 = **中转站**：被控端注册（`/ws/agent`）＋ 推帧 ＋ 转发指令；管理端 `/ws/viewer` 订阅与下发。
- 监听 `127.0.0.1:8788`，由计划任务 `StelarithCloud`（开机触发、SYSTEM）拉起。
- `node_modules` 已随迁，**无需重装**。
- `.env` 内 `CLOUD_VIEWER_SECRET` **必须与站点 `.env` 同名键一致**，否则手机集控页会被云端拒。
- 活日志（进程占用，**勿删勿移**）：`cloud-service.log`、`events.log`。

## 服务现状

| 项 | 状态 |
|---|---|
| 云端 8788 | ⏸ **已停止**，端口不再监听 |
| 计划任务 `StelarithCloud` | Ready 且仍 Enabled → 下次登录自动拉起 |
| 恢复命令 | `Start-ScheduledTask StelarithCloud` |

## 留给你的引用

- `_archive\` 内是已归档的旧日志与临时脚本（`cloud.log` / `cloud8788.log` / `probe.out` / `tmp-*.py` / `tmp-*.mjs`）

---

## 2026-10-20 改名：`Stelarith-cloud-ws` → `Stelarith-cloud`（去 ws 后缀）

用户点单「云端改名，去 ws 后缀」。**目录/包名/计划任务/bare 远端全部切换；令牌、端口、协议零改动。**

| 项 | 旧 | 新 |
|---|---|---|
| 工程目录 | `D:\Stelarith\Stelarith-xingjikong-qt\Stelarith-cloud-ws` | `D:\Stelarith\Stelarith-xingjikong-qt\Stelarith-cloud` |
| package.json `name` | `stelarith-cloud-ws` | `stelarith-cloud` |
| 计划任务 `StelarithCloud` | `…\Stelarith-cloud-ws\run-cloud.cmd` | `…\Stelarith-cloud\run-cloud.cmd` |
| 计划任务 `StelarithCloudWatchdog` | `…\Stelarith-cloud-ws\cloud-watchdog.cmd` | `…\Stelarith-cloud\cloud-watchdog.cmd` |
| bare 远端（remote `outer`） | `D:/Stelarith/Stelarith-cloud-ws` | `D:/Stelarith/Stelarith-cloud` |

**不变**：`.env`（含 `CLOUD_WS_TOKEN` / `CLOUD_VIEWER_TOKEN`，客户端成对不受影响）、8788 端口、
`run-cloud.cmd` / `cloud-watchdog.cmd` 内部逻辑（均 `%~dp0` 自定位，搬家天然无感）。

**生产在跑时的改名方法（2026-10-20 实证）**：`Disable-ScheduledTask StelarithCloudWatchdog`
→ `Stop-ScheduledTask StelarithCloud`（node 句柄锁目录，`git mv` 会 Permission denied，属正常）
→ `git mv Stelarith-cloud-ws Stelarith-cloud` → 改两条任务动作 → `Enable` + `Start`。
验证：8788 重新 LISTENING、看门狗手动触发 exit 0（no-op）、`/api/public/ota` 正常应答。
