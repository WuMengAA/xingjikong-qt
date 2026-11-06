# Stelarith-cloud · 云端中转站（server）

> 星集控的**云端**：Node.js（无框架，`node:http` + `ws`）。设备注册鉴权、在线表、
> 指令下发、事件流水、OTA 清单、插件市场、互动数据（点歌/祝福/反馈）。
> 与 viewer 同机常驻，默认 `127.0.0.1:8788`。

## 一、运行

```bash
npm start                 # = node src/index.js
npm test                  # 单元测试（tests/unit-*.mjs）
node tests/regression-interact.cjs <port>       # 互动链路回归（需先起隔离实例）
node tests/regression-voice.cjs <port>          # 语音链路回归
```

- 两条 WS 通道：`/ws/agent`（被控端注册）、`/ws/viewer`（管理端订阅下发）。
- 令牌：`CLOUD_WS_TOKEN`（设备侧）、`CLOUD_VIEWER_TOKEN`（管理侧）。
  未配置时用开发默认值，**上公网必须显式设**。
- 外网走 443/wss（`control.245959623.xyz`），cloudflared 回源本机 8788 ——
  外机连的 wss **不带端口**。

## 二、关键文件

| 路径 | 职责 |
|---|---|
| `src/index.js` | HTTP 路由 + WS 主循环（最大文件） |
| `src/registry.js` | 设备注册表、在线态、指令下发 `sendInstruction()` |
| `src/store.js` | **指令队列**落盘（24h 强 TTL、回执即删、降级为内存态继续跑） |
| `src/interact.js` | **互动数据**三表（`song_requests` / `danmaku` / `feedback`）：上报、限额、审批状态机。待审条目**永不扫**，库不可用**如实回 `STORE_UNAVAILABLE`** |
| `src/protocol.js` | **协议 v1**：`PROTOCOL_VERSION = 1`，v1 信封 ↔ 旧扁平格式规范化 |
| `src/perms.js` | 权限判据（`canAccess` / scope 过滤） |
| `src/ota.js` + `ota.json` | OTA 清单与版本比较（`cmpVersion` 是版本比较唯一真源） |
| `src/qml-patch.js` | QML 热补丁包 |
| `src/repairs.js`、`src/plugins.json`、`src/ticket.js` | 报修表 / 插件市场 / 票据 |
| `scripts/publish-release.mjs` | 发布入口（`--product agent\|viewer --version X --file <包>`） |

## 三、边界（别踩）

- ⚠️ **生产跑的就是本目录（工作区份）**：计划任务 `StelarithCloud` / `StelarithCloudWatchdog`
  均经本目录的 `run-cloud-hidden.vbs` / `cloud-watchdog-hidden.vbs` 壳执行
  `run-cloud.cmd` / `cloud-watchdog.cmd`（`node_modules` / `.env` / `assets/pkg` 都在这）。
  ⚠️ **动作禁止直接指 .cmd/.bat**（Task Scheduler 必分 cmd 控制台窗）；vbs 壳的来龙去脉见
  `scripts/fix-cloud-console-window.py` 头部注释。
  2026-10-20 已从「外部份运行」正式切回工作区份并改名（`Stelarith-cloud-ws` → `Stelarith-cloud`，
  外部 bare 远端同步改名为 `D:\Stelarith\Stelarith-cloud`，remote `outer` 已更新）——
  详见 `AI-必读-路径变更告知书.md` 的「2026-10-20 改名」节。
- ⚠️ **启动会重建 `assets/qml-patch/latest.json` + zip**（打包工作区当前 `qml/`）。
  那是**运行产物**，别当成代码改动回退——回退会让清单与 zip 不匹配。
- ⚠️ `ota.json` 被 `.gitignore` 挡住不进库：**线上文件即真源**，别 `git add -f` 强提。
- `instruction-queue.db` / `*.log` 是运行态，正被进程占用，**勿删勿移**。

## 四、相关文档

- 历史交接说明：`README-工作树说明-2026-10-06.md`
- 路径变更：`AI-必读-路径变更告知书.md`
- 全局命名 / 版本 / 提交约定：仓库根 `docs/命名与版本约定.md`

