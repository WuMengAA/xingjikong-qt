// 星集控 · 管理端后端实现（从 main.cpp 原样搬过来，行为保持一致）
#include "viewerbackend.h"
#include "permission_manager.h"   // RBAC（2026-10-20）：前端权限镜像层
#include "oauthlogin.h"
#include "vnc_client.h"       // 2026-10-18：远控 VNC 客户端（libvncclient，RFB over WS）
#include "vnc_image_provider.h" // 远控画面 → QML（image://vnc/live）
#include "class/class_service.h"   // 班级域（#460）：ClassService + typed Models
#include "class/class_admin.h"     // 班级管理写操作（2026-10-24 方案 B）
#include "audio_capture.h"   // 语音对讲（2026-10-07）：winmm waveIn 采集
#include "audio_playback.h"  // B6（2026-10-11）：winmm waveOut 播放学生回传的 audio/pcm
#include "opus_codec.h"      // 2026-10-21（P1）：Opus 解码（shared/audio，与 agent 同源）
#include <cmath>             // std::isfinite：拦住 NaN/Inf 坐标（2026-10-11）
#include <QAbstractSocket>
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QScreen>
#include <QPixmap>
#include <QBuffer>
#include <QGuiApplication>
#include <QStyleHints>
#include <QJsonDocument>
#include <QJsonValue>
#include <QDesktopServices>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSettings>
#include <QStandardPaths>
#include <QFile>
#include <QClipboard>     // 诊断与反馈：复制诊断信息到剪贴板
#include <QSysInfo>       // 诊断与反馈：系统信息
#include <QProcess>
#include "winlauncher.h"   // 2026-10-19 需求①：子进程静默启动（杜绝建快捷方式弹 cmd 窗）
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>
#include "android_notify.h"   // 2026-10-11：手机系统通知栏横幅（桌面为空实现）

#if defined(Q_OS_ANDROID)
#include <QPermissions>                        // 2026-10-13：开麦权限走 Qt 官方 QPermission（QMicrophonePermission，QtCore 自带，无需 Multimedia）
#include <QtCore/private/qandroidextras_p.h>   // 2026-10-11：手机文件选择器 startActivity（Qt 官方 Android 私有 API）
#include <QFutureWatcher>
#include <QJniArray>                          // 2026-10-11：文件传输 SAF——读 content:// 用
#include <QSaveFile>                          // 复制到缓存目录（写完才算成功，避免半截坏文件）
#include <QtCore/qcoreapplication_platform.h> // QNativeInterface::QAndroidApplication::context()
#endif

#include <algorithm>   // std::sort（离线台账清理与排序，见 refreshDevices）
// ── 语音对讲 / 屏幕广播的时长上限（2026-10-07，用户点名："没有时长限制"）──
// 开麦采集和抓屏推流都是**持续吃资源**的 —— 没人关就一直占着输入设备、一直 3fps 烧 CPU
// 和上行带宽。以前只有"再点一次"才停，老师切去改作业、下课后忘了关就一路开着到关机。
// 所以两处都上硬上限，到点自动停，并且**如实说一句**（不静默，也不说成"还在讲"）。
static constexpr int kDefaultSpeakMaxMs = 3 * 60 * 1000;    // 讲话 3 分钟
static constexpr int kDefaultBcastMaxMs = 10 * 60 * 1000;   // 广播 10 分钟
// 两个 helper 都写成文件作用域 static：文件里本来已经有**另一个**匿名命名空间（分片常量
// 那一簇），再开一个等于白白多一层 —— 内部链接的函数在文件作用域 static 是等价的。
// 2026-10-13 架构拆分：fmtLimit / envMaxMs 已提升到 viewerbackend.h（inline，跨 TU 共享），
// 此处不再定义（static + header inline 会重定义冲突）。
namespace {
// （注释保留：kFileChunkBytes / kPushAckTimeoutMs / humanBytes 已提升到
// viewerbackend.h 两个翻译单元共享，此处不再重复定义。本 namespace 余下的
// fmtLimit/envMaxMs/makeEnvelope/jsonStringLiteral 等仍在此匿名命名空间内。）
/**
 * 协议 v1 信封：{"v":1,"type":...,"id":...,"ts":...,"payload":{...}}
 * 文本消息统一走它；二进制帧（画面）另走 [1B 版本][2B 大端 headerLen][header][JPEG]。 */
QString makeEnvelope(const QString &type, const QJsonObject &payload)
{
    QJsonObject o;
    o.insert(QStringLiteral("v"), 1);
    o.insert(QStringLiteral("type"), type);
    o.insert(QStringLiteral("id"), QString());
    o.insert(QStringLiteral("ts"), QJsonValue(QDateTime::currentMSecsSinceEpoch()));
    o.insert(QStringLiteral("payload"), payload);
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
constexpr int kFrameHeaderMax = 4096;
constexpr int kReconnectMs = 5000;
// 管理端默认连的生产云端（2026-10-06：从本机 dev 的 127.0.0.1:8788 改过来）。
// ⚠️ 只有"没设 STE_VIEWER_URL"时才用；本机联调请显式设 STE_VIEWER_URL=ws://127.0.0.1:8788/ws/viewer。
// 一律 wss（走 443 的公网域名），别抄 ws —— 明文 ws 连公网会被中间人直投。
static const QString kDefaultViewerUrl = QStringLiteral("wss://control.245959623.xyz/ws/viewer");
/**
 * 只有管理员才做得了的操作（2026-10-06 打磨：权限门控）。
 *
 * 判据不是"危险不危险"，是**影响面**：
 *   ① 不可逆（锁屏/重启/关机）、② 能把整台教室机交给外人（远控/终端开一条 shell）、
 *   ③ 一次动作打到所有设备（广播、定时）、④ 把程序推起来放到前台（launch）。
 * 只读的（截图/日志/探活）和改一台机器一点小状态的（音量/通知/录播）教师都能用，
 * 否则教师这个角色等于被锁在门外——那角色就是装饰。
 *
 * ⚠️ 这张表是**界面门控用**的，不是安全边界：真正的闸门在被控端和云端。
 * 界面置灰只是"别让人白点一下"，别拿它当权限系统用。
 */
const QStringList &adminOnlyActions()
{
    static const QStringList kAdmin = {
        QStringLiteral("lock"),
        QStringLiteral("reboot"),
        QStringLiteral("shutdown"),
        QStringLiteral("remote_control_start"),
        QStringLiteral("terminal_open"),
        QStringLiteral("schedule_shutdown"),
        QStringLiteral("schedule_reboot"),
        QStringLiteral("launch_app"),
        QStringLiteral("broadcast")   // 集控页「广播」：一发打所有在线设备
    };
    return kAdmin;
}
/* ── 静态区判定（UU 远程同款思路的轻量版）─────────────────────────────
 * 被控端画面没在动的时候，收到多少帧都是同一张图 —— 解码 + 重绘一遍纯属白烧 CPU。
 * 判定办法：给每帧算一个**稀疏指纹**（隔 7919 字节抽一个点，4096 个点，比整图遍历便宜得多，
 * 但鼠标一动、窗口一开立刻就变），连续 kStaticStreak 帧指纹一致就认定"进了静态区"，
 * 界面停止换 tick（QML 的 Image.source 不变 → 不再 requestImage → 不再解码重绘）。
 *
 * 为什么不用"JPEG 字节全等"来判：被控端每次抓屏都会重新编码一次，
 * 同一张画面编出来的字节不可能一模一样，字节比较会永远判不出静态。
 *
 * 为什么不用定时器降帧率：降帧率只是从 15fps 变 5fps，带宽和解码照样在跑；
 * 这里是"真不动就不画"，跟画面里有没有东西动无关。 */
constexpr int kStaticStreak = 8;          // 约 0.5 秒（被控端 15fps）——太短会误判、太长画面发僵
/** 画面指纹：宽高 + 整块缓冲里的伪随机采样和。同尺寸同画面 → 同指纹。 */
quint64 frameFingerprint(const QImage &img)
{
    if (img.isNull()) return 0;
    const qsizetype total = static_cast<qsizetype>(img.sizeInBytes());   // Qt 里没有 byteCount()，别照抄旧代码
    if (total <= 0) return 0;
    const uchar *p = img.constBits();
    quint64 h = (quint64)img.width() << 32 | (quint64)img.height();
    constexpr qsizetype stride = 7919;    // 质数：抽样点不会周期性落在同一片区域
    for (int k = 0; k < 4096; ++k) {
        h = h * 131 + p[(qsizetype)k * stride % total];
    }
    return h;
}
/**
 * 把 QString 转成 **带双引号的合法 JSON 字符串字面量**（即 "\"...\""，含外层引号）。
 *
 * 为什么不用手写转义：SDP 里全是 / 换行 引号，ICE candidate 是嵌套 JSON，
 * 自己拼一定会在某个特殊字符上炸，而且炸出来的是 JS 语法错、报在 runJavaScript 的字符串里，
 * 很难定位。这里借 Qt 自己的 QJsonObject/QJsonDocument 去转义 —— 转义规则只认一处，不会漂。
 *
 * 顺带把坑记下：QString 没有 toJsonEscapedString()，QJsonDocument 也没有接受 QJsonValue 的
 * 构造函数（QJsonDocument(QJsonValue) 编译不过），别再往那个方向试。
 */
QString jsonStringLiteral(const QString &s)
{
    QJsonObject o;
    o.insert(QStringLiteral("_"), s);
    const QByteArray full = QJsonDocument(o).toJson(QJsonDocument::Compact);  // {"_":"..."}
    // lastIndexOf(':') 取的是最后一个冒号，即我们那个 "_" 对应的值起点；
    // mid 之后正好是带双引号的字面量本体
    return QString::fromUtf8(full.mid(full.lastIndexOf(':') + 1));
}
} // namespace
ViewerBackend::ViewerBackend(QObject *parent)
    : QObject(parent)
{
    // 2026-10-13 审计 2.9：HTTP 客户端统一在构造创建——原来 6 处 `if (!m_nam) m_nam =
    // new ...` 散落在各接口里（checkQmlPatch/fetchRecordings/requestClasses/requestStorage/
    // fetchEvents/ensureCloudTicket），每处都要判空。构造创建后这些检查全删。
    m_nam = new QNetworkAccessManager(this);
    // 班级域（#460）：ClassService 持班级工作台数据面。它自己不认识 WS ——
    // 出海信号直接接到 sendEnvelope（与其余出站消息同一条 v1 信封路）。
    m_classService = new ClassService(this);
    connect(m_classService, &ClassService::sendRequested,
            this, &ViewerBackend::sendEnvelope);
    // 班级管理写操作（2026-10-24 方案 B）：自带 QNAM，凭据每次现取
    // httpBaseUrl()/viewerToken()（与 InteractBridge 同口径，不落第二份）。
    m_classAdmin = new ClassAdmin(this, this);
    // 重认证门（2026-10-24）：ClassAdmin 的 HTTP 403 与 WS error 通道两路
    // REAUTH_REQUIRED 收敛成一个信号，QML 只监听 reauthRequired 一处。
    connect(m_classAdmin, &ClassAdmin::reauthRequired,
            this, &ViewerBackend::reauthRequired);
    // 身份与提醒开关先落下来（QSettings），后面所有门控判断都基于它。
    loadPrefs();
    // 主题三档（E4，2026-10-11）：0 浅色 / 1 深色 / 2 跟随系统。默认浅色，读自 QSettings。
    {
        QSettings s;
        m_themeMode = qBound(0, s.value(QStringLiteral("ui/themeMode"), 0).toInt(), 2);
    }
    // 跟随系统：取 OS 配色并监听变化（Qt::ColorScheme::Dark 一档）
    if (QStyleHints *sh = QGuiApplication::styleHints()) {
        m_systemDark = (sh->colorScheme() == Qt::ColorScheme::Dark);
        QObject::connect(sh, &QStyleHints::colorSchemeChanged, this,
                         [this](Qt::ColorScheme cs) {
                             const bool d = (cs == Qt::ColorScheme::Dark);
                             if (d != m_systemDark) { m_systemDark = d; emit systemDarkChanged(d); }
                         });
    }
    // 文件推送的"发下一片"由回执驱动：收到一片 done 才发下一片。
    // 不这样写就只能靠定时器盲发，一旦被控端拒收一片，后面全乱序、越堆越多。
    QObject::connect(this, &ViewerBackend::resultReceived, this,
                     [this](const QString &, const QString &action, const QString &state,
                            const QString &result, const QString &error, const QString &,
                            const QJsonObject &data) {
                         if (action != QStringLiteral("file_push")
                                 && action != QStringLiteral("file_chunk")
                                 && action != QStringLiteral("file_done")) return;
                         if (m_fileState == QStringLiteral("idle")
                                 || m_fileState == QStringLiteral("done")
                                 || m_fileState == QStringLiteral("failed")) return;
                         // 分两档看回执：state=sent 只是"云端收下了、机器还没轮到"，
                         // 这时 result 还是空的，拿它判成败会把"正在推"误报成"推失败"。
                         // 只有 executed（机器真做了）之后，result != done 才算失败。
                         if (result != QStringLiteral("done")) {
                             if (state != QStringLiteral("executed")) return;   // 还在路上，继续等
                             // 失败原因原样照搬：被控端给的是"文件名缺少扩展名"这类人话，
                             // 我们不该自己再编一套说法（两套口径迟早对不上）
                             setFileFail(error.isEmpty()
                                             ? QStringLiteral("%1 被拒（没说原因）").arg(action)
                                             : error);
                             return;
                         }
                         // 只要对面还在回话，就续一次看门狗 —— 它只在"该来的回执没来"时咬人
                         if (m_pushWatchdog) m_pushWatchdog->start(kPushAckTimeoutMs);
                         if (action == QStringLiteral("file_push")) {
                             logf("[viewer] file_push 会话已开，开始分片（总 %lld 字节）",
                                  (long long)m_fileTotal);
                            // 本机文件在 pushFile() 里就开好了（setFileName + open 一起在那儿）。
                            // 这里只兜底：推的过程中文件被删了/被别人换走。
                            // errorString() 必须带上 —— 只说"打不开"等于没报，
                            // 日志里得能看见是"文件不在了"还是"被占用/被拒绝"
                            if (!m_pushFile.isOpen() && !m_pushFile.open(QIODevice::ReadOnly)) {
                                // 诊断行：这里栽过一次（漏 setFileName，errorString 只会给无用的
                                // "Unknown error"）。出问题时要能一次看清是"句柄被谁关了"
                                // 还是"文件真没了"，所以 fileName/exists/size/error 全打出来
                                logf("[viewer] 诊断 源文件句柄：isOpen=%d fileName=[%s] exists=%d size=%lld err=[%s]",
                                     (int)m_pushFile.isOpen(),
                                     m_pushFile.fileName().toUtf8().constData(),
                                     (int)m_pushFile.exists(),
                                     (long long)m_pushFile.size(),
                                     m_pushFile.errorString().toUtf8().constData());
                                setFileFail(QStringLiteral("本机文件读不开：%1（%2）")
                                                .arg(m_pushPath)
                                                .arg(m_pushFile.errorString()));
                                return;
                            }
                             m_fileState = QStringLiteral("sending");
                             emit fileProgressChanged();
                             sendNextChunk();
                         } else if (action == QStringLiteral("file_chunk")) {
                             sendNextChunk();
                         } else if (action == QStringLiteral("file_done")) {
                             if (m_pushWatchdog) m_pushWatchdog->stop();
                             // 对账：被控端回的是它**实收**的字节数。和这边发出的不一致就是缺片
                             // （用户报的"推完的文件是空的/损坏"若真发生，这里会立刻跳出来，
                             // 而不是等谁去教室机上打开才发现）。
                             const qint64 got = data.value(QStringLiteral("bytes")).toVariant().toLongLong();
                             if (data.value(QStringLiteral("cancelled")).toBool(false)) {
                                 m_fileState = QStringLiteral("failed");
                                 m_fileError = QStringLiteral("教室机把这次推送当取消了（已推 %1，那边已删掉半截文件）")
                                                   .arg(humanBytes(m_fileBytes));
                                 if (m_pushFile.isOpen()) m_pushFile.close();
                                 logf("[viewer] ⚠️ 教室机回执 cancelled=true（已推 %lld 字节）",
                                      (long long)m_fileBytes);
                                 emit fileProgressChanged();
                                 return;
                             }
                             if (got >= 0 && got != m_fileBytes) {
                                 m_fileState = QStringLiteral("failed");
                                 m_fileError = QStringLiteral("对不上账：我这发了 %1，教室机只收到 %2")
                                                   .arg(humanBytes(m_fileBytes)).arg(humanBytes(got));
                                 if (m_pushFile.isOpen()) m_pushFile.close();
                                 logf("[viewer] ⚠️ 文件对账不符：本地 %lld / 教室机实收 %lld",
                                      (long long)m_fileBytes, (long long)got);
                                 emit fileProgressChanged();
                                 return;
                             }
                             m_fileState = QStringLiteral("done");
                             m_fileTarget = data.value(QStringLiteral("path")).toString();
                             m_fileError.clear();
                             if (m_pushFile.isOpen()) m_pushFile.close();
                             logf("[viewer] ✅ 文件已推到教室机：%s（%lld 字节）",
                                  m_fileTarget.toUtf8().constData(), (long long)m_fileBytes);
                             emit fileProgressChanged();
                         }
                     });
    // ── 语音 / 广播的时长上限（2026-10-07）──
    // 见文件头那条注释：两处都是"开着不关"的持续资源，必须有硬上限。
    m_speakMaxMs = envMaxMs("STE_MAX_SPEAK_MS", kDefaultSpeakMaxMs);
    m_bcastMaxMs = envMaxMs("STE_MAX_BCAST_MS", kDefaultBcastMaxMs);
    m_speakLimit = new QTimer(this);
    m_speakLimit->setSingleShot(true);
    m_bcastLimit = new QTimer(this);
    m_bcastLimit->setSingleShot(true);
    m_leftTick = new QTimer(this);
    m_leftTick->setInterval(1000);   // 驱动界面上的"还剩几秒"
    QObject::connect(m_speakLimit, &QTimer::timeout, this, [this] {
        stopSpeaking(QStringLiteral("讲话到 %1，已自动停（麦克风已关）").arg(fmtLimit(m_speakMaxMs)));
    });
    QObject::connect(m_bcastLimit, &QTimer::timeout, this, [this] {
        stopBroadcast(QStringLiteral("屏幕广播到 %1，已自动停").arg(fmtLimit(m_bcastMaxMs)));
    });
    QObject::connect(m_leftTick, &QTimer::timeout, this, [this] {
        auto tick = [](int &leftMs, int &leftSec) {
            if (leftMs <= 0) return 0;
            leftMs = qMax(0, leftMs - 1000);
            const int s = (leftMs + 999) / 1000;
            if (s != leftSec) { leftSec = s; return 1; }
            return 0;
        };
        int dirty = 0;
        if (m_speaking) dirty |= tick(m_speakLeftMs, m_speakLeftSec);
        if (m_broadcasting) dirty |= tick(m_bcastLeftMs, m_bcastLeftSec);
        if (dirty) emit limitsChanged();
        if (!m_speaking && !m_broadcasting) m_leftTick->stop();
    });
    qInfo("[viewer] 语音对讲上限 %lld 秒 / 屏幕广播上限 %lld 秒",
          (long long)m_speakMaxMs / 1000, (long long)m_bcastMaxMs / 1000);
    // ── 远程终端的两次"等回话"收口（2026-10-07 修 #95）──
    // 下发 terminal_open / terminal_close 之后本地状态都是 Pending（**关的时候也是 Pending，
    // 不是 Idle**）。被控端/本机不回话时，**原来没有任何东西把状态放下来** ⇒ 界面永远卡在
    // "等本机确认…"或"正在关…"，按钮永远是"关终端"、输入框永远灰，再点"开终端"又被
    // "这台已经有终端会话了"拒掉 —— 就是用户说的"终端打开后关不掉、也连不上"。
    // 两条定时器到点一律回 Idle，并且把原因说出来（不假装成功，也不是静默超时）。
    m_termWait = new QTimer(this);
    m_termWait->setSingleShot(true);
    m_termWait->setInterval(envMaxMs("STE_TERM_WAIT_MS", 20000));   // 等本机点头 20 秒
    m_termCloseWait = new QTimer(this);
    m_termCloseWait->setSingleShot(true);
    m_termCloseWait->setInterval(2500);                              // 等关断回话 2.5 秒
    QObject::connect(m_termWait, &QTimer::timeout, this, [this]() {
        if (m_termState != TermPending) return;   // 已经通了/已经关了，别乱动
        const QString sid = m_termSid;
        m_termClosing = false;
        setTermState(TermIdle, sid, QStringLiteral("等本机点头超时（本机没允许，会话没开）"));
        emit terminalClosed(sid, QStringLiteral("等本机点头超时"));
        logf("[viewer] TERM 等本机点头超时（%d ms），已回 Idle",
             (int)m_termWait->interval());
    });
    QObject::connect(m_termCloseWait, &QTimer::timeout, this, [this]() {
        if (m_termState != TermPending) return;
        const QString sid = m_termSid;
        m_termClosing = false;
        setTermState(TermIdle, sid, QStringLiteral("关断没回话（按已关处理，可以重开）"));
        emit terminalClosed(sid, QStringLiteral("关断没回话"));
        logf("[viewer] TERM 关断没回话，按已关处理并回 Idle");
    });
    // 启动时把两条兜底时长打出来：终端连不上这类病，排障第一眼就要看"是不是被人改短/改没了"，
    // 日志里有数就不用猜（等本机点头那一条可用 STE_TERM_WAIT_MS 调）。
    qInfo("[viewer] 终端兜底：等本机点头 %lld 秒 / 等关断回话 %lld 秒",
          (long long)m_termWait->interval() / 1000,
          (long long)m_termCloseWait->interval() / 1000);
    // ── 2026-10-11：文件推送看门狗 ──
    // 发下一片由回执驱动（收到一片 done 才发下一片），但云端回执可能永远不来
    // （教室机离线 / 被控端崩 / 网络断）。没这把锁，m_fileState 永久卡在 "sending"，
    // 按钮锁死、界面像卡住。每次收到 file_push/chunk/done 回执就 start() 续命，
    // 超时（kPushAckTimeoutMs=15s）即判失败并收口。singleShot 保证只咬一次。
    m_pushWatchdog = new QTimer(this);
    m_pushWatchdog->setSingleShot(true);
    m_pushWatchdog->setInterval(kPushAckTimeoutMs);
    QObject::connect(m_pushWatchdog, &QTimer::timeout, this, [this] {
        setFileFail(QStringLiteral("推送超时：%1 秒没收到教室机回执（可能它离线了，或网络断了）")
                            .arg(kPushAckTimeoutMs / 1000));
    });
}
ViewerBackend::~ViewerBackend()
{
    // 2026-10-21（审查 #3）：engine 先于本对象析构 ⇒ image provider（vnc）已被
    // engine 删除；这里只清裸指针不 delete（避免 double free）。退出阶段已无事件
    // 循环、理论风险极低，但清掉后 update() 调用点的判空（m_vncProvider &&）才
    // 在 engine 析构后真正生效。
    m_vncProvider = nullptr;
    // 修复③（2026-10-19）：退出前把远控会话礼貌地收掉 —— 以前只停本地
    // VncClient，被控端侧 g_remoteControl 永真（灵动岛冻结 + server 空转），
    // 只能等被控端看门狗（25s 无保活）兜底，白冻 25s。send 后 flush 一把，
    // 尽量让字节在进程退出前真正出网（WS 退出竞态仍在，看门狗是最终兜底）。
    if (m_remoteControlling && m_ws && m_ws->state() == QAbstractSocket::ConnectedState
        && m_authed) {
        sendAction(QStringLiteral("remote_control_stop"), QJsonObject());
        m_ws->flush();
        logf("[viewer] 进程退出，收掉对 %s 的远控", m_remoteControlUid.toUtf8().constData());
        m_remoteControlling = false;
        m_remoteControlUid.clear();
    }
    // 远控会话必须先收干净再让 QML/引擎收尾：VncClient 的解码线程仍在读
    // 帧缓冲，留到成员析构顺序里去停，容易踩到"引擎已拆、回调还在发信号"。
    if (m_vnc) { m_vnc->stop(); m_vnc = nullptr; }
}
// ───────────────────────────────────────────────────────────────────────────
// 版本 / 身份 / 角色 / 提醒偏好
// ───────────────────────────────────────────────────────────────────────────
/**
 * 编译期注入的版本号（Windows 上带三个部分才显示版本号属性，只给两个的话
 * Qt 会写 "8.0.0.0" 这种被当成文件的怪东西 —— 前面三段拼够三段就行）。
 */
#ifndef STELARITH_VIEWER_VERSION_STRING
#  define STELARITH_VIEWER_VERSION_STRING "0.0.0"
#endif
/**
 * 界面「关于」那张卡显示的版本号。
 *
 * ⚠️ 2026-10-08 修：这里原来写的是
 *      STELARITH_MAKE_VERSION(STELARITH_VIEWER_VERSION_STRING)
 *    而那个宏的签名是 (a, b, c)（`#a "." #b "." #c`）。只喂一个参数 ⇒ MSVC 报 C4003
 *    「宏的实际参数不足」，并且因为 `#` 不做宏展开，展开结果是把**形参名字符串化**：
 *      "STELARITH_VIEWER_VERSION_STRING" "." "."
 *    ——「关于」页上的版本行显示的是宏名而不是版本号（有截图实证）。
 *    这个 bug 一直存在，只是那张卡此前高度塌成 0、根本看不见，没人发现。
 *    CMake 侧给的本来就是**完整版本串**（含 -rc.2），不需要再拼三段，直接取用。
 */
QString ViewerBackend::version() const
{
    return QString::fromLatin1(STELARITH_VIEWER_VERSION_STRING);
}
/**
 * 是否处在"已经登录"的状态。
 *
 * 判据严格按**手上有没有能连云端的凭据**：有接入票，或者配了静态令牌，都算。
 * 只看 sessionToken 不够 —— 那种情况下界面会显示"已登录"、但云端连不上，
 * 用户对着一个登录着的顶栏纳闷为什么没设备（这就是以前那个坑）。
 */
bool ViewerBackend::loggedIn() const
{
    // ⚠️ 判据必须和 refreshAccountText() 一致（那边用的是 ticketUsable()）。
    //    只看"票/令牌非空"的话：接入票 30 天到期后这里仍返回 true，
    //    界面显示"已登录"、实际连不上云端，用户看到的是"登录着但用不了"，
    //    而且找不到重新登录的入口 —— 正是"登录有问题"最容易被误报的形态。
    if (!m_cloudTicket.isEmpty()) return ticketUsable();
    return !m_token.isEmpty();   // 老路子的静态令牌没有时效，非空即算有凭据
}
QString ViewerBackend::accountName() const { return m_accountUser; }
QString ViewerBackend::role() const { return m_role; }
bool ViewerBackend::mayDo(const QString &perm) const
{
    // 2026-10-14 修（审计 P0）：这里原来漏了一个 `!`，把权限门控整个判反。
    // 教师身份下 adminOnlyActions() 里那批危险动作（lock/reboot/shutdown/远控/
    // terminal_open/定时关机/launch_app/broadcast）一律返回 true = **放行**，
    // 反而是表外的普通动作被拦。
    // 文档契约在 viewerbackend.h:357-358 写得很明确：「没在这张表里的一律算
    // 『所有人都能做』」—— 也就是 !contains。grep 全库 3 个调用点
    //（Main.qml:3048 permOk / 3160 broadcastAll / 3412 命令面板）传的全都是动作名，
    // 没有一个传 "admin" 这类权限键，所以补 `!` 就同时改对了全部调用方。
    // 一直没暴露是因为默认身份是 admin（admin 分支恒 true），教师那路从来没被验证过。
    if (m_role != QStringLiteral("admin")) return !adminOnlyActions().contains(perm);
    return true;
}
bool ViewerBackend::setRole(const QString &role)
{
    // 2026-10-20（RBAC 第二批）：服务端下发受限身份期间，本地不可切回 admin ——
    // 否则「老师身份 + 本地切一下」就把服务端门控绕过去了。全权连接不锁（行为=今天）。
    if (m_roleLocked) {
        logf("[viewer] 身份切换被拒：当前身份由服务端下发（锁定的）");
        return false;
    }
    const QString r = role.trimmed().toLower();
    const bool ok = (r == QStringLiteral("admin") || r == QStringLiteral("teacher"));
    if (!ok) return false;
    if (r == m_role) return true;
    // 没登录按最低权限看：这是"当前身份"的默认值，别让它显示成管理员。
    if (!loggedIn() && r == QStringLiteral("admin")) return false;
    m_role = r;
    QSettings s;
    s.setValue(QStringLiteral("account/role"), r);
    logf("[viewer] 身份切到 %s（顶栏徽标与按钮门控同步）",
         r.toUtf8().constData());
    refreshGateState();
    return true;
}
/**
 * 2026-10-20（RBAC 第二批）：服务端身份落地 —— 受限身份由 roles 推导并锁定本端角色。
 * platform_admin / admin ⇒ admin；其余（teacher/assistant/未知）⇒ teacher
 * （mayDo 已把 adminOnlyActions 里的危险动作从非 admin 身份拦掉，无需第三档）。
 */
void ViewerBackend::applyServerRole(const QStringList &roles)
{
    m_roleLocked = true;   // 锁定：setRole 在此后拒绝切换，防本地切回 admin 绕门控
    const QString wanted = (roles.contains(QStringLiteral("platform_admin"))
                            || roles.contains(QStringLiteral("admin")))
                               ? QStringLiteral("admin")
                               : QStringLiteral("teacher");
    if (m_role == wanted) {
        refreshGateState();   // 幂等：通知 QML 徽标/门控刷新
        return;
    }
    m_role = wanted;
    QSettings s;
    s.setValue(QStringLiteral("account/role"), wanted);
    logf("[viewer] 身份由服务端下发并锁定：%s", wanted.toUtf8().constData());
    refreshGateState();
}
void ViewerBackend::openRegisterPage()
{
    if (m_siteUrl.isEmpty()) {
        setStatus(QStringLiteral("还没配站点地址（viewer.env 里加 STE_SITE_URL）"), true);
        return;
    }
    const QUrl u(QStringLiteral("%1/register").arg(m_siteUrl));
    logf("[viewer] 打开站点注册页 %s", u.toString().toUtf8().constData());
    steOpenUrlExternal(u);   // 提权安全（oauthlogin.h）
}
QString ViewerBackend::registerUrl() const
{
    // 站点改了注册路由时，用 STE_REGISTER_URL 覆盖即可，不动代码
    const QString override = qEnvironmentVariable("STE_REGISTER_URL", QString()).trimmed();
    if (!override.isEmpty()) return override;
    if (m_siteUrl.isEmpty()) return QString();
    return QStringLiteral("%1/register").arg(m_siteUrl);
}
void ViewerBackend::openExternal(const QString &url)
{
    if (url.isEmpty()) return;
    steOpenUrlExternal(QUrl(url));   // 提权安全（oauthlogin.h）
}
void ViewerBackend::setNotifyOnDone(bool on)
{
    if (m_notifyOnDone == on) return;
    m_notifyOnDone = on;
    QSettings s;
    s.setValue(QStringLiteral("prefs/notifyOnDone"), on);
    emit notifyPrefChanged();
}
void ViewerBackend::setNotifyOnOffline(bool on)
{
    if (m_notifyOnOffline == on) return;
    m_notifyOnOffline = on;
    QSettings s;
    s.setValue(QStringLiteral("prefs/notifyOnOffline"), on);
    emit notifyPrefChanged();
}
/**
 * 弹一条本机提示（状态行 + 可选桌面气泡）。
 *
 * 以前"提醒"那两张卡是画着玩的（两个空心的圆，点了没反应）—— 2026-10-06 打通：
 * 操作完成、设备离线这两件事在后端本来就是现成的时刻，直接接上就行。
 */
void ViewerBackend::notifyPref(const QString &text)
{
    setStatus(text, false);
    // 2026-10-09：一切通知统一走灵动岛（DynamicIsland.showNote），不再弹系统托盘气泡。
    // 状态行那一行仍写（setStatus）—— 就算窗口最小化了，状态也不会丢。
    // 2026-10-11：手机 App 额外补一条**系统通知栏**横幅（后台/息屏也能看到）；
    // 桌面端 androidSystemNotify 是空实现，行为不变。
#if defined(Q_OS_ANDROID)
    androidSystemNotify(QStringLiteral("星集控"), text);
#endif
    emit islandNote(text, QString(), QStringLiteral("bell"));
}
/**
 * 身份变了之后统一刷一遍：顶栏徽标、按钮置灰、连接区那行账号文案全挂着这些读数，
 * 一个一个 emit 容易漏（漏了就是"切了身份但按钮还是能点"）。
 */
void ViewerBackend::refreshGateState()
{
    emit roleChanged();
    emit accountChanged();
    refreshAccountText();
}
/** 提醒开关从 QSettings 读回来（构造时调用一次）。 */
void ViewerBackend::loadPrefs()
{
    QSettings s;
    const auto roleValue = s.value(QStringLiteral("account/role")).toString().toLower();
    // 默认值 = 管理员：这是单机自用场景（管理员在教室机上开这个程序），
    // 一上来就把按钮全灰掉比"默认宽松"更招骂。
    m_role = (roleValue == QStringLiteral("teacher")) ? QStringLiteral("teacher")
                                                      : QStringLiteral("admin");
    m_notifyOnDone = s.value(QStringLiteral("prefs/notifyOnDone"), true).toBool();
    m_notifyOnOffline = s.value(QStringLiteral("prefs/notifyOnOffline"), true).toBool();
    // 首启引导标记：默认 false（没走过引导）。写盘走 markFirstRunDone()，这里只负责读回。
    m_firstRunDone = s.value(QStringLiteral("prefs/firstRunDone"), false).toBool();
    // 进程黑名单（2026-10-11 需求#6）：默认空。存的是进程名，比较时不区分大小写。
    m_procBlacklist = s.value(QStringLiteral("proc/blacklist")).toStringList();
    if (!m_procBlacklist.isEmpty())
        logf("[viewer] 已载入进程黑名单 %d 条：%s",
             m_procBlacklist.size(), m_procBlacklist.join(QLatin1String(", ")).toUtf8().constData());
}

void ViewerBackend::setThemeMode(int m)
{
    m = qBound(0, m, 2);
    if (m == m_themeMode) return;
    m_themeMode = m;
    QSettings s;
    s.setValue(QStringLiteral("ui/themeMode"), m);
    emit themeModeChanged(m);
}

QStringList ViewerBackend::procBlacklist() const { return m_procBlacklist; }

void ViewerBackend::addProcToBlacklist(const QString &name)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) return;
    // 不区分大小写去重：Windows 进程名大小写随意（Chrome.exe / chrome.EXE 都见过），
    // 存两份会让"一键关闭"看起来只关掉一半。
    for (const QString &e : m_procBlacklist)
        if (e.compare(n, Qt::CaseInsensitive) == 0) return;
    m_procBlacklist.append(n);
    QSettings s;
    s.setValue(QStringLiteral("proc/blacklist"), m_procBlacklist);
    logf("[viewer] 进程黑名单新增：%s", n.toUtf8().constData());
    emit procBlacklistChanged();
}

