// 星集控 · 管理端后端（ViewerBackend）
//
// 为什么单独抽出来：management 端原来把「网络/协议/数据」和「界面」揉在同一个 main.cpp 里（约 750 行），
// 改样式要碰逻辑、改逻辑怕碰 UI。界面要换 Qt Quick / QML（见《星集控-电脑端UI定稿方案-2026-10-03.md》），
// 所以先把逻辑抽成**与界面无关**的 QObject —— QML 与 QWidget 都能直接用同一份。
//
// 职责边界（只做这些，别往里塞界面的事）：
//   连接 / 鉴权 / 重连退避 / 设备表 / 画面帧 / 指令下发 / 回执 / 错误
// 界面层的：弹窗、布局、状态栏文案拼装、存现场图 —— 都留在界面侧。

#pragma once

/**
 * 站点首页地址（OAuth 授权页 / 取接入票都长在这台站点上）。
 *
 * 单一真源：ViewerBackend 与 OAuthLogin 都从这里取默认值 ——
 * 两处各写一个字面量的话，改站点域名时必漏一处，界面上就表现成
 * 「登录按钮没反应」和「取票 404」同时出现，很难归因。
 *
 * 为什么要有这个默认值：管理端是发给老师机装的用法，让对方去解压包再手填
 * STE_SITE_URL 就是"半自动"。站点地址这是**公开信息**（下载中心本来就写着），
 * 不该由用户配置。真正需要保密的东西（客户端密钥）反而不在这里 ——
 * loopback 原生客户端按 RFC 8252 走公开客户端，压根不需要密钥。
 */
static constexpr const char *kDefaultSiteUrl = "https://www.245959623.xyz";

#include <QFile>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// ── 文件推送共享符号（2026-10-13 架构拆分）──
// viewerbackend.cpp / viewerbackend_file.cpp 两个翻译单元共享：常量用 constexpr
// （内联无 ODR 问题），humanBytes 用 inline。别改成非 inline 的非 constexpr 版本
// ——那会在两个 .cpp 里产生重复符号。
/**
 * 文件分片大小（为什么 64KB）：被控端 file_chunk 的 data 是 base64，大文件整发＝一条
 * 几 MB 的 JSON；一片一回报，卡在第几片能直接看出来。见 viewerbackend.cpp 顶部注释。
 */
constexpr qint64 kFileChunkBytes = 64 * 1024;
/** 等多久算"教室机没应答"（毫秒）。一片一回报，超时判对面掉线并删半截文件。 */
constexpr int kPushAckTimeoutMs = 15000;
/** 体积的人话写法（1024 进制，只到 MB）。进度条旁边那行要给人看。 */
inline QString humanBytes(qint64 n)
{
    if (n < 1024) return QString::number(n) + QStringLiteral(" B");
    if (n < 1024 * 1024)
        return QString::number((double)n / 1024.0, 'f', 1) + QStringLiteral(" KB");
    return QString::number((double)n / 1024.0 / 1024.0, 'f', 1) + QStringLiteral(" MB");
}
/**
 * 把时长上限说成人话：≥1 分钟按分钟、不足 1 分钟按秒（压测上限只有几秒，别显示"0 分钟"）。
 * 2026-10-13 架构拆分：从 viewerbackend.cpp 的 static 提升为 inline（语音/广播域新 TU 也要用）。
 */
inline QString fmtLimit(int ms)
{
    if (ms >= 60000) return QStringLiteral("%1 分钟").arg(ms / 60000);
    return QStringLiteral("%1 秒").arg(qMax(1, ms / 1000));
}
/** 读时长上限环境变量（毫秒）：只在 0 < v ≤ 1 小时时才认，写错按默认走（不因此开不起来）。 */
inline int envMaxMs(const char *key, int fallback)
{
    const qint64 v = qEnvironmentVariableIntValue(key);
    return (v > 0 && v <= 3600 * 1000) ? static_cast<int>(v) : fallback;
}

class AudioCapture; // 语音对讲（2026-10-07）：管理端采集（前向声明，避免引入 mmsystem）
class AudioPlayback; // B6：管理端播放（学生回传的 audio/pcm 帧）
class VncClient;         // 远控 · 管理端 VNC 客户端（libvncclient，RFB over WS）
class VncImageProvider;  // 远控画面 → QML（image://vnc/live）

class QDateTime;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class QWebSocket;
class OAuthLogin;

/** 全局日志（定义在 main.cpp；backend 与界面共用同一份落盘日志）。 */
void logf(const char *fmt, ...);

/**
 * 弹一条托盘气泡（设置页「提醒」那两个开关用）。
 *
 * 定义在 main.cpp：托盘那个实例是 main 建的，而 Qt 6.8 把 QSystemTrayIcon::find() 拿掉了
 * （只剩 isSystemTrayAvailable / supportsMessages），拿不回常驻的那一个。
 * 这里只做声明 —— 后端不该知道托盘长什么样，只管调。
 */
void stelarithNotifyTray(const QString &title, const QString &msg);

class ViewerBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(bool authed READ isAuthed NOTIFY authedChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(bool statusWarn READ statusWarn NOTIFY statusTextChanged)
    Q_PROPERTY(QJsonArray devices READ devices NOTIFY devicesChanged)
    /**
     * 设备表里现在有几台是"真在线"的（顶栏那句「N 台在线」用）。
     *
     * 为什么不用 devices.length：devices 现在含**离线**设备（见下面的台账说明），
     * 长度是"我知道的机器数"，不是"在线机器数"，拿它当在线数会虚高。
     */
    Q_PROPERTY(int onlineCount READ onlineCount NOTIFY devicesChanged)
    Q_PROPERTY(QString currentUid READ currentUid WRITE setCurrentUid NOTIFY currentUidChanged)
    // 主题三档（E4，2026-10-11）：0 浅色 / 1 深色 / 2 跟随系统。持久化在 QSettings ui/themeMode。
    Q_PROPERTY(int themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    Q_PROPERTY(bool systemDark READ systemDark NOTIFY systemDarkChanged)
    /** 当前选中设备上报的指令能力全集（老版本被控端没这字段 → 空）。
     *  管端据此把不支持的按钮置灰，而不是让人点下去才吃到一句「未知指令」。 */
    Q_PROPERTY(QStringList capActions READ capActions NOTIFY capActionsChanged)
    Q_PROPERTY(QImage frame READ frame NOTIFY frameChanged)
    Q_PROPERTY(QJsonObject recordings READ recordings NOTIFY recordingsChanged)
    // 2026-10-11：事件流水（/api/events，最近 50 条）——手机端告警页数据源
    Q_PROPERTY(QJsonArray events READ events NOTIFY eventsChanged)
    // 2026-10-19：动作执行排行（/api/actions/ranking，管理端排行卡）
    Q_PROPERTY(QJsonArray actionRanking READ actionRanking NOTIFY actionRankingChanged)
    // ── 自习模式（2026-10-13）：噪音检测 + 异常录制 ──
    // 被控端每拍回传一份噪音样本（type=noise_sample，走订阅中继），这里聚合成：
    //   noiseDb      最新 dB（-96..0，越大越吵；< -90 视作安静）
    //   noiseSamples 最近 N 个样本（画条/曲线用，{db,t} 新在尾）
    //   noiseAlerts  异常/录制记录（{event,count,path,ok,bytes,ts}）
    //   studyActive  自习模式是否在跑（agent 每拍回报的 study 标志，最可靠）
    Q_PROPERTY(double noiseDb READ noiseDb NOTIFY noiseChanged)
    Q_PROPERTY(bool noiseActive READ noiseActive NOTIFY noiseChanged)
    Q_PROPERTY(bool noiseCaptureOk READ noiseCaptureOk NOTIFY noiseChanged)
    Q_PROPERTY(QVariantList noiseSamples READ noiseSamples NOTIFY noiseChanged)
    Q_PROPERTY(QVariantList noiseAlerts READ noiseAlerts NOTIFY noiseAlertsChanged)
    Q_PROPERTY(bool studyActive READ studyActive NOTIFY studyChanged)
    // ⚠️ subscribeThumbnails 已挪进下面的 public: 段 —— 原来它写在这儿的**无访问说明符**位置，
    //    class 默认 private ⇒ QML 调不到（报 "Property 'subscribeThumbnails' of object
    //    ViewerBackend is not a function"），多班缩略图墙点一下就 TypeError。
    //    Q_PROPERTY 不受影响（元属性），但 Q_INVOKABLE 必须 public 才让 QML 调得着。
    /** 画面进入「静态区」（连续多帧像素一致）→ 界面停止重绘。见 applyFrameBytes 的注释。 */
    Q_PROPERTY(bool screenStatic READ screenStatic NOTIFY statsChanged)
    Q_PROPERTY(double fps READ fps NOTIFY statsChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY statsChanged)
    Q_PROPERTY(int lastFrameBytes READ lastFrameBytes NOTIFY statsChanged)
    /**
     * 现在屏幕上这张画面**属于哪台设备**（帧头里的 uid；旧格式裸帧没有 uid 时按
     * "当时正在看的那台"记）。
     *
     * 为什么要它：切换设备后，上一台的帧还会在路上（云端订阅不是瞬时的，被控端
     * 也可能还在发），这些帧落进同一个 m_frame 就会让屏幕继续显示上一台的桌面 ——
     * 老师照着旧画面去操作新机器，在教室大屏上属于事故。
     * 界面拿它和 currentUid 比：不相等就不上屏，显示"正在接通"。
     */
    Q_PROPERTY(QString frameUid READ frameUid NOTIFY frameChanged)
    Q_PROPERTY(QString cloudUrl READ cloudUrl NOTIFY cloudUrlChanged)
    // ── 站点账号（OAuth 一户通，2026-10-06）──
    // 界面直接显示这一行就够了，别让它在"已登录"和"没登录"之间猜：
    // 登不进去要能看见原因（配错了网站地址、密钥没填、站点没配密钥……都在这句里）。
    Q_PROPERTY(QString accountText READ accountText NOTIFY accountChanged)
    Q_PROPERTY(bool accountBusy READ accountBusy NOTIFY accountChanged)
    // ── 登录态 / 角色 / 权限（2026-10-06 打磨）──
    // 界面靠这几个读数决定「顶栏那个账户胶囊长什么样」以及「右边哪些按钮点得动」。
    // 一律以**后端**（真实凭据有没有、角色存 Setting 里哪一份）为唯一真源，
    // 界面不许自己记一份"我以为登录了"。
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY accountChanged)
    /** 已登录时显示谁（没登录就是空串）。 */
    Q_PROPERTY(QString accountName READ accountName NOTIFY accountChanged)
    /** 当前身份："admin"（管理员）／"teacher"（教师）。未登录时也是 teacher（按最低权限看）。 */
    Q_PROPERTY(QString role READ role NOTIFY roleChanged)
    // ── 提醒（设置页那两个开关，以前是画的假开关，2026-10-06 做成真的）──
    Q_PROPERTY(bool notifyOnDone READ notifyOnDone NOTIFY notifyPrefChanged
               WRITE setNotifyOnDone)
    Q_PROPERTY(bool notifyOnOffline READ notifyOnOffline NOTIFY notifyPrefChanged
               WRITE setNotifyOnOffline)
    // ── 应用版本（关于页 / 顶栏标题，编译期注入；以前界面上只显示一个「—」）──
    Q_PROPERTY(QString version READ version CONSTANT)
    // ── 文件推送（file_push / file_chunk / file_done 三步走）──
    // 状态：idle（没在推）→ pushing（等被控端收下会话）→ sending（一片一片推）
    //       → done（推完，fileTarget 是被控端落盘路径）／ failed（中断，fileError 有原因）
    Q_PROPERTY(QString fileState READ fileState NOTIFY fileProgressChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY fileProgressChanged)
    Q_PROPERTY(qint64 fileBytes READ fileBytes NOTIFY fileProgressChanged)
    Q_PROPERTY(qint64 fileTotal READ fileTotal NOTIFY fileProgressChanged)
    Q_PROPERTY(int filePercent READ filePercent NOTIFY fileProgressChanged)
    Q_PROPERTY(QString fileError READ fileError NOTIFY fileProgressChanged)
    Q_PROPERTY(QString fileTarget READ fileTarget NOTIFY fileProgressChanged)
    // 2026-10-11：进度条光有百分比不够看 —— 加上"第几片"和"已推/总量的人话体积"，
    // 老师能一眼分辨"在大文件慢推"还是"卡住了没动"。
    Q_PROPERTY(int fileChunks READ fileChunks NOTIFY fileProgressChanged)
    Q_PROPERTY(QString fileBytesText READ fileBytesText NOTIFY fileProgressChanged)
    Q_PROPERTY(QString fileTotalText READ fileTotalText NOTIFY fileProgressChanged)
    // ── 分班制（2026-10-08）──
    // 云端 /api/classes 返回的全部班级（含每台在线数），管理端据此在设备列表上做「按班分组/筛选」。
    // 云端算好结论推下来（requestClasses 拉 + 设备上下线时随 devices 刷新），界面只消费。
    Q_PROPERTY(QJsonArray classes READ classes NOTIFY classesChanged)
    /** 当前选中的班级筛选（空串 = 全部）。界面切班时写它，设备列表按它过滤。 */
    Q_PROPERTY(QString currentClass READ currentClass WRITE setCurrentClass NOTIFY classesChanged)
    /** 首启引导：false = 还没走完引导（主窗应叠 WelcomeOverlay）；true = 已走过。 */
    Q_PROPERTY(bool firstRunDone READ firstRunDone NOTIFY firstRunDoneChanged)
    // ── 开机自启 / 桌面快捷方式（2026-10-09 · 当前用户范围、免提权）──
    Q_PROPERTY(bool autoStart READ autoStartEnabled NOTIFY autostartChanged)
    Q_PROPERTY(bool desktopShortcut READ desktopShortcutExists NOTIFY autostartChanged)
    // ── 云端存储概况（2026-10-08，对应需求 #2「没有云端存储信息」）──
    // 管理端概览页展示：已知设备数 / 在线 / 离线 / 班级数 / 待执行指令 / 事件落盘量 / 最近活跃时刻。
    // 数据面由云端 getStorageInfo 算好推下来，界面只消费，不许前端自己再数一遍（避免两端口径不一致）。
    Q_PROPERTY(QJsonObject storage READ storage NOTIFY storageChanged)

    // ── 远控（VNC over WS，libvncclient）状态 ──
    // vncActive：是否正在远控（画面走 VNC）。vncState：连接状态文案（连接中/已连接/断开/错误），
    // 界面（Theme 组件）据此给用户提示，禁止原生弹窗。
    Q_PROPERTY(bool vncActive READ vncActive NOTIFY vncStateChanged)
    Q_PROPERTY(QString vncState READ vncState NOTIFY vncStateChanged)
    // 当前画面**实际来自哪条链路**："none"（还没帧）/ "jpeg"（被控端 JPEG 推送）
    // / "vnc"（VNC 远控实时画面）。界面靠它告诉用户"现在看的是远控还是轮询"。
    Q_PROPERTY(QString frameSource READ frameSource NOTIFY frameSourceChanged)
    // 远程终端状态读数：界面靠它决定"开终端"按钮能不能点、显示什么提示。
    // 状态机 Idle → Pending（请求已发，等本机点头）→ Open（会话在跑）→ Idle。
    Q_PROPERTY(TermState termState READ termState NOTIFY termStateChanged)
    Q_PROPERTY(QString termSid READ termSid NOTIFY termStateChanged)
    Q_PROPERTY(QString termNote READ termNote NOTIFY termStateChanged)
    Q_PROPERTY(bool termBusy READ termBusy NOTIFY termStateChanged)

public:
    /** 订阅一批设备的画面（不切当前选中，只收缩略图帧），QML 网格 3×3 展示。
     *  多班面板缩略图墙（设计文档 3.2.2）：与主画面共用一条 WS 订阅，云端按 uid 推帧，
     *  帧头带 uid → 按 uid 分槽存。 */
    Q_INVOKABLE void subscribeThumbnails(const QVariantList &uids);

    // 自习模式（2026-10-13）：收 agent 的噪音样本 / 异常记录
    void handleNoiseSample(const QJsonObject &pay);
    void handleNoiseAlert(const QJsonObject &pay);

    // 帧的来源。以前两路都调同一个 applyFrameBytes 且不区分来源，结果被控端的 JPEG
    // 轮询帧和 WebRTC 抽出来的帧互相覆盖同一张 m_frame —— 画面在两个源之间来回跳、
    // 帧率虚高一倍，而日志上看起来一切正常。
    enum FrameSource {
        PushJpeg,   ///< 被控端经云端推来的 JPEG（二进制帧 / 文本 frame / 裸帧三条老路径）
        VncGrab     ///< VNC 远控解出的一帧（libvncclient）
    };

    /// 远程终端会话状态：没会话 / 请求已发出在等被控端本机点头 / 会话开着。
    /// 中间那态要有：不然"点了开终端"之后按钮一直能点，连点五下全是五条 terminal_open。
    enum TermState {
        TermIdle,     ///< 没有会话
        TermPending,  ///< 已下发，等被控端本机确认（弹窗点头，可能要几秒，也可能直接被拒）
        TermOpen      ///< 会话是通的，可以敲命令
    };
    Q_ENUM(TermState)
    explicit ViewerBackend(QObject *parent = nullptr);
    ~ViewerBackend() override;

    /** 读环境变量（STE_VIEWER_URL / STE_VIEWER_TOKEN）并开始连接。 */
    void start();

    bool isConnected() const { return m_connected; }
    bool isAuthed() const { return m_authed; }
    QString statusText() const { return m_statusText; }
    bool statusWarn() const { return m_statusWarn; }
    QJsonArray devices() const { return m_devices; }
    /** 设备表里真在线的台数（离线的不算）。 */
    int onlineCount() const;
    QString currentUid() const { return m_currentUid; }

    QJsonArray classes() const { return m_classes; }
    QString currentClass() const { return m_currentClass; }
    // 2026-10-12 修：必须 Q_INVOKABLE —— 它是 Q_PROPERTY currentClass 的 WRITE，
    // 但 QML 里写的是 `backend.setCurrentClass(code)`（**当函数调**），WRITE 只在
    // 属性赋值时可用 ⇒ 原来报 "Property 'setCurrentClass' ... is not a function"，
    // 班级筛选静默失效（与 setCurrentUid 同款坑，那边早修了这边漏了）。
    Q_INVOKABLE void setCurrentClass(const QString &c) { if (m_currentClass != c) { m_currentClass = c; emit classesChanged(); } }
    QJsonObject storage() const { return m_storage; }

    QImage frame() const { return m_frame; }
    /** 现在这张画面属于哪台设备（见 frameUid 属性的注释）。 */
    QString frameUid() const { return m_frameUid; }
    bool screenStatic() const { return m_screenStatic; }
    double fps() const { return m_fps; }
    int frameCount() const { return m_frameCount; }
    int lastFrameBytes() const { return m_lastFrameBytes; }
    /** 云端地址（设置页要显示；它是启动时从环境变量读的，不是用户填的）。 */
    QString cloudUrl() const { return m_url; }
    /**
     * 管理面 HTTP 基址（由 m_url 派生）。
     *
     * ⚠️ m_url 是 **WebSocket** 地址（默认 `wss://control.245959623.xyz/ws/viewer`），
     *    而 `/api/*` 那一族走 HTTP ⇒ 必须先换 scheme（wss→https / ws→http），
     *    再把 `/ws/...` 那段路径截掉。
     *
     * 此前这段派生在 fetchEvents / requestClasses / fetchRecordings / requestStorage /
     * requestOnce 里各抄了一份（5 处）；**InteractBridge 当时漏了这一步** ⇒ 拼出
     * `wss://…/ws/viewer/api/interact/…`，而 QNetworkAccessManager 不支持 wss scheme
     * ⇒ 审批面板在真机上永远拉不到数据（HTTP 契约层测试用 curl 直连 http，绕过了
     * 这段派生，所以当时全绿也没发现）。2026-10-16 由真桥运行时探针实测暴露。
     *
     * 收成一个函数：以后新增管理面接口只调它，不会再漏。
     */
    QString httpBaseUrl() const;
    /**
     * 当前管理端令牌（2026-10-15 组件6 配套）。
     *
     * 为什么外露：互动审批面板（InteractBridge）走 HTTP `/api/interact/*`，
     * 那些端点的鉴权与 /api/devices 同一条路（Bearer 令牌）。令牌的真源在
     * backend.start()（环境变量 STE_VIEWER_TOKEN 或网站账号票据），面板不该
     * 自己再读一遍环境变量 —— 抄两份必然在"账号登录"这条路上漂开。
     */
    QString viewerToken() const { return m_token; }

    /**
     * 管理端默认云端地址（没设 STE_VIEWER_URL 时用它）。
     *
     * 为什么要外露：Updater（自更新）是在 engine.load() **之前**构造的，那时 backend.start()
     * 还没跑、cloudUrl() 还是空串；而自更新要拿这个地址去问云端"有没有新版"。
     * 与其在 main.cpp 里再抄一份默认地址字面量（抄了就一定会漂），不如从这里取。
     * start() 自己也用它 —— 单一真源。
     */
    static QString defaultCloudUrl();

    // ── 站点账号（OAuth 一户通）──
    /** 账号那一行该显示什么（见 accountText 属性）。 */
    QString accountText() const;
    bool accountBusy() const;
    /** 界面「关于 / 顶栏」显示的版本（编译期注入，见 version 属性）。 */
    QString version() const;
    /** 有没有能连云端的凭据（接入票 or 静态令牌）——"登录了"的唯一判据。 */
    bool loggedIn() const;
    /** 登录的是谁（没登录是空串）。 */
    QString accountName() const;
    /** 当前身份："admin" / "teacher"。 */
    QString role() const;
    /** 主题三档（E4）：0 浅色 / 1 深色 / 2 跟随系统。读来自 QSettings，写回即持久化。 */
    int themeMode() const { return m_themeMode; }
    void setThemeMode(int m);
    /** 跟随系统档用的 OS 深色判定（监听 QStyleHints::colorSchemeChanged）。 */
    bool systemDark() const { return m_systemDark; }

    /**
     * 用网站账号登录：拉起浏览器走授权页，回拨接住后自动换云端接入票并连上云端。
     * 已在登录中时会被忽略（不然点两下就是两个浏览器窗口）。
     */
    Q_INVOKABLE void loginWithSite();
    /** 首启引导：读取 / 写入「是否已走完引导」标记（QSettings prefs/firstRunDone）。 */
    Q_INVOKABLE bool firstRunDone() const;
    Q_INVOKABLE void markFirstRunDone();
    // 开机自启：读写 HKCU\...\Run（当前用户，免提权）。返回写入后的实际状态。
    Q_INVOKABLE bool autoStartEnabled() const;
    Q_INVOKABLE void setAutoStart(bool on);
    // 桌面快捷方式：当前用户桌面一枚 .lnk（免提权）。
    Q_INVOKABLE bool desktopShortcutExists() const;
    Q_INVOKABLE void setDesktopShortcut(bool on);
    /** 忘记本机上的账号（删本地凭据，下次要重新走一次授权）。 */
    Q_INVOKABLE void forgetAccount();
    /** 打开本机浏览器去站点注册新账号（注册页，不是授权页）。 */
    Q_INVOKABLE void openRegisterPage();
    /** 打开一个外部网址（站点首页 / 下载页）。走 QDesktopServices，不内嵌。 */
    Q_INVOKABLE void openExternal(const QString &url);

    // ── 角色与权限 ──
    /** 切身份（"admin" / "teacher"）。当前登录账号必须是管理员才认（教师不能自己升管理员）。 */
    Q_INVOKABLE bool setRole(const QString &role);
    /**
     * 这条操作当前身份能不能做（QML 用它把按钮置灰）。
     *
     * @param perm 权限键：等于 ADMIN_ONLY_ACTIONS 里那条（"lock" / "reboot" / "terminal_open" …）
     *             或集控页用的 "broadcast"。没在这张表里的一律算"所有人都能做"。
     */
    Q_INVOKABLE bool mayDo(const QString &perm) const;

    // ── 提醒开关 ──
    bool notifyOnDone() const { return m_notifyOnDone; }
    bool notifyOnOffline() const { return m_notifyOnOffline; }
    void setNotifyOnDone(bool on);
    void setNotifyOnOffline(bool on);
    /** 该弹一条本机提示时由内部调用（状态行 + 可选的桌面气泡）。 */
    void notifyPref(const QString &text);

    // 文件推送的 7 个读数：Q_PROPERTY 的 READ 就是这些，少了任何一个，
    // moc 生成的 _t->xxx() 和本文件里的 `ViewerBackend::xxx() const` 两侧同时报错
    QString fileState() const { return m_fileState; }
    QString fileName() const { return m_fileName; }
    qint64 fileBytes() const { return m_fileBytes; }
    qint64 fileTotal() const { return m_fileTotal; }
    int filePercent() const { return m_filePercent; }
    QString fileError() const { return m_fileError; }
    QString fileTarget() const { return m_fileTarget; }
    int fileChunks() const { return m_pushNext; }
    QString fileBytesText() const;
    QString fileTotalText() const;

    // 远控读数（上面 Q_PROPERTY 的 READ 端）
    bool vncActive() const { return m_vncActive; }
    QString vncState() const { return m_vncState; }
    QString frameSource() const { return m_frameSource; }

    // ── 远控（VNC over WebSocket · libvncclient）──
    /** 对 uid 这台设备拉起 VNC 会话：RFB 字节全部走主 WS 的 vnc-data 二进制帧。 */
    Q_INVOKABLE void startVnc(const QString &uid);
    /** 收掉 VNC 会话，画面回退到常规 JPEG 轮询通道。 */
    Q_INVOKABLE void stopVnc();
    /** 注册 QML 图像提供器（main.cpp 建 engine 时传入 image://vnc 那个实例）。 */
    void setVncImageProvider(VncImageProvider *p);

    /** 切到某台设备 → 向云端订阅它的画面。
     *  ⚠️ 2026-10-10：必须 `Q_INVOKABLE` —— 它是 Q_PROPERTY currentUid 的 WRITE，
     *    但 QML 里写的是 `backend.setCurrentUid(uid)`（**当函数调**），而 WRITE 只在属性赋值时
     *    可用、不能当方法调，所以原来 QML 报 "Property 'setCurrentUid' ... is not a function"
     *    （点缩略图墙切设备全废）。下方 setScreenActive 一直是对的，它俩本就该一致。 */
    Q_INVOKABLE void setCurrentUid(const QString &uid);
    /** 2026-10-08 修复「不在带画面页也无限拉流」：页面离开集控（带画面主页）时置 false，
     *  退订当前设备画面、清掉本地帧；回到该页置 true 重新订阅。云端按订阅发帧，退订即停流。 */
    Q_INVOKABLE void setScreenActive(bool on);
    // 契合度（2026-10-06）：能力门控。QML 里问"这台机器认不认这条指令"。
    // 见 refreshCapActions / deviceSupports 的说明，别在 QML 里自己判。
    QStringList capActions() const { return m_capActions; }
    void refreshCapActions();
    Q_INVOKABLE bool deviceSupports(const QString &action) const;
    /** 能力门控被拦下时的反馈：状态行说人话 + 日志留证（**不静默**，红线）。
     * 单独开一个而不是让 QML 直接调 setStatus —— 后者不是 slot，QML 调不到。 */
    Q_INVOKABLE void reportUnsupported(const QString &label, const QString &action);
    /** 权限被拦下的反馈（现状：说人话 + 留日志，绝不静默）。 */
    Q_INVOKABLE void reportDenied(const QString &label);

    Q_INVOKABLE void requestDevices();
    Q_INVOKABLE void requestClasses();   // 2026-10-08 拉班级列表
    Q_INVOKABLE void requestStorage();   // 2026-10-08 拉云端存储概况
    Q_INVOKABLE void sendAction(const QString &action, const QJsonObject &params = QJsonObject());

    // ── 进程黑名单（2026-10-11 需求#6：「保存 → 指定软件一键关闭」）──
    // 老师真正想做的是：把"上课不许开的那几个软件"一次性存下来，以后一键全部关掉，
    // 而不是每次在进程列表里一个个挑。名单落在**管理端本机** QSettings（跟机器走，
    // 不依赖云端），存的是进程名（如 chrome.exe），不区分大小写。
    Q_PROPERTY(QStringList procBlacklist READ procBlacklist NOTIFY procBlacklistChanged)
    QStringList procBlacklist() const;
    /** 加一条（已存在就不重复加）。传进来的名字会做 trim。 */
    Q_INVOKABLE void addProcToBlacklist(const QString &name);
    Q_INVOKABLE void removeProcFromBlacklist(const QString &name);
    /**
     * 把当前进程表里**命中黑名单**的那些一次关掉（逐个发 process_stop）。
     * @param pidsAndNames 由 QML 传进来的当前进程表 [{pid, name}, ...]
     * @return 实际下发了几个关闭指令（0 = 一个都没匹配上，界面据此提示"没开着"）
     */
    Q_INVOKABLE int killBlacklisted(const QVariantList &items);
    /** 发一次鼠标操控。button 是 left/right/middle —— 被控端按它选 MOUSEEVENTF_*DOWN/UP。
     *  ⚠️ 2026-10-11：这个参数以前**没有**，而 QML 一直在传第 4 个（btnOf 的结果），
     *     QML 侧报 `Too many arguments, ignoring 1`，按键被静默丢掉 ⇒ 被控端恒当左键
     *     （右键菜单点不出、中键自动滚全废）。补上签名，别再把按钮信息丢了。 */
    Q_INVOKABLE void sendPointer(const QString &kind, double nx, double ny,
                                 const QString &button = QStringLiteral("left"));

    // ── 远控/键鼠（2026-10-17 补齐）──
    /** 发一次键盘事件（远控）。vk = Windows 虚拟键码，action = down | up。
     *  组合键（Ctrl+C 等）由 QML 依次发多个 down 再逐个 up（被控端只做单键注入）。 */
    Q_INVOKABLE void sendKey(int vk, const QString &action);

    // ── 语音对讲（设计文档 3.3，2026-10-07）──
    /** 开始讲话：开麦克风采集 → 通知云端 audio.start → 每帧 PCM 二进制发出。 */
    // ⚠️ speaking / broadcasting / speakError / *LeftSec 必须是 **Q_PROPERTY**，不能是 Q_INVOKABLE：
    //    QML 里写的是 `backend.speaking`（不带括号），Q_INVOKABLE 不带括号取到的是**函数对象**，
    //    恒为 true —— 冷启动、一个按钮都没点，界面就写着"停止讲话"，灵动岛也永远挂着
    //    "正在语音对讲 / 正在屏幕广播"。2026-10-07 截图 b1-page1.png 拍下了这个（见改动归档）。
    Q_PROPERTY(bool speaking READ isSpeaking NOTIFY speakingChanged)
    Q_PROPERTY(int speakLeftSec READ speakLeftSec NOTIFY limitsChanged)
    // 2026-10-11：Android 录音权限是否已授予（checkSelfPermission 直查系统）。
    // 非 Android 恒 true。语音对话框顶部据此显示「未授权→去设置」，不靠弹框猜。
    Q_PROPERTY(bool audioPermGranted READ audioPermGranted NOTIFY audioPermChanged)
    Q_PROPERTY(bool broadcasting READ isBroadcasting NOTIFY broadcastingChanged)
    Q_PROPERTY(int bcastLeftSec READ bcastLeftSec NOTIFY limitsChanged)
    /** 自动停止（到时长上限 / 断线）时说人话的那句；空 = 没有新提示（QML 收到非空就弹一次）。 */
    Q_PROPERTY(QString autoNote READ autoNote NOTIFY autoNoteChanged)

    /** 开始讲话：开麦克风采集 → 通知云端 audio.start → 每帧 PCM 二进制发出。
     *  带时长上限（m_speakMaxMs），到点自动停 —— 见 stopSpeaking 的 why 参数。 */
    Q_INVOKABLE bool startSpeaking();

    /** 实际开麦采集（不查权限；Android 权限在 startSpeaking 里先进门）。
     *  2026-10-11 拆出：startSpeaking 负责 Android 权限请求（异步），回调到这里。 */
    bool startSpeakingCapture();

    Q_INVOKABLE void stopSpeaking(QString why = QString());
    /** 是否正在讲话。 */
    bool isSpeaking() const;
    /** 讲话还剩几秒（到 0 自动停）。 */
    int speakLeftSec() const;
    /** 自动停那句话的原文（m_autoNote 的 READ）。
     *  ⚠️ 这行不能少：Q_PROPERTY 的 READ 指向它，moc 会生成 autoNote() 调用，
     *     只有 .cpp 定义、头文件不声明 ⇒ C2039 "autoNote": 不是成员（2026-10-07 编译事故）。 */
    QString autoNote() const;
    /** 最后一次开麦失败原因（QML 提示用）。Q_PROPERTY READ 已声明，无需再 Q_INVOKABLE。 */
    Q_PROPERTY(QString speakError READ speakError NOTIFY speakErrorChanged)
    QString speakError() const;   // Q_PROPERTY 的 READ（moc 需要成员声明，非 Q_INVOKABLE）
    /** 2026-10-11：Android 录音权限是否已授予（桌面恒 true）。 */
    bool audioPermGranted() const;
    /** 2026-10-11：主动再请求一次录音权限（弹系统框；已在设置授权时立即生效）。 */
    Q_INVOKABLE void requestAudioPerm();
    /** 2026-10-11：跳系统「应用权限设置页」——用户拒绝过一次后弹框不再出现，
     *  只能从这里手动开（Android 13+ 对二次请求默认不再弹框）。 */
    Q_INVOKABLE void openAudioSettings();

    // ── 屏幕广播（设计文档《屏幕广播-第一版设计》，2026-10-07）──
    /** 开始屏幕广播：抓屏 QTimer → 每帧 JPEG 二进制发云端 → broadcast.start。 */
    Q_INVOKABLE bool startBroadcast();
    /** 停止屏幕广播。why 非空 = 自动停（到点 / 断线），会多说一句人话。 */
    Q_INVOKABLE void stopBroadcast(QString why = QString());
    /** 是否正在广播。 */
    bool isBroadcasting() const;
    /** 广播还剩几秒（到 0 自动停）。 */
    int bcastLeftSec() const;

    // ── 班级监控（设计文档 3.6 第一版）──
    /** 拉取云端录制列表（recordings/<uid>/ 分组），emit recordingsChanged。 */
    Q_INVOKABLE void fetchRecordings();
    // 2026-10-11：拉事件流水（/api/events）——手机端告警页
    Q_INVOKABLE void fetchEvents();
    // 2026-10-19：拉动作执行排行（/api/actions/ranking）——概览页排行卡
    Q_INVOKABLE void fetchActionRanking();
    // 2026-10-11：QML 热更（一源响应式）——查 /api/qml-patch 清单，版本不同则
    // 下载 zip 解压到本地 qml-live/（下次启动本地优先；改 UI 不用重出 APK）
    Q_INVOKABLE void checkQmlPatch();

    // ── 诊断与反馈（2026-10-12 · GUI 子系统后没有控制台，出错要看界面）──
    // 管理端 WIN32_EXECUTABLE 改 ON（双击不弹黑窗）后，stdout 不再可见；出错只能靠
    // 本机日志文件（viewer-run.log）与界面。这一组给"诊断与反馈"弹窗提供数据与动作。
    /** 注入本机日志文件路径（main.cpp 启动时调一次；backend 自己不知道路径）。 */
    void setLogFilePath(const QString &p) { m_logPath = p; }
    /** 本机日志尾部（最多 maxLines 行，从末尾取）。用于弹窗里给人看最近发生了什么。 */
    Q_INVOKABLE QString viewerLogTail(int maxLines = 300) const;
    /** 诊断摘要：版本 / 云端地址 / 连接与鉴权 / 身份 / 设备数 / 日志路径 / 系统。 */
    Q_INVOKABLE QString diagnosticsSummary() const;
    /** 复制文本到系统剪贴板（QML 无直接剪贴板访问）。 */
    Q_INVOKABLE void copyToClipboard(const QString &text) const;
    /** 用文件管理器打开日志所在目录。 */
    Q_INVOKABLE void openLogFolder() const;
    /** 托盘「诊断与反馈」入口 → 通知 QML 打开对话框。 */
    Q_INVOKABLE void requestDiagFeedback();
    QJsonArray events() const { return m_events; }
    QJsonArray actionRanking() const { return m_actionRanking; }
    /** 录制列表（QML 读，QJsonObject：uid → 文件数组）。 */
    QJsonObject recordings() const { return m_recordings; }
    /** 取某台设备的缩略图帧（多班面板；无则空图）。 */
    QImage thumbFrame(const QString &uid) const { return m_thumbFrames.value(uid); }
    /** 摄像头第二轨最新帧（2026-10-12 · 四·双画面；无则空图）。 */
    QImage camFrame() const { return m_camFrame; }
    int cameraFrameCount() const { return m_camFrameCount; }
    // ── 自习模式（2026-10-13）getter ──
    double noiseDb() const { return m_noiseDb; }
    bool noiseActive() const { return m_noiseActive; }
    bool noiseCaptureOk() const { return m_noiseCaptureOk; }
    QVariantList noiseSamples() const { return m_noiseSamples; }
    QVariantList noiseAlerts() const { return m_noiseAlerts; }
    bool studyActive() const { return m_studyActive; }
    /** 当前订阅了缩略图的多台设备 uid 列表。 */
    QStringList thumbUids() const { return m_thumbUids; }

    /**
     * 推一个本机文件给当前选中的教室机。
     *
     * 链路：file_push（开会话）→ 一片一片 file_chunk（data 走 base64）→ file_done（收口并校总字节）。
     * **一片一片等回执再发下一片**（回执 done 才继续），不是一口气全发出去 ——
     * 被控端的 file_chunk 只认"下一片"的 seq，乱序直接拒；而且 base64 后每条近 90KB，
     * 攒一堆同时发会把自己和云端都淹了。
     * @param urlText 文件的 file:// URL（界面上 FileDialog 给的）
     */
    /**
     * 弹本机"选文件"框，返回选中文件的 file:// URL（没选则返回空串）。
     *
     * 走 C++ 原生 QFileDialog 而不是 QML 的 QtQuick.Dialogs：后者是一套独立的 QML 插件，
     * 部署时漏了就是"exe 起不来 / Main.qml 整个挂掉"，排查起来比多链接一个库贵得多。
     *
     * Android 分支（2026-10-11）：QtWidgets 的 QFileDialog 在 Android 上弹不出（Qt 不
     * 提供原生对话框实现），所以手机上 pickFile() 直接空桩——用户看到"文件传输无反应"。
     * 这里改成同步返回空串 + 异步走系统 SAF 文件选择器（ACTION_GET_CONTENT），结果
     * 复制到应用缓存后 emit filePicked(file:// URL)，QML 监听该信号即可。
     */
    Q_INVOKABLE QString pickFile();
#if defined(Q_OS_ANDROID)
    /** Android 专用：把 SAF 返回的 content:// URI 复制到应用缓存目录，返回 file:// URL。 */
    static QString copyContentUriToCache(const QString &uriStr, const QString &suggestedName);
#endif
    Q_INVOKABLE void pushFile(const QString &urlText);
    /** 中断当前推送：让被控端把半截文件关掉、清会话（不留下半个坏文件在那台机器上）。 */
    Q_INVOKABLE void cancelPush();

    // ── 远程终端（2026-10-06）──────────────────────────────────────────
    /**
     * 开一个远程终端。shell 只收 "cmd" / "powershell"，别的（wscript 之流）云端这侧也不认。
     * @note 返回的不是结果：这条只是"点了"，真开没开起来看 terminalOpened / terminalClosed。
     */
    Q_INVOKABLE void termOpen(const QString &shell = QStringLiteral("cmd"));
    /** 发一行命令；回车调它。会话没开时不发（不排队：排队的命令会错序，不值当）。 */
    Q_INVOKABLE void termInput(const QString &sid, const QString &keys);
    /** 主动关会话。 */
    Q_INVOKABLE void termClose();

    // ── 自习模式（2026-10-13）──
    /** 下发 study_mode_start。opts: {thresholdDb, streakNeed, recordSec, intervalMs}（都可省）。 */
    Q_INVOKABLE void startStudyMode(const QVariantMap &opts = QVariantMap());
    /** 下发 study_mode_stop。 */
    Q_INVOKABLE void stopStudyMode();

    TermState termState() const { return m_termState; }
    QString termSid() const { return m_termSid; }
    QString termNote() const { return m_termNote; }
    bool termBusy() const { return m_termState == TermPending; }

signals:
    void connectedChanged();
    void authedChanged();
    void statusTextChanged();
    void devicesChanged();
    void classesChanged();
    void storageChanged();
    void currentUidChanged();
    /** 当前选中设备的能力清单（actions）变了 —— 按钮门控要跟着刷。 */
    void capActionsChanged();
    /** 进程黑名单变化（加/删一条）。 */
    void procBlacklistChanged();
    void frameChanged();
    void statsChanged();
    void cloudUrlChanged();
    void recordingsChanged();
    void eventsChanged();   // 2026-10-11：事件流水（告警页）
    void actionRankingChanged();   // 2026-10-19：动作执行排行（概览页排行卡）
    /** 某台设备的缩略图帧更新了（多班面板缩略图墙）。uid 为空表示全部。 */
    void thumbnailChanged(const QString &uid = QString());
    /** 站点账号状态/文本变化（登录成功、失败、票换了、用户点了忘记）。 */
    void accountChanged();
    // 语音对讲 / 屏幕广播的状态与时长上限（2026-10-07）：对应 Q_PROPERTY
    // speaking / broadcasting / speakLeftSec / bcastLeftSec / autoNote。
    // limitsChanged 每"剩余 1 秒"跳一次，界面写 ≥ 剩余秒数就能跟着自动进。
    void speakingChanged();
    void broadcastingChanged();
    void speakErrorChanged();
    void limitsChanged();
    void autoNoteChanged();
    /** 文件推送进度：状态/文件名/已推字节/百分比/失败原因/落盘路径，全走这一个信号。 */
    void fileProgressChanged();
    /**
     * 2026-10-11：Android 上 pickFile() 是异步（SAF 系统选择器 → Activity 结果回调），
     * 选完文件把 file:// URL 通过本信号通知 QML 补上 pendingUrl——桌面 pickFile() 同步
     * 返回不需要它。
     */
    void filePicked(const QString &url);
    /** 远控链路状态变化（连接中/已连接/断开/错误）。Q_PROPERTY vncActive/vncState 共用它。 */
    void vncStateChanged();
    /** 远控解出一帧（libvncclient → QImage）。主画面走 frameChanged，这里给 QML 直连。 */
    void vncFrameReady(const QImage &frame);
    /** 2026-10-12 · 四·双画面：摄像头第二轨帧就绪（→ QML 刷 image://frames/cam）。
     *  帧源（RTC 摄像头轨）已随 libdatachannel 移除，当前无人 emit；保留通道待接入。 */
    void cameraFrameChanged();
    // ── 自习模式（2026-10-13）──
    /** 噪音样本更新（db/active/samples 一起变）。 */
    void noiseChanged();
    /** 自习模式异常/录制记录更新。 */
    void noiseAlertsChanged();
    /** 自习模式开关状态变化（agent 每拍回报 study 标志）。 */
    void studyChanged();
    /** 画面来源切换（jpeg ↔ rtc）——界面上那个链路徽标靠它刷新。 */
    void frameSourceChanged();
    /** 终端状态四件套（state/sid/note/busy）共用一个信号。 */
    void termStateChanged();
    /** 帧没画成（解码失败等）——界面可以往屏幕上说明一句，别让画面无声无息不动。 */
    void frameDropped(const QString &reason);
    /** 托盘「诊断与反馈」入口触发的开对话框请求（QML Main.qml 连接它开弹窗）。 */
    void diagFeedbackRequested();
    /** 鉴权被拒：界面应停止重连并把原因显示出来。 */
    void authFailed(const QString &reason);
    /** 角色变了（顶栏徽标、按钮置灰都要跟着动）。 */
    void roleChanged();
    /** 提醒开关变了（设置页那两个开关）。 */
    void notifyPrefChanged();
    /** 2026-10-11：Android 录音权限状态变化（检查/授权回调后发）。 */
    void audioPermChanged();
    /** 云端状态通知（断线重连 / 指令完成）：统一走灵动岛瞬态提示，不再弹系统托盘气泡。 */
    void islandNote(const QString &title, const QString &desc, const QString &icon);
    /** 首启引导是否已完成（标记位从 QSettings 读回；完成写标记后界面据此收起引导层）。 */
    void firstRunDoneChanged();
    /** 开机自启 / 桌面快捷方式 任一状态变化（两者共用，读属性重新取即可）。 */
    void autostartChanged();
    /**
     * 指令结果。state: "sent"（云端已写进连接）/ "executed"（机器真做了）/ 其它=没发成。
     * result: "done" / "failed"（仅 executed 时有意义）。data: 动作附加数据（可选）。
     */
    void resultReceived(const QString &uid, const QString &action, const QString &state,
                        const QString &result, const QString &error, const QString &detail,
                        const QJsonObject &data);

    // ── 远程终端（2026-10-06）──────────────────────────────────────────
    // 四帧一一对应，别在中途合并：QML 那边靠 sid 判断"这是哪一路会话"，
    // 合并成一条信号会让"开没开起来"和"有没有输出"分不清。
    /** 会话开起来了。shell=cmd/powershell，prompt/cwd 直接拿过来画第一行。 */
    void terminalOpened(const QString &sid, const QString &shell, const QString &prompt, const QString &cwd);
    /** 输出块（被控端 50ms 攒一块，所以这里收的是一小段，不是整屏）。 */
    void terminalData(const QString &sid, const QString &data);
    /** 单条命令跑完了：code=退出码，ms=耗时。 */
    void terminalExit(const QString &sid, int code, int ms);
    /** 会话关了（remote=管理端主动关 / idle=没人理自动关 / denied=本机拒绝 / agent-exit=被控端退了）。 */
    void terminalClosed(const QString &sid, const QString &reason);
    /** 主题三档变化（E4，UI 据此切深 / 浅）。 */
    void themeModeChanged(int);
    /** OS 配色变化（跟随系统档用）。 */
    void systemDarkChanged(bool);

private:
    void setStatus(const QString &s, bool warn);
    void connectToCloud();
    /** 读回角色与提醒开关（QSettings，构造时调一次）。 */
    void loadPrefs();
    /** 切身份成功/失败后统一刷一遍依赖这些读数的状态。 */
    void refreshGateState();
    /** 重连间隔（带退避）：5s / 10s / 20s / 30s 封顶。 */
    int retryDelayMs() const;
    /** 手上有没有任何云端凭据（接入票或静态令牌都没有 = 没得连，别空转）。 */
    bool hasAnyCredential() const;
    void onConnected();
    void onDisconnected();
    void onTextMessage(const QString &text);
    /** 2026-10-11（#4）：探活 —— 已连上但超 kCtrlStaleMs 没收到云端任何消息 →
     *  主动断开触发退避重连（覆盖 Android 后台被切断网络的半开连接）。 */
    void onCtrlWatchdog();
    /** 2026-10-11（#4）：应用回到前台（ApplicationActive）→ 立即查一次半开，不等 10s 定时器。 */
    void onAppStateChanged(Qt::ApplicationState state);
    void onBinaryMessage(const QByteArray &buf);
    void refreshDevices(const QJsonArray &arr);
    void applyFrameBytes(const QByteArray &jpeg, const QJsonObject &header,
                         FrameSource src = PushJpeg);
    void setStatic(bool s);
    /** 远控解出一帧 → 写 m_frame 并上屏（frameSource 切到 "vnc"）。 */
    void onVncFrame(const QImage &frame);
    /** 云端下发的 vnc-data 原始 RFB 字节 → 喂 VncClient 的入站缓冲。 */
    void onVncData(const QByteArray &data);
    /** 改远控状态并通知界面（vncState / vncActive 共用一个信号）。 */
    void setVncState(const QString &s);
    void tickFps();
    void sendEnvelope(const QString &type, const QJsonObject &payload);
    /** 断线 / 退出时把语音和广播一起收掉，why 直接显示成状态行那句话。 */
    void stopAllLive(const QString &why);

    // ── 远程终端 ──
    /** 改状态并通知界面（状态四件套共用一个信号，省得四个 NOTIFY 各写一遍）。 */
    void setTermState(TermState s, const QString &sid, const QString &note);
    /** 组装 terminal_* 的 params 并下发；没选设备 / 会话不对一律不发。 */
    void sendTermAction(const QString &action, const QJsonObject &params);

    // ── 站点账号（OAuth 一户通）──
    /** 读/写/清本机凭据（站点会话令牌 + 云端接入票），落 AppData 目录下的 json。 */
    void loadAccount();
    void saveAccount();
    void clearAccount();
    /** 刷新 accountText，并在状态变化时通知界面。 */
    void refreshAccountText();
    /**
     * 确保手里有一张没过期的云端接入票。
     * @param interactive true = 拿不到票就走完整登录（弹浏览器）；false = 静默，只报错不打扰
     */
    void ensureCloudTicket(bool interactive);
    void onOAuthSucceeded(const QString &sessionToken, qint64 expiresInSec);
    void onOAuthFailed(const QString &reason);
    void onSessionTicketFinished();
    /** 票快到期就提前换一张（常驻程序不能等断了才想起来）。 */
    void onAccountTimer();
    void pickTicketOrToken(QJsonObject &p) const;
    /** 当前手上那张票还认不认（没票 / 已过 / 将在 soonMs 内到期都算不认）。 */
    bool ticketUsable(quint64 soonMs = 60 * 60 * 1000) const;

    void sendNextChunk();
    void setFileFail(const QString &why);
    void clearFilePush();

    // ── 站点账号（OAuth 一户通）──
    OAuthLogin *m_oauth = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    QTimer *m_accountTimer = nullptr;   // 票到期巡检（5 分钟一次）
    QString m_siteUrl;                  // STE_SITE_URL：站点首页地址
    QString m_sessionToken;             // 网站会话令牌（30 天，换云端票用）
    QString m_cloudTicket;              // 云端接入票（30 天，WS 握手用）
    qint64 m_ticketExp = 0;             // 接入票到期时刻（ms）
    QString m_accountUser;              // 登录的是哪个网站账号（审计/显示用）
    QString m_accountText;              // 状态行原文
    bool m_accountBusy = false;         // 登录/换票进行中
    bool m_accountFatal = false;        // 已明确失败：文本里带原因，别让人以为还在转
    QNetworkReply *m_sessionReply = nullptr;   // 换票请求（同一时刻只发一个）

    QWebSocket *m_ws = nullptr;
    // 远程终端会话状态（Idle/Pending/Open）。++ 会话 sid：每次开都换一个新的，
    // 这样"上一会话的迟到输出"不会灌进新窗口里。
    TermState m_termState = TermIdle;
    QString m_termSid;
    QString m_termNote;
    int m_termSidSeq = 0;
    // 终端的两个"等回话"都必须有收口（2026-10-07 修 #95）：
    // 下发 terminal_open 后本地是 Pending，若本机一直不点头、或被控端根本没回 terminal_closed，
    // 状态就永远卡在 Pending —— 界面上按钮永远显示"关终端"、输入框永远灰、再点"开终端"又被
    // "已经有会话了"拒掉。两条定时器就是给这两段等待兜底的，到点一律回 Idle 并把原因说出来。
    QTimer *m_termWait = nullptr;      // 等本机确认 terminal_open（默认 20s）
    QTimer *m_termCloseWait = nullptr; // 等 terminal_close 回话（默认 2.5s）
    bool m_termClosing = false;        // 关闭请求已下发、还没拿到 terminal_closed（去重用）
    QTimer *m_fpsTimer = nullptr;
    qint64 m_offlineSince = 0;            // 与云端断开的起始时刻（0 = 在线）

    QString m_url;
    QString m_token;

    QString m_currentUid;
    // ── 2026-10-11「选中即远控」状态：记住现在正接管哪台，切换/清空时好收尾 ──
    bool m_remoteControlling = false;
    QString m_remoteControlUid;
    // ── 2026-10-18 远控（VNC over WebSocket · libvncclient）──────────────
    // 与旧 P2 的「第二条隧道 WS」不同：RFB 字节走**主 WS** 的 vnc-data 二进制帧。
    // m_vncActive 期间，本机鼠标/键盘经 RFB PointerEvent/KeyEvent 下发（不再走
    // instruction/input，避免与 VNC 服务双重注入）；屏幕帧由 libvncclient 解出。
    VncClient *m_vnc = nullptr;
    VncImageProvider *m_vncProvider = nullptr;   // 非拥有（engine 持有）
    bool m_vncActive = false;
    QString m_vncState = QStringLiteral("idle");   // 连接中/已连接/已断开/连接失败
    quint8 m_vncBtnMask = 0;          // 当前鼠标按键位（RFB PointerEvent 用）
    QString m_vncPassword;            // 被控端 VNC 密码（STE_VNC_PASSWORD）
    bool m_screenActive = true;   // 2026-10-08：带画面页是否可见（不可见则暂停单台拉流）
    QStringList m_capActions;              // 当前设备的指令能力全集（空=没上报过）
    QStringList m_procBlacklist;           // 2026-10-11：进程黑名单（QSettings proc/blacklist）
    bool m_connected = false;
    bool m_authed = false;
    bool m_authFailed = false;      // 鉴权被拒后别再一遍遍重连
    QString m_authFailReason;
    // ── 重连退避（2026-10-06 打磨：以前固定 5 秒一轮，鉴权失败时能把日志刷爆）──
    // 第一次断线 5s、之后 10s / 20s / 30s 封顶；期间如果压根没凭据（既没票也没令牌），
    // 直接停在那儿等用户登录 —— 无凭据的循环重连只会刷日志，不会有任何进展。
    int m_retryCount = 0;
    bool m_retryStopped = false;    // 停机重连（没凭据 / 连着失败太多），要用户出手才恢复
    QString m_version;

    // ── 身份 / 提醒偏好（落在 QSettings，界面改完重启也还在）──
    QString m_role = QStringLiteral("admin");   // "admin" / "teacher"，默认管理员（单机自用）
    bool m_firstRunDone = false;                // 首启引导是否已完成（QSettings prefs/firstRunDone）
    bool m_notifyOnDone = true;
    bool m_notifyOnOffline = true;
    // ── 主题三档（E4）──
    int m_themeMode = 0;       // 0 浅色 / 1 深色 / 2 跟随系统，持久化在 QSettings ui/themeMode
    bool m_systemDark = false; // 跟随系统档的 OS 深色判定

    QString m_statusText;
    bool m_statusWarn = false;

    // ── 文件推送现场 ──
    QString m_fileState = QStringLiteral("idle");
    QString m_fileName;
    qint64 m_fileBytes = 0;
    qint64 m_fileTotal = 0;
    int m_filePercent = 0;
    QString m_fileError;
    QString m_fileTarget;      // 推完后被控端的落盘路径
    QString m_pushPath;        // 本机文件路径（便于报错时把路径直接说出来）
    QFile m_pushFile;          // 本机文件句柄（推完/取消就关，不留着）
    qint64 m_pushNext = 0;     // 下一片的 seq（被控端只认严格递增）
    // 2026-10-11：推送看门狗。每发一条指令重启一次；超时即判"教室机没应答"，
    // 报失败并让对面删掉半截文件 —— 否则流程永远卡在 sending，界面像死了一样。
    QTimer *m_pushWatchdog = nullptr;

    // ── 画面来源（JPEG 推送 vs VNC 远控）──
    QString m_frameSource = QStringLiteral("none");

    QJsonArray m_devices;
    QJsonArray m_classes;            // 2026-10-08 分班制：云端返回的全部班级
    QString m_currentClass;          // 2026-10-08 当前班级筛选（空=全部）
    QTimer *m_refreshTimer = nullptr; // 2026-10-08 设备表兜底轮询（15s，云端推送之外的保险）
    QJsonObject m_storage;            // 2026-10-08 云端存储概况（/api/storage 返回）
    // 2026-10-10：401 曾被 15s 轮询刷成一片。只在"HTTP 状态**变化**"时打一次日志，
    // 恢复 200 时复位（-1 表示"上次是好的/没记过"）。纯粹为压日志噪音，不影响请求逻辑。
    int m_classErrStatus = -1;
    int m_storageErrStatus = -1;
    /**
     * 设备台账（uid → 最后一次见到的设备对象）。
     *
     * 为什么要有：云端 listDevices() 只列**当前连着**的设备，`online` 还写死 true；
     * 机器一掉电就从表里消失。而界面早就按"有台账"写好了 —— 左栏有「离线」筛选、
     * 状态点分在线/离线、集控批量下发还会把离线机器直接判失败（见 Main.qml 的
     * isOnline / offlineDevices / 批量明细）。少了台账，这些全都恒等于空，
     * 用户看到的就是"离线显示不对"。
     *
     * 所以这里自己记一笔：设备从云端表里消失 = 记为离线（保留 kLedgerKeepMs），
     * 重新出现 = 记回在线。放进 m_devices 一起下发给界面（在线在前，离线在后）。
     */
    QHash<QString, QJsonObject> m_devLedger;
    static const qint64 kLedgerKeepMs = 24LL * 60 * 60 * 1000;   // 离线台账保留 24 小时
    QImage m_frame;
    // m_frame 属于哪台设备（frameUid 属性的本体）。空 = 手上这张画面不属于当前选中那台
    // （刚切换完、还没收到新设备的第一帧）→ 界面不许把它当"当前设备的画面"画出来。
    QString m_frameUid;
    quint64 m_frameHash = 0;         // 上一帧的稀疏指纹（0 表示还没见过帧）
    int m_sameFrameStreak = 0;       // 连续多少帧指纹一致
    bool m_screenStatic = false;     // 静态区：画面没在动，界面别浪费重绘
    int m_frameCount = 0;
    // 摄像头第二轨帧（2026-10-12 · 四·双画面）。与 m_frame 分开存，互不覆盖。
    QImage m_camFrame;
    int m_camFrameCount = 0;
    // 自习模式（2026-10-13）：噪音样本聚合 + 异常记录
    double m_noiseDb = 0.0;
    bool m_noiseActive = false;
    bool m_noiseCaptureOk = true;
    bool m_studyActive = false;
    QVariantList m_noiseSamples;   // 最近 kNoiseSamplesMax 个 {db,t}（新在尾）
    QVariantList m_noiseAlerts;    // 最近 kNoiseAlertsMax 条异常/录制记录（新在头）
    // 本机日志文件路径（诊断与反馈；main.cpp 启动时注入）。
    QString m_logPath;
    int m_framesSinceCheck = 0;
    int m_lastFrameBytes = 0;
    double m_fps = 0.0;
    qint64 m_lastFpsCheckMs = 0;
    qint64 m_pingSentMs = 0;

    // ── 语音对讲（2026-10-07）──
    AudioCapture *m_audioCapture = nullptr;   // 麦克风采集（懒创建）
    bool m_speaking = false;                  // 当前是否在讲话
    QString m_speakError;
    QTimer *m_speakLimit = nullptr;           // 讲话时长上限（到点自动停）
    int m_speakMaxMs = 3 * 60 * 1000;         // 讲话最长时长（默认 3 分钟，STE_MAX_SPEAK_MS 可覆盖）
    int m_speakLeftMs = 0;                    // 还剩多少毫秒
    int m_speakLeftSec = 0;                   // 界面显示的剩余秒数（1s 跳动）
    QString m_autoNote;                       // 自动停的人话说明（QML 收到非空就弹一次）

    // ── 屏幕广播（2026-10-07）──
    QTimer *m_bcastTimer = nullptr;           // 抓屏节拍（3fps）
    bool m_broadcasting = false;              // 是否在广播
    QTimer *m_bcastLimit = nullptr;           // 广播时长上限（到点自动停）
    int m_bcastMaxMs = 10 * 60 * 1000;        // 广播最长时长（默认 10 分钟，STE_MAX_BCAST_MS 可覆盖）
    int m_bcastLeftMs = 0;
    int m_bcastLeftSec = 0;
    QTimer *m_leftTick = nullptr;             // 剩余秒数跳动（1s，两个上限共用一个）

    // ── 班级监控（2026-10-07）──
    QJsonObject m_recordings;                 // recordings/<uid>/[文件] 分组缓存
    QJsonArray m_events;                      // 2026-10-11：/api/events 事件流水缓存
    QJsonArray m_actionRanking;               // 2026-10-19：/api/actions/ranking 动作排行缓存

    // 2026-10-11：store 模式 zip 解压（qml-patch 热更；无压缩，只读中央目录拷数据）
    bool unzipStore(const QString &outDir, const QByteArray &zip);

    // ── 语音对讲下行（B6 · 2026-10-11）────────────────────────────────────
    // 学生端回传的 audio/pcm 帧（duplex 单播回老师）在管理端播放。
    // AudioPlayback 懒打开（第一帧才开设备，无通话不占喇叭），会话结束 stop()。
    AudioPlayback *m_audioPlayback = nullptr;

    // ── 多班面板缩略图墙（2026-10-07，设计文档 3.2.2）──
    QStringList m_thumbUids;                  // 当前订阅缩略图的设备 uid 列表
    QHash<QString, QImage> m_thumbFrames;     // uid → 缩略图帧缓存

    // ── 2026-10-11「进后台就连不上云端」修复（#4）：半开连接探活 ──
    // Android 后台网络被系统切断时，QWebSocket 是**半开连接**（不触发 connected/
    // disconnected/error），回前台状态显示"已连"但发包无效。用收包时间戳 + 定时
    // 器判断"多久没收到云端任何消息"：超 threshold 主动断开 → 走既有退避重连。
    qint64 m_lastRxMs = 0;                    // 上次收到云端任何消息的时刻（0=从未收到）
    QTimer *m_ctrlWatchdog = nullptr;         // 探活定时器（10s 一跳）
    static const qint64 kCtrlStaleMs = 45000; // 45s 无收包即判半开，主动断开重连
};