## 五、闪传通道设计（2026-10-20 定稿 · 用户点单；TCP 主轴 2026-10-20 定）

> 动机：① 文件推送是停等协议（64KB/片、一片一回执、无重传，15s 看门狗直接判死）；
> ② 远控 RFB 走 WS `vnc-data` 被 200KB/s 背压闸吞掉全屏首帧 ⇒ viewer 全白；
> ③ cloudflared 隧道**只转 TCP**，公网 UDP 不可达 ⇒ 通道必须双层。
>
> 2026-10-20 演进：初版按「UDP 快速加密传输」设计；实测公网 UDP 不可达后，
> 用户定「**那就 tcp**」⇒ **公网主路改为 TCP 专用连接**（8790/tcp），
> UDP 中继保留作回环/局域网加速项（同一套帧/AEAD/会话存储，`flash-core.js` 共用）。
> UDP-over-WS 封装回退**不再计划**——TCP 主路覆盖同一场景且更简单。

### 5.1 拓扑事实（2026-10-20 实测）

- 三端当前同机：8788 上连接全是 `127.0.0.1`（viewer / agent / cloud 同进程机）。
- **公网硬约束**：cloudflared 隧道只转 TCP ⇒ 公网 UDP 不可达（「那就 tcp」的根因）。
- 落地形态（两代同存）：
  - **TCP 专用连接（公网主路，8790/tcp）**：两端各拉一条专用 TCP，经 WS `tcp-session`
    协商同一把会话密钥；与 WS 控制/心跳彻底分离 ⇒ 200KB/s 背压闸（保心跳的粗暴手段，
    却把远控全屏首帧丢光）和文件停等协议的病根一起去掉。
  - **UDP 帧中继（8789/udp）**：回环/局域网直连加速项，帧格式/加密与 TCP 完全一致。
- 公网可达还差一步（人工）：Cloudflare 仪表盘给隧道加 Public Hostname
  （`flash.245959623.xyz`，Type=**TCP**，URL `tcp://127.0.0.1:8790`）；
  域名泛解析已存在。客户端 TLS 连 `flash.245959623.xyz:443`（边缘终结，
  隧道内明文；帧本身已有 AEAD，双保险）。

### 5.2 加密

- **ChaCha20-Poly1305** AEAD：Node 端 `crypto` 原生；Qt 两端 CMakeLists 均未链
  OpenSSL ⇒ C++ 侧纯手工实现（约 300 行，零新依赖，老 CPU 无 AES-NI 时比 AES 快）。
- **会话密钥经 WS 已鉴权通道协商**（服务端随机 32B，`HELLO` 帧下发）：密钥不固化
  进二进制、每会话唯一；防重放靠 `nonce = session_id ‖ seq`（单调递增计数器）。
- `.env` 只放 `UDP_RELAY_TOKEN`（接入鉴权），不放业务密钥。

### 5.3 帧格式（定长头 + AEAD 全覆盖）

```
magic "SU"(2) | ver(1) | type(1) | session(4) | seq(8) | sendTs(8) | len(2) | tag(16) | payload(len)
```

- `type`：`HELLO`（握手/密钥）/ `DATA` / `ACK`（累计确认）/ `NACK`（SACK 位图）/
  `PARITY`（FEC 奇偶片）/ `PING` / `PONG` / `CLOSE`。
- `sendTs`：发送端单调时钟（毫秒），**延迟信息唯一真源**（见 5.6）。
- AEAD 的 AAD = 定长头（防篡改 seq/ts），tag 在 payload 后。

### 5.4 可靠性（UDP 不可靠，应用层补；TCP 主路不需要）

- 序号 `seq`（每会话单调）+ **累计 ACK + SACK 位图**（相对 ack_seq 的 64 位图）。
- 选择性重传：只补丢失片，不重传已确认片（修掉停等协议"丢一片判死整个文件"）。
- **FEC**：每 8 数据片 1 个 XOR 奇偶片（`PARITY`），抗 ≤12.5% 突发丢包零重传。
- 乱序接收：接收端 `next` 指针 + 待写缓冲（上限 64 片），按序刷盘/上屏。
- ⚠️ 以上是 **UDP 中继**所需（U2 批次做）。**TCP 主路不需要应用层 ARQ**——
  TCP 自有流控+有序可靠，中继改为严格单调 seq（重放即拒，兼守 nonce 不重用）+
  背压暂停源读取（不丢帧），见 5.9。