void ViewerBackend::removeProcFromBlacklist(const QString &name)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) return;
    const int before = m_procBlacklist.size();
    m_procBlacklist.removeIf([&n](const QString &e) { return e.compare(n, Qt::CaseInsensitive) == 0; });
    if (m_procBlacklist.size() == before) return;
    QSettings s;
    s.setValue(QStringLiteral("proc/blacklist"), m_procBlacklist);
    logf("[viewer] 进程黑名单移除：%s", n.toUtf8().constData());
    emit procBlacklistChanged();
}

int ViewerBackend::killBlacklisted(const QVariantList &items)
{
    if (m_currentUid.isEmpty()) {
        logf("[viewer] FAIL 没选设备，一键关闭黑名单无从下手");
        return -1;   // -1 = 没选设备（区别于 0 = 选了但一个都没匹配上）
    }
    if (m_procBlacklist.isEmpty()) return 0;

    int sent = 0;
    for (const QVariant &v : items) {
        const QVariantMap m = v.toMap();
        const QString name = m.value(QStringLiteral("name")).toString();
        const int pid = m.value(QStringLiteral("pid")).toInt();
        if (name.isEmpty() || pid <= 0) continue;
        bool hit = false;
        for (const QString &b : m_procBlacklist) {
            if (name.compare(b, Qt::CaseInsensitive) == 0) { hit = true; break; }
        }
        if (!hit) continue;
        // 受保护的进程（系统关键进程）不碰：被控端自己也会拒，
        // 但这里先拦一道，免得回执流水里多出一堆"拒绝"让人误会功能坏了。
        if (m.contains(QStringLiteral("protected"))) continue;
        QJsonObject p;
        p.insert(QStringLiteral("pid"), pid);
        sendAction(QStringLiteral("process_stop"), p);
        ++sent;
    }
    logf("[viewer] 一键关闭黑名单：命中并下发 %d 个 process_stop → %s",
         sent, m_currentUid.toUtf8().constData());
    return sent;
}

bool ViewerBackend::firstRunDone() const { return m_firstRunDone; }

void ViewerBackend::markFirstRunDone()
{
    if (m_firstRunDone) return;
    m_firstRunDone = true;
    QSettings s;
    s.setValue(QStringLiteral("prefs/firstRunDone"), true);
    emit firstRunDoneChanged();
}

/* ── 开机自启 / 桌面快捷方式（2026-10-09 · 用户清单第②条）─────────────────────
 * 管理端是「老师自己的机器」，所以走**当前用户**范围、**免提权**：
 *   自启 = HKCU\...\Run 下一个值（不碰 HKLM、不建计划任务）；
 *   快捷 = 当前用户桌面一枚 .lnk（WScript.Shell 建，不需管理员）。
 * ⚠️ 被控端那条同名开关走的是**另一套**（登录触发器计划任务 + 公共桌面，要最高权限）——
 *    两端机制不同是用户 2026-10-09 明确选定的（受管终端 vs 个人机器），别为了"统一"改回去。
 */
static const char *kViewerRunKey =
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const char *kViewerRunValue = "StelarithViewer";
static QString viewerDesktopLnk()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    return d.isEmpty() ? QString() : (d + QStringLiteral("/星集控管理端.lnk"));
}

bool ViewerBackend::autoStartEnabled() const
{
    QSettings run(QString::fromLatin1(kViewerRunKey), QSettings::NativeFormat);
    return run.contains(QString::fromLatin1(kViewerRunValue));
}

void ViewerBackend::setAutoStart(bool on)
{
    QSettings run(QString::fromLatin1(kViewerRunKey), QSettings::NativeFormat);
    const QString key = QString::fromLatin1(kViewerRunValue);
    if (on) {
        const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        run.setValue(key, QStringLiteral("\"%1\"").arg(exe));
    } else {
        run.remove(key);
    }
    run.sync();
    logf("[viewer] 开机自启 → %s（HKCU Run）", on ? "开" : "关");
    emit autostartChanged();
}

bool ViewerBackend::desktopShortcutExists() const
{
    const QString p = viewerDesktopLnk();
    return !p.isEmpty() && QFileInfo::exists(p);
}

