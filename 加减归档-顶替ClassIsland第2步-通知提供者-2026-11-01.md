# 加减归档 · 灵动岛顶替 ClassIsland 第 2 步：通知提供方注册制 + 设置页（2026-11-01）

> 提交：`d4330cf`（control-qt）｜ 净 **+783 / −74**
> 用户点单：「第 2 步」；优先级裁决 **管理端 > 课程 > 对讲 > 文件 > U盘 > 自动化 > 插件 > 考试**
> 口径：**不移植、不复制** —— 机制参照 ClassIsland 的 provider/channel 两级，代码全部自有实现。

---

## 1. 新增文件

| 文件 | 行数 | 职责 |
|---|---|---|
| `src/notify/provider_registry.h` | 90 | 提供方/渠道两级定义 + 门控契约 + 「未知名放行」策略 |
| `src/notify/provider_registry.cpp` | 289 | 8 个提供方 × 渠道、QSettings 持久化、`allow()`、`settingsModel()` |
| `src/notify/agent_ui_providers.cpp` | 114 | AgentUi 的设置页方法体（含搬来的 `settingsSections`） |
| `qml/shell/pages/settings/SecNotifyProviders.qml` | 109 | 设置页「通知提供者」卡片 |

## 2. 修改文件

| 文件 | 改动 |
|---|---|
| `src/island.h` / `src/island.cpp` | 新增 `setProviderForNext()` 一次性 hint；`showIsland` 入口门控（关掉的提供方整条丢弃，且同时清掉 EndTime hint 不留给下一条） |
| `src/notify/notify_entry.cpp` | `params.provider`（缺省 `admin`；`from` 前缀 `plugin` 归一）+ `params.channel` 门控，被拦返回 `skipped:provider-disabled:<id>` |
| `src/main.cpp` | 上课/下课两处声明 `provider=class`（+2 行 hint / −2 行注释，**净 0 行**，闸 PASS 4443≤4443） |
| `src/agent_ui.h` | 新增 `notifyProviders` Q_PROPERTY + 4 个 Q_INVOKABLE + 1 个信号；**同时把 `settingsSections()` 的内联实现搬出**，722 → 658 行 |
| `qml/shell/pages/settings/SecNotify.qml` | 挂载卡片（+4 行） |
| `CMakeLists.txt` | 登记 3 个新源文件 |
| `scripts/_island_qml_probe.cpp` | 新增 `--providers` 模式（14 条断言）+ QSettings 落点诊断 |
| `scripts/_agent_ui_probe.py` | 链 `agent_ui_providers.cpp.obj` + `provider_registry.cpp.obj` |

## 3. 数据与口径

**8 个提供方（顺序即默认优先级）**：`admin` 管理端 / `class` 课程提醒 / `voice` 对讲 / `file` 文件 / `usb` U盘助手 / `automation` 自动化 / `plugin` 插件 / `exam` 考试。
每个提供方带 1~3 个渠道（如课程有「准备上课 / 上课 / 下课」）。

**QSettings 键**（设备级）：
- `notify/providers/order` → 优先级序（QStringList）
- `notify/providers/<id>/enabled` → 提供方开关（缺省用内置值）
- `notify/channels/<channelId>` → 渠道开关

**三条钉死的口径**：
1. **未知名一律放行** —— 老云端/老插件不该因为被控端升级被静默丢通知（宁可多弹一条，不要"老师发了没人看见"）。
2. **渠道名写错不丢** —— 已知提供方 + 未知渠道 ⇒ 放行（不为一个拼写错误静默吞通知）。
3. **「考试」排最后是用户裁决**，注释里写明"不是笔误"（考试期静默由考试模式自己管）。

## 4. 门控接线现状（**不摆假开关**）

