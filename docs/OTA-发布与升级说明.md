# 星集控 · OTA 发布与升级说明

> 版本 v0.6.0（2026-10-06 更新）。本文说明：如何发布一个新版本、如何把升级推给教室机、如何观察结果、如何回滚。
>
> **2026-10-06 起，发布流程已收成一条命令**（见第 2 节）：`scripts/publish-release.mjs`。
> 手工改 `ota.json` 的老做法不要再用了 —— 它已经在真机上踩过一次 BOM 坑（见 §2.1）。
> 同一份 `ota.json` 现在有**两个消费方**：被控端（升级提示）+ 官网下载中心（`/download`）。

---

## 1. OTA 是什么（架构）

星集控的 OTA **复用既有的「云端下发指令 → 被控端执行 → 回执」通道**，没有另造一套。
分工：

```
   发布者                    云端                      教室机 (agent)
   ─────                    ────                      ─────────────
   构建安装包            ┌── ota.json（唯一真源）──┐
   publish-release.mjs ──┘                         │
   （算 sha256 / 复制 / 写清单）                    │
                        ① /api/ota/latest（需管理端令牌）
                        ② /api/public/ota（公开只读）──► 官网下载中心 /download
                                                       （页面上的版本号/sha256 实时读它）
                        ③ registered 回执带最新版本 ──► 被控端托盘提示「发现新版本…」
                              │                          （用户点一下 → 走 self_update）
                              └──── 下发 self_update{url,sha256,version} ────►
                                                        ④ 下载安装包（QNetworkAccessManager）
                                                        ⑤ 校验 sha256（不符即拒绝并删档）
                                                        ⑥ 派重启助手 ota-relaunch.bat
                                                        ⑦ 静默执行安装包（NSIS /S）
                              ◄──── 回执 started/installing/failed ────┘
                                                        ⑧ 安装器替换文件 → 助手把新版拉起
```

关键点：**云端只声明版本、不托管大文件**；安装包由站点 / 对象存储 / 反代 `/assets/pkg/` 托管。

---

## 2. 发布一个新版本（发布者操作）

### 2.0 一条命令（推荐路径）

```bash
cd Stelarith-cloud-ws
node scripts/publish-release.mjs publish \
     --product agent --version 0.6.0 \
     --file ../Stelarith-control-qt/dist/stelarith-agent-setup-0.6.0.exe \
     --notes "被控端 0.6.0：单实例守卫 / OTA 自更新 / 托盘升级提示 + 设置与信息面板"
```

它会：算 sha256 与大小 → **复制**（不是移动）到 `assets/pkg/` → 复制后回读校验 →
**只改 `products` 里这一个产品**（其它条目原样保留）→ 写 UTF-8 **无 BOM** → 再 parse 回读一次。

```bash
# 发布前后各跑一次：把清单里的每条记录与实际文件核对（存在性 / 大小 / sha256）
node scripts/publish-release.mjs check

# 打印当前清单与对应文件状态
node scripts/publish-release.mjs show
```

**三道护栏：**

| 护栏 | 行为 |
|---|---|
| 清单带 UTF-8 BOM | 直接拒绝并说明后果（不让它变成一个"看起来没发布"的坏文件） |
| 文件重打过、清单没跟着改 | 逐条列出「清单值 / 实际值」并给出修法命令 |
| 要发的版本**低于**当前最新 | **默认拒绝**（发布对象是全校教室机，误操作＝集体降级）；确属回滚加 `--allow-downgrade` |

> 主机名与目录前缀默认沿用上一版 URL，连发多个版本不必每次手写域名；
> 首次发布或用新域名时用 `--base-url https://你的域名`（或环境变量 `CLOUD_PUBLIC_BASE`）。
> `--dry-run` 只算 sha256 与 URL，不复制、不改清单。

### 2.1 ⚠️ 为什么不能再手工改 `ota.json`

`ota.json` **必须是无 BOM 的 UTF-8**。PowerShell 5.1 的 `Set-Content -Encoding UTF8` 会写入
`EF BB BF`，`JSON.parse` 直接抛错 → `loadManifest()` 返回 null → 接口静默变成
`{"ok":true,"published":false,"latest":null}`。

**现象上，"被控端说没有新版"和"清单是坏的"长得一模一样**，靠人盯是盯不住的 —— 所以交给脚本。

### 2.2 手工路径（仅用于没有 Node 的应急场景）

<details>
<summary>展开看老流程</summary>

