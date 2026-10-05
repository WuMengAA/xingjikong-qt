# Stelarith-xingjikong-qt · 长期项目笔记

## 🔒 单一工作树（2026-10-06 定，不要再开第二棵树）

**日常工作树 = 工作区 `Stelarith-xingjikong-qt` 下面那三份**（用户工作区就是它，改代码只在这三份里改）：

| 项目 | ✅ 日常工作树 | 只读归档（**不删**） |
|---|---|---|
| 云端 | `Stelarith-xingjikong-qt\Stelarith-cloud-ws` | `D:\Stelarith\Stelarith-cloud-ws` |
| 被控端 | `Stelarith-xingjikong-qt\Stelarith-control-qt` | `D:\Stelarith\Stelarith-control-qt` |
| 管理端 | `Stelarith-xingjikong-qt\Stelarith-viewer-qt` | `D:\Stelarith\Stelarith-viewer-qt` |

- ⚠️ **不删任何东西**（2026-10-06 用户明确）。两边 git 历史已经同一份（外部→工作区 快进/合并完成），
  各自都配了 remote：工作区三份有 `outer` 指向外部，外部三份有 `inner` 指向工作区。
- ⚠️ **生产云端已经切到工作区那份**（2026-10-06 晚核实）：
  计划任务 `StelarithCloud` → `D:\Stelarith\Stelarith-xingjikong-qt\Stelarith-cloud-ws\run-cloud.cmd`。
  （此前那条"仍跑外部 D:\Stelarith\Stelarith-cloud-ws"的记录**已过时**。）
- 合并历史（两边现在都有）：云端 `6592610`、被控端 `474cdd5`+`8d2594f`（工作区侧合并）、管理端 `83c9daf`。

### 官网（**不在工作区内**，是独立工程 + 独立 git 仓）

- 路径 `D:\Stelarith\Stelarith-website\stelarith`；SvelteKit 2 + Svelte 5 runes + Tailwind v4，adapter-node。
- 生产：计划任务 `StelarithServer` → 同目录 `run-prod.bat`，监听 **8090**；公网 `https://www.245959623.xyz`。
- **构建必须走旁路**（线上 `build/` 全程不被碰，构建中重启不会连崩）：
  ```
  cd /d/Stelarith/Stelarith-website/stelarith
  CODEBUDDY_SAFE_DELETE_ENABLED=0 <node> scripts/release-site.mjs build   # → build.next
  CODEBUDDY_SAFE_DELETE_ENABLED=0 <node> scripts/release-site.mjs switch  # 切换+重启+验收(/ 与 /console/ 200)，失败自动回滚
  ```
  日志在 `D:\Stelarith\_logs\release-build.log`（**注意不是** `Stelarith-website\_logs`）。
- ⚠️ 构建**中途**改源文件不会进这次产物（vite 构建开始时就取过源）→ 改完必须重构建再 switch。
- ⚠️ 构建偶发失败先看是不是 Windows 文件系统抖动（closeBundle 里 `.gz` `UNKNOWN: unknown error`）——
  脚本会原样重试，**别当成代码问题去改代码**。
- ⚠️ `content/nav.json` 被 `.gitignore`（运行时内容）→ 导航改动**只落盘不入库**，换机器要重建。
- ⚠️ `content/changelog.json` 追加条目要**按原文件格式文本插入**（条目对象单行内联）。
  用 `JSON.stringify` 整体重写会把全文件 reformat，diff 从 +15 行变成 114+/21-。

### 星集控发版链路（2026-10-06 立，唯一真源原则）

**版本号只维护一处 = 云端 `ota.json`**，有两个消费方，都别再抄一份：
1. 被控端：`registered` 回执带最新版本 → 托盘提示「发现新版本…」→ `self_update` 下载+校验 sha256+静默装。
2. 官网下载中心 `/download`：读**公开只读**端点 `GET /api/public/ota?product=agent|viewer` → 站点不自存版本号。

