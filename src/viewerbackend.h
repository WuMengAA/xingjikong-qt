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
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

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
    Q_PROPERTY(QString currentUid READ currentUid WRITE setCurrentUid NOTIFY currentUidChanged)
    /** 当前选中设备上报的指令能力全集（老版本被控端没这字段 → 空）。
     *  管端据此把不支持的按钮置灰，而不是让人点下去才吃到一句「未知指令」。 */
    Q_PROPERTY(QStringList capActions READ capActions NOTIFY capActionsChanged)
    Q_PROPERTY(QImage frame READ frame NOTIFY frameChanged)
    Q_PROPERTY(QJsonObject recordings READ recordings NOTIFY recordingsChanged)
    /** 画面进入「静态区」（连续多帧像素一致）→ 界面停止重绘。见 applyFrameBytes 的注释。 */
    Q_PROPERTY(bool screenStatic READ screenStatic NOTIFY statsChanged)
    Q_PROPERTY(double fps READ fps NOTIFY statsChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY statsChanged)
    Q_PROPERTY(int lastFrameBytes READ lastFrameBytes NOTIFY statsChanged)
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
    QString currentUid() const { return m_currentUid; }

    /**
     * 只给自检用：退订 → 重订，逼被控端把 WebRTC 连接重建一次（重新 offer）。
     *
     * 正常界面用不到：被控端只在 rtc-start 时建一次连接，而首次订阅必然触发 rtc-start。
     * 需要它的场景是"被控端那次 offer 发出去时并没人订阅（云端丢了），之后再订阅就没新 offer"，
     * 界面上退订重订一次是唯一不需要改被控端代码的重来办法。
     */
    Q_INVOKABLE void rtcRenegotiate(const QString &uid);
    QImage frame() const { return m_frame; }
    bool screenStatic() const { return m_screenStatic; }
    double fps() const { return m_fps; }
    int frameCount() const { return m_frameCount; }
    int lastFrameBytes() const { return m_lastFrameBytes; }
    /** 云端地址（设置页要显示；它是启动时从环境变量读的，不是用户填的）。 */
    QString cloudUrl() const { return m_url; }

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
    Q_INVOKABLE void sendAction(const QString &action, const QJsonObject &params = QJsonObject());
    Q_INVOKABLE void sendPing();
    Q_INVOKABLE void sendPointer(const QString &kind, double nx, double ny);
    Q_INVOKABLE void sendType(const QString &text);

    // ── 语音对讲（设计文档 3.3，2026-10-07）──
    /** 开始讲话：开麦克风采集 → 通知云端 audio.start → 每帧 PCM 二进制发出。 */
    Q_INVOKABLE bool startSpeaking();
    /** 停止讲话：关麦 → audio.stop。 */
    Q_INVOKABLE void stopSpeaking();
    /** 是否正在讲话。 */
    Q_INVOKABLE bool speaking() const;
    /** 最后一次开麦失败原因（QML 提示用）。 */
    Q_INVOKABLE QString speakError() const;

    // ── 屏幕广播（设计文档《屏幕广播-第一版设计》，2026-10-07）──
    /** 开始屏幕广播：抓屏 QTimer → 每帧 JPEG 二进制发云端 → broadcast.start。 */
    Q_INVOKABLE bool startBroadcast();
    /** 停止屏幕广播。 */
    Q_INVOKABLE void stopBroadcast();
    /** 是否正在广播。 */
    Q_INVOKABLE bool broadcasting() const;

    // ── 班级监控（设计文档 3.6 第一版）──
    /** 拉取云端录制列表（recordings/<uid>/ 分组），emit recordingsChanged。 */
    Q_INVOKABLE void fetchRecordings();
    /** 录制列表（QML 读，QJsonObject：uid → 文件数组）。 */
    QJsonObject recordings() const { return m_recordings; }

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
    void currentUidChanged();
    /** 当前选中设备的能力清单（actions）变了 —— 按钮门控要跟着刷。 */
    void capActionsChanged();
    void frameChanged();
    void statsChanged();
    void cloudUrlChanged();
    void recordingsChanged();
    /** 站点账号状态/文本变化（登录成功、失败、票换了、用户点了忘记）。 */
    void accountChanged();
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
    QImage m_frame;
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
    AudioCapture *m_audioCapture = nullptr;   // 麦克风采集（懒创建）
    bool m_speaking = false;                  // 当前是否在讲话
    QString m_speakError;

    // ── 屏幕广播（2026-10-07）──
    QTimer *m_bcastTimer = nullptr;           // 抓屏节拍（3fps）
    bool m_broadcasting = false;              // 是否在广播

    // ── 班级监控（2026-10-07）──
    QJsonObject m_recordings;                 // recordings/<uid>/[文件] 分组缓存
};