void ViewerBackend::setDesktopShortcut(bool on)
{
    const QString lnk = viewerDesktopLnk();
    if (lnk.isEmpty()) {
        setStatus(QStringLiteral("取不到桌面路径，快捷方式建不了"), true);
        return;
    }
    if (!on) {
        if (QFile::exists(lnk) && !QFile::remove(lnk)) {
            setStatus(QStringLiteral("桌面快捷方式删不掉（可能被占用）"), true);
            return;
        }
        emit autostartChanged();
        return;
    }
    const QString exe  = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString work = QDir::toNativeSeparators(QFileInfo(exe).absolutePath());
    // 用 WScript.Shell 建 .lnk（无 Qt Ax 依赖）。失败会在签名/策略受限的机器上发生 ——
    // 这里只 startDetached，不阻塞主线程；建没建成靠 desktopShortcutExists() 复查。
    const QString ps = QStringLiteral(
        "$s=(New-Object -ComObject WScript.Shell).CreateShortcut('%1');"
        "$s.TargetPath='%2';$s.WorkingDirectory='%3';$s.IconLocation='%2,0';$s.Save()")
        .arg(QDir::toNativeSeparators(lnk), exe, work);
    // #368 需求①：静默 detached 拉起，杜绝建桌面快捷方式时弹出 powershell 窗
    stelarithStartHiddenDetached(QStringLiteral("powershell"),
        {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("bypass"),
         QStringLiteral("-Command"), ps});
    emit autostartChanged();
}
// ───────────────────────────────────────────────────────────────────────────
// 站点账号（OAuth 一户通）
// ───────────────────────────────────────────────────────────────────────────
// 凭据落盘位置：%LOCALAPPDATA%/Stelarith Viewer/account.json（程序名由 main.cpp 钉死，
// 所以这个路径是可预期的；不是 exe 同目录 —— 绿色包会被覆盖、重装也会丢，
// 把令牌跟程序文件放一起还容易被整文件夹拷走）。
// JSON 里只有四个字段：会话令牌、接入票、票到期时刻、登录账号。
// 强度与原来 viewer.env 里的静态令牌**同档**（都是本机明文），但多一层"是谁在连"的账号。
void ViewerBackend::start()
{
    // ⚠️ 2026-10-06 改默认值：原来是 ws://127.0.0.1:8788/ws/viewer（本机 dev 云端）。
    // 包一发出去老师机拿到的就是这份默认值 → 指向"这台机器自己" = 永远连不上，
    // 界面上表现就是"连不上云端 + 设备列表空"（用户看到的"半成品"其实是根本没连上云端）。
    // 生产入口 = 云端监听 0.0.0.0:8788 的公网域名（见 Stelarith-cloud/src/provision.js 的 CLOUD_PUBLIC_BASE）。
    // 本机联调照旧设 STE_VIEWER_URL 覆盖就行，不要为了联调把默认值改回去。
    m_url = qEnvironmentVariable("STE_VIEWER_URL", kDefaultViewerUrl).trimmed();
    if (m_url.isEmpty()) m_url = kDefaultViewerUrl;
#if defined(Q_OS_ANDROID) && defined(STE_VIEWER_URL_CMake)
    if (m_url == kDefaultViewerUrl)   // Android 无 env；联调/隔离实例用宏覆盖
        m_url = QString::fromUtf8(STE_VIEWER_URL_CMake).trimmed();
#endif
    m_token = qEnvironmentVariable("STE_VIEWER_TOKEN", QString()).trimmed();
    // 2026-10-11：Android 无环境变量 → 用编译期宏注入（CMake ANDROID 分支传
    // -DSTE_VIEWER_TOKEN_CMake="…"，mobile 同款方案；方便未登录时连云端）
#if defined(Q_OS_ANDROID) && defined(STE_VIEWER_TOKEN_CMake)
    if (m_token.isEmpty())
        m_token = QString::fromUtf8(STE_VIEWER_TOKEN_CMake).trimmed();
#endif
    if (m_token.isEmpty()) {
        logf("[viewer] WARN 未设环境变量 STE_VIEWER_TOKEN：云端 /ws/viewer 已加鉴权，"
             "既没静态令牌也没接入票的话会被拒绝（不是网络问题，是没带凭据）");
    }
    m_siteUrl = qEnvironmentVariable("STE_SITE_URL", QString()).trimmed();
    if (m_siteUrl.isEmpty()) m_siteUrl = QString::fromUtf8(kDefaultSiteUrl);
    logf("[viewer] 云端地址 %s / 站点 %s", m_url.toUtf8().constData(),
         m_siteUrl.toUtf8().constData());
    emit cloudUrlChanged();
    // ── 站点账号（OAuth 一户通，2026-10-06）──
    // 顺序：先读本机有没有存过的凭据 → 有就静默取票（取不到也不打扰）→ 没有就等用户点登录。
    // 没配 STE_SITE_URL 时完全按老路走（静态令牌），这条改动对老机器是零影响。
    m_oauth = new OAuthLogin(this);
    QObject::connect(m_oauth, &OAuthLogin::succeeded, this, &ViewerBackend::onOAuthSucceeded);
    QObject::connect(m_oauth, &OAuthLogin::failed, this, &ViewerBackend::onOAuthFailed);
    loadAccount();
    if (m_siteUrl.isEmpty()) {
        logf("[viewer] 没配 STE_SITE_URL：不走网站账号登录，仍用 viewer.env 里的静态令牌");
        m_accountText = QStringLiteral("未配置站点地址（只走静态令牌）");
        emit accountChanged();
    } else {
        m_accountTimer = new QTimer(this);
        m_accountTimer->setInterval(5 * 60 * 1000);   // 票到期巡检
        QObject::connect(m_accountTimer, &QTimer::timeout, this, &ViewerBackend::onAccountTimer);
        m_accountTimer->start();
        refreshAccountText();
        ensureCloudTicket(false);   // 静默：有票直接用，没票也不弹浏览器
    }
    // Qt6 的 QWebSocket 构造是三参 (origin, version, parent)，只给 parent 会重载歧义；
    // 它是 QObject 子类但不收 QObject*，用 setParent 挂上来
    m_ws = new QWebSocket();
    m_ws->setParent(this);
    QObject::connect(m_ws, &QWebSocket::connected, this, &ViewerBackend::onConnected);
    QObject::connect(m_ws, &QWebSocket::disconnected, this, &ViewerBackend::onDisconnected);
    QObject::connect(m_ws, &QWebSocket::textMessageReceived, this, &ViewerBackend::onTextMessage);
    QObject::connect(m_ws, &QWebSocket::binaryMessageReceived, this, &ViewerBackend::onBinaryMessage);
    QObject::connect(m_ws, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError err) {
        logf("[viewer] FAIL WS 错误 %d（%s）→ 会自己退避重连", (int)err,
             m_ws->errorString().toUtf8().constData());
        setStatus(QStringLiteral("连不上云端：") + m_ws->errorString(), true);
    });
    // fps 统计：每秒算一次（当前画面是 2 秒一帧，别把 0.5fps 说成 200fps）
    m_fpsTimer = new QTimer(this);
    QObject::connect(m_fpsTimer, &QTimer::timeout, this, &ViewerBackend::tickFps);
    m_fpsTimer->start(1000);
    m_lastFpsCheckMs = QDateTime::currentMSecsSinceEpoch();

    // ── 2026-10-11（#4）「进后台就连不上云端」：半开连接探活 ──
    // Android 后台网络被系统切断时 QWebSocket 是半开连接（不触发任何信号），
    // 回前台还显示"已连"但发包无效。定时检查"多久没收到云端消息"，超阈值
    // 主动 disconnect → 走既有退避重连。onAppStateChanged 让回前台立刻查一次。
    m_ctrlWatchdog = new QTimer(this);
    m_ctrlWatchdog->setInterval(10000);
    connect(m_ctrlWatchdog, &QTimer::timeout, this, &ViewerBackend::onCtrlWatchdog);
    m_ctrlWatchdog->start();
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            &ViewerBackend::onAppStateChanged);

    connectToCloud();
}
void ViewerBackend::setStatus(const QString &s, bool warn)
{
    if (m_statusText == s && m_statusWarn == warn) return;
    m_statusText = s;
    m_statusWarn = warn;
    emit statusTextChanged();
}
bool ViewerBackend::hasAnyCredential() const
{
    return !m_cloudTicket.isEmpty() || !m_token.isEmpty();
}
int ViewerBackend::retryDelayMs() const
{
    // 5s → 10s → 20s → 30s 封顶。断线时先快速补一次（网络抖一下就回来了），
    // 一直不通就退到分钟级 —— 常驻程序在后台空转连一个连不上的地址，
    // 除了把日志刷爆没有任何收益，还会把真正的错误挤出去。
    return qMin(30000, 5000 * (1 << qMin(m_retryCount, 2)));
}
void ViewerBackend::connectToCloud()
{
    if (!m_ws) return;
    // 没凭据就别连了（2026-10-06 打磨：这是重连风暴的真正根因）。
    // 以前"既没有 ticket 也没有 token"也要硬连，云端回一句鉴权失败，
    // 界面再 5 秒重试一次，日志里就是几十轮一模一样的 "已连上云端 → 正在鉴权 → FAIL"。
    // 站在用户那边看：屏幕上一句"没登录"就够了，不需要后面的循环。
    if (!hasAnyCredential()) {
        if (!m_retryStopped) {
            m_retryStopped = true;
            logf("[viewer] 手上没有云端凭据（没登录），已停在那儿等用户登录——不再空转重连");
            setStatus(QStringLiteral("没登录，连不上云端。点右上角「登录」用星璃账号进去"),
                      true);
            emit authFailed(QStringLiteral("未登录"));
        }
        return;
    }
    m_retryStopped = false;
    m_ws->close();
    m_ws->open(QUrl(m_url));
    logf("[viewer] 正在连云端 %s", m_url.toUtf8().constData());
}
void ViewerBackend::sendEnvelope(const QString &type, const QJsonObject &payload)
{
    if (!m_ws) return;
    m_ws->sendTextMessage(makeEnvelope(type, payload));
}
// ── 自习模式（2026-10-13）：噪音检测 + 异常录制 ────────────────────────────
// 被控端每拍回传一份噪音样本（db/active/study/captureOk…，见 agent main.cpp）。
// 这里只做"聚合成 QML 能直接画的东西"：最新 dB + 最近 N 个样本 + 开关状态。
// 异常/录制记录（noise_alert）单独进 m_noiseAlerts。
static constexpr int kNoiseSamplesMax = 60;   // 曲线/条带保留的样本数（2s 一拍 ≈ 2 分钟窗口）
static constexpr int kNoiseAlertsMax  = 50;   // 异常记录保留条数

void ViewerBackend::handleNoiseSample(const QJsonObject &pay)
{
    // 只认**当前选中设备**的噪音。管理端可能同时订阅了缩略图墙的多台设备，
    // 别的机器若也在跑噪音检测，样本会一起飘过来 —— 不能混进当前这台的曲线。
    const QString uid = pay.value(QStringLiteral("uid")).toString();
    if (!uid.isEmpty() && !m_currentUid.isEmpty() && uid != m_currentUid) return;

    m_noiseDb = pay.value(QStringLiteral("db")).toDouble();
    m_noiseActive = pay.value(QStringLiteral("active")).toBool();
    // captureOk：老版本被控端没这字段 → 默认 true（别把"无此字段"误判成"采不到"）
    m_noiseCaptureOk = pay.value(QStringLiteral("captureOk")).toBool(true);

    QVariantMap s;
    s.insert(QStringLiteral("db"), m_noiseDb);
    s.insert(QStringLiteral("active"), m_noiseActive);
    s.insert(QStringLiteral("t"), double(QDateTime::currentMSecsSinceEpoch()));
    m_noiseSamples.append(s);
    while (m_noiseSamples.size() > kNoiseSamplesMax) m_noiseSamples.removeFirst();

    // 开关状态以被控端每拍回报的 study 标志为准（比本地乐观置位可靠 —— 别的老师
    // 开/关、或本地下发失败，这里都能自愈）。
    const bool study = pay.value(QStringLiteral("study")).toBool(false);
    if (study != m_studyActive) {
        m_studyActive = study;
        logf("[viewer] 自习模式 %s", study ? "已开启" : "已关闭");
        emit studyChanged();
    }
    emit noiseChanged();
}

void ViewerBackend::handleNoiseAlert(const QJsonObject &pay)
{
    const QString uid = pay.value(QStringLiteral("uid")).toString();
    if (!uid.isEmpty() && !m_currentUid.isEmpty() && uid != m_currentUid) return;

    QVariantMap a;
    a.insert(QStringLiteral("event"), pay.value(QStringLiteral("event")).toString());
    a.insert(QStringLiteral("count"), pay.value(QStringLiteral("count")).toInt());
    a.insert(QStringLiteral("ok"), pay.value(QStringLiteral("ok")).toBool(true));
    a.insert(QStringLiteral("bytes"), pay.value(QStringLiteral("bytes")).toDouble());
    a.insert(QStringLiteral("seconds"), pay.value(QStringLiteral("seconds")).toInt());
    a.insert(QStringLiteral("device"), pay.value(QStringLiteral("device")).toString());
    a.insert(QStringLiteral("path"), pay.value(QStringLiteral("path")).toString());
    a.insert(QStringLiteral("reason"), pay.value(QStringLiteral("reason")).toString());
    a.insert(QStringLiteral("t"), double(QDateTime::currentMSecsSinceEpoch()));
    m_noiseAlerts.prepend(a);   // 新在头：列表自上而下就是时间倒序
    while (m_noiseAlerts.size() > kNoiseAlertsMax) m_noiseAlerts.removeLast();
    emit noiseAlertsChanged();
    logf("[viewer] 噪音告警：%s（第 %d 次）", a.value(QStringLiteral("event")).toString().toUtf8().constData(),
         a.value(QStringLiteral("count")).toInt());
}

void ViewerBackend::startStudyMode(const QVariantMap &opts)
{
    QJsonObject p;
    const char *keys[] = { "thresholdDb", "streakNeed", "recordSec", "intervalMs" };
    for (const char *k : keys) {
        const QString key = QString::fromLatin1(k);
        if (opts.contains(key)) p.insert(key, QJsonValue::fromVariant(opts.value(key)));
    }
    // 乐观置位（界面立刻反映）；真状态以被控端每拍回报的 study 标志为准。
    if (!m_studyActive) { m_studyActive = true; emit studyChanged(); }
    sendAction(QStringLiteral("study_mode_start"), p);
}

void ViewerBackend::stopStudyMode()
{
    if (m_studyActive) { m_studyActive = false; emit studyChanged(); }
    sendAction(QStringLiteral("study_mode_stop"), QJsonObject());
}