- 发版用 `Stelarith-cloud-ws/scripts/publish-release.mjs`：`publish`（算 sha256→**复制**到 assets/pkg→
  只改该产品→显式 Buffer 写 UTF-8 **无 BOM**→回读解析）/ `check`（清单 vs 磁盘逐条核对）/ `show`。
  护栏：带 BOM 拒收；清单与文件不一致逐条点出并给修法；**版本倒退默认拒绝**（`--allow-downgrade` 才放行）。
- ⚠️ `ota.json` **必须 UTF-8 无 BOM**（PS 5.1 `Set-Content -Encoding UTF8` 会加 BOM）：
  带 BOM → `JSON.parse` 抛错 → 接口静默变 `latest:null`（"被控端说没有新版"和"清单坏了"现象一模一样）。
- `ota.json` / `assets/pkg/` 都被 gitignore（部署产物，不入库），只留 `ota.json.example`。

### 远端备份（2026-10-06）

- 远端：`https://github.com/WuMengAA/xingjikong-qt.git`（公开仓；默认分支 `main` 是仓库原有内容，**不要强推**）。
- 一个远端装四份历史，各占一条分支：`agent` / `viewer` / `cloud` / `workspace`。
- 推送命令（token 当用户名，本机验证可用）：
  `git push https://<PAT>@github.com/WuMengAA/xingjikong-qt.git HEAD:refs/heads/<分支>`
- ⚠️ 本机 `credential.helper=store`（`~/.git-credentials` 明文）：**别把 PAT 塞进凭据存档**；
  PAT 用过就让用户去 GitHub revoke（聊天里出现过 = 已泄露）。

### ⚠️ 我在这上面栽过的跟头（必须记住）

1. **别把分叉说成"权威副本 / WIP 副本"含糊过去** —— 真相是两边各有对方没有的真实提交，只能合并，不能挑一边覆盖。
2. **别自顾自推进**：用户明确说了重点（"别用外部那三个""WebRTC 暂时不做"）之后，我还在外部路径上操作、
   还继续做被叫停的功能 → 用户会很受挫。**先复述确认重点，再动手；被叫停就真停。**
3. 删除/回滚前先说清代价；永远不用 `--force` 推送。

---

## （历史记录，2026-10-06 前半段的旧结论，已被上面取代）

三对副本曾经按"外部为真源"合并过一遍（内部三份打过 `DEPRECATED-勿再修改-2026-10-06.md` 标记，
文件内容已于同日改为"这里就是日常工作树"）：

| 项目 | ✅ 唯一真源（改这里） | ⛔ 已停用副本（只读历史） | 合并提交 |
|---|---|---|---|
| 云端 | `D:\Stelarith\Stelarith-cloud-ws` | `xingjikong-qt\Stelarith-cloud-ws` | `6592610` |
| 被控端 | `D:\Stelarith\Stelarith-control-qt` | `xingjikong-qt\Stelarith-control-qt` | `474cdd5` |
| 管理端 | `D:\Stelarith\Stelarith-viewer-qt` | `xingjikong-qt\Stelarith-viewer-qt` | `83c9daf` |

- 三个真源里都配好了名为 **`inner` 的 remote**（指向停用副本），需要老提交就
  `git fetch inner && git log inner/master`，**不要**再复制目录。
- 分叉来历：2026-09-12 整盘 `D:\Stellara`→`D:\Stelarith` 改名 + 后来"并入旧位置成果"的合并操作，
  两棵树都被留下，之后各自又都提交了 → 分叉。**是事故，不是设计**。
- ⚠️ 教训（我自己犯的）：不要把分叉说成"权威副本 / WIP 副本"来含糊过去 ——
  真相是两边各有对方没有的真实提交，只能**合并**，不能挑一边覆盖。

---

## ⚠️ 被控端 control-qt 有两份副本，已经分叉（2026-10-05 发现；2026-10-06 已合并，见上表）

