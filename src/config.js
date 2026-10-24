// 云端最小配置。纪律：任何取值都得能跑起来，不许出现"留空就死"。
export const PORT = Number(process.env.CLOUD_WS_PORT || 8788);

/* 监听地址（2026-10-05 · 公网上线改造）：
 * 默认 0.0.0.0 —— 公网/局域网可达，被控端与移动管理端才能从别的机器连进来。
 * 要"只在本机联调"时设 CLOUD_WS_HOST=127.0.0.1 收回到回环（旧行为）。
 * ⚠️ 暴露到公网前**必须先**把 CLOUD_WS_TOKEN / CLOUD_VIEWER_TOKEN 换成生产强随机值：
 *    0.0.0.0 + 内置开发令牌 = 任何人可发指令、可读教室机实时画面。
 * 公网部署建议再叠一层 TLS 反代（wss://），云端自身只跑明文 ws。 */
export const HOST = (process.env.CLOUD_WS_HOST || '0.0.0.0').trim();

// 设备接入令牌：生产环境由站点下发；本机联调用内置开发令牌，启动会显式警告。
export const DEV_TOKEN = process.env.CLOUD_WS_TOKEN || 'dev-cloud-token';

export const DEV_TOKEN_IS_DEFAULT = !process.env.CLOUD_WS_TOKEN;

/* ---------- 管理端（viewer）接入鉴权 ----------
 * 两条路，任一条通就能进：
 *   ① 长期令牌 CLOUD_VIEWER_TOKEN —— 给桌面管理端（Qt viewer）这类能守住密钥的客户端；
 *   ② 短票据（HMAC，默认 2 分钟）—— 给浏览器：站点登录后在服务端现签，浏览器只拿到票，
 *      票抄走了也很快失效，不会像长期令牌那样"一泄露就永久泄露"。
 * 没配 CLOUD_VIEWER_TOKEN 时回落到开发令牌并显式告警（不许"没配就整条通道死掉"）。
 */
export const VIEWER_TOKEN = process.env.CLOUD_VIEWER_TOKEN || DEV_TOKEN;

export const VIEWER_TOKEN_IS_DEFAULT = !process.env.CLOUD_VIEWER_TOKEN;

// 票据签名密钥：站点与云端共用。没配 → 票据通道关闭（长期令牌仍可用），启动时告警。
export const VIEWER_SECRET = process.env.CLOUD_VIEWER_SECRET || '';

export const VIEWER_TICKET_TTL_MS = Number(process.env.CLOUD_VIEWER_TICKET_TTL_MS || 120000);

/* ---------- 云端管理台 /admin 走网站账号登录（2026-10-06 · 一户通）----------
 * 三件配齐 = /admin 的登录页多出「用网站账号登录」；缺任一 = 还是手敲静态令牌
 * CLOUD_VIEWER_TOKEN（老机器、离线机房照旧能用，这条是向后兼容不是新版强制）。
 *
 * 站点侧**零改动**：复用已登记的 loopback 客户端 xingjikong_native ——
 * 站点的 validateRedirectUri 对 loopback 只要求 http + 127.0.0.1 + 路径以 /oauth-callback
 * 结尾、端口任意（见站点 src/lib/server/oauth-clients.ts:257）。所以云端自己在
 * 127.0.0.1 上开个临时端口当回跳出口就行，不必给后台再登记一个客户端。
 *
 * 密钥与站点的 OAUTH_DESKTOP_SECRET 是同一个值（客户端 xingjikong_native 的密钥），
 * 两边各存一份：站点在 .env 里，云端在 STE_OAUTH_CLIENT_SECRET。改了一边要记得改另一边，
 * 否则换完 code 会 401 invalid_client —— 失败信息里我们只说"客户端校验不通过"，
 * 真实原因打在站点日志（[oauth/token]）。 */
export const SITE_URL = (process.env.STE_SITE_URL || '').trim();
export const OAUTH_CLIENT_ID = (process.env.STE_OAUTH_CLIENT_ID || 'xingjikong_native').trim();
export const OAUTH_CLIENT_SECRET = process.env.STE_OAUTH_CLIENT_SECRET || '';
export const OAUTH_REDIRECT_PATH = (process.env.STE_OAUTH_REDIRECT_PATH || '/oauth-callback').trim();

