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

## 十六、安全封堵 + 噪音按班上报 + 重复合并（加减归档 2026-11-15 · 用户 m29848）

口径（用户 m29905 / m29968）：**不产 HTML、不套外部标准条文，只按项目自己的真源收敛**。
本批只改干净文件；他线未提交件（`src/index.js`、`src/main.cpp`、`viewerbackend.cpp`、
`AccountMenu.qml`、7 个 admin-server 文件、大批 QML 与 VoiceHub 接线）一行不碰。

### 16.1 改动清单（删除即回退）

| 文件 | 变化 | 作用 |
|---|---|---|
| `src/perms.js` | `:48` 无 claims 时 `scopes: []`（原 `null`） | 裸 uid 票据从「全权」变零权限；静态运维令牌 `mode==='token'` 仍是唯一 `scopes:null` 全权通道 |
| `src/perms.js` | `:26` import `TENANT_ID_RE`，删 `:29` 本地正则副本 | 租户 id 正则与 `tenant.js` 单一真源 |
| `src/tenant.js` | `:33` 改 `export const TENANT_ID_RE` | 供 perms.js 复用，格式逐字不变 |
| `src/user-claims.js` | 8 处注释纠偏 | 「无 claims ⇒ 全权」全部改成「零权限」；`[]` 与「无 claims」不再有语义差 |
| `src/ticket.js` | `:75` 注释纠偏 | 旧票纯 uid 不再自称全权 |
| `src/registry.js` | `:981-1006` 两段重复 JSDoc 合一；别名改 `export const relayTerminalFrame = relayToSubscribers;` | 删掉同体两名的转发层，导出名与调用点零变化 |
| `package.json` | `test:unit` 追加 2 个单测 | 门禁化，不靠人肉 review |
| `tests/unit-auth-failclosed.mjs` | 新增 9 断言 | 静态令牌全权 / 裸票零权限 / 脱敏 / 白名单回填 / 删授权不放大 / 票内 claims 优先 |
| `tests/unit-noise-class-event.mjs` | 新增 17 断言 | 班级归属只认云端表、四道闸、去重零行为变化 |
| `Stelarith-control-qt/src/noise/noise_setup.cpp` | 163→203 行 | 新增 `sendNoiseClassEvent()`；`thresholdReached` 挂 `noise_alert`；`ensureNoise()` 注入 `setClassEventSink()` |
| `Stelarith-control-qt/src/noise/noise_incident_pipeline.h/.cpp` | 59→66 / 46→66 行 | 新增 `ClassEventSink`（`std::function`，不加 signal 不动 moc/CMake）；存证结案投 `noise_incident` 摘要 |

### 16.2 行为差异

- **安全**：`identityFromAuth()` 对普通票永远返回 scopes 数组 ⇒ `index.js` 里散落的 5 处
  `ident.scopes === null` 判全权自动收紧；`viewerbackend.cpp:1097-1131` 收到数组即
  `setIdentity()` + `applyServerRole()` ⇒ `m_roleLocked=true`，本地「切管理员」按钮锁死。
  站点 claims 票、`user-claims` 白名单、`platform_admin` 三条合法路径不受影响。
- **噪音**：上课自动开启本就存在（`noiseOnLessonStart()` 由 `main.cpp:4175` 接课表钟）；
  本批补齐「对应班级」：超阈与存证各发一条低频 `class-event`，走云端既有
  `handleAgentClassMsg()`（班级归属只认 `knownDevices`，载荷不带 classCode），viewer
  班级 Timeline 的 `data.message` 直接显示，云端与 viewer 零改动。2 秒一拍的
  `noise_sample` 仍走原订阅中继；三张 base64 图仍走 `noise_incident` 取证，不塞进
  64KB 班级事件（每节次最多 2 条，不会冲光 200 条历史水位）。
- **重复合并**：只合能安全合的两处，且保持导出名不变（`index.js` 仍 import 旧名）。
  其余重复见 16.5，按 AGENTS §2「不删非本次自建文件」只登记不动手。

### 16.3 验证证据（命令 + 数字）

- `node tests/unit-auth-failclosed.mjs` **PASS 9 / FAIL 0**；
  `node tests/unit-noise-class-event.mjs` **PASS 17 / FAIL 0**；
  `npm test`（10 套单测）**EXIT=0，逐套全绿**（14/26/13/21/16/13/14/14/9/17，合计 157 通过 / 0 失败）。
