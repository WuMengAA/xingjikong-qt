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