void ViewerBackend::onConnected()
{
    m_connected = true;
    m_offlineSince = 0;      // 离线看门狗解除
    emit connectedChanged();
    logf("[viewer] 已连上云端 %s，正在鉴权…", m_url.toUtf8().constData());
    setStatus(QStringLiteral("已连上云端，正在鉴权…"), false);
    m_authed = false;
    emit authedChanged();
    // 云端要求第一条消息必须是 auth（v1 信封），否则直接拒收（鉴权前不推任何设备表/画面）。
    // 手里有没过期的接入票就发票，一张都没有才退回静态令牌 ——
    // 两条通道云端一直都认（详见 cloud-ws/src/index.js 的 authorizeViewer）。
    QJsonObject p;
    pickTicketOrToken(p);
    sendEnvelope(QStringLiteral("auth"), p);
}
void ViewerBackend::onDisconnected()
{
    m_connected = false;
    // 断线 / 重连时必须把语音与广播收掉：麦还开着、抓屏还 3fps 推着，
    // 而老师那边的"正在讲话"不会自己变。旧代码一直保持 m_speaking = true，
    // 表现出来就是"我什么都没点，怎么一直在讲话"。
    stopAllLive(QStringLiteral("与云端断开，语音与广播已自动停"));
    // 远控会话也一并收掉：主 WS 断了，vnc-data 帧已经不可能到达，
    // 留着 VncClient 的解码线程只会对着空缓冲干等。
    if (m_vncActive) stopVnc();
    if (m_classService) m_classService->onDisconnected();   // #460：置未订、保留数据、重连自动重订
    if (!m_offlineSince) m_offlineSince = QDateTime::currentMSecsSinceEpoch();
    emit connectedChanged();
    // 「设备离线时提醒」：断线是常驻程序最该让用户知道的一件事，
    // 以前只在日志里留一行，用户得盯着屏幕才知道"怎么没画面了"。
    if (m_notifyOnOffline && !m_retryStopped && !m_authFailed) {
        notifyPref(QStringLiteral("与云端断开了，正在自动重连…"));
    }
    if (m_authFailed || m_retryStopped) {
        // ⚠️ 分两种情形说，别混成一句：
        //   ① 手上没凭据 = 用户刚点了「退出登录」⇒ 说"已退出登录"（说成"鉴权失败"是冤枉它，
        //      也会让用户以为票坏了、跑去乱改配置）；
        //   ② 有凭据但被拒 = 真的是鉴权没过，把云端原话带上。
        if (!hasAnyCredential()) {
            logf("[viewer] 已退出登录（无凭据），停在登录页等用户，不再重连");
            setStatus(QStringLiteral("已退出登录。点右上角账户 → 登录，可换一个网站账号"), false);
            emit authFailed(QStringLiteral("已退出登录"));
            return;
        }
        logf("[viewer] FAIL 鉴权没通过，已停止重连：%s", m_authFailReason.toUtf8().constData());
        setStatus(QStringLiteral("云端鉴权失败，已停止重连：") + m_authFailReason, true);
        emit authFailed(m_authFailReason);
        return;
    }
    ++m_retryCount;
    const int delay = retryDelayMs();
    logf("[viewer] FAIL 与云端断开，%d 秒后重连（第 %d 次）", delay / 1000, m_retryCount);
    setStatus(QStringLiteral("与云端断开，%1 秒后重连…").arg(delay / 1000), true);
    QTimer::singleShot(delay, this, [this] { connectToCloud(); });
}
// ── 2026-10-11（#4）「进后台就连不上云端」：半开连接探活 ──
// Android 后台网络被系统切断后，QWebSocket 是半开连接——不触发 connected /
// disconnected / errorOccurred 任何信号，状态仍是"已连"，但实际收不到云端消息
// 也发不出去。这里用"最后收到云端任何消息的时刻"判断：如果已连上、却超过
// kCtrlStaleMs 一点消息都没有，那基本就是半开了 → 主动 disconnect() 走既有
// 退避重连路径。云端正常时每 15s 至少广播一次 devices，45s 阈值给它留足余量。
void ViewerBackend::onCtrlWatchdog()
{
    if (!m_ws || m_ws->state() != QAbstractSocket::ConnectedState || !m_authed)
        return;   // 没连上/没鉴权成功时不需要探活（连接流程自己管）
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastRxMs > 0 && now - m_lastRxMs > kCtrlStaleMs) {
        logf("[viewer] ⚠️ 探活：超过 %lld 秒没收到云端任何消息——判定半开连接，主动断开重连",
             (long long)(kCtrlStaleMs / 1000));
        setStatus(QStringLiteral("云端连接疑似掉线，正在重连…"), true);
        m_ws->abort();   // abort 触发 disconnected → onDisconnected → 退避重连
    }
}
void ViewerBackend::onAppStateChanged(Qt::ApplicationState state)
{
    // 回前台（Android onResume）立刻查一次半开，不用等 10s 定时器：
    // 后台被挂起几十秒、回前台那一刻就要马上恢复，别让用户干等一个探活周期。
    if (state == Qt::ApplicationActive)
        onCtrlWatchdog();
}
void ViewerBackend::requestDevices()
{
    sendEnvelope(QStringLiteral("devices"), QJsonObject());
}
void ViewerBackend::setCurrentUid(const QString &uid)
{
    if (uid.isEmpty() || uid == m_currentUid) return;
    const QString prev = m_currentUid;
    m_currentUid = uid;
    refreshCapActions();          // 换机器 = 换一套能力，门控要立刻跟着变

    // ⚠️ 2026-10-08 修「切了设备画面还是第一台」的两个源头，两个都要治：
    //
    // ① **必须退订上一台**。云端 subscribeViewer 只是往订阅集合里 add，从不替我们取消；
    //    而被控端是"有人看才抓屏发帧"（watch 档）。不退订 ⇒ 上一台一直以为自己被看着、
    //    一直发帧 ⇒ 两台的帧都落到同一张 m_frame 上。谁的帧最后到，屏幕上就是谁 ——
    //    表现为"切了也没用，还是第一台"。
    //    （退订与 m_screenActive 无关：即使当前页不可见，上一台也该停发——它本来就不在看了。）
    if (!prev.isEmpty()) {
        QJsonObject un;
        un.insert(QStringLiteral("uid"), prev);
        sendEnvelope(QStringLiteral("unsubscribe"), un);
        logf("[viewer] 退订上一台 %s（不退订它会一直发帧，和新设备抢同一张画面）",
             prev.toUtf8().constData());
        // ── 2026-10-11 需求#7：离开一台机器 = 收掉对它的远控 ──
        //    "选中即远控"之后，切换设备如果不给上一台发 stop，那台会一直认为自己
        //    在被远控（学生端提示不消失、抓屏帧率一直压在高档），而且老师表面上看
        //    不出任何异常 —— 这就是无声的资源泄漏 + 状态错位。
        if (m_remoteControlling && m_remoteControlUid == prev) {
            QJsonObject st;
            sendAction(QStringLiteral("remote_control_stop"), st);
            logf("[viewer] 离开 %s，收掉对它的远控", prev.toUtf8().constData());
            m_remoteControlling = false;
            m_remoteControlUid.clear();
            stopVnc();      // 2026-10-18：顺手关掉这台设备的 VNC 会话
        }
    }

    // ② 手上那张旧画面立刻作废。新设备第一帧到之前必须显示"正在接通"，
    //    绝不能把上一台的桌面留在屏幕上冒充新设备（老师会照着旧画面操作新机器）。
    //    m_frameUid 清空 = 界面据此判"这张不是当前设备的"。
    m_frameUid.clear();
    m_frame = QImage();
    m_frameHash = 0;
    m_sameFrameStreak = 0;
    setStatic(false);

    emit currentUidChanged();
    emit frameChanged();
    emit statsChanged();
    // 2026-10-08：带画面页不可见（用户切到概览/批量/设置）时**不订阅**——
    // 退订即停流，云端不再往这台推帧，省流量也省被控端抓屏开销。回到该页由 setScreenActive(true) 再订阅。
    // 2026-10-24：原来这里 `if (!m_screenActive) return;` —— 但 m_screenActive 由外壳
    //   按"当前栏目是否带画面"维护，实测在集控页上也会被判成 false（真机日志：
    //   "切到 X，但带画面页不可见，暂不订阅"），结果选中设备后既不下发 remote_control_start
    //   也不订阅画面 —— 远控整条链起不来。订阅本身很便宜（云端只是把 uid 加进集合），
    //   正确性优先：选中就订阅，离开页面由 setScreenActive(false) 统一退订。
    if (!m_screenActive) {
        logf("[viewer] 切到 %s（页面标记为不可见，仍先订阅以保证远控能起来）",
             uid.toUtf8().constData());
    }
    logf("[viewer] 切到 %s，向云端订阅它的画面", uid.toUtf8().constData());
    QJsonObject sp;
    sp.insert(QStringLiteral("uid"), uid);
    sendEnvelope(QStringLiteral("subscribe"), sp);

    // ── 2026-10-11 需求#7：选中一台设备 = 就是在远控它，不用再点开关 ──
    //    原来的界面上摆着「远控开 / 远控关」两个按钮，但人已经在这个远控界面里、
    //    也已经选好机器了 —— 那两个开关是纯多余的一步。改成选中即接管：
    //    · 教师角色不发（权限不足，发了也是被拒，还会在回执流水里留一条失败）
    //    · 设备没上报 remote_control_start 能力就不发（老版本被控端，别让回执流水里全是失败）
    //    被控端收到 remote_control_start 会自己弹"已被老师接管"的提示（学生知情）。
    if (role() == QStringLiteral("admin")
        && m_capActions.contains(QStringLiteral("remote_control_start"))) {
        QJsonObject rc;
        // 帧率随单下发（2026-10-18 晚修复）：原先 rc 是空对象，被控端取默认 20
        // 且 VncServer 抓屏节奏写死 100ms=10fps —— 客户端要多少都只有 10fps。
        // 现在一路带到 VncServer::start(0, fps)，抓屏间隔按 1000/fps 算（clamp 33ms）。
        rc.insert(QStringLiteral("fps"), 24);
        sendAction(QStringLiteral("remote_control_start"), rc);
        m_remoteControlling = true;
        m_remoteControlUid = uid;
        m_pendingVncUid = uid;   // 等回执/兜底再拉 VNC（见下）
        logf("[viewer] 选中即远控：已对 %s 下发 remote_control_start", uid.toUtf8().constData());
        // ⚠️ 2026-10-24 修复（VNC 从未通过的根因）：
        //   原来这里是「发指令」和「startVnc」在同一毫秒执行 —— 而被控端要等指令
        //   到达后才起抓屏线程、绑 127.0.0.1:5900、接桥。管理端立刻连过去，等不到
        //   "RFB 003.008"，rfbInitClient 直接失败（日志"连接或握手未完成"），
        //   于是整场远控都退回 JPEG 轮询照片（1fps，无交互）。
        //   现在改为**等被控端回执确认 VNC 已起**再拉（见 instruction-result 分支），
        //   并留一个 2s 兜底定时器（回执丢失时不至于永远不拉）。
        QTimer::singleShot(2000, this, [this, uid]() {
            if (m_pendingVncUid != uid || m_vncActive) return;
            logf("[viewer] 2s 兜底：仍未见回执，直接拉起 VNC（uid=%s）", uid.toUtf8().constData());
            startVnc(uid);
        });
    } else {
        logf("[viewer] 选中 %s 但未自动进远控（role=%s，远控能力=%d）——权限或设备能力不满足",
             uid.toUtf8().constData(), role().toUtf8().constData(),
             (int)m_capActions.contains(QStringLiteral("remote_control_start")));
    }
}

