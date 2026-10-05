# ⚠️ 路径变更告知书 —— 致本工程（`Stelarith-viewer-qt`）的 AI 助手

> 生成时间：2026-10-04｜**开工前先读完。**
> 你若是在旧路径下被唤醒的，立刻改用新路径，并告知使用者。

**本工程已迁移：**

- 旧：`C:\Users\Administrator\Documents\stelarith-viewer-qt`
- 新：`D:\Stelarith\Stelarith-viewer-qt`
- 校验：2,773 = 2,773 文件，**零丢失**

---

## 路径替换速查

| 类型 | 旧 | 新 |
|---|---|---|
| 工程根 | `C:\Users\Administrator\Documents\stelarith-viewer-qt` | `D:\Stelarith\Stelarith-viewer-qt` |
| 日志 | `…\viewer-run.log` | `D:\Stelarith\Stelarith-viewer-qt\viewer-run.log` |
| 截图 | `…\shots` | `D:\Stelarith\Stelarith-viewer-qt\shots` |

> 同批迁移的还有 `Stelarith-control-qt`、`Stelarith-cloud-ws`。
> 总册见 `D:\Stelarith\路径变更告知书-2026-10-04.md`。

---

## 已代改（勿改回）—— 🚨 **必须重编才生效**

| 文件 | 改动 |
|---|---|
| `src\main.cpp` 第 27 行 | `kLogFile` → `D:/Stelarith/Stelarith-viewer-qt/viewer-run.log` |
| `src\main.cpp` 第 186 行 | `shotDir` → `D:/Stelarith/Stelarith-viewer-qt/shots` |

> 这两处是**源码**，不重编的话程序仍会往**已不存在的旧路径**写日志和截图。

**无需改动**：`build-qt-viewer.bat`（相对 `-S . -B build`）

## 你必须做的

1. **重编本工程** —— 源码已改，`deploy\viewer-qt.exe` 里烧的还是旧路径。
2. **删 `build\`** —— `CMakeCache.txt` 是旧绝对路径，删后重跑 `build-qt-viewer.bat`。
3. 本工程角色 = **老师机管理端**：看实时画面、发指令。UI 在 `qml\`。

## 服务现状

| 项 | 状态 |
|---|---|
| viewer 进程 | ⏸ **本次迁移时本就没在跑**，无计划任务，由人工启动 |
| 依赖 | 需云端 8788 在跑（`Start-ScheduledTask StelarithCloud`） |

## 留给你的引用

- `_archive\` 内是已归档的旧日志（`build2.log` / `cmake-cfg.log` / `viewer.log{,.bak,.prev}` / `qml-run.out`）
- `src\main-widgets-legacy.cpp.txt` 是历史留存文件，内有旧路径，**非编译单元**