- 反向验证（把 `handleAgentClassMsg` 的 `getDeviceClassCode(uid)` 改成误信自报 `classCode`）⇒
  **BAD_EXIT=1 / PASS 15 FAIL 2**，两条信任模型断言精准变红；`finally` 按字节恢复后
  **FINAL_EXIT=0 / PASS 17 FAIL 0**。安全侧同样做过反向（`:48` 改回 `null` ⇒ 新单测红）。
- 端侧：`python scripts/_c1obj.py src/noise/noise_setup.cpp` **rc=0**；
  `python scripts/_noise_pipeline_probe.py` **exit=0 / FAILS=0**（22 条，含新增 7 条按班摘要断言：
  不带 classCode、不带三张图、原取证上报仍走 MockTransport）。
- 门禁 17 件：**14 绿**。3 红与本批无关且可复现：`test-file-size` 5 项全在他线
  （`viewerbackend.cpp` 2409/2282、`viewerbackend.h` 1126/1098、`timetable_hub.cpp` 637、
  `AttachedSettingsVmCore.h` 423、`ClassDeviceTile.qml` 210）；`test-change-contract` 43 项全是
  基线之后历史提交；`test-ota-guard` 需 127.0.0.1:18999 服务（ECONNREFUSED）。
- WIP 复核：cloud / control 两仓 `git status --short -- <本批路径>` 无一条他线夹带；
  提交一律精确 pathspec。

### 16.4 未验证（如实记账）

- 生产 8788 与现役被控端**未重启**（AGENTS §2 铁律 4）；真机上课→超阈→班级 Timeline
  端到端未跑，只跑通隔离单测 / 离屏探针 / 单文件编译。
- 全量 `python scripts/_build_agent.py` **BUILD_EXIT=0**（8/8 ninja 步：噪声三件重编 + moc + 链接 + qml/island/ui 同步；
  `[island-parity] OK 47 件` / `[ui-parity] OK 26 件`）。首次后台 job 报宿主侧
  `EXECUTOR_UNAVAILABLE: Niubash preparation failed ... hardlink`，重试即通过，非代码失败。
  唯一编译告警是既有 `noise_incident_pipeline.cpp(34): warning C4858`（QtConcurrent 返回 QFuture 未接），非本批引入。
- 静态 `ADMIN_TOKEN` / `VIEWER_TOKEN` 仍是全权通道，本批**没有**做令牌轮换或票据吊销；
  已有裸 uid 票在 TTL 内仍可用，只是权限为零。
- 端侧探针 `scripts/_noise_pipeline_probe.cpp` 沿用既有「不入库」口径，保留在工作区便于复跑。

### 16.5 重复内容盘点（全方位评估，只合能安全合的）

| 重复 | 处置 | 理由 |
|---|---|---|
| `relayToSubscribers` / `relayTerminalFrame` 同体两名 | **已合**（本批） | 干净文件 + 导出名不变 + 单测断言同一函数 |
| `TENANT_ID_RE` 在 `perms.js` / `tenant.js` 两份字面量 | **已合**（本批） | 干净文件 + 导出面最小改动 |
| `admin-server` 与 `Stelarith-cloud` 两套后端 | 登记不合 | 两条线各自在跑，合并需点单与停机窗口 |
| `admin-server._githead_bak_20261108` 整仓备份 | 登记不删 | 非本次自建，批量删需备份 + 清单 + 人确认 |
| 权限判据三份（cloud/perms.js、admin-server/rbac/identity.js、viewer/permission_manager.cpp） | 登记不合 | 跨语言跨仓，且 viewer 文件是他线 WIP |
| `index.js` 仍用旧别名 `relayTerminalFrame` | 登记不改 | 他线 WIP；别名保留即行为不变 |
| 发布副本 `_b10_release/src/noise`、`_b28_release/src/noise`、根目录 `*.obj/*.log` 等 | 登记不删 | 非本次自建，可能是他线归档 |

一句话：**合并的判据不是「看起来一样」，而是「干净文件 + 导出面不变 + 有断言守住零行为变化」**。

## 十七、实例自治 + 六角色预设 + 独立部署文档（加减归档 2026-11-15 · 用户 m30108 / m30161）