void ViewerBackend::setScreenActive(bool on)
{
    if (m_screenActive == on) return;
    m_screenActive = on;
    // 诊断（2026-10-24）：这个标记直接决定"选中设备要不要订阅/拉 VNC"，
    // 但它被 QML 多处置位，出问题时完全看不见 —— 每次翻转都留痕。
    logf("[viewer] 画面页可见性 -> %s（当前设备 %s）", on ? "可见" : "不可见",
         m_currentUid.isEmpty() ? "(无)" : m_currentUid.toUtf8().constData());
    if (m_currentUid.isEmpty()) return;   // 没选设备，无所谓订阅
    QJsonObject sp;
    sp.insert(QStringLiteral("uid"), m_currentUid);
    if (on) {
        sendEnvelope(QStringLiteral("subscribe"), sp);
        logf("[viewer] 回到带画面页，恢复 %s 的画面订阅", m_currentUid.toUtf8().constData());
    } else {
        // 离开带画面页：退订 + 清本地帧。云端停推，被控端停抓这台。
        sendEnvelope(QStringLiteral("unsubscribe"), sp);
        // 修复③（2026-10-19）：离开画面页 = 不再看这台 ⇒ 远控会话一并收口。
        //   以前只退订不停远控：被控端 g_remoteControl 永真、灵动岛冻结、
        //   VNC server 空转（老师切到概览页就不管了，无声泄漏；与被控端
        //   看门狗兜底相比，这里主动发 stop 是零代价的优雅路径）。
        if (m_remoteControlling) {
            sendAction(QStringLiteral("remote_control_stop"), QJsonObject());
            logf("[viewer] 离开带画面页，收掉对 %s 的远控", m_currentUid.toUtf8().constData());
            m_remoteControlling = false;
            m_remoteControlUid.clear();
            stopVnc();
        }
        m_frameUid.clear();
        m_frame = QImage();
        m_frameHash = 0;
        m_sameFrameStreak = 0;
        setStatic(false);
        emit frameChanged();
        emit statsChanged();
        logf("[viewer] 离开带画面页，停掉 %s 的画面订阅（省流量）", m_currentUid.toUtf8().constData());
    }
}
// 多班面板缩略图墙（2026-10-07，设计文档 3.2.2）：
// 订阅一批设备的画面（不切当前选中，只收缩略图帧），QML 网格 3×3 展示。
// 与主画面共用一条 WS 订阅（云端按 uid 推帧，帧头带 uid → 按 uid 分槽存）
void ViewerBackend::subscribeThumbnails(const QVariantList &uids)
{
    QStringList want;
    for (const QVariant &v : uids) {
        const QString uid = v.toString().trimmed();
        if (!uid.isEmpty() && !want.contains(uid)) want.append(uid);
    }
    // 之前的订阅不再需要 → 退订（保留当前选中那台：它走主画面，不算缩略图需求）
    for (const QString &uid : m_thumbUids) {
        if (want.contains(uid)) continue;
        if (uid == m_currentUid) continue;
        QJsonObject sp;
        sp.insert(QStringLiteral("uid"), uid);
        sendEnvelope(QStringLiteral("unsubscribe"), sp);
        // 2026-10-12 修（审计 #7/#19）：缓存帧随订阅一起清——原来 m_thumbFrames
        // 只增不减，机房换 uid/缩略图墙从 30 台切到 3 台后老图像一直挂着，内存无上界。
        m_thumbFrames.remove(uid);
    }
    // 新订阅
    for (const QString &uid : want) {
        if (m_thumbUids.contains(uid)) continue;
        if (uid == m_currentUid) continue;   // 主画面重点保障，不入缩略图再抢一条
        QJsonObject sp;
        sp.insert(QStringLiteral("uid"), uid);
        sendEnvelope(QStringLiteral("subscribe"), sp);
    }
    // 上限兜底（LRU 意识：超 32 台只留最新的那批），防 m_thumbUids 无限涨
    while (m_thumbFrames.size() > 32) {
        m_thumbFrames.erase(m_thumbFrames.begin());
    }
    m_thumbUids = want;
    emit thumbnailChanged();
    logf("[viewer] 多班缩略图订阅 %d 台：%s", want.size(),
         want.join(QLatin1String(",")).toUtf8().constData());
}
void ViewerBackend::onTextMessage(const QString &text)
{
    // #4 探活：任何云端消息都刷新"最后收到"时刻（含未识别的、解析失败的消息）
    m_lastRxMs = QDateTime::currentMSecsSinceEpoch();
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isObject()) {
        logf("[viewer] FAIL 云端消息解析不出来：%s", text.left(160).toUtf8().constData());
        return;
    }
    const QJsonObject o = doc.object();
    // v1 信封与旧扁平格式都认：有 v 字段就取 payload，否则整条就是旧扁平结构
    const bool isV1 = o.contains(QStringLiteral("v"));
    const QJsonObject pay = isV1 ? o.value(QStringLiteral("payload")).toObject() : o;
    const QString type = o.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("authed") || type == QStringLiteral("auth-ok")) {
        m_authed = true;
        m_retryCount = 0;           // 通了就把退避计清零，下一次断线还是 5 秒起步
        m_retryStopped = false;
        emit authedChanged();
        logf("[viewer] ✅ 云端鉴权通过（mode=%s）",
             pay.value(QStringLiteral("mode")).toString().toUtf8().constData());
        // RBAC（2026-10-20 第二批）：云端 auth-ok 现在带 identity（{uid, schoolId, roles, scopes}）。
        //   · scopes 是数组 ⇒ 受限身份（票带 claims / 云端按 uid 回填）→ setIdentity + 服务端角色锁定；
        //   · scopes 是 null（静态全权令牌 / 无 claims 旧票）或 identity 缺失 ⇒ 全权
        //     → setFullAccess()，且解锁手动角色切换（行为与接入 RBAC 前完全一致）。
        // ⚠️ 回归坑（第二批现场修的）：原实现 ident.value("scopes").toArray() 对 JSON null 也
        //    返回空数组 ⇒ 全权身份被当成「空 scopes 受限身份」⇒ 设备列表全拒、管理端黑屏。
        //    必须先判 isArray() 再取，null 走全权分支。
        if (m_perms) {
            const QJsonObject ident = pay.value(QStringLiteral("identity")).toObject();
            const QJsonValue scp = ident.value(QStringLiteral("scopes"));
            if (!ident.isEmpty() && scp.isArray()) {
                const QString uid = ident.value(QStringLiteral("uid")).toString();
                const QJsonValue sid = ident.value(QStringLiteral("schoolId"));
                const QString schoolId = (sid.isNull() || sid.isUndefined()) ? QString() : sid.toString();
                QStringList roles;
                for (const QJsonValue &r : ident.value(QStringLiteral("roles")).toArray())
                    roles.append(r.toString());
                m_perms->setIdentity(uid, schoolId, roles, scp.toArray());
                applyServerRole(roles);   // 服务端角色落地 + 锁定本地切换
                logf("[viewer] RBAC identity 注入（受限）：roles=%s scopes=%d",
                     roles.join(QLatin1Char(',')).toUtf8().constData(),
                     scp.toArray().size());
            } else {
                m_perms->setFullAccess();   // scopes=null / 缺 identity ⇒ 全权（不脱敏，兼容现状）
                m_roleLocked = false;       // 解锁手动角色切换（全权连接，行为=今天）
                logf("[viewer] RBAC identity 注入（全权）：scopes=null 或无 identity");
            }
        }
        setStatus(QStringLiteral("已连上云端，等教室机推画面…"), false);
        requestDevices();   // 鉴权过后再拉设备表（之前是连上就拉，现在会被云端拒）
        requestClasses();  // 2026-10-08 拉班级列表
        requestStorage();  // 2026-10-08 拉云端存储概况（需求 #2：概览页要展示）
        fetchActionRanking();  // 2026-10-19 拉动作排行（用户指令②；概览页排行卡）
        // 2026-10-08 兜底轮询：云端推 + 本地 15s 轮询双保险，避免 WS 抖动时设备表卡旧。
        if (!m_refreshTimer) {
            m_refreshTimer = new QTimer(this);
            m_refreshTimer->setInterval(15000);
            connect(m_refreshTimer, &QTimer::timeout, this, [this] {
                if (m_authed) { requestDevices(); requestClasses(); requestStorage(); fetchActionRanking(); }
            });
        }
        m_refreshTimer->start();
        if (m_classService) m_classService->onAuthed();   // #460：曾订过班就自动重订
    } else if (type == QStringLiteral("auth-fail")) {
        m_authFailReason = pay.value(QStringLiteral("reason")).toString();
        // 拿票连的、却被拒 —— 票可能是过期的、也可能是站点那边换了密钥（旧票仍签得出但验不过）。
        // 这种情况下"停起重连"没意义（票还是那张票），正确做法是把票废掉、逼一次重新登录，
        // 由用户在浏览器里重新同意一次拿到新票。别把人卡在一条死路上。
        if (!m_cloudTicket.isEmpty()) {
            logf("[viewer] FAIL 接入票被云端拒（%s）→ 废掉这张票",
                 m_authFailReason.toUtf8().constData());
            m_cloudTicket.clear();
            m_ticketExp = 0;
            // ⚠️ 2026-10-08 修「过期后无法自动登录」：以前这里直接判死刑
            //    （m_accountFatal = "接入票失效，请重新登录"），但**被云端拒的只是那张票，
            //    不是网站会话** —— 票（云端 30 天）和会话（站点 30 天）是两样东西，
            //    票过期/站点换了签名密钥而作废时，会话往往还好着。
            //    会话还在就**静默**去站点换一张新票再连，不该逼用户重走一遍浏览器授权。
            //    只有站点也回 401（会话真失效）才需要人来 —— 那条路由 onSessionTicketFinished
            //    用人话写在界面上。
            if (!m_sessionToken.isEmpty()) {
                m_retryStopped = true;                 // 先停这轮，等新票到手
                m_accountFatal = false;
                logf("[viewer] 本机还有网站会话 → 静默重新取票（不用用户点登录）");
                setStatus(QStringLiteral("接入票失效，正在自动重新取票…"), false);
                emit accountChanged();
                QTimer::singleShot(600, this, [this] {
                    if (!m_sessionToken.isEmpty()) ensureCloudTicket(false);
                });
            } else {
                m_retryStopped = true;
                m_accountFatal = true;
                m_accountText = QStringLiteral("接入票失效，请重新登录");
                emit accountChanged();
                setStatus(QStringLiteral("接入票失效，请点右上角重新登录"), true);
            }
            saveAccount();
            // ⚠️ 2026-10-06：这里必须停机重连。旧代码只 close()，紧接着被 onDisconnected
            // 重新排了一次 5 秒重连 → 那张（已清掉的）票换了个姿势再来一遍 →
            // 日志里就是几十轮 "已连上云端 → 正在鉴权 → auth-fail → 关闭 → 5 秒后重连"，
            // 界面上看着像"程序自己在抽风"。票废了就该停在这儿等新票/重新授权。
            m_authFailReason = QStringLiteral("接入票失效（%1）").arg(m_authFailReason);
            emit authFailed(m_authFailReason);
            if (m_ws) m_ws->close();
            return;
        }
        m_authFailed = true;
        m_retryStopped = true;      // 同 onDisconnected：拒绝一次就是拒绝一路，别硬碰
        logf("[viewer] FAIL 云端拒绝鉴权：%s", m_authFailReason.toUtf8().constData());
        setStatus(QStringLiteral("云端拒绝鉴权：") + m_authFailReason, true);
        emit authFailed(m_authFailReason);
        if (m_ws) m_ws->close();
    } else if (type == QStringLiteral("error")) {
        // 统一错误通道（v1）：云端拒绝必须看得见，不许静默
        // 错误码体系（2026-10-06）：ecode = 数字码（AU1001/CM4001…），code = 语义码兼容，
        // message = 人话。按 ecode 决定 UI 行为（认证类强制重登），其余展示 message。
        const QString ecode = pay.value(QStringLiteral("ecode")).toString();
        const QString code = pay.value(QStringLiteral("code")).toString();
        const QString message = pay.value(QStringLiteral("message")).toString();
        logf("[viewer] FAIL 云端拒绝 → %s/%s：%s",
             ecode.toUtf8().constData(), code.toUtf8().constData(), message.toUtf8().constData());
        // #460 班级域旁路：订阅 pending 窗口内到来的 error 就是订阅被拒
        // （无权限 / 班级不存在…）。ClassService 内部只在该窗口内认领。
        if (m_classService) m_classService->onError(pay);
        // 2026-10-24 重认证门：敏感操作超期被云端 REAUTH_REQUIRED。
        // 与 AU1002/AU1003 的区别：**票没废，连接不断**（云端 ERR.REAUTH_REQUIRED
        // close=0）——只提示"该重新验证密码了"，界面弹窗走一次重新登录即可恢复，
        // 不必停机重连（这是一直连接红线在客户端这边的对应实现）。
        if (code == QLatin1String("REAUTH_REQUIRED") || ecode == QLatin1String("REAUTH_REQUIRED")) {
            setStatus(QStringLiteral("需要重新验证：") + message, true);
            emit reauthRequired(message.isEmpty()
                ? QStringLiteral("长时间未验证，敏感操作被云端拦截，请重新登录一次")
                : message);
            return;   // 连接保持 open，只读操作照常
        }
        // 认证失效（AU1002 过期 / AU1003 无效）：旧令牌已不可用，直接走重登流程，
        // 避免"看起来还连着，但每条指令都被拒"的假在线状态。
        if (ecode == QLatin1String("AU1002") || ecode == QLatin1String("AU1003")) {
            m_authFailReason = message;
            m_authFailed = true;
            m_retryStopped = true;
            // 2026-10-08：和 auth-fail 同一条路数 —— 云端说过期/无效的是**票**，
            // 而票能由网站会话换新的。会话还在就静默换一张再连（"过期后无法自动登录"
            // 的另一半就是这个），会话没了才请人重新登录。
            m_cloudTicket.clear();
            m_ticketExp = 0;
            if (!m_sessionToken.isEmpty()) {
                m_accountFatal = false;
                m_authFailed = false;
                logf("[viewer] 云端说票过期/无效，但本机还有网站会话 → 静默重新取票");
                setStatus(QStringLiteral("登录凭据过期，正在自动重新取票…"), false);
                emit accountChanged();
                QTimer::singleShot(600, this, [this] {
                    if (!m_sessionToken.isEmpty()) ensureCloudTicket(false);
                });
                if (m_ws) m_ws->close();
                return;
            }
            setStatus(QStringLiteral("登录已过期，请重新登录"), true);
            emit authFailed(message);
            if (m_ws) m_ws->close();
            return;
        }
        setStatus(QStringLiteral("云端拒绝：") + message, true);
    } else if (type == QStringLiteral("viewer-ready")) {
        logf("[viewer] 云端回话：管理端在线（云端在线管理端 %d 个）",
             pay.value(QStringLiteral("viewers")).toInt(-1));
    } else if (type == QStringLiteral("devices")) {
        refreshDevices(pay.value(QStringLiteral("devices")).toArray());
    } else if (type == QStringLiteral("broadcast.started")) {
        // 云端确认广播已开始（老师屏幕 → 全部在线设备）。以云端权威状态为准，
        // 本端若因本地先设而漏发/错发，这里兜底对齐 UI。
        if (!m_broadcasting) { m_broadcasting = true; emit broadcastingChanged(); }
    } else if (type == QStringLiteral("broadcast.stopped")) {
        // 云端确认广播结束的回执（批次7：原 viewer 只发 broadcast.stop 不等回执，
        // 云端 stop 后本端状态不跟着变 → 界面卡在"广播中"）。这里接回执收口。
        if (m_broadcasting) { m_broadcasting = false; emit broadcastingChanged(); }
    } else if (type == QStringLiteral("subscribed")) {
        logf("[viewer] 云端确认订阅 %s", pay.value(QStringLiteral("uid")).toString().toUtf8().constData());
    } else if (type == QStringLiteral("frame")) {
        // 兼容旧文本 frame（base64）；v1 客户端走的是二进制帧分支
        const QByteArray b64 = QByteArray::fromBase64(pay.value(QStringLiteral("data")).toString().toUtf8());
        if (b64.isEmpty()) logf("[viewer] FAIL 收到一帧但内容是空的（云端推了空 base64）");
        else applyFrameBytes(b64, QJsonObject());
    } else if (type == QStringLiteral("terminal_opened")) {
        // 会话真开起来了。被控端这侧已经在往外吐 terminal_data，这里只负责转给界面。
        const QString sid = pay.value(QStringLiteral("sid")).toString();
        // ⚠️ 这里以前写的是 `m_termState != TermOpen` 就整条丢掉。可本地下发 terminal_open
        //    之后设的是 TermPending（Pending=1，Open=2）—— **Pending 状态下回的
        //    terminal_opened 会被自己扔光**，状态永远停在"等本机确认…"，输入框一直是灰的，
        //    看起来就是"终端打不开 / 连不上"（#95）。
        // 判据改成：没会话（Idle）或 sid 串台才丢；Pending 与 Open 都收。
        if (m_termState == TermIdle || sid != m_termSid) {
            logf("[viewer] WARN 收到 terminal_opened 但本机没在等（state=%d sid=%s our=%s）",
                 (int)m_termState, sid.toUtf8().constData(), m_termSid.toUtf8().constData());
            return;
        }
        m_termClosing = false;   // 通了就不是"正在关"了，去重标记跟着放掉
        setTermState(TermOpen, sid, QString());
    } else if (type == QStringLiteral("terminal_data")) {
        if (m_termState != TermOpen) return;
        const QString sid = pay.value(QStringLiteral("sid")).toString();
        if (sid != m_termSid) return;
        const QString data = pay.value(QStringLiteral("data")).toString();
        if (data.isEmpty()) return;
        // 被控端拿本地代码页编的字节，云端原样转；这里按本地八位还原即可，
        // 换行符留原样（被控端发的是 \r\n），不然回显会挤成一行。
        emit terminalData(sid, data);
    } else if (type == QStringLiteral("terminal_exit")) {
        if (m_termState != TermOpen) return;
        emit terminalExit(m_termSid, pay.value(QStringLiteral("code")).toInt(-1),
                          pay.value(QStringLiteral("ms")).toInt(0));
    } else if (type == QStringLiteral("terminal_closed")) {
        // 没开过会话时也照转：本机拒绝走的就是这条（状态从 Pending 直接回 Idle），
        // 界面要靠它把"点了没反应"变成一句"被本机拒绝了"。
        const QString sid = pay.value(QStringLiteral("sid")).toString();
        const QString reason = pay.value(QStringLiteral("reason")).toString();
        m_termClosing = false;
        setTermState(TermIdle, sid, reason);
        emit terminalClosed(sid, reason);
    } else if (type == QStringLiteral("instruction-result")) {
        const QString state = pay.value(QStringLiteral("state")).toString();
        const QString action = pay.value(QStringLiteral("action")).toString();
        const QString uid = pay.value(QStringLiteral("uid")).toString();
        const QString detail = pay.value(QStringLiteral("detail")).toString();
        const QString result = pay.value(QStringLiteral("result")).toString();
        const QString err = pay.value(QStringLiteral("error")).toString();
        QJsonObject data = pay.value(QStringLiteral("data")).toObject();
        logf("[viewer] 指令 %s → %s（%s）", action.toUtf8().constData(),
             state.toUtf8().constData(), detail.toUtf8().constData());
        // 修复③（2026-10-19）：被控端泄漏看门狗自动收口时，回执的 action 也是
        // remote_control_stop（本地没发过）—— 照它清理本地状态 + 停 VNC，
        // 否则老师对着定格画面和"已远控"的陈旧状态继续操作，越点越错。
        // 本地自己发的 stop 在发出时就已清理（m_remoteControlling 已落），
        // 不会双重触发；uid 不匹配当前远控对象的回执不误伤。
        if (action == QStringLiteral("remote_control_stop") && m_remoteControlling
            && (uid.isEmpty() || uid == m_remoteControlUid)) {
            logf("[viewer] 被控端侧收掉了远控（uid=%s），本地状态同步清理",
                 uid.toUtf8().constData());
            m_remoteControlling = false;
            m_remoteControlUid.clear();
            stopVnc();
        }
        // 2026-10-24：被控端确认 VNC 已起 → 这时候才拉 VNC 客户端（修「必然握手失败」竞态）。
        //   判据用 m_pendingVncUid（点"接管"时置位），不用 m_remoteControlling ——
        //   被控端看门狗收口回执会把后者清掉（真实踩坑）。
        if (action == QStringLiteral("remote_control_start") && !m_vncActive
            && !uid.isEmpty() && uid == m_pendingVncUid
            && result != QStringLiteral("failed")) {
            logf("[viewer] 被控端 VNC 已就绪，拉起客户端（uid=%s）", uid.toUtf8().constData());
            startVnc(uid);
        }
        // 探活的往返时间只有 backend 知道（发出时刻记在这儿），算好塞进 data 给界面用
        if (action == QStringLiteral("ping") && m_pingSentMs > 0) {
            data.insert(QStringLiteral("rttMs"),
                        (double)(QDateTime::currentMSecsSinceEpoch() - m_pingSentMs));
        }
        // 「操作完成时提示」这个开关接在这儿：机器真做了（executed）才响，
        // 云端"收下了"不算 —— 收下了但机器没做，用户等到的却是"完成了"，那 worse than 不提示。
        // 2026-10-11 用户「紧急锁定实则为学生回复（默认开启）」：锁屏（lock）是被控端
        // 的回复确认，属于紧急动作，**不受 notifyOnDone 开关影响，永远通知**——
        // 学生端回复"锁上了/没锁上"不能因为老师关了普通提示就被吞掉。
        const bool lockReply = action == QStringLiteral("lock");
        if (state == QStringLiteral("executed") && (m_notifyOnDone || lockReply)) {
            notifyPref(err.isEmpty()
                           ? QStringLiteral("%1 在 %2 上完成了").arg(action, uid)
                           : QStringLiteral("%1 在 %2 上失败：%3").arg(action, uid, err));
        }
        emit resultReceived(uid, action, state, result, err, detail, data);
    } else if (type == QStringLiteral("unsubscribed")) {
        // 2026-10-10：云端对我们主动退订的确认。它**不是错误**，原来落到下面那句
        // "收到未处理的云端消息"里 —— 每次切设备/离开带画面页都刷一条，纯噪音。
        // 这里显式收口（本轮不改变订阅状态机，只把回执认下来）。
        logf("[viewer] 云端已确认退订（%s）",
             pay.value(QStringLiteral("uid")).toString().toUtf8().constData());
    } else if (type == QStringLiteral("noise_sample")) {
        // 自习模式/噪音检测：被控端每拍回传的噪音样本（走订阅中继，不进事件流水）
        handleNoiseSample(pay);
    } else if (type == QStringLiteral("noise_alert")) {
        // 自习模式：噪音异常 → 自动录制 的开始/结束记录
        handleNoiseAlert(pay);
    } else if (type == QLatin1String("class-subscribed") || type == QLatin1String("class-unsubscribed")
               || type == QLatin1String("class.event") || type == QLatin1String("class.member.online")
               || type == QLatin1String("class.member.offline")) {
        // 班级域（#460）：订阅回执 / 事件 / 成员上下线 → ClassService（模型在它身上）
        onClassDomainMessage(type, pay);
    } else {
        logf("[viewer] 收到未处理的云端消息 type=%s", type.toUtf8().constData());
    }
}
void ViewerBackend::onBinaryMessage(const QByteArray &buf)
{
    // #4 探活：二进制帧也是"云端活着的证据"，同样刷新最后收到时刻
    m_lastRxMs = QDateTime::currentMSecsSinceEpoch();
    // v1 帧：[1B 版本=1][2B 大端 headerLen][header JSON][JPEG]
    if (buf.size() >= 4 && (quint8)buf.at(0) == 1) {
        const int hLen = ((unsigned char)buf.at(1) << 8) | (unsigned char)buf.at(2);
        if (hLen > 0 && hLen <= kFrameHeaderMax && buf.size() >= 3 + hLen) {
            const QJsonDocument hd = QJsonDocument::fromJson(buf.mid(3, hLen));
            if (!hd.isObject()) { logf("[viewer] FAIL 帧头解析失败（非 JSON）"); return; }
            const QJsonObject h = hd.object();
            // B6（2026-10-11）：语音对讲下行 —— 学生端回传的音频与画面帧同为
            // type:"frame"，仅 mime 不同。先按 mime 分流，别再拿 type 一刀切成"非帧丢弃"。
            // P1（2026-10-21）：新增 audio/opus（Opus 包需先解成 PCM）。
            //   audio/pcm 分支保留 = 滚动升级兜底（旧被控端仍发裸 PCM），两种都能播。
            const QString mime = h.value(QStringLiteral("mime")).toString();
            if (mime == QLatin1String("audio/opus") || mime == QLatin1String("audio/pcm")) {
                if (!m_audioPlayback) m_audioPlayback = new AudioPlayback(this);
                const QByteArray body = buf.mid(3 + hLen);
                if (mime == QLatin1String("audio/pcm")) {
                    m_audioPlayback->write(body);
                    return;
                }
                // Opus 解码：实例是**按连接惰性建**的（成员静态只存一次，成本可忽略）。
                static OpusCodec s_dec;
                if (!s_dec.decoderReady()) s_dec.initDecoder(16000, 1);
                if (!s_dec.decoderReady()) return;
                // 5760 = 120ms@16k 上限，防对端发来非 20ms 包越界
                const auto pcm = s_dec.decode(
                    reinterpret_cast<const uint8_t *>(body.constData()), body.size(), 5760);
                if (pcm.empty()) return;   // 丢包/坏帧：丢这一帧，不阻塞后续
                m_audioPlayback->write(QByteArray(
                    reinterpret_cast<const char *>(pcm.data()),
                    static_cast<int>(pcm.size() * sizeof(int16_t))));
                return;
            }
            // 远控：云端下发的 vnc-data 帧 = 被控端 VNC 服务产出的**原始 RFB 字节**。
            // 这是本任务唯一新增的路由（不改 WS 握手）；字节直接喂 libvncclient 的入站缓冲。
            if (h.value(QStringLiteral("type")).toString() == QStringLiteral("vnc-data")) {
                onVncData(buf.mid(3 + hLen));
                return;
            }
            if (h.value(QStringLiteral("type")).toString() != QStringLiteral("frame")) {
                logf("[viewer] FAIL 帧头 type 不是 frame");
                return;
            }
            applyFrameBytes(buf.mid(3 + hLen), h);
            return;
        }
    }
    // 兜底：旧裸帧（无头），整块即 JPEG
    applyFrameBytes(buf, QJsonObject());
}
/**
 * 把当前选中设备的能力清单抽出来给 QML 用（2026-10-06 契合度改造）。
 * 设备的 actions 是云端 devices 广播里带下来的（云端原样转发被控端 register 的 caps.actions）。
 * 老版本被控端/老云端都不带这个字段 → m_capActions 保持空，见 deviceSupports 的兜底。
 */
void ViewerBackend::refreshCapActions()
{
    const QStringList before = m_capActions;
    m_capActions.clear();
    if (!m_currentUid.isEmpty()) {
        for (const QJsonValue &v : m_devices) {
            const QJsonObject d = v.toObject();
            if (d.value(QStringLiteral("uid")).toString() != m_currentUid) continue;
            const QJsonValue acts = d.value(QStringLiteral("actions"));
            if (acts.isArray()) {
                for (const QJsonValue &a : acts.toArray()) {
                    const QString s = a.toString();
                    if (!s.isEmpty()) m_capActions.append(s);
                }
            }
            break;
        }
    }
    if (m_capActions != before) emit capActionsChanged();
}
void ViewerBackend::reportUnsupported(const QString &label, const QString &action)
{
    logf("[viewer] 拦下「%s」：这台被控端的能力清单里没有 %s",
         label.toUtf8().constData(), action.toUtf8().constData());
    setStatus(QStringLiteral("这台被控端不支持「%1」（它没上报 %2 这个能力）").arg(label).arg(action), true);
}
/**
 * 权限被拦下的反馈（和 reportUnsupported 成对：那边是"机器不支持"，
 *  这边是"人没这个身份"—— 两种灰按钮的解释必须一样直白）。
 * 不静默：拦了就得说一句，否则用户只会觉得"点了没反应"。
 */
