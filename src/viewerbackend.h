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

class AudioCapture; // 语音对讲（2026-10-07）：管理端采集（前向声明，避免引入 mmsystem）

class QDateTime;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class QWebSocket;
class QWebEngineView;
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
    /** 当前选中设备上报的指令能力全集（老版本被控端没这字段 → 空）。
     *  管端据此把不支持的按钮置灰，而不是让人点下去才吃到一句「未知指令」。 */
    Q_PROPERTY(QStringList capActions READ capActions NOTIFY capActionsChanged)
    Q_PROPERTY(QImage frame READ frame NOTIFY frameChanged)
    Q_PROPERTY(QJsonObject recordings READ recordings NOTIFY recordingsChanged)
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

    // ── WebRTC 收流（T-3，2026-10-04）──
    // 管理端做 answer 侧：被控端 captureStream 推 video track → 云端中继 →
    // 这里 QWebEngineView 离屏跑 RTCPeerConnection.setRemoteDescription(offer) →
    // createAnswer 回云端 → ontrack 拿到 track 塞进 <video> → 抽帧走现有 frame 通道。
    // 只收、不发——发是 agent 侧的事。
    Q_PROPERTY(bool rtcReady READ rtcReady NOTIFY rtcStateChanged)
    Q_PROPERTY(QString rtcState READ rtcState NOTIFY rtcStateChanged)
    // 当前画面**实际来自哪条链路**："none"（还没帧）/ "jpeg"（被控端 JPEG 推送）
    // / "rtc"（WebRTC 实时流）。界面要靠它告诉用户"现在看的是实时还是轮询"，
    // 否则 RTC 通没通、有没有降级回落，只能靠猜。
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

    // 帧的来源。以前两路都调同一个 applyFrameBytes 且不区分来源，结果被控端的 JPEG
    // 轮询帧和 WebRTC 抽出来的帧互相覆盖同一张 m_frame —— 画面在两个源之间来回跳、
    // 帧率虚高一倍，而日志上看起来一切正常。
    enum FrameSource {
        PushJpeg,   ///< 被控端经云端推来的 JPEG（二进制帧 / 文本 frame / 裸帧三条老路径）
        RtcGrab     ///< WebRTC 收流页抽出来的帧
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
    /** 析构：QWebEngineView 必须在 event loop 停止之后销毁，否则 Chromium 直接崩。 */
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
    void setCurrentClass(const QString &c) { if (m_currentClass != c) { m_currentClass = c; emit classesChanged(); } }
    QJsonObject storage() const { return m_storage; }

    /**
     * 只给自检用：退订 → 重订，逼被控端把 WebRTC 连接重建一次（重新 offer）。
     *
     * 正常界面用不到：被控端只在 rtc-start 时建一次连接，而首次订阅必然触发 rtc-start。
     * 需要它的场景是"被控端那次 offer 发出去时并没人订阅（云端丢了），之后再订阅就没新 offer"，
     * 界面上退订重订一次是唯一不需要改被控端代码的重来办法。
     */
    Q_INVOKABLE void rtcRenegotiate(const QString &uid);
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

    // RTC 读数
    bool rtcReady() const { return m_rtcReady; }
    QString rtcState() const { return m_rtcState; }
    QString frameSource() const { return m_frameSource; }

    /** 切到某台设备 → 向云端订阅它的画面。 */
    void setCurrentUid(const QString &uid);
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
    Q_INVOKABLE void sendPing();
    Q_INVOKABLE void sendPointer(const QString &kind, double nx, double ny);
    Q_INVOKABLE void sendType(const QString &text);

    // ── 语音对讲（设计文档 3.3，2026-10-07）──
    /** 开始讲话：开麦克风采集 → 通知云端 audio.start → 每帧 PCM 二进制发出。 */
    // ⚠️ speaking / broadcasting / speakError / *LeftSec 必须是 **Q_PROPERTY**，不能是 Q_INVOKABLE：
    //    QML 里写的是 `backend.speaking`（不带括号），Q_INVOKABLE 不带括号取到的是**函数对象**，
    //    恒为 true —— 冷启动、一个按钮都没点，界面就写着"停止讲话"，灵动岛也永远挂着
    //    "正在语音对讲 / 正在屏幕广播"。2026-10-07 截图 b1-page1.png 拍下了这个（见改动归档）。
    Q_PROPERTY(bool speaking READ isSpeaking NOTIFY speakingChanged)
    Q_PROPERTY(int speakLeftSec READ speakLeftSec NOTIFY limitsChanged)
    Q_PROPERTY(bool broadcasting READ isBroadcasting NOTIFY broadcastingChanged)
    Q_PROPERTY(int bcastLeftSec READ bcastLeftSec NOTIFY limitsChanged)
    /** 自动停止（到时长上限 / 断线）时说人话的那句；空 = 没有新提示（QML 收到非空就弹一次）。 */
    Q_PROPERTY(QString autoNote READ autoNote NOTIFY autoNoteChanged)

    /** 开始讲话：开麦克风采集 → 通知云端 audio.start → 每帧 PCM 二进制发出。
     *  带时长上限（m_speakMaxMs），到点自动停 —— 见 stopSpeaking 的 why 参数。 */
    Q_INVOKABLE bool startSpeaking();
    /** 停止讲话：关麦 → audio.stop。why 非空 = 自动停（到点 / 断线），会多说一句人话。 */
    Q_INVOKABLE void stopSpeaking(QString why = QString());
    /** 是否正在讲话。 */
    bool isSpeaking() const;
    /** 讲话还剩几秒（到 0 自动停）。 */
    int speakLeftSec() const;
    /** 自动停那句话的原文（m_autoNote 的 READ）。
     *  ⚠️ 这行不能少：Q_PROPERTY 的 READ 指向它，moc 会生成 autoNote() 调用，
     *     只有 .cpp 定义、头文件不声明 ⇒ C2039 "autoNote": 不是成员（2026-10-07 编译事故）。 */
    QString autoNote() const;
    /** 最后一次开麦失败原因（QML 提示用）。 */
    Q_PROPERTY(QString speakError READ speakError NOTIFY speakErrorChanged)
    Q_INVOKABLE QString speakError() const;

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
    /** 录制列表（QML 读，QJsonObject：uid → 文件数组）。 */
    QJsonObject recordings() const { return m_recordings; }
    /** 取某台设备的缩略图帧（多班面板；无则空图）。 */
    QImage thumbFrame(const QString &uid) const { return m_thumbFrames.value(uid); }
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
     */
    Q_INVOKABLE QString pickFile();
    Q_INVOKABLE void pushFile(const QString &urlText);
    /** 中断当前推送：让被控端把半截文件关掉、清会话（不留下半个坏文件在那台机器上）。 */
    Q_INVOKABLE void cancelPush();

    // ── QWebChannel 回调（JS → C++，收流页里的 __qt.* 就是调这四个）──
    /** JS 抽到的一帧 JPEG（base64，可能带 data: 前缀）；复用 frame 通道交给 QML。 */
    Q_INVOKABLE void setRtcFrame(const QString &b64);
    /** 远端 answer 的 SDP 字符串（正常流里管理端不产出，仅日志记录）。 */
    Q_INVOKABLE void rtcGotAnswer(const QString &sdp);
    /**
     * 收流页**本端** RTCPeerConnection 产出的 ICE candidate（JS 侧 onicecandidate 回调）。
     *
     * 只做一件事：发给云端转交被控端。**不再回灌给自己** —— 自己的候选本来就是从
     * 这个 pc 里出来的，再 addIceCandidate 回去只会让 Chromium 静默报 ice-FAIL。
     */
    Q_INVOKABLE void rtcGotIce(const QString &candidateJson);
    /** JS 侧的诊断输出（setOffer/ontrack/ICE 全程）。 */
    Q_INVOKABLE void rtcDiag(const QString &s);
    /** JS 侧 ontrack 触发（真正拿到远端视频轨）。 */
    Q_INVOKABLE void rtcGotTrack();

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
    void frameChanged();
    void statsChanged();
    void cloudUrlChanged();
    void recordingsChanged();
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
    /** WebRTC 收流链路状态变化（ready / negotiating / failed / idle）。 */
    void rtcStateChanged();
    /** 画面来源切换（jpeg ↔ rtc）——界面上那个链路徽标靠它刷新。 */
    void frameSourceChanged();
    /** 终端状态四件套（state/sid/note/busy）共用一个信号。 */
    void termStateChanged();
    /** 帧没画成（解码失败等）——界面可以往屏幕上说明一句，别让画面无声无息不动。 */
    void frameDropped(const QString &reason);
    /** 鉴权被拒：界面应停止重连并把原因显示出来。 */
    void authFailed(const QString &reason);
    /** 角色变了（顶栏徽标、按钮置灰都要跟着动）。 */
    void roleChanged();
    /** 提醒开关变了（设置页那两个开关）。 */
    void notifyPrefChanged();
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
    void onBinaryMessage(const QByteArray &buf);
    void refreshDevices(const QJsonArray &arr);
    void applyFrameBytes(const QByteArray &jpeg, const QJsonObject &header,
                         FrameSource src = PushJpeg);
    void setStatic(bool s);
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

    // ── WebRTC 收流内部 ──
    void initRtcView();          // 懒建 QWebEngineView + 注入 answer 侧 JS
    void setRtcState(const QString &s);
    /**
     * 把**远端**（被控端）的 ICE candidate 灌进收流页的 RTCPeerConnection。
     *
     * 和 rtcGotIce() 是严格相反的两个方向，别再合并：
     *   本端候选 → rtcGotIce()   → 发云端
     *   远端候选 → addRemoteIce() → 注入页面（**不回发云端**，否则被控端收到自己的候选，回环）
     *
     * 页面 load 完成前先入队（见 m_pendingIce），就绪后按序补灌。
     */
    void addRemoteIce(const QString &candJson);
    /**
     * 把 offer 灌进收流页（页面没就绪就先存 m_pendingOffer，就绪时补灌）。
     *
     * ⚠️ 灌之前会先 __resetPc()：被控端每个 offer 都是一个全新 peer，复用旧 pc 会
     * "called in wrong state"，表现为"第一台机器有画面、切到第二台永远黑屏"。
     */
    void deliverOffer(const QString &sdp);
    /**
     * 没人看的时候把离屏收流页（Chromium 渲染进程，实测 ~137MB）回收掉。
     *
     * 和被控端的 releaseRtcView() 是对称的一套：那边回收采集页，这边回收收流页。
     * 两边都不回收的话，几十台机器常年挂着的 WebEngine 就是纯粹的固定开销 ——
     * 老师只看其中一两台，剩下的 137MB × N 全在空转。
     */
    void releaseRtcView();
    void scheduleRtcViewReap();
    void cancelRtcViewReap();
    void stopRtcTimers();        // 抽帧/诊断节拍停表（收流页回收时必须一起停）

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
    QTimer *m_rtcReap = nullptr;          // 收流页延迟回收（scheduleRtcViewReap）
    // 抽帧节拍与 RTC 诊断心跳：必须是成员，不能在建页的加载回调里 new。
    // 之前挂在 this 上、页面回收时不停 —— 收流页每建一次就多留两个常驻 timer，
    // 一天下来断线重连几十轮就是上百个空转的 timer，全在 tick 里空判 nullptr。
    QTimer *m_grabTimer = nullptr;        // 收流页抽帧节拍（默认 40ms ≈ 25fps 上限）
    QTimer *m_rtcDiagTimer = nullptr;     // 每 2 秒捞一次收流页内部状态
    qint64 m_offlineSince = 0;            // 与云端断开的起始时刻（0 = 在线）

    QString m_url;
    QString m_token;

    QString m_currentUid;
    bool m_screenActive = true;   // 2026-10-08：带画面页是否可见（不可见则暂停单台拉流）
    QStringList m_capActions;              // 当前设备的指令能力全集（空=没上报过）
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

    // ── WebRTC 收流现场 ──
    QWebEngineView *m_rtcView = nullptr;
    QString m_rtcHtmlPath;        // rtc page temp file (created by QTemporaryFile), removed on exit
    QString m_pendingOffer;   // 收流页 load 完成前到的 offer 先存这儿（见 initRtcView 的补灌）
    bool m_rtcReady = false;
    QString m_rtcState = QStringLiteral("idle");

    // 收流页 load 完成前到的远端 ICE 先排这儿，就绪后补灌。
    // 为什么必须有这个队列：判断"页面能不能用"不能看 m_rtcView 是否非空 —— view 对象一 new
    // 出来就非空了，但 page->load() 是异步的，那段时间里 runJavaScript 是对着 about:blank
    // 执行的，window.__addIce 还不存在，调用**静默失败**（C++ 侧连个错都收不到），
    // 表现就是"ICE 收到了但 ICE 永远连不上"。offer 那边早就有 m_pendingOffer 处理同一件事，
    // 这一半当时漏了。
    bool m_rtcPageReady = false;
    QStringList m_pendingIce;
    // 队列上限：被控端一次建连最多也就几十个候选，超了说明有异常（比如对端在疯狂重连），
    // 与其无限攒着把内存吃光，不如丢掉并留一行日志。
    static const int kMaxPendingIce = 128;

    // ── 画面来源（JPEG 推送 vs WebRTC 实时流）──
    QString m_frameSource = QStringLiteral("none");
    // 最后一次收到 RTC 帧的时刻（ms）。用来判断"RTC 还活着吗"：
    // 活着就不要让 JPEG 轮询帧把它盖掉，超过 kRtcStaleMs 没来就认为断了、自动让 JPEG 接管。
    qint64 m_rtcLastFrameMs = 0;
    // 这个数直接决定"多久没 RTC 帧算断"。1.5s 是被控端抽帧节拍（25fps≈40ms）的近 40 倍，
    // 网络抖一两拍不会误判；又短于 JPEG 轮询的可见间隔，断了能在人感觉到之前就降回来。
    static const qint64 kRtcStaleMs = 1500;

    QJsonArray m_devices;
    QJsonArray m_classes;            // 2026-10-08 分班制：云端返回的全部班级
    QString m_currentClass;          // 2026-10-08 当前班级筛选（空=全部）
    QTimer *m_refreshTimer = nullptr; // 2026-10-08 设备表兜底轮询（15s，云端推送之外的保险）
    QJsonObject m_storage;            // 2026-10-08 云端存储概况（/api/storage 返回）
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
    int m_framesSinceCheck = 0;
    int m_lastFrameBytes = 0;
    double m_fps = 0.0;
    qint64 m_lastFpsCheckMs = 0;
    qint64 m_pingSentMs = 0;

    // ── 语音对讲（2026-10-07）──
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

    // ── 多班面板缩略图墙（2026-10-07，设计文档 3.2.2）──
    QStringList m_thumbUids;                  // 当前订阅缩略图的设备 uid 列表
    QHash<QString, QImage> m_thumbFrames;     // uid → 缩略图帧缓存
};