### 5.5 拥控（用户 2026-10-20 选定：窗口自适应）

- AIMD：慢启动（cwnd 1→ssthresh 指数增）→ 拥塞避免（每 RTT +1）。
- 丢包判定分两档：RTO 超时 ⇒ cwnd 回 1 慢启动；SACK 显示丢片但后续到达 ⇒ 快重传 + cwnd 减半。
- 窗口上限 64 片（4MB @64KB/片），教室机弱网自适应。

### 5.6 延迟信息（用户明示"交给你处理"）

- `PING/PONG` 测 RTT；每个 `DATA` 的 `sendTs` 供单向延迟估算。
- 采样算法：SRTT + RTTVAR（Karn 修正，重传样本不采样），RTO = SRTT + 4·RTTVAR，
  钳制 **[200ms, 15s]**（15s 与原 WS 看门狗口径一致）。
- 云端每跳打点（收/发时刻）进 `file_sessions`，排障可直接读每段耗时。
- 接收端**自适应抖动缓冲**：按测得抖动调缓冲深度（有界 0–200ms），平衡延迟与平滑。
- 统计上报：RTT / 丢包率 / 抖动 / 吞吐 / 当前窗口，QML 诊断页可读。

### 5.7 切换顺序与兼容

1. **先远控 RFB**（`vnc-data` 二进制帧搬闪传通道，绕开 200KB/s WS 背压 ⇒ 解首帧全白；
   TCP 主路优先，UDP 中继供回环/局域网低延迟备选）；
2. **再文件传输**（`file_push/chunk/done` 走闪传通道流式窗口——TCP 下连应用层 ACK 都可省）；
3. JPEG / 音频可选（低延迟模式，允许丢帧；UDP+FEC 在此场景才重新有意义）。
- 兼容：协商失败（旧端无 `tcp-session` 字段）**自动回落 WS 既有路径**，旧端零改动可用。
- 令牌红线：`CLOUD_WS_TOKEN` / `CLOUD_VIEWER_TOKEN` **不随改名动**（客户端成对，改名只动目录）。

### 5.8 U1 落地纪实（2026-10-20 · 云端侧完成）

| 文件 | 职责 |
|---|---|
| `src/udp-relay.js` | 帧编解码 + ChaCha20-Poly1305（node crypto 原生）+ 会话表 + dgram 中继 + HELLO/PING/CLOSE + 帧率闸门 + 空闲收口 + stats |
| `src/config.js` | `UDP_RELAY_PORT`(8789) / `UDP_RELAY_HOST`(0.0.0.0) / `UDP_RELAY_TOKEN`（**空=不启动**，fail-closed）/ 帧率闸门 / 空闲 MS |
| `src/index.js` | WS 消息 `udp-session`（open/close）：权限同「看画面」（canAccess），设备不在线直拒；`opened` 双端下发 `{sessionId, key(base64), udp:{host,port}}`；viewer/agent WS 断开 ⇒ `closeSessionsWhere` 收口（防泄漏） |
| `tests/unit-udp-relay.mjs` | 31 断言：编解码/偏移/大值边界、AEAD 篡改、fail-closed、回环全链路、重传转发、seq 空间、闸门 |
| `tests/regression-udp-session.mjs` | 15 断言（隔离实例联测）：WS 协商→双端同密钥→真 UDP 握手→DATA 原样→PONG→双关闭路径→断线防泄漏 |