| 路径 | git | 内容 | 现状 |
|---|---|---|---|
| `D:\Stelarith\Stelarith-control-qt` | 自己的仓库（commit `0733bf2`） | 有 `src/singleinstance.{h,cpp}`（单实例守卫，2026-10-05 加），exe 315,392 B | 已打 NSIS、已同步云端（`stelarith-agent-setup-0.5.1.exe` 70,664,927 B） |
| `D:\Stelarith\Stelarith-xingjikong-qt\Stelarith-control-qt` | 本仓库（xingjikong-qt）把该目录当 **gitlink/子模块条目** 记着，实际是普通目录副本 | `src/` 里**只有 main.cpp**，没有 singleinstance；2026-10-05 晚改了全屏通知（palette 背景 / ✓确认按钮 / paintEvent 圆角），exe 337,408 B | **缺单实例守卫** |

- 两份 `src/main.cpp` 的 md5 不同（`dc50f731…` vs `a6316fb1…`）。
- 权威发布链路以 **`D:\Stelarith\Stelarith-control-qt`** 为准（打包/NSIS/云端都走它）。
- 在 xingjikong-qt 副本里改代码前**先问用户**：是把它升级成权威副本，还是把改动移植回
  `D:\Stelarith\Stelarith-control-qt`。两边不同步会直接导致"打出来的包没有某个修复"。
- 同理 `Stelarith-viewer-qt`、`Stelarith-cloud-ws` 在 xingjikong-qt 下也各有一份副本。

## ⚠️ control-qt 两份副本的 diff 必须先归一化行尾（2026-10-05 踩到）

- `D:\Stelarith\Stelarith-control-qt\src\main.cpp` 是 **LF**；
  `D:\Stelarith\Stelarith-xingjikong-qt\Stelarith-control-qt\src\main.cpp` 是 **CRLF**。
- 直接 `diff` 两份会得到 `@@ -1,3064 +1,3376 @@` 一条巨型 hunk（"整文件都改了"的假象）。
  必须先两边 `.replace(b'\r\n', b'\n')` 归一化再 diff，真实差异是 15 块 / 529 行。
- 内容分叉现状：通知三改（全屏 palette / ✓确认按钮 / paintEvent 圆角）**两边都有**了
  （`D:\Stelarith` 那份 commit `6f5d99c` 移植的）；**OTA 自更新整块 + `kAppVersion="0.5.0"`
  仍只在 xingjikong 那份里**，没移植过来（等用户点单）。

## 被控端打包链路（2026-10-05 实测）

1. `build-qt-agent.bat`（cmake + ninja + MSVC 18 BuildTools，cl.exe 在 `bin\Hostx64\x64`）
2. cmake/ninja 在 python 虚拟环境里 → 自写 `.bat` 必须把
   `C:\Users\Administrator\.workbuddy\binaries\python\envs\default\Scripts` 加进 PATH，否则 `cmake: 无法运行`。
3. `windeployqt.exe` 在 `D:\Qt\6.8.1\msvc2022_64\bin\` —— ⚠️ **它会把瘦身时删掉的
   `deploy/translations`（30 个 .qm，42MB）、`position/`、`qmltooling/`、`resources/`
   重新搬回来，跑完必须删回去**，否则安装包凭空涨 20MB（89MB→70MB 就是这么来的）。
4. `deploy/tls/q{schannel,certonly,openssl}backend.dll` **要留**（连云端 WSS/TLS 需要）。
5. `makensis` 在 `C:\Program Files (x86)\NSIS\makensis.exe`（`where` 找不到，别白查）。
6. 绿色包用 `C:\Program Files\7-Zip\7z.exe a -tzip -mx=1`；**别把 zip 打进包目录里**。

## 沙箱里的构建小坑

- Git Bash 会把 `/I<path>` 这类参数当路径转换 → 跑 cl.exe 加 `/I` 时加 `MSYS_NO_PATHCONV=1` 或干脆用 cmake 自己算 include。
- 工作区外（如 `%TEMP%`）cl.exe 读 `D:\Qt\...` 头会 C1083；临时探针工程要**放在工程目录内**。
- 临时验证用的探针/`.bat`/`build/` 验完就删，别留在仓库里。