口径：用户裁决「云端就没有控制其他云端的权利」「不要以行业标准行事，懂得变通」，并补七条架构建议。
按项目真源收敛 —— **不换栈、不重写后台、不产 HTML**；只改干净文件，他线 WIP（index.js / config.js /
viewer QML / admin-server 7 件）一行不碰。完整文档见 `docs/开发文档-班级后台-2026-11-15.md`。

### 17.1 改动清单（删除即回退）

| 文件 | 变化 | 作用 |
|---|---|---|
| `src/instance.js` | 新增 78 行 | 自治契约单一真源：`crossCloudControl:false` / `resolvePublicBase()` / `wsUrlOf()` / `provisionReadiness()` / 禁用的跨云端键 / `isVendorBase()` / `foreignBaseConfigured()` |
| `src/provision.js` | 199 行，8 处替换 | 去掉 `https://control.245959623.xyz` 默认回落；两个装机 bat 生成器在 `publicBase()` 为空时抛错（fail-closed） |
| `src/class-roles.js` | 新增 76 行 | 六角色 → claims 预设纯函数，**不参与判定**；判定仍是 `perms.js:canAccess()` + `class-admin-shared.js` 成员表 |
| `scripts/check-standalone.mjs` | 新增 84 行 | 独立部署自检：Node 22.5+ / node:sqlite / ws / 本实例地址 / 三把强令牌 / 禁止键；`--strict` 额外拒官方域名与站点依赖 |
| `tests/unit-instance-sovereignty.mjs` | 新增 38 行 / 20 断言 | 地址不回落、ws/wss 派生、禁用键、源码不含官方域名 |
| `tests/unit-class-roles.mjs` | 新增 70 行 / 23 断言 | 六角色形状 + 灌进真 `canAccess()` 验证跨班/跨租户隔离与 `maskDevice` 脱敏 |
| `.env.example` | 55→65 行 | `CLOUD_WS_HOST` + `CLOUD_PUBLIC_BASE` 一等配置；独立部署提示 `STE_SITE_URL` 留空 |
| `package.json` | +2 测试链 + `test:standalone` | 门禁化 |
| `docs/开发文档-班级后台-2026-11-15.md` | 新增 421 行 | API 调用 / 功能进度与使用 / 单独部署三块；七条建议逐条给「本项目落点 + 不做与为什么」 |
| 本地 `.env` | +3 行（**不入库**） | 现役实例显式声明自己的 `CLOUD_PUBLIC_BASE=https://control.245959623.xyz`，保住现役装机包行为 |

### 17.2 行为差异

- **别人单独部署**：旧代码装机 bat 会连回我方官方域名；现在不填自己的 `CLOUD_PUBLIC_BASE`
  直接拒发装机包，绝不猜、绝不回落。我方云端没有跨实例控制通道，别的实例也连不回我方。
- **现役实例**：已在本地 `.env` 显式填回自己的地址，装机包行为逐字不变（只是默认值不再写死在代码里）。
- **六角色**：超级管理员 / 学校管理员 / 班主任 / 任课老师 / 学生 / 家长各自有确定的 claims 形状；
  学生与家长 `scopes` 是**非空数组**（零权限 fail-closed 口径），只有 `platform_admin` 与静态令牌能跨租户。
- **技术栈**：不变。Node 原生 http/ws + `node:sqlite` 继续是现役真源；admin-server 的 PG/Redis 适配层保留。
  NestJS / Svelte / Docker 只在文档里写清「为什么现在不做」，不落第二套代码。

### 17.3 验证证据（命令 + 数字）

- `node tests/unit-instance-sovereignty.mjs` **PASS 20 / FAIL 0**；`node tests/unit-class-roles.mjs` **PASS 23 / FAIL 0**；
  `npm test`（12 套）**EXIT=0**，末尾三套 `PASS=17 FAIL=0` / `PASS 20 / FAIL 0` / `PASS 23 / FAIL 0`。
- `node --check src/{provision,instance,class-roles}.js` 三件全 0；
  `node scripts/check-standalone.mjs --strict` 对现役 `.env` 返回 **exit 1**，唯一原因
  `CLOUD_PUBLIC_BASE 仍指向本项目官方域名；独立部署必须填学校自己的域名` —— 严格判据确实生效。
