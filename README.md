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
  均指向本目录的 `run-cloud.cmd` / `cloud-watchdog.cmd`（`node_modules` / `.env` / `assets/pkg` 都在这）。
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

## 五、UDP 闪传通道设计（2026-10-20 定稿 · 用户点单）

> 动机：① 文件推送是停等协议（64KB/片、一片一回执、无重传，15s 看门狗直接判死）；
> ② 远控 RFB 走 WS `vnc-data` 被 200KB/s 背压闸吞掉全屏首帧 ⇒ viewer 全白；
> ③ cloudflared 隧道**只转 TCP**，公网 UDP 不可达 ⇒ 通道必须双层。

### 5.1 拓扑事实（2026-10-20 实测）

- 三端当前同机：8788 上连接全是 `127.0.0.1`（viewer / agent / cloud 同进程机）。
- UDP 落地形态：**loopback / 局域网直连中转**（端口 `8789/udp`）；
  公网场景用 **UDP-over-WS 封装**回退（复用已鉴权 WS 二进制通道，仍绕开指令路径背压）。

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

### 5.4 可靠性（UDP 不可靠，应用层补）

- 序号 `seq`（每会话单调）+ **累计 ACK + SACK 位图**（相对 ack_seq 的 64 位图）。
- 选择性重传：只补丢失片，不重传已确认片（修掉停等协议"丢一片判死整个文件"）。
- **FEC**：每 8 数据片 1 个 XOR 奇偶片（`PARITY`），抗 ≤12.5% 突发丢包零重传。
- 乱序接收：接收端 `next` 指针 + 待写缓冲（上限 64 片），按序刷盘/上屏。

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

1. **先远控 RFB**（`vnc-data` 二进制帧搬 UDP，绕开 200KB/s WS 背压 ⇒ 解首帧全白）；
2. **再文件传输**（`file_push/chunk/done` 走 UDP 滑动窗口）；
3. JPEG / 音频可选 UDP+FEC（低延迟模式，允许丢帧）。
- 兼容：协商失败（旧端无 UDP 字段）**自动回落 WS 既有路径**，旧端零改动可用。
- 令牌红线：`CLOUD_WS_TOKEN` / `CLOUD_VIEWER_TOKEN` **不随改名动**（客户端成对，改名只动目录）。