void ViewerBackend::reportDenied(const QString &label)
{
    logf("[viewer] 拦下「%s」：当前身份（%s）做不了这件事",
         label.toUtf8().constData(), m_role.toUtf8().constData());
    setStatus(QStringLiteral("「%1」要管理员身份才做得到（点右上角账户就能切）").arg(label), true);
}
bool ViewerBackend::deviceSupports(const QString &action) const
{
    if (action.isEmpty()) return false;
    // 没能力表就一律放开：老版本被控端没上报 actions（或云端还没升级），
    // 这时候门控不能反过来把整排按钮锁死 —— 那比不门控更糟，老师会以为软件坏了。
    // 门控只用来"少让人白点一下"，不是用来拦人的。
    if (m_capActions.isEmpty()) return true;
    return m_capActions.contains(action);
}
int ViewerBackend::onlineCount() const
{
    int n = 0;
    for (const QJsonValue &v : m_devices) {
        const QJsonObject d = v.toObject();
        if (d.value(QStringLiteral("online")).toBool(true)) ++n;
    }
    return n;
}
void ViewerBackend::refreshDevices(const QJsonArray &arr)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // ── 把云端推来的整批设备记进台账（在线 + 离线都在）─────────────────
    // 10-08 后云端 listDevices() 已含离线设备，且 online 不再写死 true（由 socket 派生）。
    // 这里必须**信任云端算好的 online**，不能自己强制 true —— 否则离线机器会被打成在线，
    // 正是用户报的"假绿 / 离线不显示"根因（旧逻辑是云端只发在线设备时写的，云端的契约已变）。
    // 缺省 true 仅兜底老云端（不发 online 字段时）。
    QHash<QString, bool> seen;
    for (const QJsonValue &v : arr) {
        QJsonObject d = v.toObject();
        const QString uid = d.value(QStringLiteral("uid")).toString();
        if (uid.isEmpty()) continue;
        const bool online = d.value(QStringLiteral("online")).toBool(true);
        d.insert(QStringLiteral("online"), online);
        // lastSeenAt：在线用此刻；离线用云端给的真实值（云端发 ISO 串，转成 ms 给 QML）。
        qint64 ls = now;
        const QJsonValue lsRaw = d.value(QStringLiteral("lastSeenAt"));
        if (lsRaw.isString()) {
            const qint64 parsed = QDateTime::fromString(lsRaw.toString(), Qt::ISODate).toMSecsSinceEpoch();
            if (parsed > 0) ls = parsed;
        } else if (lsRaw.isDouble()) {
            ls = (qint64)lsRaw.toDouble();
        }
        if (online) {
            d.insert(QStringLiteral("lastSeenAt"), (double)now);
            d.remove(QStringLiteral("lastSeenAgoSec"));   // 在线的不说"多久没见"
        } else {
            d.insert(QStringLiteral("lastSeenAt"), (double)ls);
            d.insert(QStringLiteral("lastSeenAgoSec"),
                     (double)((ls > 0) ? (now - ls) / 1000 : -1));
        }
        m_devLedger.insert(uid, d);
        seen.insert(uid, true);
    }
    // 过期台账清掉：机器被拔走/换掉之后，不该在列表里躺一辈子。
    // 顺带封顶，别让一个被反复改 uid 的坏客户端把内存撑起来。
    for (auto it = m_devLedger.begin(); it != m_devLedger.end(); ) {
        const double t = it.value().value(QStringLiteral("lastSeenAt")).toDouble();
        if (t > 0 && now - (qint64)t > kLedgerKeepMs) it = m_devLedger.erase(it);
        else ++it;
    }
    if (m_devLedger.size() > 200) {
        // 超上限丢最旧的：正常场景（一间机房几十台）永远走不到这儿
        QList<QPair<double, QString>> byAge;
        for (auto it = m_devLedger.constBegin(); it != m_devLedger.constEnd(); ++it)
            byAge.append({ it.value().value(QStringLiteral("lastSeenAt")).toDouble(), it.key() });
        std::sort(byAge.begin(), byAge.end());
        for (int i = 0; i < byAge.size() - 200; ++i) m_devLedger.remove(byAge.at(i).second);
    }

    // ── 组装给界面的表：在线按云端的顺序在前，离线的按"最近掉线"在后 ──
    QJsonArray full;
    for (const QJsonValue &v : arr) {
        const QString uid = v.toObject().value(QStringLiteral("uid")).toString();
        if (uid.isEmpty() || !m_devLedger.contains(uid)) continue;
        full.append(m_devLedger.value(uid));
    }
    QList<QPair<double, QJsonObject>> offline;
    for (auto it = m_devLedger.constBegin(); it != m_devLedger.constEnd(); ++it) {
        if (seen.contains(it.key())) continue;
        QJsonObject d = it.value();
        d.insert(QStringLiteral("online"), false);
        const double t = d.value(QStringLiteral("lastSeenAt")).toDouble();
        d.insert(QStringLiteral("lastSeenAgoSec"),
                 (double)(t > 0 ? (now - (qint64)t) / 1000 : -1));
        offline.append({ t, d });
    }
    // 刚掉线的排前面（t 大的在前）
    std::sort(offline.begin(), offline.end(),
              [](const QPair<double, QJsonObject> &a, const QPair<double, QJsonObject> &b) {
                  return a.first > b.first;
              });
    for (const auto &p : offline) full.append(p.second);

    const int onlineNow = arr.size();
    const int offlineNow = offline.size();
    // RBAC（2026-10-20）：体验层字段脱敏兜底——非平台超管/非全权时剥离敏感字段（ip/mac 系）。
    // 最终以云端「字段脱敏」为准；m_devLedger 保留原始值供内部逻辑（按 uid 路由），仅展示数组脱敏。
    // 2026-10-25：先按 scope **裁剪**再脱敏 —— 设备表是十几次 QML 消费的唯一入口，
    //   在这一处裁掉越界设备，才不会出现"某个页面还看得见别班设备"的漏网。
    //   全权 / 平台超管原样返回（scopedDevices 内部判，别在调用点再写一遍）。
    const QJsonArray visible = m_perms ? m_perms->maskDevices(m_perms->scopedDevices(full)) : full;
    // 变化判定必须用**裁剪后**的表：拿 full 比 m_devices，受限身份下两者恒不等 ⇒ 每轮都刷日志。
    const bool countChanged = (visible.size() != m_devices.size());
    const int rawCount = (int)full.size();
    m_devices = visible;
    if (countChanged || offlineNow > 0) {
        logf("[viewer] 设备表更新：在线 %d 台，离线台账 %d 台（可见 %d / 原始 %d 条）",
             onlineNow, offlineNow, (int)m_devices.size(), rawCount);
    }
    refreshCapActions();          // 设备一变（新增/掉线/换 uid），能力表就要重算
    emit devicesChanged();
    if (arr.isEmpty()) {
        if (!m_currentUid.isEmpty()) {
            m_currentUid.clear();
            m_frameUid.clear();
            m_frame = QImage();
            emit currentUidChanged();
            emit frameChanged();
        }
        return;
    }
    // 第一台上线就默认看它（和云端 autoSubscribe 一个口径）。
    // ⚠️ 必须从 m_devices（已按 scope 裁剪）里取，不能拿云端原始 arr：受限身份下
    //    arr.first() 很可能是别人的设备 —— 选中它等于替用户越权订阅了一路画面。
    if (m_currentUid.isEmpty() && !m_devices.isEmpty()) {
        setCurrentUid(m_devices.first().toObject().value(QStringLiteral("uid")).toString());
    }
}
void ViewerBackend::applyFrameBytes(const QByteArray &jpeg, const QJsonObject &header,
                                    FrameSource src)
{
    // 2026-10-18：VNC 远控激活时，常规画面通道（JPEG 轮询）整帧让行，
    // 屏幕上只显示 libvncclient 解出的画面，避免两套抓屏抢同一张桌面、显示打架。
    if (m_vncActive) return;
    Q_UNUSED(src)   // 目前只剩 JPEG 一路；VNC 帧走 onVncFrame（不经这里）
    if (jpeg.isEmpty()) {
        logf("[viewer] FAIL 收到一帧但内容是空的");
        return;
    }

    // ── 先按 uid 分诊：这一帧到底是谁的？────────────────────────────────
    // 这是「切换设备后画面还是第一台」的根治点（2026-10-08）。
    // 背景：云端 subscribeViewer 只往订阅集合里 add，一台设备只要被订过就会一直推帧
    // （见 setCurrentUid 的说明）。所以到这里必须自己判，三条路：
    //   · 当前正在看的这台        → 主画面
    //   · 缩略图墙点名要的那几台  → **只**进缩略图槽，绝不碰主画面
    //   · 其余（上一台还赖着发的）→ 丢掉
    // 旧格式裸帧没有 uid 头（老被控端/老云端）⇒ 无从判断，按"当前这台"处理，别把画面弄黑。
    const QString fuid = header.value(QStringLiteral("uid")).toString();
    const bool isMain = fuid.isEmpty() || fuid == m_currentUid;
    const bool isThumb = !isMain && m_thumbUids.contains(fuid);
    if (!isMain && !isThumb) {
        static int dropped = 0;
        if (++dropped == 1 || dropped % 300 == 0) {
            logf("[viewer] 丢掉非当前设备（%s）的帧 %d 张 —— 订阅没退干净时，这就是画面不跟着切的原因",
                 fuid.toUtf8().constData(), dropped);
        }
        return;
    }

    // 缩略图帧到这儿就够了：只缓存一张小图，**不写 m_frame**。
    // 以前两者共用一条路，多班面板一开，主画面就在几台缩略图之间乱跳。
    if (isThumb) {
        QImage t;
        if (!t.loadFromData(jpeg)) return;
        // ⚠️ 变量名别叫 small：Windows 头（rpcndr.h）里有 `#define small char`，
        //    写了就是 `const QImage char = …` ⇒ C2628「QImage 后面跟 char 是非法的」，
        //    报错落在这一行、看着像 QImage 没定义，其实跟 QImage 一点关系没有。
        const QImage thumbImg = (t.width() > 160) ? t.scaledToWidth(160, Qt::SmoothTransformation) : t;
        if (m_thumbFrames.value(fuid) != thumbImg) {
            m_thumbFrames.insert(fuid, thumbImg);
            emit thumbnailChanged(fuid);
        }
        return;
    }

    // ── 画面来源标记：常规轮询一律记 jpeg（VNC 帧由 onVncFrame 自己记账）──
    if (m_frameSource != QStringLiteral("jpeg")) {
        m_frameSource = QStringLiteral("jpeg");
        emit frameSourceChanged();
    }
    m_frameCount++;
    m_framesSinceCheck++;
    m_lastFrameBytes = jpeg.size();
    QImage img;
    if (!img.loadFromData(jpeg)) {
        logf("[viewer] FAIL 这一帧解码不出来（%lld 字节），画面保留上一帧", (long long)jpeg.size());
        emit frameDropped(QStringLiteral("这一帧解不出来，画面还是上一张"));
        emit statsChanged();
        return;
    }
    // 不在这里缩放：缩放是界面的事（QML 有自己的 Image 缩放与填充策略）
    m_frame = img;
    // 记下这帧的归属：裸帧（fuid 空）按"当前这台"记账，否则界面会因为"uid 对不上"永远不上屏。
    m_frameUid = fuid.isEmpty() ? m_currentUid : fuid;
    // ── 静态区：画面连续 kStaticStreak 帧一模一样 → 不再 emit frameChanged，
    //    界面就不换 tick，等于"这张已经画出过了，别再画一遍"。
    //    （画面内容本身没变，所以停刷显示的仍是最新内容，不是旧图。）
    const quint64 fp = frameFingerprint(img);
    m_sameFrameStreak = (fp == m_frameHash) ? (m_sameFrameStreak + 1) : 0;
    m_frameHash = fp;
    setStatic(m_sameFrameStreak >= kStaticStreak);
    if (!m_screenStatic) emit frameChanged();
    if (m_frameCount % 30 == 1) {   // 每 30 帧报一次，别把日志刷爆
        // header 空 = 旧帧（无 v1 帧头）；现在 RTC 帧不再混在这条路里，
        // 所以 "(旧帧)" 这三个字不会再误导人以为 RTC 帧走了裸帧通道
        const QString seq = header.isEmpty()
                                ? QStringLiteral("(旧帧)")
                                : QString::number(header.value(QStringLiteral("seq")).toInt());
        logf("[viewer] 画面在动[JPEG]：第 %d 帧（seq=%s，%d 字节，%.1f fps）",
             m_frameCount, seq.toUtf8().constData(),
             m_lastFrameBytes, m_fps);
    }
    emit statsChanged();
}
void ViewerBackend::setStatic(bool s)
{
    if (m_screenStatic == s) return;
    m_screenStatic = s;
    if (s) logf("[viewer] 画面进入静态区（连续 %d 帧一致）→ 停止重绘", kStaticStreak);
    else   logf("[viewer] 画面恢复动态 → 恢复全帧率重绘");
    emit statsChanged();
}
void ViewerBackend::tickFps()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const double secs = (now - m_lastFpsCheckMs) / 1000.0;
    if (secs < 1.0) return;
    // 基准时间戳**必须无条件推进**。原来的写法把它写在 if (有帧) 里面：
    // 一旦有某个 1 秒窗口里一帧都没有（切设备、RTC 建连、被控端 momentarily 断流），
    // 基准就冻在那儿不动，下一次算出 secs = 好几秒，真实帧率被除以这段空窗 →
    // 明明 21.5fps 的流显示成 "1.0 fps"。窗口空着就报 0，别拿旧分母糊。
    m_fps = (m_framesSinceCheck > 0) ? m_framesSinceCheck / secs : 0.0;
    m_framesSinceCheck = 0;
    m_lastFpsCheckMs = now;
    emit statsChanged();
}
void ViewerBackend::sendAction(const QString &action, const QJsonObject &params)
{
    if (m_currentUid.isEmpty()) {
        setStatus(QStringLiteral("还没选设备，指令没发（不报错就是骗人）"), true);
        logf("[viewer] FAIL 没选设备就点 %s，不发出去", action.toUtf8().constData());
        return;
    }
    sendActionTo(m_currentUid, action, params);
}

// 2026-10-21（审查 #3/#6）：显式指定目标 uid 下发，不碰 currentUid。
void ViewerBackend::sendActionTo(const QString &uid, const QString &action, const QJsonObject &params)
{
    if (uid.isEmpty()) {
        setStatus(QStringLiteral("目标设备为空，指令没发（不报错就是骗人）"), true);
        logf("[viewer] FAIL sendActionTo 收到空 uid（%s），不发出去", action.toUtf8().constData());
        return;
    }
    setStatus(QStringLiteral("已把「%1」发给 %2，等云端回话…").arg(action).arg(uid), false);
    QJsonObject p;
    p.insert(QStringLiteral("uid"), uid);
    p.insert(QStringLiteral("action"), action);
    if (!params.isEmpty()) p.insert(QStringLiteral("params"), params);
    sendEnvelope(QStringLiteral("instruction"), p);
}
// ──────────────────────────────────────────────────────────────────────
// 语音对讲（设计文档 3.3，2026-10-07）
// 老师端：开麦 → audio.start → 每帧 PCM 二进制发云端 → 云端 fan-out → 学生端播放。
// 安全（3.3.6）：默认静音，只有点"开始讲话"才采集；停止即完全静音；不录音不存储。
// ──────────────────────────────────────────────────────────────────────
// 2026-10-13 架构拆分：语音对讲 + 屏幕广播已搬到 viewerbackend_voice.cpp（同 header 成员，跨 TU 合法）。
QString ViewerBackend::defaultCloudUrl()
{
    return kDefaultViewerUrl;
}

// ── 管理面 HTTP 基址（m_url 是 WebSocket 地址，派生见头文件注释）──
// 2026-10-16 新增：把原先散在 5 处的同款派生收成单一真源；InteractBridge 漏做这步
// 导致审批面板在真机上永远 404，由真桥运行时探针实测发现。
QString ViewerBackend::httpBaseUrl() const
{
    QString httpUrl = m_url;
    httpUrl.replace(QLatin1String("wss://"), QLatin1String("https://"))
           .replace(QLatin1String("ws://"), QLatin1String("http://"));
    const int wsIdx = httpUrl.indexOf(QLatin1String("/ws/"));
    if (wsIdx > 0) httpUrl.truncate(wsIdx);
    return httpUrl;
}

// ── 2026-10-11：拉事件流水（/api/events，最近 50 条）→ m_events + eventsChanged ──
// 手机端告警页数据源（桌面 NotifyDialog 已有本地通知，不需要它；仅手机端拉）。
void ViewerBackend::fetchEvents()
{
    // 2026-10-13 审计 2.9：m_nam 已在构造函数创建，这里直接用
    QString httpUrl = m_url;
    httpUrl.replace(QLatin1String("wss://"), QLatin1String("https://"))
           .replace(QLatin1String("ws://"), QLatin1String("http://"));
    const int wsIdx = httpUrl.indexOf(QLatin1String("/ws/"));
    if (wsIdx > 0) httpUrl.truncate(wsIdx);
    httpUrl += QStringLiteral("/api/events?limit=50");
    QNetworkRequest req{ QUrl(httpUrl) };
    const QByteArray token = !m_cloudTicket.isEmpty()
        ? m_cloudTicket.toUtf8() : m_token.toUtf8();
    if (!token.isEmpty()) req.setRawHeader("Authorization", "Bearer " + token);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject()) {
                m_events = doc.object().value(QStringLiteral("events")).toArray();
                emit eventsChanged();
            }
        } else {
            logf("[viewer] 拉事件流水失败（HTTP %d）", status);
        }
        reply->deleteLater();
    });
}

// ── 2026-10-19：拉动作执行排行（/api/actions/ranking，最近 7 天）→ 概览页排行卡 ──
void ViewerBackend::fetchActionRanking()
{
    // 2026-10-13 审计 2.9：m_nam 已在构造函数创建，这里直接用
    QString httpUrl = m_url;
    httpUrl.replace(QLatin1String("wss://"), QLatin1String("https://"))
           .replace(QLatin1String("ws://"), QLatin1String("http://"));
    const int wsIdx = httpUrl.indexOf(QLatin1String("/ws/"));
    if (wsIdx > 0) httpUrl.truncate(wsIdx);
    httpUrl += QStringLiteral("/api/actions/ranking?days=7&limit=20");
    QNetworkRequest req{ QUrl(httpUrl) };
    const QByteArray token = !m_cloudTicket.isEmpty()
        ? m_cloudTicket.toUtf8() : m_token.toUtf8();
    if (!token.isEmpty()) req.setRawHeader("Authorization", "Bearer " + token);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject()) {
                m_actionRanking = doc.object().value(QStringLiteral("ranking")).toArray();
                emit actionRankingChanged();
            }
        } else {
            logf("[viewer] 拉动作排行失败（HTTP %d）", status);
        }
        reply->deleteLater();
    });
}

// ── 2026-10-11：QML 热更（一源响应式）──
// 手机端定期拉与桌面一致的 QML 树：GET /api/qml-patch → {version,url}，
// 与本地 qml-live/.version 不同 → 下载 zip → QZipReader 解压到 qml-live/
// （下次启动 main.cpp 本地优先加载）。改 UI 不用重出 APK。
// ── 诊断与反馈（2026-10-12 · GUI 子系统后没控制台，出错要看界面）─────────────
QString ViewerBackend::viewerLogTail(int maxLines) const
{
    if (m_logPath.isEmpty()) return QStringLiteral("（未知日志路径）");
    QFile f(m_logPath);
    if (!f.exists()) return QStringLiteral("（日志文件还不存在：%1）").arg(m_logPath);
    if (!f.open(QIODevice::ReadOnly)) return QStringLiteral("（日志打不开：%1）").arg(f.errorString());
    // 只读尾部：日志可能不小，从末尾最多回读 512KB，够几百行。
    const qint64 sz = f.size();
    const qint64 cap = 512 * 1024;
    const qint64 start = qMax<qint64>(0, sz - cap);
    f.seek(start);
    const QByteArray raw = f.readAll();
    f.close();
    QStringList lines = QString::fromLocal8Bit(raw).split(QLatin1Char('\n'));   // 日志以本机编码落盘
    if (start > 0 && !lines.isEmpty()) lines.removeFirst();                      // 首行可能被截断
    if (maxLines > 0 && lines.size() > maxLines)
        lines = lines.mid(lines.size() - maxLines);
    return lines.join(QLatin1Char('\n'));
}

QString ViewerBackend::diagnosticsSummary() const
{
    QStringList out;
    out << QStringLiteral("星集控 · 管理端 诊断信息");
    out << QStringLiteral("版本：%1").arg(m_version);
    out << QStringLiteral("时间：%1").arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    out << QStringLiteral("系统：%1（%2）").arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture());
    out << QStringLiteral("云端地址：%1").arg(m_url);
    out << QStringLiteral("连接：%1 ｜ 鉴权：%2")
               .arg(m_connected ? QStringLiteral("已连接") : QStringLiteral("未连接"),
                    m_authed ? QStringLiteral("已鉴权") : QStringLiteral("未鉴权"));
    out << QStringLiteral("身份：%1").arg(m_role.isEmpty() ? QStringLiteral("（无）") : m_role);
    out << QStringLiteral("设备数：%1（在线 %2）").arg(m_devices.size()).arg(onlineCount());
    out << QStringLiteral("当前设备：%1").arg(m_currentUid.isEmpty() ? QStringLiteral("（未选）") : m_currentUid);
    out << QStringLiteral("日志文件：%1").arg(m_logPath);
    return out.join(QLatin1Char('\n'));
}

void ViewerBackend::copyToClipboard(const QString &text) const
{
    if (QClipboard *cb = QGuiApplication::clipboard()) cb->setText(text);
}

void ViewerBackend::openLogFolder() const
{
    const QString dir = m_logPath.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
            : QFileInfo(m_logPath).absolutePath();
    if (!dir.isEmpty()) steOpenUrlExternal(QUrl::fromLocalFile(dir));   // 提权安全（oauthlogin.h）
}

void ViewerBackend::requestDiagFeedback()
{
    emit diagFeedbackRequested();
}

void ViewerBackend::checkQmlPatch()
{
#if defined(Q_OS_ANDROID)
    // 2026-10-13 审计 2.9：m_nam 已在构造函数创建，这里直接用
    QString httpUrl = m_url;
    httpUrl.replace(QLatin1String("wss://"), QLatin1String("https://"))
           .replace(QLatin1String("ws://"), QLatin1String("http://"));
    const int wsIdx = httpUrl.indexOf(QLatin1String("/ws/"));
    if (wsIdx > 0) httpUrl.truncate(wsIdx);
    const QString manifestUrl = httpUrl + QStringLiteral("/api/qml-patch");
    QNetworkRequest req{ QUrl(manifestUrl) };
    const QByteArray token = !m_cloudTicket.isEmpty()
        ? m_cloudTicket.toUtf8() : m_token.toUtf8();
    if (!token.isEmpty()) req.setRawHeader("Authorization", "Bearer " + token);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, httpUrl, token]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) { logf("[viewer] qml-patch 清单失败（HTTP %d）", status); return; }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        const QJsonObject obj = doc.object();
        const QString ver = obj.value(QStringLiteral("version")).toString();
        if (ver.isEmpty()) return;
        const QString liveDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
            + QStringLiteral("/qml-live");
        const QString verFile = liveDir + QStringLiteral("/.version");
        if (QFile::exists(verFile)) {
            QFile vf(verFile);
            if (vf.open(QIODevice::ReadOnly)) {
                const QString localVer = QString::fromUtf8(vf.readAll()).trimmed();
                if (localVer == ver) { logf("[viewer] qml-patch 已是最新 %s", ver.toUtf8().constData()); return; }
            }
        }
        // 版本不同 → 下载 zip
        const QString zipUrl = httpUrl + QStringLiteral("/api/qml-patch/download");
        QNetworkRequest dz{ QUrl(zipUrl) };
        if (!token.isEmpty()) dz.setRawHeader("Authorization", "Bearer " + token);
        QNetworkReply *zr = m_nam->get(dz);
        connect(zr, &QNetworkReply::finished, this, [this, zr, liveDir, ver]() {
            zr->deleteLater();
            const int zs = zr->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (zs != 200) { logf("[viewer] qml-patch 下载失败（HTTP %d）", zs); return; }
            const QByteArray zipData = zr->readAll();
            // 自写 store-zip 解压（云端 qml-patch 是 store 模式无压缩）：
            // 读 EOCD → 中央目录 → 每条本地头 → 直接拷数据。不依赖 Qt private API。
            QDir().mkpath(liveDir);
            const bool ok = unzipStore(liveDir, zipData);
            if (ok) {
                { QFile vf(liveDir + QStringLiteral("/.version"));
                  if (vf.open(QIODevice::WriteOnly)) { vf.write(ver.toUtf8()); } }
                logf("[viewer] qml-patch 更新到 %s，下次启动生效", ver.toUtf8().constData());
            } else {
                logf("[viewer] qml-patch 解压失败");
            }
        });
    });