- 反向验证口径：把 `resolvePublicBase` 的默认值改回官方域名，`unit-instance-sovereignty` 的
  「provision.js 不再硬编码官方域名」与「不配地址 ⇒ 空串」两条断言会同时变红（本批未改回，留作门禁哨兵）。
- 文档事实全部来自逐行读代码：`class-admin.js` 29 个 action、`PUBLISH_TYPES` 4 类、
  `ATTENDANCE_STATUSES` 4 态、`store.js` 指令 4 态、`class-events.js` 四道闸、`REGISTER` 回执字段。

### 17.4 未验证（如实记账）

- 生产 8788 与现役被控端**未重启**（AGENTS §2 铁律 4）；现役进程仍持旧 provision 代码，
  下轮按 `stelarith-deploy` SOP 重启才完全生效。
- 独立部署只跑静态自检 + 纯函数断言，**没有在另一台机器真装一套并连设备**；
  自检不校验 TLS 证书、不探测端口可达性。
- 六角色预设只验到 `canAccess` / `maskDevice`；`class_members` 的写权限未做真 HTTP 集成。
- 成绩 / 家长绑定 / 通知已读 / 作业收缴 / 软删除 **没有**，文档明确标 ⬜，不拿设计稿冒充进度。
- 生产库上没跑过恢复演练；隔离目录的 `scripts/backup-restore-drill.py` 已实跑（见 §17.6）。

### 17.5 七条建议的处置（用户 m30161）

| 条 | 处置 |
|---|---|
| 1 模块化别写死 | 既有 `class-admin*.js` 按对象拆；`classroom-*.js` 是他线 WIP 未入库；代码真源固化，不重写 |
| 2 RBAC 六角色 | 新增 `class-roles.js` 预设 + 23 断言；判定真源不变 |
| 3 三列预留 | `sqlite-schema.sql` 复合主键已含 `school_id`；文档 + 断言固化，不迁移现役数据 |
| 4 安全审计 | 已有 `events.log` / `backup.mjs` / `maskDevice`；软删除与登录表列为待点单 |
| 5 流程不是 Excel 页 | 文档列 6 条状态机；缺的流程直说没有 |
| 6 部署运维 | 文档给 A/B 两档；不装 PG/Redis、不切生产、不假装有对象存储与告警 |
| 7 换主流栈 | **明确不采纳**：现役在生产 + UI 迁移在途 + 两套后端并存 ⇒ 换栈 = 第二套真源 |

### 17.6 代码真源 + 备份收敛（2026-11-15 · 用户 m30305）

| 文件 | 变化 | 作用 |
|---|---|---|
| `src/class-catalog.js` | 新增 | 模块 / 流程 / 进度 / 备份清单唯一真源；不参与运行时判定 |
| `scripts/class-catalog-blocks.mjs` | 新增 | 把五个渲染块输出 JSON 给 Python 同步桥 |
| `scripts/sync-class-doc.py` | 新增 | 文档 `CATALOG:*` 块生成 / `--check` 漂移判据 |
| `tests/unit-class-catalog.mjs` | 新增 | 29 action / 7 组状态词 / 4 个库名 / 备份脚本 / 文档块逐项对账 |
| `scripts/backup-restore-drill.py` | 新增 | 隔离目录造 4 库 + 3 文件 → 快照 → 删库 → 恢复 → 回读 |
| `scripts/backup.mjs` | 改写 | 备份集从真源派生；补 `class-room.db` / `class-dir.db` / `interact.db`；写 manifest |
| `docs/开发文档-班级后台-2026-11-15.md` | 5 个生成块 | 不再手写模块表 / 进度表 / 流程 / action / 备份段 |
| `package.json` | test:unit +1 | 挂上 `unit-class-catalog.mjs` |

验证证据：`node tests/unit-class-catalog.mjs` PASS 62 / FAIL 0；`python scripts/sync-class-doc.py --check` PASS 5 blocks；
`python scripts/backup-restore-drill.py` PASS 34 / FAIL 0；`node scripts/backup.mjs --root <隔离目录>` 六类源全落盘。
全链 13 套单测 EXIT=0；反向验证 4 组（文档漂移 / action 写错 / 备份脚本第二份 SOURCES / 清单漏两库）全部抓到并按字节恢复。
未验证：生产库上没跑恢复、没重启 8788；绝对路径库恢复要人工放回；备份不做加密/异地/定时器。