**实现期抓到的三个设计级 bug（已修，均有回归断言）**：
1. 帧头偏移重叠——seq 写在 12/sendTs 写在 20 会啃掉 len 字段（应为 8/16/24）；
2. HELLO_ACK 复用请求帧头 ⇒ payload 变长但 len 不变 ⇒ 对端 `bad-length` 拒收（ACK 必须新建帧头）；
3. **nonce 重用（最严重）**——HELLO_ACK 若与 HELLO 同 seq=0 同密钥，ChaCha20 keystream 重用，XOR 两密文可还原 HELLO 里的**令牌**。定稿规则：**端→中继帧 seq < 2^63；中继自发帧 seq = 2^63 + 会话内计数**，越界拒收。
另：中继**不做** seq 单调闸门（会把 U2 选择重传的合法重传挡掉）——重放防护在两端（AEAD nonce + 端侧单调校验）。

**运行**：`.env` 配 `UDP_RELAY_TOKEN` 后重启云端即起（监听日志 `UDP 闪传中继已监听 host:port` 进 events.log）。
**未做（后续批次）**：U2 可靠性/拥控/延迟层（仅 UDP 需要）、U3 远控 RFB over 闪传通道、U4 文件传输 over 闪传通道 + Psql；Qt 两端 C++ ChaCha20-Poly1305 + QUdpSocket/QTcpSocket 客户端。

### 5.9 TCP 主轴落地纪实（2026-10-20 · 用户定「那就 tcp」后完成）

| 文件 | 职责 |
|---|---|
| `src/flash-core.js` | **共享内核**：帧编解码 / AEAD / nonce / seq 空间纪律 / 中继自发帧构造 / HELLO+PING 校验 / 会话存储。UDP 与 TCP 两个中继共用（安全逻辑一份代码两处审计） |
| `src/tcp-relay.js` | TCP 传输层：`node:net` 8790 + 流式分帧（半帧/多帧合一/极限帧）+ HELLO 绑定 + **严格单调 seq**（重放即拒）+ 背压暂停源读取（drain 续读，不丢帧）+ 一端断即收口 + 空闲/HELLO 宽限收口 + 连接封顶 + stats（含 pauses/resumes） |
| `src/udp-relay.js` | 重构为消费 flash-core（行为逐字不变，31 个单测当回归护栏全绿）；保留作回环/局域网加速项 |
| `src/config.js` | `TCP_RELAY_PORT`(8790) / `TCP_RELAY_HOST`(0.0.0.0) / `TCP_RELAY_TOKEN`（**空=不启动**，fail-closed）/ `TCP_SESSION_IDLE_MS`(60000) / `TCP_HELLO_GRACE_MS`(15000) |
| `src/index.js` | WS 消息 `tcp-session`（open/close）：权限同「看画面」，设备不在线直拒；`opened` 双端下发 `{sessionId, key(base64), tcp:{host,port}}`；viewer/agent WS 断开 ⇒ `closeSessionsWhere` 收口（防泄漏） |
| `tests/unit-tcp-relay.mjs` | 41 断言：流式分帧边界（切三瓣/合一 chunk/空 payload/65535 极限半帧/垃圾流）、回环全链路、严格单调、重复 HELLO 拒、断线收口、空闲/宽限、**背压暂停实证（pauses>0 且 96 帧一帧不丢）**、fail-closed |
| `tests/regression-tcp-session.mjs` | 19 断言（隔离实例联测）：WS 协商→双端同密钥→真 TCP 握手→DATA 原样→PONG→主动关/双侧断线三路径防泄漏 |

**与 UDP 中继的关键差异（同一套帧协议的两种管道）**：
1. TCP 有序可靠 ⇒ 中继**做**严格单调 seq 闸门（UDP 那边故意不做，会挡 U2 合法重传）；
2. 一端 socket 断 ⇒ **整个会话收口**（`peer-tcp-gone`，双端 WS 收 closed）——防跨连接
   nonce 重用窗口，也让两端明确走 WS 重协（新密钥）或回落 WS 既有路径；
3. 背压不丢帧：对端写缓冲满 ⇒ `pause()` 源 socket，`drain` 后 `resume()`（中继内存有界）；
4. 无帧率闸门（TCP 自带流控，且不再与心跳抢同一条连接）、无 MTU 分片（len 上限 65535）。