#endif // Q_OS_ANDROID
}

// ── store 模式 zip 解压（qml-patch 用；无压缩，只读中央目录拷数据）──
// 格式：中央目录每条 46 字节头（sig 0x02014b50）+ 名字 + 本地文件头 30 字节 + 数据。
bool ViewerBackend::unzipStore(const QString &outDir, const QByteArray &zip)
{
    // ⚠️ 2026-10-12 修（安全审计 #4/#5）：zip slip 任意路径写 + 越界读。
    //   · name 直接取自 zip 中央目录，原来没做路径规范化 —— 一个名称含
    //     `../../../Start Menu/Programs/Startup/evil.bat` 的 zip 能在任意路径落文件。
    //   · cdOff/cdSize/lho/compSize 全来自内容，原来无边界检查可直接越界读内存。
    const QString root = QDir::cleanPath(outDir);
    const int rootLen = root.length();

    // 路径必须落在 outDir 内（含反斜杠当分隔符规范化；首段不允许 ..）
    auto pathInside = [&root, rootLen](const QString &raw) -> QString {
        // 反斜杠（Windows 分隔符）也当分隔符处理 → replace('/') 后 cleanPath 统一
        QString p = raw;
        p.replace(QLatin1Char('\\'), QLatin1Char('/'));
        const QString canon = QDir::cleanPath(root + QLatin1Char('/') + p);
        if (!canon.startsWith(root + QLatin1Char('/')) && canon != root) return QString();
        // 绝对路径（如 C:/x）会被 cleanPath 冲掉前缀 → 必然不 startsWith root → 上面已拒
        if (QDir::isAbsolutePath(raw)) return QString();
        return canon;
    };

    // 找 EOCD（结尾 22+ 字节，sig 0x06054b50）
    for (int i = zip.size() - 22; i >= 0; --i) {
        if (zip.size() - i < 22) break;
        const uchar *p = (const uchar *)zip.constData() + i;
        if (p[0] == 0x50 && p[1] == 0x4b && p[2] == 0x05 && p[3] == 0x06) {
            quint32 cdSize, cdOff;
            memcpy(&cdSize, p + 12, 4);
            memcpy(&cdOff, p + 16, 4);
            // 中央目录偏移/大小必须整段落在 zip 内
            if ((qint64)cdOff > zip.size() || (qint64)cdSize > zip.size() - (qint64)cdOff)
                return false;
            // 解析中央目录
            int pos = (int)cdOff;
            const int cdEnd = pos + (int)cdSize;
            while (pos + 46 <= cdEnd) {
                const uchar *c = (const uchar *)zip.constData() + pos;
                if (c[0] != 0x50 || c[1] != 0x4b || c[2] != 0x01 || c[3] != 0x02) break;
                quint32 method, lho, compSize, uncompSize, nameLen, extraLen, commentLen;
                memcpy(&method, c + 10, 2);
                memcpy(&compSize, c + 20, 4);
                memcpy(&uncompSize, c + 24, 4);
                memcpy(&nameLen, c + 28, 2);
                memcpy(&extraLen, c + 30, 2);
                memcpy(&commentLen, c + 32, 2);
                memcpy(&lho, c + 42, 4);
                const QByteArray name = QByteArray((const char *)c + 46, (int)nameLen);
                pos += 46 + (int)nameLen + (int)extraLen + (int)commentLen;
                if (name.isEmpty() || name.endsWith('/')) continue;   // 目录项跳过
                if (method != 0) continue;                            // 非 store 跳过
                // 本地头在 lho：30 字节头 + 名字 + 数据 —— qint64 比较防 32 位溢出绕过
                const qint64 need = (qint64)lho + 30 + (qint64)nameLen + (qint64)compSize;
                if (need > zip.size()) continue;
                const uchar *lh = (const uchar *)zip.constData() + lho;
                const char *data = zip.constData() + lho + 30 + (int)nameLen;
                // zip slip：路径必须留在 outDir 内，越界（.. / 绝对路径）整条丢弃
                const QString out = pathInside(QString::fromUtf8(name));
                if (out.isEmpty()) {
                    logf("[viewer] 拒绝解压越界路径：%.200s", QString::fromUtf8(name).toUtf8().constData());
                    continue;
                }
                QDir().mkpath(QFileInfo(out).absolutePath());
                QFile wf(out);
                if (wf.open(QIODevice::WriteOnly)) wf.write(data, (qint64)compSize);
                Q_UNUSED(uncompSize)
            }
            return true;
        }
    }
    return false;
}

void ViewerBackend::fetchRecordings()
{
    // 2026-10-13 审计 2.9：m_nam 已在构造函数创建，这里直接用
    // 云端地址：m_url 是 ws://host:port/ws/viewer，派生 http://host:port/api/recordings
    QString httpUrl = m_url;
    httpUrl.replace(QLatin1String("wss://"), QLatin1String("https://"))
           .replace(QLatin1String("ws://"), QLatin1String("http://"));
    const int wsIdx = httpUrl.indexOf(QLatin1String("/ws/"));
    if (wsIdx > 0) httpUrl.truncate(wsIdx);
    httpUrl += QStringLiteral("/api/recordings");
    QNetworkRequest req{ QUrl(httpUrl) };
    // 云端令牌：票据优先（有 uid 可审计），否则静态令牌
    const QByteArray token = !m_cloudTicket.isEmpty()
        ? m_cloudTicket.toUtf8() : m_token.toUtf8();
    if (!token.isEmpty()) req.setRawHeader("Authorization", "Bearer " + token);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, httpUrl]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject()) {
                m_recordings = doc.object().value(QStringLiteral("recordings")).toObject();
                emit recordingsChanged();
            }
        } else {
            logf("[rec] 拉录制列表失败（HTTP %d）", status);
        }
        reply->deleteLater();
    });
}
// ── 分班制（2026-10-08）──
// 拉全部班级（含每台在线数）。云端算好结论推下来，界面只消费。
void ViewerBackend::requestClasses()
{
    // 2026-10-13 审计 2.9：m_nam 已在构造函数创建，这里直接用
    QString httpUrl = m_url;
    httpUrl.replace(QLatin1String("wss://"), QLatin1String("https://"))
           .replace(QLatin1String("ws://"), QLatin1String("http://"));
    const int wsIdx = httpUrl.indexOf(QLatin1String("/ws/"));
    if (wsIdx > 0) httpUrl.truncate(wsIdx);
    httpUrl += QStringLiteral("/api/classes");
    QNetworkRequest req{ QUrl(httpUrl) };
    // 凭据选择：**登录票据优先，静态令牌兜底**。
    //   2026-10-10 曾是反过来的（静态优先），当时的理由是「云端 isViewerApi 只认静态令牌，
    //   拿票据拉必 401」。但云端后来已改成两种都认（index.js 的 vStatic || vTicket），
    //   于是静态优先从"修 bug"变成了**功能障碍**：云端把每次请求都当运维身份
    //   （mode=token、uid=null），/api/classes 的按班过滤永远不触发 —— 老师端看到全校班级。
    //   现在票据优先 ⇒ 云端认得出「这个人是谁」，只回他带的班（响应带 filtered:true）；
    //   未登录 / 票据过期时退回静态令牌（系统身份，仍给全量，运维要看得见全校）。
    const bool useTicket = !m_cloudTicket.isEmpty() && ticketUsable();
    const QByteArray token = useTicket ? m_cloudTicket.toUtf8() : m_token.toUtf8();
    if (!token.isEmpty()) req.setRawHeader("Authorization", "Bearer " + token);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, httpUrl, useTicket]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject() && doc.object().value(QStringLiteral("ok")).toBool()) {
                const QJsonObject o = doc.object();
                m_classes = o.value(QStringLiteral("classes")).toArray();
                // 2026-10-25：票据优先之后，云端对「我带的班」会回 filtered:true（无成员时
                // 兜底给全量 + false）。界面靠它说明"为什么只列出这几个班"，别让老师以为
                // 班级数据丢了。静态令牌 / 未登录恒为 false。
                m_classesFiltered = o.value(QStringLiteral("filtered")).toBool(false);
                emit classesChanged();
                if (m_classErrStatus != -1) { m_classErrStatus = -1; logf("[class] 班级列表恢复正常"); }
            }
        } else if (status != m_classErrStatus) {
            // 只在状态**变化**时记（15s 轮询下原来会一直刷同一句 401）
            m_classErrStatus = status;
            // 2026-10-11：光说"401，多半是令牌不一致"没法动手 —— 得说清这次用的是
            // 哪一种令牌、多长（长度为 0 = 压根没配），才能对上是 viewer.env 漏配
            // 还是和云端 .env 的 VIEWER_TOKEN 对不上。
            logf("[class] 拉班级列表失败 HTTP %d；本次凭据＝%s（长 %d），URL=%s",
                 status,
                 useTicket ? "登录票据" : (m_token.isEmpty() ? "完全没有凭据" : "静态令牌 STE_VIEWER_TOKEN"),
                 (int)(useTicket ? m_cloudTicket.size() : m_token.size()),
                 httpUrl.toUtf8().constData());
            if (status == 401)
                logf("[class] 401 的修法：① 未登录时用的是 viewer.env 的 STE_VIEWER_TOKEN，"
                     "须与云端 .env 的 VIEWER_TOKEN 逐字一致；② 已登录则可能是票据过期，"
                     "到设置页「账户与登录」重新登录即可");
        }
        reply->deleteLater();
    });
}

void ViewerBackend::requestStorage()
{
    // 2026-10-13 审计 2.9：m_nam 已在构造函数创建，这里直接用
    QString httpUrl = m_url;
    httpUrl.replace(QLatin1String("wss://"), QLatin1String("https://"))
           .replace(QLatin1String("ws://"), QLatin1String("http://"));
    const int wsIdx = httpUrl.indexOf(QLatin1String("/ws/"));
    if (wsIdx > 0) httpUrl.truncate(wsIdx);
    httpUrl += QStringLiteral("/api/storage");
    QNetworkRequest req{ QUrl(httpUrl) };
    // 存储概况不带个人身份（磁盘/在线数都是全校口径），故沿用静态令牌优先；
    // 未配静态令牌时用票据兜底。与 requestClasses 的差别是有意的，别顺手统一。
    const QByteArray token = !m_token.isEmpty()
        ? m_token.toUtf8() : m_cloudTicket.toUtf8();
    if (!token.isEmpty()) req.setRawHeader("Authorization", "Bearer " + token);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, httpUrl]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject() && doc.object().value(QStringLiteral("ok")).toBool()) {
                m_storage = doc.object().value(QStringLiteral("storage")).toObject();
                emit storageChanged();
                if (m_storageErrStatus != -1) { m_storageErrStatus = -1; logf("[storage] 云端存储概况恢复正常"); }
            }
        } else if (status != m_storageErrStatus) {
            m_storageErrStatus = status;
            logf("[storage] 拉云端存储概况失败（HTTP %d）", status);
        }
        reply->deleteLater();
    });
}
// ──────────────────────────────────────────────────────────────────────
// 远程终端
// ──────────────────────────────────────────────────────────────────────
void ViewerBackend::setTermState(TermState s, const QString &sid, const QString &note)
{
    if (m_termState == s && m_termSid == sid && m_termNote == note) return;
    m_termState = s;
    m_termSid = sid;
    m_termNote = note;
    emit termStateChanged();
}
void ViewerBackend::sendTermAction(const QString &action, const QJsonObject &params)
{
    if (m_currentUid.isEmpty()) {
        setStatus(QStringLiteral("还没选设备，终端动作没发（不报错就是骗人）"), true);
        return;
    }
    QJsonObject p;
    p.insert(QStringLiteral("uid"), m_currentUid);
    p.insert(QStringLiteral("action"), action);
    if (!params.isEmpty()) p.insert(QStringLiteral("params"), params);
    sendEnvelope(QStringLiteral("instruction"), p);
    logf("[viewer] → %s 终端动作 %s", m_currentUid.toUtf8().constData(), action.toUtf8().constData());
}
void ViewerBackend::termOpen(const QString &shell)
{
    const QString sh = shell.trimmed().toLower();
    // 只放行 cmd / powershell：和被控端一致（那边也只认这两个），别让"开了个空壳终端"
    if (sh != QStringLiteral("cmd") && sh != QStringLiteral("powershell")) {
        setStatus(QStringLiteral("不支持的 shell：%1（只认 cmd / powershell）").arg(shell), true);
        return;
    }
    if (m_termState != TermIdle) {
        setStatus(QStringLiteral("这台已经有终端会话了，先关掉再开"), true);
        return;
    }
    const QString sid = QStringLiteral("v%1").arg(++m_termSidSeq);
    QJsonObject params;
    params.insert(QStringLiteral("sid"), sid);
    params.insert(QStringLiteral("shell"), sh);
    params.insert(QStringLiteral("cols"), 120);
    params.insert(QStringLiteral("rows"), 40);
    // 空闲 10 分钟没人理就自己关：这条链路上没人看着的会话开着等于给个后门
    params.insert(QStringLiteral("idleMs"), 600000);
    params.insert(QStringLiteral("cmdMs"), 30000);
    setTermState(TermPending, sid, QStringLiteral("等本机点头…"));
    sendTermAction(QStringLiteral("terminal_open"), params);
    // 收口：本机可能一直不点头（弹窗没人看），到点上面那条定时器会把状态放回 Idle
    m_termWait->start();
}
void ViewerBackend::termInput(const QString &sid, const QString &keys)
{
    if (m_termState != TermOpen || sid != m_termSid) {
        setStatus(QStringLiteral("终端会话不在，命令没发（先开终端）"), true);
        return;
    }
    // 不排队：REPL 是按"一条跑完再来下一条"的口子做的，排队的命令会错序
    QJsonObject params;
    params.insert(QStringLiteral("sid"), sid);
    params.insert(QStringLiteral("keys"), keys);
    sendTermAction(QStringLiteral("terminal_input"), params);
}
void ViewerBackend::termClose()
{
    if (m_termState == TermIdle) return;
    // 去重：「等本机点头」和「关断请求已发、在等回话」两边状态都是 Pending（前一种也要能取消），
    // 光看状态分不出来。不加这一道，连点按钮 / 弹窗 esc 关（onClosed→doClose）会重复下发
    // terminal_close —— 界面上那颗按钮已经禁用了，这里再兜一道，别只靠 QML 那一层。
    if (m_termClosing) return;
    m_termClosing = true;
    const QString sid = m_termSid;
    QJsonObject params;
    params.insert(QStringLiteral("sid"), sid);
    sendTermAction(QStringLiteral("terminal_close"), params);
    // 本地先收摊：等被控端回 terminal_closed 可能要几百毫秒，
    // 这期间输入框留着会让人以为还能敲（敲了也是发给一个已经关掉的会话）
    setTermState(TermPending, sid, QStringLiteral("正在关…"));
    // 收口：被控端不回 terminal_closed（会话早死了/网络断了）时，2.5 秒后按已关处理，
    // 否则状态永远停在 Pending ⇒ 这台再也开不了新终端（#95 的"关不掉"那一半）。
    m_termCloseWait->start();
}
void ViewerBackend::sendPointer(const QString &kind, double nx, double ny, const QString &button)
{
    if (m_currentUid.isEmpty()) { logf("[viewer] FAIL 没选设备不发操控"); return; }
    // 2026-10-11：坐标不是有限数就别发。画面区还没算出尺寸时 fitRect 宽高为 0，
    // QML 的 (mx - r.x) / r.width 得到 NaN，转成整数就是 INT_MIN —— 日志里那几百行
    // `操控→ move 在 (-2147483648%, -2147483648%)` 就是这么来的：光标被甩到教室机角上。
    if (!std::isfinite(nx) || !std::isfinite(ny)) {
        logf("[viewer] FAIL 操控坐标不是有限数（画面区尺寸还没算出来），本次 %s 不发",
             kind.toUtf8().constData());
        return;
    }
    // 2026-10-18：VNC 会话激活 → 鼠标走 RFB PointerEvent（与 VNC 服务同源，不双重注入）。
    if (m_vncActive && m_vnc) {
        quint8 bit = 1;   // 默认左键
        if (button == QStringLiteral("right")) bit = 2;
        else if (button == QStringLiteral("middle")) bit = 4;
        if (kind == QStringLiteral("down")) m_vncBtnMask |= bit;
        else if (kind == QStringLiteral("up")) m_vncBtnMask &= ~bit;
        // move 保持当前 mask 不变
        m_vnc->sendPointer(nx, ny, m_vncBtnMask);
        return;
    }
    QJsonObject params;
    params.insert(QStringLiteral("kind"), kind);
    params.insert(QStringLiteral("x"), nx);
    params.insert(QStringLiteral("y"), ny);
    params.insert(QStringLiteral("button"),
                  button.isEmpty() ? QStringLiteral("left") : button);
    QJsonObject p;
    p.insert(QStringLiteral("uid"), m_currentUid);
    p.insert(QStringLiteral("action"), QStringLiteral("input"));
    p.insert(QStringLiteral("params"), params);
    sendEnvelope(QStringLiteral("instruction"), p);
    logf("[viewer] 操控→ %s 在 (%d%%, %d%%)", kind.toUtf8().constData(),
         (int)(nx * 100), (int)(ny * 100));
}

// ──────────────────────────────────────────────────────────────────────
// 远控/键鼠（2026-10-17 补齐）：键盘事件下发
// 与 sendPointer 同一条 instruction/input 通道；kind=key + vk + action(down|up)。
// 被控端（agent main.cpp input 分支 kind=key）直接 SendInput 注入。
void ViewerBackend::sendKey(int vk, const QString &action)
{
    if (m_currentUid.isEmpty()) { logf("[viewer] FAIL 没选设备不发按键"); return; }
    if (vk <= 0 || vk > 255) { logf("[viewer] FAIL vk 越界 %d，不发", vk); return; }
    // 2026-10-18：VNC 会话激活 → 键盘走 RFB KeyEvent（VK→X11 keysym），同源不双重注入。
    if (m_vncActive && m_vnc) {
        m_vnc->sendKeyVk(vk, action == QStringLiteral("down"));
        logf("[viewer] 远控键→ vk=%d %s", vk,
             action == QStringLiteral("down") ? "down" : "up");
        return;
    }
    QJsonObject params;
    params.insert(QStringLiteral("kind"), QStringLiteral("key"));
    params.insert(QStringLiteral("vk"), vk);
    params.insert(QStringLiteral("action"), action);
    QJsonObject p;
    p.insert(QStringLiteral("uid"), m_currentUid);
    p.insert(QStringLiteral("action"), QStringLiteral("input"));
    p.insert(QStringLiteral("params"), params);
    sendEnvelope(QStringLiteral("instruction"), p);
    logf("[viewer] 按键→ %s vk=%d", action.toUtf8().constData(), vk);
}

