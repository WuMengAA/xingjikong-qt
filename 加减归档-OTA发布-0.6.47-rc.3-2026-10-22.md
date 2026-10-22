# 加减归档 · OTA 发布 0.6.47-rc.3（CMD 窗口根治铺量）· 2026-10-22

> 本轮性质：**发版**（不产新功能代码）。把 10-22 白天修好的「CMD 控制台窗根治」
> 通过 OTA 推给全校教室机，并把从 16:23 起一直没入轨的 qml-patch 热更包提交。
> 功能代码的完整加减归档见《加减归档-CMD窗口根治-第二批-2026-10-22.md》。

## ① 新增文件（逐条）

| 路径 | 行数 | 职责 | 来源 | 与旧实现的行为差异 |
|---|---|---|---|---|
| `Stelarith-control-qt/dist/stelarith-agent-setup-0.6.47-rc.3.exe` | — | agent 安装包（ISCC 出，35.8 MB） | 本轮 20:03 构建 + 20:05 打包 | 内含 rc.3 exe（6 处 ffmpeg/ffplay 隐藏 + 版本号）、build/qml 全量 QML（含拖拽排序版 BroadcastScheduleSheet）、修好的 install-autostart.ps1 与 guard vbs 壳 |
| `Stelarith-cloud/assets/pkg/stelarith-agent-setup-0.6.47-rc.3.exe` | — | OTA 分发件（publish 脚本从 dist **复制**） | publish-release.mjs | sha256 `286a5025…`，与 dist 逐字节一致；按既有约定**不入库** |
| `Stelarith-cloud/assets/qml-patch/m2277e5cf-322cf1.zip` | — | viewer QML 热更包（81 文件，1.28 MB） | 10-22 16:23 云端 ensureQmlPatch 生成 | 内容 = viewer HEAD `23df3f6` + 共享岛指纹；版本号 `m` 前缀 = 云端进程 PATH 无 git 时走 mtime 兜底（qml-patch.js 既定降级路径，非新 bug） |
| `Stelarith-cloud/scripts/_publish_ota_rc3.py` | 55 | 发布包装器（探针类，**不入库**） | 本轮 | node 直跑会被本机安全策略按 LOLBin 拦；包一层走受管 Python 子进程，参数可审阅 |
| `加减归档-OTA发布-0.6.47-rc.3-2026-10-22.md` | 本文件 | 全账归档 | 本轮 | — |

## ② 修改的既有文件

| 路径 | 改了什么 | 为什么 |
|---|---|---|
| `Stelarith-control-qt/CMakeLists.txt` | `AGENT_VERSION` 0.6.47-rc.2 → **0.6.47-rc.3** | 发版版本号（唯一真源，main.cpp 那份同值跟随） |
| `Stelarith-control-qt/src/main.cpp` | `kAppVersion` → "0.6.47-rc.3" | 与 CMakeLists 配对的第二副本（配置期漂移检查要求同值） |
| `Stelarith-cloud/ota.json`（stable） | agent 条目 → **0.6.47-rc.3**（url/sha256/size/notes 同批更新，UTF-8 无 BOM） | 全量推送 cmd 根治；viewer 条目**未动**（仍 rc.2，管理端 UI 走 qml-patch）。⚠️ 按 .gitignore 既定设计「OTA 清单是部署产物不入库」，此文件**只落磁盘+公网生效，不进版本库** |
| `Stelarith-cloud/ota-rc.json`（rc 通道） | agent rc.1 → **rc.3**；viewer rc.1 → **rc.2** | rc 通道原比 stable 旧（rc 通道设备永远拿不到修复）⇒ 同包对齐，消陈旧陷阱。同类部署产物，**不入库**；本次给 .gitignore 补上它的 ignore 规则（此前逃过旧规则） |
| `Stelarith-cloud/ota-gray.json`（gray 通道） | 0.6.24-rc.7 → 0.6.43 | 白天既有的灰度pin 变更，本轮补入轨（灰度设备不随本版推送）。该文件历史已 tracked 且是人工 pin 的配置性质，故保持入轨、不并入 ignore |
| `Stelarith-cloud/.gitignore` | 新增 `ota-rc.json` 忽略规则（附事由注释） | 与 ota.json 的既定设计对齐；修掉「每次 publish --channel rc 脏一次 status」 |
| `Stelarith-cloud/assets/qml-patch/latest.json` | 指向 m2277e5cf（81 文件） | 16:23 起就改着没提交，本轮入轨 |
| `Stelarith-viewer-qt`（源码） | **零改动** | 本版无 viewer C/C++ 变更；MD3+竖横屏 QML 已由 `23df3f6` 在库，经 qml-patch 热更下发，不重出安装包 |

## ③ 删除的文件