**实测结论（单测 + 隔离实例回归）**：停读对端时中继转出 9 帧即触发背压暂停（不是靠
用户态缓冲硬扛），续读后 96 帧全部到达；`len` 是 UInt16 ⇒ 65535 即字段上限，
「len 越界即断」分支在协议封顶下不可达（防御性保留，封顶值调小时才激活）。

**运行**：`.env` 配 `TCP_RELAY_TOKEN` 后重启云端即起（日志 `TCP 闪传中继已监听 host:port`）。
**未做（后续批次）**：U2 延迟/统计层、U3 远控 RFB over 闪传通道（用户痛点验收靶点）、
U4 文件传输 over 闪传通道 + file-relay 60s 缓存 + Psql；Qt 两端 C++ 客户端
（ChaCha20-Poly1305 + QTcpSocket/QSslSocket，公网走 `flash.245959623.xyz:443`）。


---

## 十二、本地密钥存储（vault.py）

> 2026-10-31 自建：GitHub PAT / 云令牌这类凭据的**进程级私人凭据库**。
> 动机：凭据不该散落在 .env / 脚本 / 进程列表 / 对话里；上传 GitHub Release 资产等
> 需要 PAT 的场景应有安全落点。实现**独立**（不依赖 WorkBuddy 凭据体系），云/端/站点通用。

### 特性

- **AES-256-GCM**（cryptography 50.x）：每条值独立 12B 随机 nonce + GCM tag，
  同一值重写密文不同（不可比对）；篡改/错 key 一律优雅拒绝（InvalidTag 已归一）。
- **独立主密钥**：`%%LOCALAPPDATA%%\stelarith-vault\master.key`（32B 随机）。
  Windows ACL 已收紧到当前用户 (R,W)；丢失 = 全部不可解密（设计如此）。
- **库文件**：`%%LOCALAPPDATA%%\stelarith-vault\vault.json`（仅密文，含版本头）。
- **用户级存储**：绝不进 git；换机需整目录迁移 + 重设（主密钥不可导出明文）。
- **控制台编码兜底**：Windows CMD 默认 GBK，写 emoji/生僻字会 `UnicodeEncodeError` **崩命令**
  （不是乱码小事，是退出码 1）。进程启动即把 stdout/stderr 切 UTF-8 + `errors=replace`，
  并尽力把控制台代码页设到 65001。

### 用法

> ⚠️ **先看这条**：脚本要用**完整路径或先 cd 到仓库根**。
> 在 `C:\Users\Administrator>` 下敲 `python scripts\vault.py ...` 会报
> `can't open file 'C:\Users\Administrator\scripts\vault.py'` —— 相对路径是相对**当前目录**解析的。

**交互式（推荐，手敲一次用这个）**：不带管道运行 `set`，会提示隐藏输入（不回显、不进 shell 历史、不进进程列表），
粘贴后回车即写入，**不需要** Ctrl+Z。

```
cd /d D:\Stelarith\Stelarith-xingjikong-qt
python Stelarith-cloud\scripts\vault.py init
python Stelarith-cloud\scripts\vault.py set github_pat
      → [vault] 输入 github_pat 的值（不回显，回车确认）: ****<粘贴 PAT><回车>
python Stelarith-cloud\scripts\vault.py list
python Stelarith-cloud\scripts\vault.py get github_pat
python Stelarith-cloud\scripts\vault.py delete github_pat
python Stelarith-cloud\scripts\vault.py check
```

**脚本化（管道 / 重定向）**：读全部 stdin。

```
<你的工具> | python Stelarith-cloud\scripts\vault.py set github_pat
```

> ⚠️ **CMD 里没有 `echo -n`**。`echo -n xxx | python ...` 会把字面量 `-n` 写进值里
> （PowerShell 同理，`-n` 是不同参数）。要用管道就别用 echo 拼，直接喂文件或工具输出。

### 前置 / 边界

- 受管 Python（3.14.3）已带 cryptography 50.0.2；其它解释器缺库会明确报错（不静默降级）。
- set --value 显式传值会进进程列表（慎用）；默认从 stdin 读。
- 名字禁路径分隔符（防逃逸写盘）；空值拒绝。
- 已验证：加解密回环 / 篡改字节 / 同长度错 key / 换错 key / 空值 / 逐条独立——全优雅。

