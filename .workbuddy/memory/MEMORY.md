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
   ⇒ 发版后**站点无需重新构建**，刷新即可看到新版本（2026-10-06 实测 0.6.1 即如此）。

#### 发布：`Stelarith-cloud-ws/scripts/publish-release.mjs`（2026-10-06 加）

```
node scripts/publish-release.mjs check        # 体检：清单 vs 磁盘文件（存在性/大小/sha256）
node scripts/publish-release.mjs show
node scripts/publish-release.mjs publish --product agent|viewer --version x.y.z --file <包> [--notes "..."]
```
- 算 sha256 → **复制**（不是移动）到 `assets/pkg/` → 复制后回读再校验 → **只改该产品条目**
  → 显式 Buffer 写 UTF-8 **无 BOM** → 写完立刻 parse 回读。
- 主机名与目录前缀默认**沿用上一版 URL**（连发多版不必手写域名）；首次用 `--base-url`。
- 三道护栏：清单带 BOM 拒收 / 清单与文件不一致逐条点出并给修法 / **版本倒退默认拒绝**
  （发布对象是全校教室机，误操作＝集体降级；回滚才加 `--allow-downgrade`）。
- ⚠️ `ota.json` **必须 UTF-8 无 BOM**（PS 5.1 `Set-Content -Encoding UTF8` 会加 BOM）：
  带 BOM → `JSON.parse` 抛错 → 接口静默变 `latest:null`
  （"被控端说没有新版"和"清单坏了"现象一模一样，靠人盯不住）。
- `ota.json` / `assets/pkg/` 都被 gitignore（部署产物，不入库），只留 `ota.json.example`。

#### 出包：`Stelarith-viewer-qt/scripts/make-portable.mjs`（2026-10-06 加）

```
node scripts/make-portable.mjs              # 从 build/ 组装 dist/pkg-<版本>/ 并压成 zip
node scripts/make-portable.mjs --check <zip>  # 只检查一个已存在的包，不改任何东西
```
- 版本号读自 `src/main.cpp` 的 `kViewerVersion`（**不另设一份**）。
- **"包里必须有什么"是脚本里的 15 项必需件清单**，缺了当场失败。其中
  `resources/icudtl.dat` / `Qt6WebEngine*.dll` / `QtWebEngineProcess.exe` 是 WebEngine（远程画面）必需品。
- 还断言：不许有 `dist/` 嵌套旧副本；不许有自检用的 `platforms/qoffscreen.dll`；
  `Stelarith/qml/*` 磁盘副本必须与 `qml/*` 源码一致。
- ⚠️ **0.6.0 的管理端绿色包根层缺整个 QtWebEngine**（只有嵌套的 `dist\pkg-0.6.0\` 里才有）
  ⇒ 那份绿色版**做不了远程画面**，而它照常启动、只在要看画面时才炸（WebEngine 视图延迟创建）。
  0.6.1 起根层就是完整应用。**手工拼包就会这样** —— 所以出包一律走这个脚本。
- ⚠️ `dist/` 已 gitignore（一跑 400M）。**被控端仓库已把 `dist/stelarith-agent-setup.exe`
  （67MB）提交进库**，别在那边重演。

#### 看 GUI 有没有真加载成功（沙箱里也能做）

`build/platforms/` 里补上 `qoffscreen.dll`（windeployqt 默认只给 `qwindows.dll`，
**这就是以前"离屏自检起不来"的原因**），然后
`viewer-qt.exe -platform offscreen`：日志里**没有** `FAIL QML 没能加载` 即通过。
⚠️ 离屏只能证明"能加载/类型能解析"，**看不出排版好看与否**。

#### 管理端通知契约（2026-10-06，照被控端 `notifyFromParams()` 对齐）

`qml/NotifyParams.js`（纯函数，唯一一份拼装逻辑）+ `scripts/test-notify-params.mjs`（16 条断言）：
- `{kind:"popup"|"island"|"fullscreen", title(必填), content, seconds?（>0 才带，夹 3600）, flags:{speech 永远显式, severity 仅 fullscreen, emergency_confirm?}}`
- 标题超 **24** 字、正文超 **64** 字由**被控端**截断（管理端只做计数，不截断）。
- **不发 `notice_id`**（站点侧对账专用，管理端没有 `notice_kinds` 表）。
- 真机实测：被控端日志 `📥 收到指令 action=notify（D5 真执行）` / `📤 回执 result=done`。

#### 已知未修（等点单）

- `qml/Main.qml:264` 的 `Row` 里有 `anchors.right` → Qt 每次启动报两条
  `Row will not function`（截图上看是对的，靠锚点歪打正着）。修法：换成 `Item{width:parent.width}` + 两个内联 Text。
- 右栏动作区是固定高的一列，空间不够**不压缩只溢出**；`minimumHeight` 已抬到 680，
  **下次再加按钮要先改成 Flickable**。
- `msi/viewer-qt.wxs` 的 `Version` 还是 0.5.0（MSI 那条路当前不用于发布）。
- 被控端仓库里 67MB 的 `dist/stelarith-agent-setup.exe` 仍在版本库里。


### 远端备份（2026-10-06）

- 远端：`https://github.com/WuMengAA/xingjikong-qt.git`（公开仓；默认分支 `main` 是仓库原有内容，**不要强推**）。
- 一个远端装四份历史，各占一条分支：`agent` / `viewer` / `cloud` / `workspace`。
  **工作区仓自己那条叫 `workspace`**（含三份子工程的 gitlink），所以它有 5 条分支要维护。