```bat
:: 1) 构建
cd Stelarith-control-qt
build-qt-agent.bat                       rem 编译 → build\stelarith-agent-qt.exe
makensis installer.nsi                   rem → dist\stelarith-agent-setup.exe

:: 2) 算 sha256 与大小
certutil -hashfile dist\stelarith-agent-setup.exe SHA256
for %A in (dist\stelarith-agent-setup.exe) do @echo size=%~zA

:: 3) 复制到托管位（**复制**，别把源文件移走）
copy dist\stelarith-agent-setup.exe ..\Stelarith-cloud-ws\assets\pkg\stelarith-agent-setup-0.6.0.exe

:: 4) 改 ota.json —— 用能存「UTF-8 无 BOM」的编辑器，改完必须回读验证：
node -e "console.log(JSON.parse(require('fs').readFileSync('ota.json','utf8')).products.agent.version)"
```
最后一步解析不报错、且打印出期望版本号，才算改对。
</details>

### 2.3 确认清单已生效

```bash
# 需要管理端令牌
curl -H "Authorization: Bearer <CLOUD_VIEWER_TOKEN>" \
     https://<域名>/api/ota/latest?product=agent

# 公开只读（无需鉴权，官网下载中心用的就是这个）
curl https://<域名>/api/public/ota?product=agent
# 期望：{"ok":true,"product":"agent","published":true,"latest":{"version":"0.6.0",...}}
```

> 两个接口都是**每次请求现读** `ota.json`，改完即生效，**不必重启云端**。

---

## 3. 把升级推给教室机

### 3.0 教室机自助（2026-10-06 起，通常不需要管理员介入）

发布完新版本后，教室机**自己就能发现**：云端在 `registered` 回执里带上 `agent` 的最新版本，
被控端比对后：

- 托盘右键菜单出现 **「发现新版本…」**（没有新版时该项隐藏）；
- **「设置与信息…」** 面板里能看到：当前版本 / 升级状态 / 设备代号 / 云端地址 / 连接状态 /
  数据目录 / 日志文件，并有「立即升级」按钮。

点「立即升级」复用与云端下发**完全相同**的 `self_update` 链路（不是另写一套）：
下载 → 校验 sha256 → 派助手 → 静默安装。所以下面 3.1 的护栏与回执观测对它同样适用。

> ⚠️ 这部分 UI（托盘菜单项 / 设置与信息面板）是 2026-10-06 新加的，**代码路径已验证、编译通过、
> 日志走到了"托盘已就绪"，但还没人在真实桌面上点开看过**。首次上线请在真机点一次确认。
> 另外「发现新版本」这个分支**至今没有被真实数据触发过**（一直在 0.6.0 = 0.6.0），所以对它的把握
> 来自代码审查而非实测 —— 下次真发版时请盯着看这一条。

### 3.1 单台（灰度先行）

```bash
curl -X POST https://<域名>/api/instructions \
  -H "Authorization: Bearer <CLOUD_VIEWER_TOKEN>" \
  -H "Content-Type: application/json" \
  -d '{
        "uid": "CLASS101-PC01",
        "action": "self_update",
        "params": {
          "url": "https://control.example.com/assets/pkg/stelarith-agent-setup-0.6.0.exe",
          "sha256": "<64位小写十六进制>",
          "version": "0.6.0"
        }
      }'
# 期望：{"ok":true,"state":"sent",...}；409 表示该机不在线
```

> `url` / `sha256` 直接照抄 `ota.json` 里那一行（或用 `publish-release.mjs show` 打印），**别手抄** ——
> sha256 抄错一位，教室机就会下完再删掉，表现为"点了升级没反应"。

### 3.2 批量
对设备清单逐台发同一指令（云端目前无"批量一键"接口，可脚本化遍历 `/api/devices` 的 uid）。

> ⚠️ **务必先灰度 1–2 台**，确认自更新链路在该校网络/系统环境下可用，再全网铺开。
> 理由见第 6 节「未验证部分」。

---

## 4. 回执与状态观测

被控端会为同一条指令 id 分阶段回执（管理端指令记录 / 云端 `events.log` 可见）：

| result | data.stage | 含义 |
|---|---|---|
| `started` | `downloading` | 已受理，正在下载 |
| `installing` | `installing` | 已校验通过，正在执行安装包（**这是本进程最后一条回执**） |
| `failed` | — | 失败，`error` 字段给出原因（下载失败 / sha256 不符 / 体积超限 / url 非 http(s) / sha 非法） |

### 4.1 如何确认"升级真的成功了"

由于安装器会替换掉被控端自身进程，**`done` 回执无法由旧进程发出**。判定成功的**权威信号**是：

> 教室机重连后，云端 `/api/devices` 里该 uid 的 `version` 变为新版本号（`0.5.0`）。

```bash
curl -H "Authorization: Bearer <CLOUD_VIEWER_TOKEN>" https://<域名>/api/devices
# 看目标 uid 的 version 字段
```