---

## 十三、班级绑定统一 + 扫码选设备直接绑（加减归档 2026-11-05 · 用户 m01083 / m01202）

### 13.1 三条线一起收

| 问题（用户 m01083 原话） | 处置 | 状态 |
|---|---|---|
| 「每一个班都必须是一个独立的后台」 | 每班数据域早已按 `classCode` 隔离（class-room / class-data / timetable / attendance / songkey） | 已存在，本批未改 |
| 绑定 API 不统一 | 五路（admin / code / qr / device / **class-scan**）全部归 `bindDeviceCore` | ✅ 本批补齐 |
| 设备重名 | 新增 `src/device-identity.js`（重名判定 + 改名 API + **namePin 钉住**） | ✅ 本批 |
| 扫码选设备直接绑（m01202） | `/api/bind/info` 回候选清单 + `/api/bind/confirm` 认 `uid` + `bind.html` 设备列表 | ✅ 本批 |

### 13.2 新增（可回退：全为新增文件，删除即回退）

| 文件 | 行数 | 作用 |
|---|---|---|
| `src/bind-api.js` | 282 | 五路统一绑定核心 + `classScanCandidates/classScanDecide/classScanBind` |
| `src/device-identity.js` | — | 重名判定 `markNameConflicts` + 改名 `handleDeviceRename` + `applyNamePin` |
| `tests/regression-class-scan.mjs` | 148 | 扫码绑定的纯逻辑断言（26 条）+ 反向验证 |
| `tests/regression-bind-unify.mjs` | 523 | 端到端 HTTP 16 条（含 ⑮ 扫码选设备 / ⑯ 换设备拒绝） |

> ⚠️ `bind-api.js` **不在** `oversize-legacy.json` 台账内（非白名单），可正常增长；
> `bind.html` 也不受 `test-file-size` 管（不在扫描后缀）。

### 13.3 修改（可回退：`git checkout` 即回退）

| 文件 | 行数变化 | 说明 |
|---|---|---|
| `src/index.js` | 3562 → **3557**（净减，白名单余 5 行） | 删 `needsDeviceInput`、删 confirm 的假成功块、接 `classScanBind`；加设备改名路由 |
| `src/class-admin-classes.js` | +39 −? | `adminIssueBindCode` 增加回 `matrix/size/version/scanUrl`（调 `bindQrOfTicket`），旧字段一个不动 |
| `src/class-admin.js` | +1 −1 | `adminIssueBindCode(...)` 多传 `req`（拼扫码地址要看请求 Host） |
| `src/bind.html` | 266 → **296** | kind=class 显示设备列表 + 6 位码兜底；`submit()` 带 `body.uid` |

### 13.4 关键设计（为什么这么做）

- **票是唯一凭据**：`class-scan` 进 `TICKET_ONLY_CHANNELS`，`requireManage:false`。
  班级 / 租户**全从票里取，绝不读请求体**——否则匿名手机页就是一条跨租户绕道。
- **七道拒**（`classScanDecide`）：`BAD_TICKET` / `TICKET_USED` / `TICKET_EXPIRED` /
  `NO_UID` / `DEVICE_UNKNOWN` / `DEVICE_BOUND` / `CROSS_TENANT`。
- **候选清单脱敏**：只回 `{uid,name,online}`，且只列「同租户 + 未分班」的设备。
- **删掉 confirm 的提前返回**：原 `t.state==='bound'` 直接回成功是个安全洞
  （攻击者可换设备拿假成功），改由核心判一次性。
- **namePin 为什么必须有**：`registry.markConnected` 是「设备自报名优先」，
  被控端每次重连都报 `QHostInfo::localHostName()` ⇒ 管理端改的名字下一次重连就被顶回去，
  功能看起来像没做。改名时同登记 `namePin`，register 落 meta 前 `applyNamePin` 覆盖。

### 13.5 验证证据（实跑）