| 提供方 | 门控是否真生效 | 路径 |
|---|---|---|
| 管理端 | ✅ | `notify_entry` 的 `params.provider`（缺省 admin） |
| 插件 | ✅ | 同上；`from` 以 `plugin` 开头时归一为 plugin |
| 课程提醒 | ✅ | `island::setProviderForNext("class")`（上课/下课两处） |
| 对讲 / 文件 / U盘 / 自动化 / 考试 | ⏳ **尚未接线** | 表里可开关，但当前不影响这些路径 |

> 这 5 个的调用点在模块/域层（`usbmodule` 的 `islandAsk`、文件传输、语音、自动化引擎、考试模式），
> 需要给 `ModuleAgentApi` / 各域加一条 provider 声明，属下一批。**没有假装它们已生效** —— 归档与提交信息都写明。

## 5. 验证证据（实跑）

| 验证 | 结果 |
|---|---|
| 探针 `--providers` | **14 条断言全 PASS**：默认序 / model 8 行 / 未知名放行（provider 与 channel 两层）/ 关 provider 拦截且不影响他人 / 关渠道只拦该类 / 同提供方其它渠道不受影响 / 渠道名写错不丢 / 上移生效 / 首行 priority=1 且 canUp=false / 末行 canDown=false / 重置回默认 / 重置后恢复启用 |
| 探针 `--endat 3000` | PASS（第 1 步 EndTime 未被破坏） |
| 被控端 UI 探针 | `g_qmlErrors=0`（新卡片与嵌套 Repeater 加载零错误），21 张截图正常产出 |
| 新卡片单独渲染 | 截图确认：8 行提供方 + 开关 + 上移/下移（首末行只画该有的那枚）+ 渠道子行 + 恢复默认；版面无重叠/裁切 |
| `check-main-size.mjs` | PASS 4443 ≤ 4443（**main.cpp 净 0 行**） |
| `test-ui-terms` / `test-qml-structural` | 7/0 PASS ／ 0 新增违例 |
| `test-file-size` | 本批文件无违规；`agent_ui.h` 722 → 658 回落到既有超线档（`~`，不判红） |
| 构建 | `_build_agent.py` exit 0 |

## 6. 过程中踩到并记下的坑（都写进了代码注释）

1. **临时 `QSettings` 写入不落地**：`QSettings().setValue(...)` 用临时对象时，同进程紧接着回读得到 `<unset>`。
   改成显式 `sync()` 的读写助手后确定可读回。
2. **受限会话写不了注册表**：裸写一个键 `QSettings::status() == AccessError`。
   探针因此把后端切到 **ini + 临时目录**（验的是同一套逻辑，生产仍走注册表）。
3. **搬家后必须去掉 `inline`**：`settingsSections` 从头部内联搬到 TU 后仍是 `inline` ⇒ 只在一个 TU 定义、
   不产生外部符号 ⇒ moc 那边 `LNK2019`。去 `inline` 即好。
4. **探针链接集要跟着长**：`island.cpp` 现在引 `provider_registry`、`moc` 引 AgentUi 新成员，
   两个探针（island / agent_ui）都要补链 obj，否则 LNK2019。

## 7. 未验证声明

- **未部署生产**、未跑真机；探针均为 offscreen。
- **5 个提供方的门控尚未接线**（对讲/文件/U盘/自动化/考试）—— 表里可开关但当前不影响这些路径。
- 云端 `params.provider` 链路**无端到端探针**（notify_entry 不在 island 探针链接集内）。
- 深浅两套主题下新卡片的观感只核了浅色（渲染 harness 默认浅色；生产深浅跟随 `Theme.dark`）。
- `scripts/_agent_ui_probe.py` **原本就带另一条线 2026-10-31 的未提交改动**（mic_selector/noise_monitor
  链接 + winmm），为让探针跑通一并提交，非本批产出 —— 已在提交信息里记账。

## 8. 下一步（第 3 步候选）

1. 把 5 个未接线的提供方接上（模块/域层加 provider 声明）；
2. 排队与仲裁：队列 4 + 三层优先（抢占 / 排队 / 合并），先做同步版最小可用。