## 十八、备份加密 + 异地副本 + 定时语义（2026-11-15 · 用户 m30600）

上一节最后一句「备份不做加密/异地/定时器」是本节的起点。补齐方式是**多一个脚本，不改备份集**：
`scripts/backup.mjs` 仍负责从 `src/class-catalog.js` 的 `BACKUP_ITEMS` 出明文快照（单一真源不动），
`scripts/backup-vault.mjs` 只负责把那份快照**加密、打包、投递、清理**。

### 18.1 改动清单

| 文件 | 行数 | 作用 |
|---|---|---|
| `scripts/backup-vault.mjs` | 255 新增 | 加密归档 / 解密还原 / 本地副本 / 可插拔 ESM sink / 保留策略 / 周期语义 |
| `scripts/backup-vault-drill.py` | 新增 | 33 条断言演练（见 18.3） |

**没有改**：`src/class-catalog.js`（备份清单真源）、`scripts/backup.mjs`（明文快照逻辑）、
`src/config.js`（他线 WIP 期间不碰）、任何定时任务注册点。

### 18.2 设计取舍

- **零第三方依赖**：只用 `node:crypto`。密钥派生 `scryptSync(passphrase, salt, 32, {N:16384,r:8,p:1})`，
  加密 `aes-256-gcm` + 随机 12 字节 iv。仓库没装 encryption 库，也不该为一个脚本装。
- **明文可被校验**：`.vault` 的 header 行带 `plainSha256`。GCM 只保护密文（它认证的是 iv+密文），
  **header 不在保护范围内** —— 所以解密后必须再比一次摘要，这条单独有断言守着（反向验证 C 组就靠它变红）。
- **密钥不落地**：只读 `BACKUP_VAULT_KEY` 或 `--passphrase-file`。**没有默认口令**，缺了就 exit != 0。
  代码里不打印口令、不写进 header、不写日志。
- **异地是接口不是实现**：`--remote-dir` 是本地另一个目录，`--sink <esm>` 动态 import 一个模块调
  `put({localPath, name})`。仓库不认任何云厂商 SDK；接 S3/OSS 是学校自己写 15 行 sink 的事。
- **定时只演练语义**：`--schedule --cycles N --interval-ms M` 在进程内串行跑 N 个周期，进程退出即停。
  删掉一个周期就停，不需要清理任何注册项 —— **故意不注册** Windows 计划任务 / PM2 cron / systemd timer。

### 18.3 验证证据

`python scripts/backup-vault-drill.py` → **VAULT PASS 33 / FAIL 0**，覆盖：

- 与明文链接得上（`backup.mjs --root` 出的快照直接当 `--source`）；
- 加密后 `.vault` 里搜不到源文件里的明文哨兵、搜不到口令；
- 解密后**文件集等于快照文件集**、4 个 SQLite 库逐个 `SELECT` 回读 sentinel、流水文件字节一致；
- 三种失败：错口令 / 篡改密文末字节 / **改 header 的 plainSha256**，全部 exit != 0 且不产出文件；
- 缺口令时明确拒绝，且错误文案里能看出「不提供默认口令」；
- `--sink` 模块真被调用并写进另一个目录；
- `--schedule --cycles 3 --keep 2` 后 vault-dir 与 remote-dir **各只剩 2 份**。

反向验证：把 `plainSha256` 校验那一行注释掉 → 同一条演练变 **30 / FAIL 3**（三条断言全红），
`try/finally` 按字节恢复后回到 33/0。

### 18.4 未验证

- **生产库上没跑**：演练全在 `D:/Stelarith/_demo/_vault_drill` 的隔离目录，现役库一个字节没动。
- **没有真对象存储**：`--sink` 只验了「模块被调用」，没接过 S3/OSS/MinIO。
- **没有真定时器**：周期语义是进程内 `setTimeout` 串行，崩了就停；生产要活定时得靠计划任务。
- **口令轮换没有**：改口令后旧 `.vault` 只能用旧口令解，没有 re-encrypt 子命令。

## 十九、单一更新渠道 + 咸林中学校区 + 教师扫码网关（2026-11-16 · 用户 m33430 / m33515）

### 1. 取消灰度