| 项 | 命令 | 结果 |
|---|---|---|
| 纯逻辑 | `node tests/regression-class-scan.mjs` | **26 通过 / 0 失败 · 反向验证 PASS**（坏版本红 2 条） |
| 端到端 | `node tests/regression-bind-unify.mjs` | **16 通过 / 0 失败** |
| 端到端反向 | `node tests/regression-bind-unify.mjs --reverse` | **14 通过 / 2 失败**（⑮⑯ 变红）⇒ 非假阴性 |
| 生产零污染 | `grep -c` 生产 `events.log` | 测试假数据 0 命中（隔离 `mkdtempSync` + 环境变量前置） |
| 语法 | `node --check` × 4 文件 | 全过 |

**测试隔离的关键坑**：`src/config.js` 在**模块评估期**读死 `EVENTS_FILE`/`CLASS_DIR_DB_FILE`，
所以必须**先设环境变量、再 `await import()`**（静态 `import` 会被 hoist 到前面 ⇒ 隔离失效、写脏生产库）。

**反向验证的关键坑**：坏版本抽掉判据后会放行到 `bindDeviceCore` → `resolveClassCode`，
独立 `data:` 模块没有初始化 class-dir ⇒ 抛异常逃逸、整个反向验证判 FAIL。
修法：每条探针 `try/catch`、异常计为红（异常本身就是"放行了本该拒的设备"的正确信号）。

### 13.6 未验证（如实声明）

- **生产 8788 未重启**：跑的是 2026-11-03 18:25 的进程，不含本批任何改动
  （实测 `POST /api/class/bindcode` 不回 `matrix`；隔离实例同源码则正常回 `size=29 version=3`）。
  按铁律 4 未动生产进程。
- 真手机扫码绑定的**真人端到端**未做（只有 HTTP 层 ⑮⑯ 覆盖）。
- 设备改名后的 namePin 在**设备真实重连**时是否真顶住自报名，未做真实设备验证
  （只有单元级 `applyNamePin` 断言）。
- `bind.html` 在手机浏览器的实际版式未截图核对。

## 十四、祝福弹幕词表下发端点（加减归档 2026-11-05 · 用户点单「1345」①）

### 14.1 边界：不造第二套审核真源

审核闭环早在 `interact.js:194 submitBlessing` → `index.js:1783/1800` 审批 →
`index.js:2542 broadcastBlessingDanmaku` 已存在（详见 control 仓 README §7.1）。本批**只补词表下发**，
不重复造端点；被控端 `src/system/danmaku_wordlist.cpp` 拉取后与本机三词取并集。

### 14.2 新增（删除即回退）

| 文件 | 职责 |
|---|---|
| `src/danmaku-words.js` | `DANMAKU_WORDS_PATH='/api/danmaku/words'`、`sanitizeWords()`（去非串/去空白/截断 64/去重/限量 200）、`loadDanmakuWords()`（缺文件/坏 JSON 回落空表、绝不抛）、`danmakuWordsPayload()` 带 `cacheSeconds:30` |
| `src/danmaku-words.json` | 空表起步（`words:[]`），管理员改这份热更新；本机三词兜底不写进来 |
| `tests/regression-danmaku-words.mjs` | 三层：L1 纯逻辑 / L2 隔离实例 HTTP 8883（无鉴权 200 + 对照 `/api/interact/*` 401）/ L3 删薄壳反向（404） |

### 14.3 修改（薄壳，守 index.js 行数）

- `src/index.js`：`:30` 附近加 `import { danmakuWordsPayload, DANMAKU_WORDS_PATH } from './danmaku-words.js';`；
  公开只读区 `/api/plugins` 之后（`:731`，**在 `isManageApi:915` 之前**）加一行
  `if (req.method === 'GET' && u.pathname === DANMAKU_WORDS_PATH) return send(200, danmakuWordsPayload());`。
- 为什么必须公开：被控端只有设备令牌、HTTP 调用不带 `Authorization`，挂 `/api/interact/*` 会被 401 拒。
- `index.js` 3561 行 < 台账 3562（净 +3，仍在额度内）。

### 14.4 验证与未验证

- `node tests/regression-danmaku-words.mjs` **17/0 PASS**（含反向删薄壳 ⇒ 404，证明 L2 非假阴性）。
- 未验证：生产 8788 未重启（跑 2026-11-03 旧进程，按铁律 4 不动）；真被控端拉取未端到端；
  `danmaku-words.json` 手改后真机 30min 热更新周期未实测。