// ──────────────────────────────────────────────────────────────────────
// 远控（2026-10-18 · VNC over WebSocket · libvncclient）
//
// 与上一版（P2 第二条隧道 WS + 手写 RFB）的区别：
//   · 不再开第二条 WS、不再自己解析 RFB —— 协议栈交给 libvncclient；
//   · libvncclient 只连**本进程回环端口**（见 vnc_client.cpp），字节由本类与主 WS
//     互搬：
//       入站：云端下发 vnc-data 二进制帧 → onVncData() → VncClient::feedIncoming()
//       出站：VncClient::vncOut → 这里包成 vnc-data 二进制帧 → 主 WS
//   · 被控端侧是同源的 libvncserver（只监听 127.0.0.1，由 agent 泵字节），公网 5900 不暴露。
//
// vnc-data 帧格式（与既有画面帧同信封）：
//   [1B 版本=1][2B 大端 headerLen][header JSON][原始 RFB 字节流]
//   header：type=vnc-data / uid / direction（down=管理端→被控端，up=被控端→管理端）
//           / seq（发送方自增，调试与丢包检测用）。
//   ⚠️ Payload 直接装 RFB 原始字节，**不在帧内再切分** —— 拆包/粘包由两端各自
//   的流式消费处理（libvncclient/libvncserver 对 socket 天然按流读）。
// ──────────────────────────────────────────────────────────────────────
static quint64 g_vncOutSeq = 0;   // 出站帧序号（仅调试/丢包检测，非协议强依赖）

/** 把一段原始 RFB 字节包成主 WS 的 vnc-data 二进制帧（信封格式与画面帧一致）。 */
static QByteArray makeVncFrame(const QString &uid, const QByteArray &rfb)
{
    QJsonObject h;
    h.insert(QStringLiteral("v"), 1);
    h.insert(QStringLiteral("type"), QStringLiteral("vnc-data"));
    h.insert(QStringLiteral("mime"), QStringLiteral("application/vnc"));
    h.insert(QStringLiteral("uid"), uid);
    h.insert(QStringLiteral("direction"), QStringLiteral("down")); // 管理端→被控端
    h.insert(QStringLiteral("seq"), double(++g_vncOutSeq));
    const QByteArray hb = QJsonDocument(h).toJson(QJsonDocument::Compact);
    QByteArray out;
    out.reserve(3 + hb.size() + rfb.size());
    out.append(char(1));                                  // 版本
    out.append(char((hb.size() >> 8) & 0xFF));            // 大端头长
    out.append(char(hb.size() & 0xFF));
    out.append(hb);
    out.append(rfb);
    return out;
}

void ViewerBackend::setVncImageProvider(VncImageProvider *p)
{
    m_vncProvider = p;
}

void ViewerBackend::setVncState(const QString &s)
{
    if (m_vncState == s) return;
    m_vncState = s;
    emit vncStateChanged();
}

void ViewerBackend::startVnc(const QString &uid)
{
    if (uid.isEmpty()) return;
    // 幂等：同一台已经在连/已连上，就不要再重启会话（重启必然打断正在进行的握手）。
    if (m_vncActive && m_vncUid == uid) {
        logf("[viewer] VNC 会话已在 %s 上，忽略重复拉起", uid.toUtf8().constData());
        return;
    }
    m_vncUid = uid;
    if (!m_vnc) {
        m_vnc = new VncClient(this);
        connect(m_vnc, &VncClient::frameReady, this, &ViewerBackend::onVncFrame);
        // 出站：libvncclient 想发的 RFB 字节（协议版本/认证/更新请求/键鼠）→ 主 WS
        connect(m_vnc, &VncClient::vncOut, this, [this](const QByteArray &data) {
            if (data.isEmpty() || !m_ws) return;
            const qint64 n = m_ws->sendBinaryMessage(makeVncFrame(m_currentUid, data));
            // 诊断（2026-10-18 深夜排障）：down 方向此前完全黑盒 —— 打头几帧看字节去哪了
            static int dbgSent = 0;
            if (dbgSent < 4)
                logf("[viewer-vnc] down 已发 #%d：%d 字节（uid=%s）", ++dbgSent,
                     int(n), m_currentUid.toUtf8().constData());
        });
        connect(m_vnc, &VncClient::stateChanged, this, [this](int st) {
            switch (st) {
            case VncClient::Connecting: setVncState(QStringLiteral("连接中")); break;
            case VncClient::Connected:  setVncState(QStringLiteral("已连接")); break;
            case VncClient::Error:      setVncState(QStringLiteral("连接失败")); break;
            default:                    setVncState(QStringLiteral("已断开")); break;
            }
            emit vncStateChanged();   // vncActive 与状态同信号，一起刷新
        });
    }
    // VNC 密码：环境变量 STE_VNC_PASSWORD 优先（被控端 libvncserver 侧的密码）。
    if (m_vncPassword.isEmpty())
        m_vncPassword = qEnvironmentVariable("STE_VNC_PASSWORD", QString()).trimmed();

    m_vncActive = true;
    m_vncBtnMask = 0;
    if (m_frameSource != QStringLiteral("vnc")) {
        m_frameSource = QStringLiteral("vnc");
        emit frameSourceChanged();
    }
    setVncState(QStringLiteral("连接中"));
    emit vncStateChanged();
    logf("[viewer] 拉起 VNC 会话：uid=%s（RFB 字节走主 WS 的 vnc-data 帧）",
         uid.toUtf8().constData());
    m_vnc->start(m_vncPassword);
    m_pendingVncUid.clear();   // 已拉起，兜底定时器不再需要动手

    // ⚠️ 2026-10-18 深夜实证修复：这里**不能退订**！云端 vncDataToViewer 只投给
    //    「订阅中」的管理端（谁订阅投谁），退订之后被控端的 up 帧会被云端静默丢弃
    //    —— RFB 握手第一帧（ProtocolVersion 12B）就到不了 viewer，画面永远黑。
    //    （此前退订的动机是"两套抓屏抢桌面"，实际上 agent 侧抓屏不受订阅影响，
    //     真正的互斥靠 m_vncActive 让 applyFrameBytes 让行，见 onBinaryMessage。）
    //    stopVnc 里的重新订阅保留（幂等，双保险）。
}

void ViewerBackend::stopVnc()
{
    if (!m_vncActive && !m_vnc) return;
    m_vncActive = false;
    m_vncUid.clear();
    m_pendingVncUid.clear();
    m_vncBtnMask = 0;
    if (m_vnc) m_vnc->stop();
    if (m_vncProvider) m_vncProvider->clear();
    if (m_frameSource == QStringLiteral("vnc")) {
        m_frameSource = QStringLiteral("none");
        emit frameSourceChanged();
    }
    setVncState(QStringLiteral("已断开"));
    logf("[viewer] VNC 会话已停止");
    // 恢复常规画面通道：重新订阅当前设备。
    if (!m_currentUid.isEmpty() && m_screenActive) {
        QJsonObject sp;
        sp.insert(QStringLiteral("uid"), m_currentUid);
        sendEnvelope(QStringLiteral("subscribe"), sp);
    }
}

void ViewerBackend::onVncData(const QByteArray &data)
{
    if (data.isEmpty()) return;
    // 2026-10-24 诊断：vnc-data 是否真到达管理端（此前只能看被控端"已发"，到达与否是黑盒）
    static int dbgVncIn = 0;
    if (dbgVncIn < 8)
        logf("[viewer] vnc-data 到达 #%d：%d 字节", ++dbgVncIn, data.size());
    if (!m_vnc) {
        logf("[viewer] WARN 收到 vnc-data 但 VNC 客户端未建立（丢弃 %d 字节）", data.size());
        return;
    }
    m_vnc->feedIncoming(data);
}

void ViewerBackend::onVncFrame(const QImage &img)
{
    if (img.isNull() || !m_vncActive) return;
    if (m_frameSource != QStringLiteral("vnc")) {
        m_frameSource = QStringLiteral("vnc");
        emit frameSourceChanged();
    }
    m_frameCount++;
    m_frame = img;
    m_frameUid = m_currentUid;              // libvncclient 帧不带 uid，按当前这台记账
    m_lastFrameBytes = (int)img.sizeInBytes();
    const quint64 fp = frameFingerprint(img);
    m_sameFrameStreak = (fp == m_frameHash) ? (m_sameFrameStreak + 1) : 0;
    m_frameHash = fp;
    setStatic(m_sameFrameStreak >= kStaticStreak);
    if (m_vncProvider) m_vncProvider->update(img);   // image://vnc/live
    if (!m_screenStatic) emit frameChanged();
    emit vncFrameReady(img);
    emit statsChanged();
    if (m_frameCount % 30 == 1) {
        logf("[viewer] 远控画面在动[VNC]：第 %d 帧（%dx%d）",
             m_frameCount, img.width(), img.height());
    }
    // 取证开关（2026-10-24）：设 STE_VNC_CAPTURE=<png 路径> 时，每 60 帧把**实际解出并送屏**
    // 的画面落盘一张。排障时"到底有没有画面"不该靠猜或靠人盯着看 —— 直接看图。
    static const QString capPath = qEnvironmentVariable("STE_VNC_CAPTURE", QString()).trimmed();
    if (!capPath.isEmpty() && m_frameCount % 60 == 1) {
        if (img.save(capPath, "PNG"))
            logf("[viewer] 取证帧已落盘：%s（第 %d 帧 %dx%d）",
                 capPath.toUtf8().constData(), m_frameCount, img.width(), img.height());
    }
}

// ──────────────────────────────────────────────────────────────────────
// 文件推送
// ──────────────────────────────────────────────────────────────────────
// 2026-10-13 架构拆分（审计 2.1）：文件推送域（fileBytesText/fileTotalText/
// clearFilePush/setFileFail/pushFile/sendNextChunk/cancelPush）已搬到独立的
// viewerbackend_file.cpp（同 header 的 ViewerBackend 成员，跨编译单元实现合法）。
// 这里只留 pickFile（Android SAF 选择，依赖 JNI，留在主文件）。
QString ViewerBackend::pickFile()
{
#if defined(Q_OS_ANDROID)
    // ── 2026-10-11 手机「文件传输无法传」根因修复 ──
    // 桌面走 QtWidgets 的 QFileDialog::getOpenFileName 同步弹窗；Android 上 Qt 不
    // 提供原生对话框实现，上面那一行直接返回空串 → pickFile() 空转 → 用户看到
    // 「文件传输点了没反应」。这里改成异步：调系统 SAF（ACTION_GET_CONTENT）弹
    // 「应用内/最近文件/下载…」选择器，用户选完由 Activity 结果回调，把 content://
    // URI 复制到应用缓存后 emit filePicked(file:// URL)——QML 监听该信号即可。
    {
        QJniObject intent("android/content/Intent",
                          "(Ljava/lang/String;)V",
                          QJniObject::fromString(
                              QStringLiteral("android.intent.action.GET_CONTENT"))
                              .object<jstring>());
        intent.callMethod<void>(
            "setType", "(Ljava/lang/String;)V",
            QJniObject::fromString(QStringLiteral("*/*")).object<jstring>());
        QJniObject chooser = intent.callObjectMethod(
            "createChooser",
            "(Landroid/content/Intent;)Landroid/content/Intent;",
            intent.object());
        if (!chooser.isValid()) {
            logf("[viewer] FAIL Android 选文件：createChooser 拿不到 intent");
            return QString();
        }
        QtAndroidPrivate::startActivity(chooser, 9001, [this](int resultCode, jint flags,
                                                              const QJniObject &data) {
            // Activity.RESULT_OK = -1
            if (resultCode != -1 || !data.isValid()) {
                logf("[viewer] Android 选文件：用户取消（resultCode=%d）", resultCode);
                return;
            }
            // data 是回调给的 Intent 对象，从中取 getData() 的 Uri
            const QJniObject uriObj = data.callObjectMethod(
                "getData", "()Landroid/net/Uri;");
            if (!uriObj.isValid()) {
                logf("[viewer] FAIL Android 选文件：getData 拿不到 Uri");
                return;
            }
            const QJniObject uri(uriObj);
            const QString uriStr =
                uri.callMethod<jstring>("toString", "()Ljava/lang/String;").toString();
            // 文件名：优先 _display_name；取不到（query 太脆/权限受限）就退回 URI 末段
            // —— 常见 content://com.android.providers/.../DCIM/IMG_0001.jpg 末段就是真名
            QString name = QStringLiteral("picked.bin");
            {
                const QJniObject ctx(
                    QNativeInterface::QAndroidApplication::context().object());
                const QJniObject resolver = ctx.callObjectMethod(
                    "getContentResolver", "()Landroid/content/ContentResolver;");
                if (resolver.isValid()) {
                    QJniObject cursor = resolver.callObjectMethod(
                        "query",
                        "(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;"
                        "Ljava/lang/String;Ljava/lang/String;)"
                        "Landroid/databases/Cursor;",
                        uri.object(),
                        QJniArray<QString>({QStringLiteral("_display_name")})
                            .object<jobject>(),
                        nullptr, nullptr, nullptr);
                    if (cursor.isValid()) {
                        const bool moved =
                            cursor.callMethod<jboolean>("moveToFirst", "()Z");
                        if (moved) {
                            const jint idx = cursor.callMethod<jint>(
                                "getColumnIndex", "(Ljava/lang/String;)I",
                                QJniObject::fromString(QStringLiteral("_display_name"))
                                    .object<jstring>());
                            if (idx >= 0) {
                                const QString dn =
                                    cursor.callMethod<jstring>(
                                        "getString", "(I)Ljava/lang/String;", idx)
                                        .toString();
                                if (!dn.isEmpty()) name = dn;
                            }
                        }
                        cursor.callMethod<void>("close", "()V");
                    } else {
                        logf("[viewer] Android 选文件：cursor 空（走 URI 末段兜底）");
                    }
                }
            }
            // URI 末段兜底
            if (name.isEmpty() || name == QStringLiteral("picked.bin")) {
                const int slash = uriStr.lastIndexOf(QLatin1Char('/'));
                if (slash >= 0 && slash + 1 < uriStr.size()) {
                    const QString tail = uriStr.mid(slash + 1);
                    if (!tail.isEmpty() && !tail.contains(QLatin1Char('?')))
                        name = tail;
                }
            }
            const QString cacheUrl =
                copyContentUriToCache(uriStr, name);
            if (!cacheUrl.isEmpty()) {
                logf("[viewer] 选文件 → %s（Android SAF）",
                     cacheUrl.toUtf8().constData());
                emit filePicked(cacheUrl);
            } else {
                logf("[viewer] FAIL Android 选文件：复制到缓存失败 uri=%s",
                     uriStr.toUtf8().constData());
            }
        });
        return QString();   // 同步返回空——真正的 URL 通过 filePicked 信号异步补
    }
#else
    const QString path = QFileDialog::getOpenFileName(
                nullptr, QStringLiteral("选要推给这台教室机的文件"), QString(),
                QStringLiteral("所有文件 (*.*)"));
    if (path.isEmpty()) return QString();
    // 统一给 file:// URL，让 pushFile 那边的 QUrl::fromUserInput 有统一入口可解
    const QString url = QUrl::fromLocalFile(path).toString();
    logf("[viewer] 选文件 → %s", url.toUtf8().constData());
    return url;
#endif
}

#if defined(Q_OS_ANDROID)
QString ViewerBackend::copyContentUriToCache(const QString &uriStr,
                                             const QString &suggestedName)
{
    if (uriStr.isEmpty()) return QString();
    QJniEnvironment envGuard;
    JNIEnv *env = QJniEnvironment::getJniEnv();
    if (!env) return QString();

    const QJniObject ctx(QNativeInterface::QAndroidApplication::context().object());
    if (!ctx.isValid()) return QString();
    const QJniObject resolver = ctx.callObjectMethod(
        "getContentResolver", "()Landroid/content/ContentResolver;");
    if (!resolver.isValid()) return QString();

    // Uri.parse(String) —— content:// 字符串转 android.net.Uri
    // 正确写法：callStaticObjectMethod（QJniObject 无 staticObjectMethod）
    const QJniObject uri = QJniObject::callStaticObjectMethod(
        "android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
        QJniObject::fromString(uriStr).object<jstring>());
    if (!uri.isValid()) return QString();

    const QJniObject streamObj = resolver.callObjectMethod(
        "openInputStream", "(Landroid/net/Uri;)Ljava/io/InputStream;", uri.object());
    if (!streamObj.isValid()) return QString();
    const jobject jstreamObj = streamObj.object();
    if (!jstreamObj) return QString();
    // InputStream.read(byte[]) —— 每次读 CHUNK 字节，n=-1 表示 EOF
    const jclass jIn = env->FindClass("java/io/InputStream");
    if (!jIn) return QString();
    const jmethodID mRead = env->GetMethodID(jIn, "read", "([B)I");
    env->DeleteLocalRef(jIn);
    if (!mRead) return QString();

    // 目标：应用缓存目录/pushed/<时间戳>_<安全化文件名>
    const QJniObject filesDir = ctx.callObjectMethod(
        "getFilesDir", "()Ljava/io/File;");
    if (!filesDir.isValid()) {
        env->DeleteLocalRef(jstreamObj);
        return QString();
    }
    const QString base = filesDir.callMethod<jstring>(
                             "getAbsolutePath", "()Ljava/lang/String;")
                             .toString();
    const QString dirPath =
        QDir(base).absoluteFilePath(QStringLiteral("pushed"));
    QDir().mkpath(dirPath);

    const QString safeName =
        suggestedName.isEmpty() ? QStringLiteral("picked.bin") : suggestedName;
    QString clean = safeName;
    for (QChar &c : clean) {
        if (c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char(':')
            || c == QLatin1Char('?') || c == QLatin1Char('*') || c == QLatin1Char('"')
            || c == QLatin1Char('<') || c == QLatin1Char('>') || c == QLatin1Char('|'))
            c = QLatin1Char('_');
    }
    const QString dstPath =
        dirPath + QStringLiteral("/") +
        QStringLiteral("push_%1_").arg(
            QDateTime::currentMSecsSinceEpoch()) + clean;
    QSaveFile out(dstPath);
    if (!out.open(QIODevice::WriteOnly)) {
        logf("[viewer] FAIL 文件推送：打不开缓存目标 %s",
             dstPath.toUtf8().constData());
        env->DeleteLocalRef(jstreamObj);
        return QString();
    }

    // InputStream.read(byte[]) 循环：一次读 64KB，攒够就 fwrite 落盘
    const jsize CHUNK = 65536;
    const jbyteArray buf = env->NewByteArray(CHUNK);
    if (!buf) {
        env->DeleteLocalRef(jstreamObj);
        return QString();
    }
    // 2026-10-12 修（审计 #20）：栈上 jbyte data[CHUNK] 是 64KB——Android 部分设备
    // 线程栈可能只有 1MB（还有别的局部变量），可能栈溢出。改堆分配（QByteArray 预分配 + GetByteArrayRegion 直填）。
    QByteArray data; data.resize(CHUNK);
    qint64 total = 0;
    for (;;) {
        const jint n = env->CallIntMethod(jstreamObj, mRead, buf);
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            break;
        }
        if (n <= 0) break;   // EOF / -1 / 0
        env->GetByteArrayRegion(buf, 0, n, reinterpret_cast<jbyte *>(data.data()));
        if (out.write(data.constData(), n) != n) {
            logf("[viewer] FAIL 文件推送：写缓存失败（%s）",
                 out.errorString().toUtf8().constData());
            env->DeleteLocalRef(buf);
            env->DeleteLocalRef(jstreamObj);
            return QString();
        }
        total += n;
    }
    env->DeleteLocalRef(buf);
    env->DeleteLocalRef(jstreamObj);

    if (!out.commit()) {
        logf("[viewer] FAIL 文件推送：commit 缓存失败 %s",
             out.errorString().toUtf8().constData());
        return QString();
    }
    if (total <= 0) {
        QFile::remove(dstPath);
        logf("[viewer] FAIL 文件推送：content:// 读出 0 字节 uri=%s",
             uriStr.toUtf8().constData());
        return QString();
    }
    logf("[viewer] content:// 复制到缓存：%s（%lld 字节）",
         dstPath.toUtf8().constData(), (long long)total);
    return QUrl::fromLocalFile(dstPath).toString();
}
#endif
// 2026-10-13 架构拆分：pushFile/sendNextChunk/cancelPush 已搬到 viewerbackend_file.cpp
// （文件推送状态机独立编译单元，见 viewerbackend.cpp 文件推送分区注释）。