- 用户裁决「合并被控端更新渠道 / 取消灰度」。`src/ota.js` 新增 `OTA_CHANNELS_UNIFIED=true` 与 `unifiedChannel()`；`manifestFileFor(channel)` 永远返回 `OTA_MANIFEST_FILE`，`alpha/gray/beta/rc/../secret` 都不再参与文件路径拼接。
- `latestFor()` 从 `ota-builds.json` 按**版本序**挑最新，不再等 5h/24h 分档；旧 `ota-.json` 文件保留但不读。`promotionStatus()` 保留旧字段名但全部置为统一正式渠道，避免旧管理端解析崩。
- 被控端 `channelCanonical()` 把所有旧档归一到 `stable`；设置页只给一个「正式」按钮；旧 `promotion` 快照不再挡升级。
- 行业依据：[Martin Fowler · Canary Release](<https://martinfowler.com/bliki/CanaryRelease.html>) 把灰度定义为「逐步放量降低风险」；本项目现在选择**统一渠道 + 发版前验证**，不再让设备按档位拿不同版本。

### 2. 咸林中学校区与 12 班课表

- 新增 `src/campus-registry.js`：登记「渭南市华州区咸林中学校区」，返回校区服务器 `publicBase / agentWsUrl / viewerWsUrl`，班级可**选择校区服务器**并拿到装机信封；对外地址严格 `协议://主机[:端口]`，不收路径/查询。
- 新增 `src/campus-server.mjs`：隔离端口 `CAMPUS_PORT`（默认 18099）提供 `/api/campus/servers`、`/api/campus/classes/choose`、`/api/campus/teacher-gateway`、`/api/campus/teacher-gate`；不碰现役 8788、不写生产库。
- 新增 `scripts/init-xianlin-campus.mjs`：用 `D:/Stelarith/_backup/classisland-2026-10-06/instance2/Profiles/Default.json`（190081B / 24 groups / 72 plans / 4 TimeLayouts / 23 Subjects）在隔离目录建租户、注册 12 班、按 `ClassPlans` 首次出现序归一化 12 份课表，并模拟 12 台设备各自 `GET /api/device/timetable` 拉本班课表。
- 实测：`INIT EXIT=0 / groups=12 / plans=72 / classes=12 / devicePullable=12`。反向验证：用只有 2 个群的 `D:/Classlsland/data/Profiles/Default.json` 跑，明确报 `档案不是 12 班全校课表：groups=2` 并退出 1。

### 3. 端侧解耦 / 教师扫码网关

- 新增 `src/teacher-gateway.js`：白名单只有 `my-classes / timetable / timetable-delivery / class-scan-bind`；`teacherScanPlan()` 只产出 `channel:"class-scan"` 的最小计划，明确 `requireManage:false / allowCreateClass:false / allowBatch:false`。
- 绝对不复制判权：入口层仍必须调用既有唯一核心 `classScanBind(ticket, uid, devices)`；本网关不 import registry / bind-api，避免第二套绑定真源。
- `StudentClassPicker.qml` 文案改为「本机可独立运行；老师扫码只是绑定班级」「断网可用」「稍后再说」，不再写成「之后由老师统一管理」。
- 权限依据：[NIST CSRC · least privilege](<https://csrc.nist.gov/glossary/term/least_privilege>)；教师扫码渠道不提供超级管理员、批量、建班、删除班、导入名单/考勤等动作。

### 4. 验证与边界

- `tests/unit-ota-unified.mjs` 20/0；`tests/unit-ota-promote.mjs` 56/0；`tests/unit-version-compare.mjs` 41/0；`tests/unit-campus-teacher.mjs` 14/0；`tests/unit-campus-server.mjs` 12/0。已接入 `npm test`。
- 本批**没有**生产推送、没有改 `assets/pkg`、没有动 OTA 清单、没有重启 8788 / 8090 / 在线被控端。用户点名 v0.6.70，但 HEAD 两端 CMakeLists 是 0.6.72 且 `assets/pkg` 无稳定 0.6.70；生产发版版本仍待用户裁决。
- 未验证：干净生产库上真注册 12 班没跑（只在隔离目录跑）；校区服务没接进 `src/index.js`（那是他线 WIP，按 AGENTS §2 不碰）；12 台真设备没实连。
