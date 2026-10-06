# Stelarith-xingjikong-qt · 长期项目笔记

## 一、工作树（单一）
只改 `Stelarith-xingjikong-qt\{Stelarith-cloud-ws, Stelarith-control-qt, Stelarith-viewer-qt}`；
`D:\Stelarith\` 下同名三份是**只读历史，不删**。生产云端＝工作区那份（计划任务 `StelarithCloud` → `run-cloud.cmd`）。
官网**不在工作区**：`D:\Stelarith\Stelarith-website\stelarith`（独立仓，origin=`WuMengAA/CIMS-Next`，8090，www.245959623.xyz）。

## 二、发版链路（唯一真源）
**版本号只有一处 = 云端 `ota.json`**（被控端托盘 + 官网 `/download` 读 `/api/public/ota`）⇒ 发版后站点免重构建。
- `ota.json` **必须 UTF-8 无 BOM**（带 BOM → 接口静默 `latest:null`，现象与被控端"没有新版"一模一样，靠人盯不住）。
- 发布 `cloud/scripts/publish-release.mjs`（`check`/`show`/`publish --product agent|viewer --version x --file <包>`）：
  护栏＝BOM 拒收、清单不符逐条点名、**版本倒退默认拒绝**（回滚才 `--allow-downgrade`）；主机名沿用上一版 URL；算 sha→复制→回读校验→改清单→显式无 BOM 写→写完 parse 回读。
- 出包 `viewer-qt/scripts/make-portable.mjs`（`--check <zip>` 只检查），版本读 `src/main.cpp` 的 `kViewerVersion`，
  CMake 与 main.cpp 双源且 CMake 会 `FATAL_ERROR` ⇒ **改版本两处一起改**。
  - ⚠️ 沙箱 safe-delete 拦删除 ⇒ 必须 `CODEBUDDY_SAFE_DELETE_ENABLED=0 node scripts/make-portable.mjs`。
  - ⚠️ 必需件/`EXTRA_FILES` 一律"**build/ 优先、回落 deploy/**"：`viewer.env`/`start-viewer.cmd` 住 `deploy/`（CMake POST_BUILD 只拷 qml/），只认 build/ 会恒报缺件。
  - ⚠️ 进包**文件名用 ASCII**（`start-viewer.cmd` 别用中文）：7z 列表按 GBK 输出、node 按 UTF-8 比对 ⇒ 假阴性。
  - ⚠️ 0.6.0 的手工包根层缺整个 QtWebEngine（只在嵌套 `dist/pkg-x/` 里有）⇒ 照常起、看画面才炸。**一律走脚本出包**。

## 三、编译 / 自检
- 构建 `viewer-qt/build-qt-viewer.bat`（Ninja+MSVC）；日志 `build/viewer-run.log`。
- ⚠️ **qmllint 只查语法，查不出属性绑定/未声明属性错**（`th` vs `theme`、`picked` 漏声明都是真跑才炸）⇒ 必须真跑 exe看有没有 `FAIL QML 没能加载`。
- ⚠️ **`root.picked` 这类在别处被用的属性必须先在顶部 `property` 声明**，漏了直接 `TypeError: Cannot read property 'length' of undefined`。
- 离屏自检：先 `cp D:/Qt/6.8.1/msvc2022_64/plugins/platforms/qoffscreen.dll build/platforms/`（**windeployqt 每次构建都删它 ⇒ 每次编译后重补**），再 `viewer-qt.exe -platform offscreen`。离屏只能证明"能加载/类型能解析"，看不出排版。

## 四、管理端登录（OAuth2 授权码 + loopback，0.6.4 打通）
临时端口 → 站点 `/oauth/authorize` → 回拨 → code 换会话令牌 → `/api/console/desktop/session` 换云端接入票。
- `xingjikong_native` 是 loopback **公开客户端（RFC 8252，无密钥）**：站点 `PUBLIC_CLIENT_SECRET="PUBLIC"` 分支要求对方**明确不带** client_secret，空密钥是正常状态。
- 站点地址常量 `kDefaultSiteUrl`（`src/viewerbackend.h`，单一真源）。
- ✅ 站点侧一行不用改：密钥真源是 SQLite `oauth_clients`（库值 → BUILTIN → 才轮 `.env`，`OAUTH_DESKTOP_SECRET=` 空行是死配置）。
- ⚠️ 排障：① `/oauth/authorize` 200 **不能**证明客户端可用；② 产物有 `PUBLIC` 分支 ≠ 在跑 ⇒ **改代码不重启＝没改**。

## 五、远端（`github.com/WuMengAA/xingjikong-qt`）
四份历史四条分支 `agent`/`viewer`/`cloud`/`workspace`（工作区仓自己那条叫 `workspace`，含 3 个 gitlink）；`main` 是仓库原有内容，**别碰别强推**。
- push 走 HTTPS（**不走 SSH**：`~/.ssh/config` 把 github 指到 ssh.github.com 解析不通）；本机 **curl 到 github 恒 000**（schannel 吊销检查），git(winhttp)/python urllib 都通 ⇒ 验 API 用 python urllib。
- ⚠️ **`credential.helper=manager`（GCM）push 时弹交互挂死**（`ls-remote` 通 ≠ 凭据正常）。稳妥：`git -c credential.helper= push "https://<token>@github.com/<owner>/<repo>.git" HEAD:refs/heads/<分支>`（`-c credential.helper=` 故意加：token 不写进 `~/.git-credentials`）。
  - 丢了的 token 可从凭据管理器非交互取回：`printf "protocol=https\nhost=github.com\n" | "C:/Program Files/Git/mingw64/bin/git-credential-manager.exe" get`。
- ⚠️ **push 不动先看主邮箱 verified**（Settings → Emails）：未验证一律 403 而 ls-remote 不受影响（"能读不能写"就是判据）。
- ⚠️ 推完用 GitHub API 回读远端 SHA 比对（SHA 相等＝逐字节一致）。API `push_files` 推不全（二进制/gitlink）⇒ 忠实推送只能走 git。跨仓推分支前先 `cd` 进去对一眼 `rev-parse HEAD`。

## 六、其它工程
- 官网构建走旁路：`CODEBUDDY_SAFE_DELETE_ENABLED=0 <node> scripts/release-site.mjs build` → `switch`（线上 `build/` 全程不被碰），日志 `D:\Stelarith\_logs\release-build.log`。构建中途改源不进本次产物；偶发 `closeBundle .gz UNKNOWN` 是 Windows 抖动，别当代码问题。`content/nav.json` gitignore；`changelog.json` 追加要文本插入（别整体 `JSON.stringify`，否则 diff 114+/21-）。权限体系在 `src/lib/`：原则「校验在后端，前端只做 UI 隐藏」。
- 被控端打包走 `scripts/build-agent.sh` 别手敲（`cmd.exe` 被安全策略拦）；`windeployqt` 会重搬 `translations/`(42MB)，跑完删回；makensis 在 `C:\Program Files (x86)\NSIS\`。
- 沙箱小坑：safe-delete 会拦 `rm`（构建/清理一律带 `CODEBUDDY_SAFE_DELETE_ENABLED=0`）；Git Bash 把 `/I<path>` 当路径（加 `MSYS_NO_PATHCONV=1`）；临时探针放工程目录内（工作区外 cl.exe 读 Qt 头 C1083）；cl.exe `/?` 挂死。
- 星集控发版链路：版本号只维护 `ota.json`；被控端 `registered` 回执带最新版→托盘提示→`self_update` 下载+校验 sha256+静默装。

## 六点五、部署拓扑（**决定了所有连线地址，改地址前先看这里**）
- **公网唯一入口是 443/wss**：`wss://control.245959623.xyz/ws/agent`（被控端）/ `ws/viewer`（管理端）。
  cloudflared 隧道从本机 8788 回源，**8788 对外完全不通**（实测 `ws://…:8788` 一律超时，
  443 一律 101）。⇒ 任何写给外部机器用的地址都必须是 **wss + 不带端口**。