/** 后台票据有效期：与桌面端一户通同一口径（30 天），可按需调，但硬顶 30 天。 */
export const ADMIN_TICKET_TTL_MS = Number(
  process.env.CLOUD_ADMIN_TICKET_TTL_MS || 30 * 24 * 60 * 60 * 1000);
export const ADMIN_TICKET_TTL_MAX_MS = 30 * 24 * 60 * 60 * 1000;

export const ADMIN_OAUTH_ENABLED = !!SITE_URL && !!OAUTH_CLIENT_SECRET && !!OAUTH_CLIENT_ID;

// 握手（连上后必须在此期间内完成 register / auth，否则踢掉）——不做的话，
// 没通过鉴权的连接会挂在那儿白占资源，且面板看着像"在线"。
export const HANDSHAKE_TIMEOUT_MS = Number(process.env.CLOUD_HANDSHAKE_TIMEOUT_MS || 8000);

// 心跳超时阈值：超过这么久既没心跳也没帧 → 判定离线（PRD FR-STA-01：心跳 10s、30s 判离线）。
// 为什么必须扫：机器掉电/拔网线时 TCP 不会立刻关闭，只靠 close 事件会让设备"永远在线"。
export const HEARTBEAT_TIMEOUT_MS = Number(process.env.CLOUD_HEARTBEAT_TIMEOUT_MS || 30000);

// 建议给设备的心跳间隔：由云端在 registered 里**下发**，不再让两端各猜一个数字（协议 v1）。
export const HEARTBEAT_INTERVAL_MS = Number(process.env.CLOUD_HEARTBEAT_INTERVAL_MS || 10000);

// 离线清扫周期（多久检查一次有没有僵死设备）
export const SWEEP_INTERVAL_MS = Number(process.env.CLOUD_SWEEP_INTERVAL_MS || 5000);

export const EV_LIMIT = Number(process.env.CLOUD_EV_LIMIT || 200);

// 最近一帧缓存：2026-10-11 用户裁决「不要云端缓存啊，这又不是监控」——
// 云端不存帧（画面走实时转发 broadcastFrame，不是拉缓存）。置 false 后
// addFrame 清 recentFrame，/api/frame 无缓存帧可拉（实时画面不受影响）。
export const KEEP_LAST_FRAME = false;

// 协议模式（Phase 1 · 协议 v1 的过渡开关）：
//   v1-compat（默认）：认 v1 信封，也认旧扁平格式；对端发什么就回什么格式 —— 不破坏现有两端
//   v1-strict        ：只认 v1（两端都切完之后开，旧客户端会被**明确拒绝**而不是静默）
//   legacy           ：只认旧格式（回滚兜底）
export const PROTOCOL_MODE = (process.env.CLOUD_PROTOCOL_MODE || 'v1-compat').trim();

// 事件流水落盘文件
export const EVENTS_FILE = process.env.CLOUD_EVENTS_FILE || 'events.log';

/* 事件日志轮转（2026-10-06 · 占用优化）：
 * 之前 events.log 只 append、从不轮转 —— 教室机跑一学期能写几百兆，盘吃满了
 * 云端连写文件都失败，排障日志反而成了放坏东西的地方。
 * 现在按体积轮转：超过 CLOUD_EVENTS_MAX_BYTES 就改名归档 + 重建当前文件，
 * 归档只留最近 CLOUD_EVENTS_KEEP_FILES 个，更老的删掉。
 * 归档名带时间戳且全 ASCII —— 这台机器上非 ASCII 文件名曾把云端整个打挂过
 * （ERR_INVALID_CHAR，见 index.js 的 asciiHeaderName 注释），别再踩同一个坑。 */
export const EVENTS_MAX_BYTES = Number(process.env.CLOUD_EVENTS_MAX_BYTES || 10 * 1024 * 1024);
export const EVENTS_KEEP_FILES = Number(process.env.CLOUD_EVENTS_KEEP_FILES || 3);

/* ---------- 指令队列落盘（P0：设备离线一次不能永久漏掉那条指令）----------
 * 只解决"已下发但没等到回执"这一种丢失：那种指令落盘 → 云端重启不丢 → 设备重连按原顺序补发。
 * 设备**不在线**时下发的指令不进队列（离线一律仍返回 409，绝不把离线当成功）。
 * 用 Node 内置的 node:sqlite（本机 v22.22.2 与生产运行的 v24.14.0 均实测可用，零新增依赖）。
 */
