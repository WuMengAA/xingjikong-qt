// 云端最小配置。纪律：任何取值都得能跑起来，不许出现"留空就死"。
export const PORT = Number(process.env.CLOUD_WS_PORT || 8788);

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
