# ⚠️ 路径变更告知书 —— 致本工程（`Stelarith-control-qt`）的 AI 助手

> 生成时间：2026-10-04｜**开工前先读完。**
> 你若是在旧路径下被唤醒的，立刻改用新路径，并告知使用者。

**本工程已迁移：**

- 旧：`C:\Users\Administrator\Documents\stelarith-control-qt`
- 新：`D:\Stelarith\Stelarith-control-qt`
- 校验：1,358 = 1,358 文件，**零丢失**

---

## 路径替换速查

| 类型 | 旧 | 新 |
|---|---|---|
| 工程根 | `C:\Users\Administrator\Documents\stelarith-control-qt` | `D:\Stelarith\Stelarith-control-qt` |
| 配置 | `…\deploy\agent.env` | `…\deploy\agent.env`（内容已改） |
| 容器根 | — | `D:\Stelarith` |

> 同批迁移的还有 `Stelarith-viewer-qt`、`Stelarith-cloud-ws`。跨工程引用请一并更新。
> 总册见 `D:\Stelarith\路径变更告知书-2026-10-04.md`。

---

## 已代改（勿改回）

| 文件 | 改动 |
|---|---|
| `deploy\agent.env` | `STE_QT_SHOT_DIR` → `D:\Stelarith\Stelarith-control-qt\deploy\shots` |

**无需改动**：`src\main.cpp`（无硬编码绝对路径）· `build-qt-agent.bat`（相对 `-S . -B build`）· `installer.nsi`（只写 `dist\…`）

## 你必须做的

1. **删 `build\` 与 `build2\`** —— `CMakeCache.txt` 里是旧绝对路径，删后重跑 `build-qt-agent.bat`。
2. 本工程角色 = **教室机被控端**：收下发、真执行、推画面。运行副本装在 `C:\Program Files\Stelarith\`，**不在本目录**。

## 服务现状

| 项 | 状态 |
|---|---|
| 被控端进程 `stelarith-agent-qt.exe` | ⏸ **已停止**（本次迁移前强制终止，未重启） |
| 计划任务 `StelarithAgentQt` | Ready 且仍 Enabled → 下次登录自动拉起；`Start-ScheduledTask StelarithAgentQt` 可立即恢复 |
| 本地测试配置 `deploy\agent.env` | `ws://127.0.0.1:8788/ws/agent`、uid `TEST1`、token `dev-cloud-token` |

## 留给你的引用

- `星集控-MSI安装包-加减归档-2026-10-03.md` 正文里仍写旧路径（文档，非代码）
- `_archive\` 内是已归档的调试日志/截图（含 `shots-d4\`、`shots-d5\`），可查不可删