export const QUEUE_DB_FILE = process.env.CLOUD_QUEUE_DB_FILE || 'instruction-queue.db';

/* 班级目录落盘（2026-10-20 · 班级身份统一）
 * 班级码是设备归属、权限判定（perms.canAccess）、按班下发的**唯一键**，必须跨重启稳定。
 * 在这之前它只活在 registry.js 的内存 Map 里 ⇒ 云端一重启，班级名与归属全丢，
 * 再绑一次就派生出新码（"同一个班长出三套码"的根因之一）。
 */
export const CLASS_DIR_DB_FILE = process.env.CLOUD_CLASS_DIR_DB_FILE || 'class-dir.db';

/* 班级房间落盘（2026-10-24 · 持久房间）
 * 「每班一个永不停息的子服务器」的落地形态：班级 = 云端一个持久房间，
 * 消息历史与成员表都在这里（class-room.js），重启不丢。
 * 为什么不塞进 class-dir.db：班级**身份**与班级**内容**生命周期不同——
 * 身份要按外部体系码对齐、内容要按条数裁剪，混一库会让两边的迁移互相牵制。
 */
export const CLASS_ROOM_DB_FILE = process.env.CLOUD_CLASS_ROOM_DB_FILE || 'class-room.db';

// 房间历史热缓存保留的班数上限。3000 班 × 200 条全塞内存 ≈ 120MB（2GB 云机扛不住），
// 所以只缓存"最近被读/写过"的班，超出按 LRU 淘汰；未命中的班从库懒加载。
export const CLASS_ROOM_CACHE_CLASSES = Number(process.env.CLOUD_CLASS_ROOM_CACHE_CLASSES || 64);

/* 环境白名单（2026-10-24 · 内网只属于本地调试）
 * test 设备 = 云端维护的 uid 清单；不在清单 = 生产设备（学校设备）。
 * 设备自报 env 一律忽略（与班级归属同一信任模型）。文件即持久，重启不丢。
 */
export const TEST_WHITELIST_FILE = process.env.CLOUD_TEST_WHITELIST_FILE || 'test-whitelist.json';

/* 管理端重认证（2026-10-24 · 静默续期 + 定期弹窗）
 * 人（账号）上次通过验证后，超过此间隔的**敏感操作**拒绝执行（回 REAUTH_REQUIRED），
 * 但连接不断（"一直连接"红线）。静态令牌（系统身份）不适用。
 * 8h = 一整天两段课，弹窗频率不至于烦；敏感操作清单见 index.js reauth 分支。
 */
export const REAUTH_INTERVAL_MS = Number(process.env.STE_REAUTH_INTERVAL_MS || 8 * 3600 * 1000);

/* ---------- 班级域事件通道（2026-10-23 · #458 班级工作台地基）----------
 * class-events.js 的四个闸门。设计动机见该文件头：班级域与 devices 广播、
 * pushEvent 流水、按设备订阅中继三条通道正交，专服务"按班"的订阅/推送/历史。
 * 全部可按需调，缺省值即"任何取值都跑得起来"（纪律：不许留空就死）。 */
// 每班环形事件缓冲条数：Timeline 只看最近几十条，200 覆盖一节课的全部事件量级；
// 无限增长会把云端内存吃穿（噪音样本 2s/条的前车之鉴）。
export const CLASS_EVENT_LOG_KEEP = Number(process.env.CLOUD_CLASS_EVENT_LOG_KEEP || 200);

// 订阅时快照带回的事件条数：后订者先看最近 20 条历史，再收实时。
export const CLASS_EVENT_SNAPSHOT = Number(process.env.CLOUD_CLASS_EVENT_SNAPSHOT || 20);

// 每 uid×kind 最小投递间隔（ms）：为 #459 缩略图（每台约 1s 一张）预置的闸门。
// 现在没有高频发送方也开着——先立规矩，等负载真来了不用改协议。
export const CLASS_EVENT_MIN_MS = Number(process.env.CLOUD_CLASS_EVENT_MIN_MS || 1000);