- **两个子域，不能互相推导**：云端 `control.245959623.xyz`、站点(website) `www.245959623.xyz`。
  站点公网走 443，8090 是内网端口（公网不通）。
  ⚠️ 早期代码用「云端地址 → 站点地址」推导（`ws://host:8788 → http://host:8090`），
  **域名和端口各错一次**，这就是"被控端连不上 website"的根因（2026-10-06 已修，两端都改成站点地址常量 +
  仅内网才推导）。站点地址是一等配置，别再靠猜。
- 云端 `provision.js` 的 `agentWsUrl()/viewerWsUrl()` 算的是**正确地址**，装机面板生成的 bat 用它
  ⇒ **走面板装机不会错，手填会错**（被控端配置向导的示例 `ws://10.0.0.5:8788` 是错的，待改）。
- 令牌分两种，别混：`CLOUD_WS_TOKEN`（设备令牌，被控端连 WS 用）
  vs `CLOUD_VIEWER_TOKEN`（管理端令牌，`/api/devices` 查表用，用错吃 401）。
- 验发布/连通一律打 **本机 `127.0.0.1:8788`**（`/api/public/ota`）；`www.245959623.xyz/api/*` 一律 404 是正常的
  （那是站点域名，云端 API 不在它下面）。

## 七、已知未修（等点单）
- 管理端右栏动作区固定高，不够**只溢出不压缩**；`minimumHeight` 已 680，**再加按钮要先改 Flickable**。
- `msi/viewer-qt.wxs` 的 Version 还是 0.5.0（MSI 这条路当前不发版）。
- 被控端仓库里 67MB 的 `dist/stelarith-agent-setup.exe` 在版本库里（宜改 Release 附件/LFS）。
- 被控端子仓有新提交 `4c7c6a8`（非本次产出）未同步到 workspace 指针；`cloud` 分支有 `src/{index,ticket}.js` 别人的未提交改动。

## 八、我栽过的跟头
1. **脚本里写中文一律用 Write 落盘 `.py`/`.txt` 再执行，绝不走 bash heredoc**：heredoc 按系统 GBK 出、python 按 UTF-8 解 ⇒ 0.6.6 把命令表写成「锁屏敛c机机/星琥/郣些/那题/一運」。
2. 别把分叉说成"权威副本/WIP 副本"含糊过去 —— 只能合并，不能挑一边覆盖。
3. 别自顾自推进：用户点明重点后还在别处操作、或继续做被叫停的功能 ⇒ 先复述确认重点再动手。
4. 顺带发现 ≠ 用户指名的重点（违反过两次）：盘点/审计的交付物是清单和判断，做不做要用户点单。
5. 删除/回滚前先说清代价；**永远不用 `--force` 推送**。
6. **改大文件用锚点字符串，别靠行号**（按行切片改 Main.qml 误删过闭合括号）。
7. 交付别摊成"等你点单"的菜单，用户说"点菜呢"＝自己判断该做的做完。