### 4.2 常见失败
| error 内容 | 原因 |
|---|---|
| `url 必须是 http(s) 地址` | 用了 `file://` 或裸路径 |
| `sha256 必须是 64 位小写十六进制` | 未提供校验值（云端**拒绝无校验更新**） |
| `下载失败：…` | URL 不可达 / 证书问题 / 教室机无外网 |
| `sha256 校验不符（期望 … 实得 …）` | 安装包被改动或版本不匹配；已自动删除下载文件。**多半是 `ota.json` 与磁盘文件不一致** → 跑 `publish-release.mjs check` |
| `安装包启动失败` | 安装包权限/被拦截 |
| `本机已设 STE_QT_OTA_DISABLE` | 该机被运维冻结了自更新 |
| `已有一个更新任务在进行中` | 重复下发，等前一次结束 |
| `现在没有可升级的版本：…云端还没发布（ota.json），或清单里缺 url/sha256` | 清单不存在/是坏的/该产品条目缺字段。**先看 `publish-release.mjs check`**，再看清单是不是带了 BOM |
| 托盘里**根本没有**「发现新版本…」 | 正常：该项只在与云端版本比对到"有新版"时才出现。先确认 `/api/public/ota` 能取到更高版本号 |

---

## 5. 安全设计与护栏

自更新等于"远程替换可执行文件"，风险最高，故设了四道护栏（**缺一不升**）：

1. **URL 只认 `http(s)`** —— 挡掉本地路径注入。
2. **必须提供合法 `sha256`** —— 没有校验的更新等于任意代码执行，一律拒绝。
3. **体积上限 300MB** —— 防磁盘被灌满。
4. **只落 `%LOCALAPPDATA%\xingjikong\ota\*.exe`** —— 限制落盘位置。

另有应急开关：任一教室机设 `STE_QT_OTA_DISABLE=1` 即**硬关本机自更新**（默认开启）。

---

## 6. ⚠️ 未验证部分（务必先灰度）

**下载 / 校验 / 触发安装这条链路无法在开发机端到端验证** —— 一执行就会把开发机自身的进程替换掉，属于"测一次就毁一次"的操作。因此当前实现：

- ✅ 护栏与逻辑正确、**编译通过**；
- ✅ 云端清单与检查接口**已实测**（无清单→null、无令牌→401、有清单→返回条目）；
- ❌ **真实"下载→安装→重启拉起"未在真机跑通**（需教室机灰度）。

**上线动作**：先在 **1 台非关键教室机** 走完整流程，确认：
① 下载成功；② sha256 校验通过；③ 安装器静默完成；④ `ota-relaunch.bat` 把新版拉起；⑤ `/api/devices` 版本号变化。
五项全绿后再批量。

### 已知设计限制
1. **重启依赖固定延时**：`ota-relaunch.bat` 等待 25 秒后启动新版 exe。若安装器耗时超过 25 秒（极慢磁盘/杀软全盘扫描），可能出现"新版尚未就绪即被启动"。届时**登录一次**或手动跑 `start-agent.bat` 即可恢复。
2. **旧进程发不出 `done`**：升级成功以"重连后版本号变化"为准（见 4.1）。
3. **管理端 UI 尚无"一键升级"按钮**：管理端界面集成列为后续。
   （被控端侧的自助升级入口已于 2026-10-06 落地，见 §3.0。）
4. **回滚需留存旧包**：OTA 不内置回滚，回滚 = 重推一个低版本号的 `ota.json` + 下发旧包。
   ⚠️ `publish-release.mjs` 对**版本倒退默认拒绝**，回滚时要显式加 `--allow-downgrade`。
5. **升级不自动、不强制**：默认只是"托盘提示 + 面板可见"，要点一下才升。
   批量强制升级（`mandatory`）目前只写进了清单字段，被控端侧尚未按它做强制处理。

---

## 7. 发布检查清单

- [ ] 安装包已构建；文件**名里带版本号**（如 `stelarith-agent-setup-0.6.0.exe`）。
- [ ] `node scripts/publish-release.mjs publish --product agent --version x.y.z --file <包>` 成功。
- [ ] `node scripts/publish-release.mjs check` **全部一致 ✓**。
- [ ] 安装包公网可下载，且**教室机能访问到**（先在某台教室机 curl 试下；注意 `Content-Length` 与清单一致）。
- [ ] `curl https://<域名>/api/public/ota?product=agent` 返回期望版本。
- [ ] 官网站点 `/download` 显示的是新版本号（它读的同一个接口，站点无需重新构建）。
- [ ] 灰度 1 台 → 五项观测全绿（见第 6 节）。
- [ ] 批量下发 → 逐台核对 `/api/devices` 版本号。
- [ ] 旧版本安装包归档留存（备回滚）。