// 单条事件 data 的序列化体积上限（字节）：64KB 够一张小缩略图的 base64 之外的
// 一切元数据；超了说明发送方有问题，拒收并如实回错（不静默丢弃）。
export const CLASS_EVENT_MAX_BYTES = Number(process.env.CLOUD_CLASS_EVENT_MAX_BYTES || 64 * 1024);

// 班级事件 fanout 的背压阈值：订阅的管理端消费不过来（bufferedAmount 超限）时
// 跳过本条，与 registry 的 RELAY_BUFFER_MAX 同口径（事件类数据丢一条远好过
// 把心跳/指令一起堵死）。
export const CLASS_RELAY_BUFFER_MAX = Number(
  process.env.CLOUD_CLASS_RELAY_BUFFER_MAX || 8 * 1024 * 1024);

/* 站点基址（用于把站点签发的「接入码 XJK-…」兑换成班级身份）。
 * 云端是设备侧的唯一入口；站点继续持有它自己的班级库，由云端代为兑换并归一，
 * 设备端因此**只需要认识一个地址**。
 * 留空 = 不启用兑换（此时 XJK 码会被明确拒绝，不静默）。
 */
export const SITE_BASE_URL = (process.env.STE_SITE_URL || 'https://www.245959623.xyz').replace(/\/+$/, '');
export const SITE_ACTIVATE_TIMEOUT_MS = Number(process.env.STE_SITE_ACTIVATE_TIMEOUT_MS || 6000);

// 未回执指令的存活上限（TTL）：超过就作废、不再补发。
// 取 24 小时的理由：覆盖单次断电/断网/夜间维护这类"短暂离线"；再久的指令多半已经失效甚至有害
// ——比如一条"关机/锁屏"隔了一两天、设备一重连就突然执行，那比丢掉更糟。宁可判过期，也不隔夜补刀。
export const PENDING_TTL_MS = Number(process.env.CLOUD_PENDING_TTL_MS || 24 * 60 * 60 * 1000);

// 单台设备"未回执"指令条数上限：超了丢最旧的那几条（标记 expired，不静默）。
// 取 50 的理由：一个班一节课的下发量级是个位数，50 足够覆盖一次离线期的正常指令；
// 又能挡住"面板被刷 / 云端出 bug"把队列撑爆——每台无限增长会让重连瞬间批量执行上百条指令。
export const PENDING_MAX_PER_DEVICE = Number(process.env.CLOUD_PENDING_MAX_PER_DEVICE || 50);

// 已回执（done/failed/expired）行在队列表里的保留期：只是短时审计追溯，过期即删。
// 取 24 小时的理由：完整留痕已经在 events.log 里，队列表没必要长期留；删掉避免 DB 无限增大。
export const SETTLED_RETENTION_MS = Number(process.env.CLOUD_SETTLED_RETENTION_MS || 24 * 60 * 60 * 1000);

// 队列清扫周期：定期把"过期未回执 / 超上限 / 已回执超保留期"的行处理掉。
export const QUEUE_SWEEP_INTERVAL_MS = Number(process.env.CLOUD_QUEUE_SWEEP_INTERVAL_MS || 5 * 60 * 1000);

/* ---------- 互动数据落盘（2026-10-15 · 灵动岛扩展组件6）----------
 * 点歌（song_requests）/ 反馈点赞（feedback）/ 祝福弹幕（danmaku）三张表。
 * 为什么单独一个库文件而不并进 instruction-queue.db：关注点与保留策略都不同——
 * 指令队列是"运维管道"（24h 强 TTL、每台上限、回执即删），互动数据是"内容审核流"
 * （pending 永不扫、已处理行留 7 天供追溯、按班汇总）。混在一个库里，两边清扫
 * 策略互相踩（sweepQueue 的 SETTLED_RETENTION_MS 会把待审的祝福当垃圾删掉）。
 * 同样用 Node 内置 node:sqlite，零新增依赖（与 store.js 同理由）。
 */
export const INTERACT_DB_FILE = process.env.CLOUD_INTERACT_DB_FILE || 'interact.db';

// 已处理（approved/rejected/played）行的保留期：只是短时审计追溯，过期即删。
// 取 7 天：覆盖"这周谁点了什么歌、哪些祝福被拦"的日常核查；更早的没有意义。
export const INTERACT_RETENTION_MS = Number(process.env.CLOUD_INTERACT_RETENTION_MS || 7 * 24 * 60 * 60 * 1000);