- ⚠️ **push 走 HTTPS，不要走 SSH**（2026-10-06 实测）：`~/.ssh/config` 把 `github.com` 指到
  `ssh.github.com:443`，本次 push 报 `Could not resolve hostname ssh.github.com`；
  而同一会话 `git fetch origin` 却能成功 —— SSH 通道时通时不通。
  稳妥命令：`git -c credential.helper= push "https://<token>@github.com/<owner>/<repo>.git" HEAD:refs/heads/<分支>`
  （`-c credential.helper=` 是**故意**的：确保 token 不被写进 `~/.git-credentials`）。
- **推完必须用 GitHub API 独立校验**，不要拿本地 git 自证：
  `list_branches` 读回远端分支 SHA，与本地 `git rev-parse HEAD` 比对 ——
  **提交哈希覆盖整棵树，"分支 SHA 相等"就等于逐字节证明内容一致**，不必逐文件比对。
- ⚠️ **GitHub API（`push_files`）推不全，别指望它**：它只接受字符串内容，
  ①推不了二进制（`agent` 分支有 67.37 MB 的 `dist/stelarith-agent-setup.exe`）；
  ②造不出 **gitlink**（mode 160000，`workspace` 分支有 3 个）。
  ⇒ 忠实推送只能走 git；**API 的强项是推送后的校验与推送前的状态侦察**。
- ⚠️ `agent` 分支跟踪了 67 MB 安装包（GitHub 建议上限 50MB、硬上限 100MB）——
  宜改 Release 附件或 LFS。**未处理，等用户点单。**
- ⚠️ 本机 `credential.helper=store`（`~/.git-credentials` 明文）：**别把 PAT 塞进凭据存档**；
  PAT 用过就让用户去 GitHub revoke（聊天里出现过 = 已泄露）。

### 官网远端 = `WuMengAA/CIMS-Next`（**独立仓库**，与 xingjikong-qt 不同）

- 官网 `D:\Stelarith\Stelarith-website\stelarith` 的 `origin` 是
  `git@github.com:WuMengAA/CIMS-Next.git`（**公开仓**；`xingjikong-qt` 里没有官网的历史）。
- ⚠️ **申报远端差距前必须先 `git fetch`**：本地 `origin/main` 引用可能停得很早
  （2026-10-06 那次停在 09-26，快十天没 fetch），只看本地会误判成"领先 23 条"，
  真相是**分叉**（远端独有 9 / 本地独有 23）。
- ⚠️ **官网 `main` 与远端已分叉且尚未合并**（2026-10-06 发现）：
  两边改动**零文件重叠**（合并理论无冲突），远端那 9 条是 09-24～09-26 的另一批工作。
  **没有擅自合并、没有推 `main`、没有强推**；只新建 `wip/site-download-center-20261006` 做备份。
  按用户原则这类分叉应当**合并**（不是挑一边），但**要用户点单**才动。
- 公开仓 ⇒ 推之前先扫一遍新增行有没有凭据（`.env`/`stelarith.db`/`users.json` 都已 gitignore）。

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

## ⚠️ 编被控端（control-qt）走 `scripts/build-agent.sh`，别手敲（2026-10-06）

本机跑不了 `cmd.exe` → `build-qt-agent.bat`（vcvarsall 那路）在 WorkBuddy 内根本执行不了。
不跑 vcvarsall 就得自己喂 INCLUDE/LIB，而且**本机 MSVC 的 include 里 15 个 C 运行头是缺的**
（`stdio.h`/`stdlib.h`/`string.h`/`math.h`/`time.h`/`stddef.h`/`malloc.h`/`ctype.h`/`float.h`/
`errno.h`/`wchar.h`/`io.h`/`process.h`/`signal.h`/`sys\stat.h`）—— 它们由
**Windows SDK 的 UCRT** 提供（`Windows Kits\10\Include\<ver>\ucrt` 头 +
`Lib\<ver>\ucrt\x64\ucrt.lib` 导入库；缺后者链接报 LNK1104）。INCLUDE/LIB 里必须写
`C:/...` 不能写 `/c/...`（Git Bash 会传给 Win32 程序当相对路径）。全部已固化在
`Stelarith-control-qt/scripts/build-agent.sh`。

## 被控端 0.6.3 强制自动升级（2026-10-06，唯一真源 = control-qt `src/main.cpp`）

- 触发：云端 registered 回执的 `updateMandatory` == true → 8 秒后 `maybeAutoUpdate()`
  → 复用 `startSelfUpdate()`（下载→sha256→NSIS /S 静默装），**不再需要老师在教室机点**。
- 三道保命：① 退避（3分/15分/1时/4时/**12时封顶**，记在 `ota-state.json`）
  ② 装前备份当前 exe 到 `%LOCALAPPDATA%\xingjikong\ota\prev\` + 写 `ota-pending.json`
  ③ 看门狗 = **升级前写下的** `ota-relaunch.bat`（旧字节，新 exe 崩了也救得回来）。
- 回滚只认"新版本连没连上云端"；从没连通过就**不回滚**（避免网络差被误判降版）。
- 紧急刹车：`%LOCALAPPDATA%\xingjikong\ota\ota-pause`（空文件）→ 本机暂停自动升级；
  面板也有「暂停/恢复」按钮。灰度就用它。
- ⚠️ 0.6.3 是第一个 mandatory=true 的版本，下发即全校教室机无人值守自装；
  **真机端到端（下载→装→重启→回滚）一次都没验证过**，只有编译+冒烟+清单下发正确。