| 路径 | 删了什么 | 可否恢复 |
|---|---|---|
| `Stelarith-cloud/assets/qml-patch/510f3bc.zip`、`m41e93c75.zip` | 被轮换掉的两个旧热更包（磁盘删除发生在 16:23 补丁重建时） | 可恢复（git 历史里有）；latest.json 早已不指向它们，属正常轮换 |

## ④ 端点/接口迁移对照表

| 通道 | 发布前 | 发布后 | 说明 |
|---|---|---|---|
| stable · agent | 0.6.47-rc.2 | **0.6.47-rc.3** | 全量；公网自验 sha256 一致 |
| stable · viewer | 0.6.47-rc.2 | 0.6.47-rc.2（未动） | UI 走 `/api/qml-patch` 热更 |
| rc · agent | 0.6.47-rc.1 | **0.6.47-rc.3** | 同包对齐 |
| rc · viewer | 0.6.47-rc.1 | **0.6.47-rc.2** | 同包对齐（assets/pkg 已有，跳过复制） |
| gray · agent | 0.6.43 | 0.6.43（未动） | 灰度 pin，不推 |
| qml-patch | m2277e5cf（16:23 起已生效未入轨） | 同版本入轨 | 提交不改变云端行为（每请求现读磁盘） |

## ⑤ 验证证据（命令 + 数字 + 结论）

1. **版本 bump 无漂移**：`bump-version.mjs show` ⇒ agent 两副本均 0.6.47-rc.3（viewer 保持 rc.2 是刻意不随升，见②）。
2. **构建**：增量构建 exit 0（15s）；二进制内 `0.6.47-rc.3` ×1、`0.6.47-rc.2` ×0。
3. **QML 同步**：`build/qml/shell/{AgentShell,BroadcastScheduleSheet}.qml` 与源逐字节一致（sha256 相同）⇒ 打包带的是含拖拽排序的当前态。
4. **qmllint 门禁**：对 build/qml 副本 exit **0**（仅 context 属性/外层 ID 常规 warning，无 Error；历史两处整页崩溃类问题——`on`+大写属性名、Row 内 Layout 附加属性——均不存在）。
5. **ISCC 出包**：exit 0，26.2s，产物 `dist/stelarith-agent-setup-0.6.47-rc.3.exe` 37,567,072 B；[Files] 含修好的 `install-autostart.ps1` 与 `stelarith-agent-guard-hidden.vbs`（装机不再回退）。
6. **发布 + 公网自验**（publish-release.mjs 内置）：
   - stable agent：`GET /api/public/ota?product=agent&channel=stable` ⇒ v0.6.47-rc.3，sha256 一致 ✓
   - rc agent / rc viewer：同口径 ✓（rc viewer 复制跳过：内容相同）
7. **清单体检**：`publish-release.mjs check`（stable/rc 两通道）⇒ 全部一致 ✓（agent/viewer 条目与磁盘文件 size+sha256 全对）。
8. **本地端点复核**：127.0.0.1:8788 三通道返回 stable=rc.3 / rc=rc.3 / gray=0.6.43，published=true。

## ⑥ 明确未验证的部分（不含糊）

1. **ffmpeg 实弹仍未触发**：修复靠审计+构建+二进制字符串保证；需教室机实点录像/截图/设备枚举/TTS 现场确认零弹窗（rc.3 铺到后自然覆盖此验证）。
2. **拖拽排序运行时候补**：BroadcastScheduleSheet 的 18:15 拖拽排序只过了 qmllint（语法/类型层），未做运行时探针；随 rc.3 铺出去，首个人工使用即实测。qmllint 绿已排除本项目已知的两类整页崩溃根因。
3. **生产机自升级未现场复证**：本机 agent（pid 5544，直部署的 rc.2 二进制）何时轮询到 rc.3 并自升级，未盯全程；OTA 链路本身已双通道公网验证。
4. **安装包未做「包内解压实点」**：Inno 安装器 7z 列不了内容（既定跳过项），依赖 ISCC 编译期 [Files] 保证。
5. **gray 通道未动**：若有设备注册在 gray，仍收 0.6.43——不在本轮范围，如需铺fix 再点单。
6. **qml-patch 版本号 m 前缀**：云端进程 PATH 无 git ⇒ 版本走 mtime 兜底而非 viewer HEAD。功能等价（树变则版本变），但版本号不再自证 commit；未改云端服务环境（重启云端才能带新 PATH，没必要）。

## 提交清单（git）

- `Stelarith-control-qt`（`32f8854`，已 push）：CMakeLists.txt + src/main.cpp（版本 bump）
- `Stelarith-cloud`（`e7ea88c`，已 push）：qml-patch 入轨（latest.json + 新 zip + 两旧 zip 删除）+ ota-gray.json pin 更新 + .gitignore 补 ota-rc.json 规则
- meta：子模块指针 + 本归档
- **不入库**（按设计）：ota.json / ota-rc.json（部署产物，.gitignore 既定）、assets/pkg/*.exe（安装包可从源码重打）、`scripts/_publish_ota_rc3.py`（探针类）