// feedback 行保留期：汇总口径按天/按周看，留 30 天够月度对比。
export const INTERACT_FEEDBACK_RETENTION_MS = Number(
  process.env.CLOUD_INTERACT_FEEDBACK_RETENTION_MS || 30 * 24 * 60 * 60 * 1000);

// 单台设备每小时上报上限（云端闸门，与端侧闸门并存 = 深度防御）：
// 端侧有 30s 发送冷却（祝福≈120/h）与点歌配额 3，但端侧可被改包绕过，
// 云端必须自己能兜底。取 60/20 的理由：正常课堂使用远低于此（一个班一节课
// 点歌个位数、祝福十几条），超了的一律拒收并如实回错，不静默丢弃。
export const INTERACT_SONG_PER_HOUR = Number(process.env.CLOUD_INTERACT_SONG_PER_HOUR || 60);
export const INTERACT_DANMAKU_PER_HOUR = Number(process.env.CLOUD_INTERACT_DANMAKU_PER_HOUR || 20);
export const INTERACT_FEEDBACK_PER_HOUR = Number(process.env.CLOUD_INTERACT_FEEDBACK_PER_HOUR || 120);

// 互动数据清扫周期（只扫已处理行，pending 永不扫——那是待审批的内容，不是垃圾）。
export const INTERACT_SWEEP_INTERVAL_MS = Number(process.env.CLOUD_INTERACT_SWEEP_INTERVAL_MS || 10 * 60 * 1000);

/* ---------- OTA 版本清单（2026-10-05 · OTA 本期落地）----------
 * 云端按此文件声明"各产品最新版本 + 安装包 URL + sha256"；每次请求现读，改完即生效。
 * 文件缺失即"尚未发布过任何版本"（latestFor 返回 null，接口如实回 null，不谎报最新）。 */
export const OTA_MANIFEST_FILE = process.env.CLOUD_OTA_MANIFEST_FILE || 'ota.json';

/* ---------- 装机资产（2026-10-05 · 面板化装机）----------
 * assets/ 目录由云端原样托管 via /assets/<相对路径>，专门给教室机取安装包和装机脚本用。
 * 里面不许出现任何秘密（.env 在仓库根，天然不在这个目录下）——
 * 白名单只放 exe/msi/ps1/bat/zip/json/txt/sha256，见 provision.js 的 ASSET_ALLOW_EXT。
 * 子目录：assets/pkg/（教室机安装包 .exe）、assets/provision/（装机脚本 .ps1）。 */
export const ASSETS_DIR = process.env.CLOUD_ASSETS_DIR || 'assets';

/* ---------- UDP 闪传通道（2026-10-20 · U1 骨架，用户点单「新增 udp 协议快速加密传输」）----------
 * 形态：云端在 UDP_RELAY_PORT 上开帧中继（dgram），两端（被控端/管理端）直连。
 *   可达范围：回环 + 局域网直连。⚠️ cloudflared 隧道只转 TCP，公网 UDP 不可达 ——
 *   2026-10-20 用户定「那就 tcp」后，**公网主路改为 TCP 专用连接**（见下节），
 *   UDP 中继保留作回环/局域网加速项（同一套帧与加密，flash-core.js 共用）。
 * 加密：ChaCha20-Poly1305 端到端（RFC 7539），密钥经**已鉴权的 WS 通道**协商
 *   （服务端随机 32B，不固化二进制）；UDP 帧头明文（中继要按 session 路由），
 *   payload + 16B tag 加密，AAD = 整个帧头。nonce = session(4B) + seq(8B)，单调不重用。
 * 鉴权：HELLO 帧 payload 内带 UDP_RELAY_TOKEN（同样过 AEAD），中继解密后 timingSafeEqual 校验。
 * ⚠️ 令牌未配（UDP_RELAY_TOKEN 空）= 通道**不启动**并告警 —— 新攻击面不许 fail-open。
 *   默认 0.0.0.0 的理由同 CLOUD_WS_HOST：教室机常在另一台机器，局域网直连要用；
 *   只在本机联调就设 UDP_RELAY_HOST=127.0.0.1 收回回环。 */
export const UDP_RELAY_PORT = Number(process.env.UDP_RELAY_PORT || 8789);
export const UDP_RELAY_HOST = (process.env.UDP_RELAY_HOST || '0.0.0.0').trim();
export const UDP_RELAY_TOKEN = (process.env.UDP_RELAY_TOKEN || '').trim();