## 十五、设备侧课表只读端点（加减归档 2026-11-06 · 用户 m08463 第 3 项）

### 15.1 为什么必须有这条端点

- `index.js:1249` 的 `isViewerApi` 含 `/api/class/` 前缀 ⇒ 班级域全部要 `CLOUD_VIEWER_TOKEN` 或票据，
  被控端只有 `CLOUD_WS_TOKEN`（设备令牌）恒 401。
- 现役下放是云端**主动 push**（`class-timetable-push.js` 的 `sendToClassDevices(code,'timetable-apply')`），
  只在班级在线且被触发时发；首次开机 / 错过推送 / 重装就再也拿不到。本批补「设备主动拉一次」。
- 与 §十三 / §十四 同构：**不造第二套下发真源**，只读现成的 `getTimetable()` / `getDeviceClassCode()`。

### 15.2 新增（删除即回退）

| 文件 | 行数 | 作用 |
|---|---|---|
| `src/device-timetable.js` | 110 | `deviceTimetable({uid,classCodeOf,tenantOf,read})` 六分支（缺 uid 400 / 未分班 NO_CLASS / 有班无课表 NO_TIMETABLE / 正常 透传 profile / read 抛 500 / ok:false 400）；`deviceTimetableDefault()` 绑真源 |
| `src/device-api.js` | 新增 | 设备侧 GET 一次分派：`/api/device/timetable`（读本班）+ `/api/device/classes`（本租户班级清单，脱敏 `code/name/onlineCount`）+ `?classCode=` 选班预览；`authDevice()`（静态 DEV_TOKEN 或 agent ticket，票里 uid 权威不可被 query 覆盖）、`teq()` 定长比较 |
| `tests/regression-device-timetable.mjs` | 311 | 三层回归（L1 纯逻辑 / L1b 选班与鉴权 / L2 隔离实例 HTTP 8891 / L3 摘薄壳反向） |

关键安全约束：班级清单**不下发 `devices` 数组**；选班预览必须校验班号在本租户清单里，
不在回 **403 `NO_SUCH_CLASS`** 且不区分「不存在」与「跨租户」（区分等于泄露别校班号真实存在）。

### 15.3 修改（薄壳，守 index.js 行数）

| 文件 | 变化 | 行为差异 | 恢复 |
|---|---|---|---|
| `src/index.js` | `:36` import 改 `deviceGet`；`:737` 原单行薄壳改成**分派一行** `if (req.method==='GET' && u.pathname.startsWith('/api/device/')) { const r = deviceGet(u,req,{devToken:DEV_TOKEN,verifyTicket:verifyAgentTicket}); if (r) return send(r.status,r.body); }` | 设备侧新增端点只改 `device-api.js`，不再动 index.js | 退回原单行薄壳 |
| `src/index.js` 行数 | **3561** < 台账 3562 | 守 §10.5 零余量纪律 | — |

之后新增设备端点只改 `device-api.js` —— 这是把「一次分派」抽出来的全部理由（index.js 有规模门禁）。

### 15.4 验证与未验证

- `node tests/regression-device-timetable.mjs` **67 通过 / 0 失败**：L1 18 + L1b 20（含票优先、票 ok 但无 uid 不算通过、Bearer 大小写不敏感、跨租户班号 403）+ L2 24（隔离实例 8891，建班/写课表/绑设备/拉取 profile 逐字节一致/未分班不串班）+ L3 反向（摘薄壳 ⇒ 端点 404，相邻 `/api/plugins` 仍 200）。
- 两个复用坑：① `config.js` 模块评估期读死 env ⇒ 隔离必须设 env 后再动态 import；② 子进程 stdout 必须落文件（Fastify 每请求一条日志灌满 PIPE 会阻塞事件循环）。
- 未验证：生产 8788 未重启（旧进程，按铁律 4 不动）；真被控端 HTTP 拉取端到端未跑；`onlineCount` 脱敏未做真机多租户越权实测。
