# 星集控 · OTA 发布与升级说明

> 版本 v0.5.0。本文说明：如何发布一个新版本、如何把升级推给教室机、如何观察结果、如何回滚。

---

## 1. OTA 是什么（架构）

星集控的 OTA **复用既有的「云端下发指令 → 被控端执行 → 回执」通道**，没有另造一套。
分工：

```
   发布者                    云端                      教室机 (agent)
   ─────                    ────                      ─────────────
   构建安装包
   算 sha256
   写 ota.json  ─────►  ① /api/ota/latest 声明最新版本
                        ② 收到指令 self_update{url,sha256,version}
                              │
                              └──── 下发指令 ─────────►  ③ 下载安装包（QNetworkAccessManager）
                                                        ④ 校验 sha256（不符即拒绝并删档）
                                                        ⑤ 派重启助手 ota-relaunch.bat
                                                        ⑥ 静默执行安装包（NSIS /S）
                              ◄──── 回执 started/installing/failed ────┘
                                                        ⑦ 安装器替换文件 → 助手把新版拉起
```

关键点：**云端只声明版本、不托管大文件**；安装包由站点 / 对象存储 / 反代 `/ota/` 托管。

---

## 2. 发布一个新版本（发布者操作）

### 步骤 1：构建安装包
```bat
cd Stelarith-control-qt
build-qt-agent.bat                       rem 编译 → build\stelarith-agent-qt.exe
rem 再走打包（NSIS）：makensis installer.nsi  →  dist\stelarith-agent-setup.exe
```

### 步骤 2：算 sha256 与大小
```bat
certutil -hashfile dist\stelarith-agent-setup.exe SHA256
for %A in (dist\stelarith-agent-setup.exe) do @echo size=%~zA
```

### 步骤 3：把安装包放上托管位
- 站点（8090）静态目录，或
- 对象存储（OSS/COS/S3），或
- 反代 `/ota/` 映射的本地目录（见部署手册 3.2）。

最终拿到一个**公网可下载**的 URL，例如：
`https://control.example.com/ota/stelarith-agent-setup-0.5.0.exe`

### 步骤 4：更新云端 `ota.json`
在 `Stelarith-cloud-ws\ota.json`（从 `ota.json.example` 复制）写入：
```json
{
  "products": {
    "agent": {
      "version": "0.5.0",
      "url": "https://control.example.com/ota/stelarith-agent-setup-0.5.0.exe",
      "sha256": "<64位小写十六进制>",
      "size": 17208133,
      "notes": "被控端 0.5.0：RTC 自愈 + 按需回收 WebEngine",
      "mandatory": false
    }
  }
}
```
> `ota.json` **每次请求现读**，改完即生效，**不必重启云端**。

### 步骤 5：确认清单已生效
```bash
curl -H "Authorization: Bearer <CLOUD_VIEWER_TOKEN>" \
     https://<域名>/api/ota/latest?product=agent
# 期望：{"ok":true,"product":"agent","latest":{"version":"0.5.0","url":"...","sha256":"...",...}}
```

---

## 3. 把升级推给教室机

### 3.1 单台（灰度先行）

```bash
curl -X POST https://<域名>/api/instructions \
  -H "Authorization: Bearer <CLOUD_VIEWER_TOKEN>" \
  -H "Content-Type: application/json" \
  -d '{
        "uid": "CLASS101-PC01",
        "action": "self_update",
        "params": {
          "url": "https://control.example.com/ota/stelarith-agent-setup-0.5.0.exe",
          "sha256": "<64位小写十六进制>",
          "version": "0.5.0"
        }
      }'
# 期望：{"ok":true,"state":"sent",...}；409 表示该机不在线
```

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
| `sha256 校验不符（期望 … 实得 …）` | 安装包被改动或版本不匹配；已自动删除下载文件 |
| `安装包启动失败` | 安装包权限/被拦截 |
| `本机已设 STE_QT_OTA_DISABLE` | 该机被运维冻结了自更新 |
| `已有一个更新任务在进行中` | 重复下发，等前一次结束 |

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
3. **管理端 UI 尚无"一键升级"按钮**：当前经云端 `/api/instructions` 下发（可脚本化）。管理端界面集成列为后续。
4. **回滚需留存旧包**：OTA 不内置回滚，回滚 = 重推一个低版本号的 `ota.json` + 下发旧包（版本号比较当前不强制，需人工确认目标）。

---

## 7. 发布检查清单

- [ ] 安装包已构建，sha256 / size 已记录并归档。
- [ ] 安装包已上传到公网可下载 URL，且**教室机能访问到**（先在某台教室机 curl 试下）。
- [ ] `ota.json` 已更新（version / url / sha256 / size / notes）。
- [ ] `/api/ota/latest` 返回正确。
- [ ] 灰度 1 台 → 五项观测全绿。
- [ ] 批量下发 → 逐台核对 `/api/devices` 版本号。
- [ ] 旧版本安装包归档留存（备回滚）。