// 单会话帧率闸门（防一只端把中继打爆）：正常远控 30fps×2 端 + 文件分片远低于此。
export const UDP_MAX_FPS_PER_SESSION = Number(process.env.UDP_MAX_FPS_PER_SESSION || 2000);

// 会话空闲多久无帧即收口（秒）：远控停了/端掉线，中继侧不能永远挂着 peer 地址。
export const UDP_SESSION_IDLE_MS = Number(process.env.UDP_SESSION_IDLE_MS || 60000);

/* ---------- TCP 闪传通道（2026-10-20 · 公网主路，用户定「那就 tcp」）----------
 * 形态：云端在 TCP_RELAY_PORT 上开帧中继（node:net），两端各拉一条**专用 TCP 连接**，
 *   经 WS「tcp-session」消息协商同一把会话密钥（flash-core.js 与 UDP 完全同一套帧/AEAD）。
 * 为什么 TCP 是公网主路（而不是 UDP-over-WS 封装那类回退）：
 *   ① cloudflared 隧道只转 TCP，公网 UDP 不可达——这是硬约束；
 *   ② 专用连接把「实时/大批数据」与「WS 控制+心跳」彻底分离：被控端 200KB/s 背压闸
 *      （保心跳的粗暴手段，却把远控全屏首帧丢光 ⇒ viewer 全白）和文件传输停等协议
 *      （64KB 一片一回执）的病根一起去掉；
 *   ③ TCP 自有流控+有序可靠，中继侧反而更简单：严格单调 seq（重放即拒，兼守 nonce
 *      不重用）、背压时暂停源读取（不丢帧）、一端断即收口会话。
 * 公网落地：cloudflared ingress 加一条主机名 → tcp://127.0.0.1:8790（客户端 TLS 到边缘，
 *   隧道内明文；帧本身已有 AEAD，双保险）。回环/局域网联调可直连 127.0.0.1:8790。
 * 鉴权/安全面同 UDP：HELLO 带 TCP_RELAY_TOKEN；空 = 不启动（fail-closed）；
 *   久不 HELLO 的连接按宽限断开；连接数封顶；会话空闲收口。 */
export const TCP_RELAY_PORT = Number(process.env.TCP_RELAY_PORT || 8790);
export const TCP_RELAY_HOST = (process.env.TCP_RELAY_HOST || '0.0.0.0').trim();
export const TCP_RELAY_TOKEN = (process.env.TCP_RELAY_TOKEN || '').trim();

// 会话空闲多久无帧即收口（ms）：与 UDP 同值。远控有帧流自然不会收；停了就收，防半死会话。
export const TCP_SESSION_IDLE_MS = Number(process.env.TCP_SESSION_IDLE_MS || 60000);

// 连上多久不发 HELLO 即断开（ms）：防裸连接堆积（扫描/探测类流量）。
export const TCP_HELLO_GRACE_MS = Number(process.env.TCP_HELLO_GRACE_MS || 15000);

/* ---------- 广播社成员（2026-10-23 · 管理端独立身份）----------
 * 账号体系自包含在云端（用户名+scrypt 哈希，落 club-members.json），不依赖站点 OAuth、不碰 VoiceHub 后端。
 * 排期读写经云端代理到 VoiceHub：云端持有写密钥（下面两项），成员令牌只用于云端鉴权，密钥绝不下发。
 * 联调期 STE_VOICEHUB_WRITE_BASE 指向本机开发夹具（http://127.0.0.1:3000）即可端到端跑通。 */
export const VOICEHUB_WRITE_BASE = (process.env.STE_VOICEHUB_WRITE_BASE || '').trim();
export const VOICEHUB_WRITE_KEY = process.env.STE_VOICEHUB_WRITE_KEY || '';
export const CLUB_MEMBERS_FILE = process.env.CLOUD_CLUB_MEMBERS_FILE || 'club-members.json';
export const CLUB_AUDIT_FILE = process.env.CLOUD_CLUB_AUDIT_FILE || 'broadcast-club-audit.log';
export const CLUB_TOKEN_TTL_MS = Number(process.env.CLOUD_CLUB_TOKEN_TTL_MS || 12 * 60 * 60 * 1000);
