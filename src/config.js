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

// 最近一帧只留在内存里给管理端拉（D4 会用），不落盘：截图属于机房敏感数据。
export const KEEP_LAST_FRAME = true;

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
