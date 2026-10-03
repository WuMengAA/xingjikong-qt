// 星集控被控端 · Qt 版 · D3：接真云端（令牌注册 / 收指令 / 回执）
//
// 纪律（从 D1/D2 延续）：
//   · 成功/失败都如实打印；连不上必须说"连不上"，不许静默重试装在跑。
//   · 环境变量：STE_QT_WS_URL   必填，ws://host:port/ws/agent  云端地址
//              STE_QT_WS_TOKEN  设备令牌，与云端 CLOUD_WS_TOKEN 一致
//              STE_QT_SHOT_DIR  可选，设了就同时本地存图（离线取证用）
//              STE_QT_UID       可选，本机设备码，缺省取 COMPUTERNAME
//              STE_QT_HEARTBEAT_MS 可选，心跳间隔毫秒，缺省 10000（10 秒）
//                 —— 只当云端没下发 heartbeatMs 时才用它；云端在 registered 里给了就听云端的，
//                    不再两端各猜一个数字（协议规范 v1 第四节）。
//              —— 心跳必须与帧解耦：将来上按需拉流后没人看的机器不推帧，
//                 帧就不能当存活依据了，云端靠这条独立信号判在线/离线。
//
// D5 会把收到的指令真正执行（锁屏/关机等）；D3 先把"链路上能收到、且收得到"证明出来。
//
// 2026-10-03 Phase1：改讲**协议 v1**（《星集控-协议规范v1-2026-10-03.md》）：
//   · 文本消息一律走信封 {v:1, type, id, ts, payload}；
//   · 画面帧走二进制帧 [1B 版本][2B 大端 headerLen][header JSON][JPEG]，带 seq/ts/mime；
//   · 云端拒绝时回的是统一 error 通道（code + 人话原因），这里必须打出来，不许静默。

#include <QApplication>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QPainter>
#include <QDesktopServices>
#include <QTimer>
#include <QScreen>
#include <QPixmap>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QBuffer>
#include <QElapsedTimer>
#include <QUrl>
#include <QWebSocket>
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QVector>
#include <QFileInfo>
#include <QStringList>
#include <QStandardPaths>
#include <QSet>
#include <QProcess>
#include <QThread>      // QThread::msleep（camera_record 确认"真的在录"时等两秒）
#include <windows.h>
#include <tlhelp32.h>   // 进程快照（process_list / process_stop）
#include <psapi.h>      // 进程工作集内存
#include <mmsystem.h>   // waveOutSetVolume（set_volume，Rust 版实测零依赖可用）
#include <shellapi.h>   // ShellExecuteW（launch_app）+ SHFileOperationW（media_delete 进回收站）
#include <cstdio>

// 音量与进程内存信息要显式链接（MSVC 下 pragma 最省事，不必动 CMakeLists）
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shell32.lib")  // SHFileOperationW

namespace {

constexpr int kFrameIntervalMs = 2000;   // 2 秒一帧，与 D2 保持一致
constexpr int kHeartbeatIntervalMs = 10000; // 心跳缺省值（云端 registered 里给了就以云端为准）
constexpr int kMaxBackoffMs = 15000;
constexpr int kProtocolVersion = 1;      // 协议 v1（《星集控-协议规范v1-2026-10-03.md》）
constexpr int kProcessListLimit = 50;    // process_list 默认条数（与 Rust DEFAULT_LIMIT 同量级）

QWebSocket *g_ws = nullptr;
int g_backoffMs = 1000;
int g_frameSeq = 0;
qint64 g_frameBytes = 0;      // 累计发出的 JPEG 净荷（不含帧头）——与云端 bytesIn 对账用这个
int g_instructionSeq = 0;
bool g_registered = false;     // 云端回了 registered
QString g_uid;                 // 本机设备码（register 时定下来，心跳复用，避免每次重算）
int g_heartbeatSeq = 0;
int g_heartbeatMs = kHeartbeatIntervalMs;   // 实际心跳间隔：云端下发就听云端的
int g_timeoutMs = 30000;                    // 云端判离线阈值（只用于日志说明，不自己判）
QTimer *g_hbTimer = nullptr;   // 心跳定时器（registered 拿到参数后要能改间隔，所以放外面）
QString g_logPath;             // 被控端日志落盘（装机后没有 stdout，没日志就只能靠猜）
QString g_shotDir;             // 截图/存图目录（screenshot 动作与抓屏都用它）

// 前向声明：调度器回调在文件后面用到它，避免 C++ 的"用到才定义"顺序问题。
bool enableShutdownPriv();

// ══ 批次 2（全量补齐）新增的全局状态 ══
QVector<QTimer *> g_schedTimers;   // 定时关机：每个任务一个单次定时器（触发即执行 ExitWindowsEx）
QHash<QString, QTimer *> g_schedTimerById;   // 任务 id → 定时器（取消时必须停掉，否则到点仍会执行）
QString g_schedFile;               // 调度持久化文件（崩溃恢复后重挂未到期任务）
QString g_schedDir;                // 调度文件所在目录（写文件要 mkpath）
QString g_mediaDir;                // 媒体库目录（media_list/media_session 默认扫这里）
QString g_recvDir;                 // file_push 落盘目录
QProcess *g_recProc = nullptr;     // camera_record 正在录制的 ffmpeg 进程（stop 时 terminate）
QString g_recOutPath;              // 正在录制的输出 mp4 路径
QProcess *g_playProc = nullptr;    // media_session 正在播放的 ffplay 进程
bool g_remoteControl = false;      // remote_control_start/stop 的真实会话状态
int g_currentFps = 5;              // 动态帧率：当前抓屏频率（次/分钟）—— 远控开会时提高
int g_frameDiffMs = -1;            // 上一帧耗时（毫秒），动态帧率控制器用它判断画面是否稳定

// file_push 接收会话状态：一个文件一段会话（file_push 初始化 → 多次 file_chunk 追加 → file_done 收尾）
QString g_fileRecvTarget;          // 目标文件绝对路径（file_push 时定下）
QFile *g_fileRecvFile = nullptr;   // 接收文件句柄（WriteOnly）
int g_fileRecvNext = 0;            // 下一个期望的分片序号（必须按序，乱序即失败）
qint64 g_fileRecvBytes = 0;        // 累计已写入字节（file_done 时与声明值比对）

QString envOr(const char *key, const QString &fallback)
{
    const QString v = qEnvironmentVariable(key);
    return v.isEmpty() ? fallback : v;
}

/* ---------- 协议 v1 封装 ----------
 * 文本：{"v":1,"type":...,"id":...,"ts":...,"payload":{...}}
 * 帧  ：[1B 版本=1][2B 大端 headerLen][header JSON][JPEG]
 * 为什么要这套：以前是"想到一条加一条"的扁平 {type,...}，没版本号、帧没序号，
 * 加任何新机制都要在两端各猜一遍。现在加功能 = 加一个带类型的消息，骨架不动。
 */

QString makeEnvelope(const QString &type, const QJsonObject &payload, const QString &id = QString())
{
    QJsonObject o;
    o.insert(QStringLiteral("v"), kProtocolVersion);
    o.insert(QStringLiteral("type"), type);
    o.insert(QStringLiteral("id"), id);
    o.insert(QStringLiteral("ts"), QJsonValue(QDateTime::currentMSecsSinceEpoch()));
    o.insert(QStringLiteral("payload"), payload);
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QByteArray makeFrameBytes(const QString &uid, int seq, const QByteArray &jpeg)
{
    QJsonObject h;
    h.insert(QStringLiteral("v"), kProtocolVersion);
    h.insert(QStringLiteral("type"), QStringLiteral("frame"));
    h.insert(QStringLiteral("uid"), uid);
    h.insert(QStringLiteral("seq"), seq);
    h.insert(QStringLiteral("ts"), QJsonValue(QDateTime::currentMSecsSinceEpoch()));
    h.insert(QStringLiteral("mime"), QStringLiteral("image/jpeg"));
    h.insert(QStringLiteral("bytes"), jpeg.size());
    const QByteArray header = QJsonDocument(h).toJson(QJsonDocument::Compact);

    QByteArray out;
    out.reserve(3 + header.size() + jpeg.size());
    out.append(char(kProtocolVersion));                 // 1B 版本
    out.append(char((header.size() >> 8) & 0xFF));      // 2B 大端 headerLen
    out.append(char(header.size() & 0xFF));
    out.append(header);
    out.append(jpeg);
    return out;
}

void scheduleReconnect();

void connectNow()
{
    const QString url = qEnvironmentVariable("STE_QT_WS_URL");
    if (url.isEmpty()) {
        fprintf(stderr, "[agent-qt] FAIL: 未配置 STE_QT_WS_URL，无法连云端\n");
        return;
    }
    qInfo("[agent-qt] 连接 %s …", qPrintable(url));
    // Qt 6.8：setUrl()+connectToHost() 已合并为 open(url)
    g_ws->open(QUrl(url));
}

void scheduleReconnect()
{
    QTimer::singleShot(g_backoffMs, connectNow);
    qInfo("[agent-qt] %d ms 后重试（退避上限 %d ms）", g_backoffMs, kMaxBackoffMs);
    g_backoffMs = qMin(g_backoffMs * 2, kMaxBackoffMs);
}

void sendRegister()
{
    g_uid = envOr("STE_QT_UID", QHostInfo::localHostName());
    const QString &uid = g_uid;
    const QString token = qEnvironmentVariable("STE_QT_WS_TOKEN");
    if (token.isEmpty()) {
        fprintf(stderr, "[agent-qt] FAIL: 未配置 STE_QT_WS_TOKEN（被云端拒收）\n");
    }
    // caps：告诉云端这台机器能做什么（现在云端不用，先占位——协议规范 v1 第四节）
    QJsonObject caps;
    caps.insert(QStringLiteral("screen"), true);
    caps.insert(QStringLiteral("input"), true);
    caps.insert(QStringLiteral("power"), true);

    QJsonObject p;
    p.insert(QStringLiteral("uid"), uid);
    p.insert(QStringLiteral("token"), token);
    p.insert(QStringLiteral("version"), QStringLiteral("0.4.0-v1"));
    p.insert(QStringLiteral("caps"), caps);

    const QString hello = makeEnvelope(QStringLiteral("register"), p);
    g_ws->sendTextMessage(hello);
    qInfo("[agent-qt] 已发送 register v1（uid=%s）→ %s", qPrintable(uid), qPrintable(hello));
}

// 心跳：告诉云端"我还活着"。
// 硬约束：必须在 registered 之后才发 —— 云端握手阶段只认 register，
// 那期间发任何别的消息都会被当成非法握手踢掉（AUTH_REQUIRED / close 4003）。
void sendHeartbeat()
{
    if (!g_registered) return;   // 还没通过云端校验，先别发（发了会被踢）
    if (!g_ws || g_ws->state() != QAbstractSocket::ConnectedState) return;

    QJsonObject p;
    p.insert(QStringLiteral("seq"), ++g_heartbeatSeq);
    const QString hb = makeEnvelope(QStringLiteral("heartbeat"), p);
    const qint64 n = g_ws->sendTextMessage(hb);
    if (n <= 0)
        fprintf(stderr, "[agent-qt] FAIL: 心跳发送失败（写了 %lld 字节）—— 云端可能已断\n", n);
    else
        qInfo("[agent-qt] 💓 心跳 #%d", g_heartbeatSeq);
}

// 真执行一个指令。返回 result＝"done" / "failed"（绝不许报 done 其实没做）。
// D5 起：lock/shutdown/reboot 真的做；input 真的注入鼠标键盘。
// 2026-10-03 批次 1：成功/失败统一成 "done"/"failed"，动作的语义化返回值放进 data
// （协议规范第十节：加**可选字段**，老端忽略不报错）—— 这样三端"绿=done"的判据不用动。
struct ExecOut { QString result; QString error; QJsonObject data; };

/* ══════════ 批次 2：全量补齐的公共工具（ffmpeg / 调度持久化 / 路径规整） ══════════ */

// ffmpeg 定位：STL_AGENT_FFMPEG 显式指定 > STE_QT_FFMPEG > PATH 里找 > 报"没装"。
// 找不到时**如实报错**，绝不假装做了（fail-silent 红线）。
static QString ffmpegBin()
{
    const QString env = qEnvironmentVariable("STELARITH_AGENT_FFMPEG")
                         .isEmpty() ? qEnvironmentVariable("STE_QT_FFMPEG")
                                    : qEnvironmentVariable("STELARITH_AGENT_FFMPEG");
    if (!env.isEmpty() && QFileInfo::exists(env)) return env;
    const QString found = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    return found.isEmpty() ? env : found;   // 空串让调用方判失败
}

// ffplay 定位：与 ffmpeg 同目录优先（WinGet 装的俩在一处），再退回 PATH。
static QString ffplayBin()
{
    const QString env = qEnvironmentVariable("STELARITH_AGENT_FFPLAY");
    if (!env.isEmpty() && QFileInfo::exists(env)) return env;
    const QString ff = ffmpegBin();
    if (!ff.isEmpty()) {
        const QString cand = QFileInfo(ff).absolutePath() + QStringLiteral("/ffplay");
        if (QFileInfo::exists(cand)) return cand;
    }
    return QStandardPaths::findExecutable(QStringLiteral("ffplay"));
}

// 校验路径不逃逸出指定根目录。返回 true＝安全；false＝参数里带了越界路径。
// 用途：file_push / media_delete 这类"目标由远程指令指定"的动作，防 `..` 写到系统区。
static bool pathInside(const QString &root, const QString &target)
{
    // 踩过的坑（2026-10-03 实测）：`canonicalPath()` 在 Windows 上返回的是 **Qt 内部路径，
    // 分隔符是 '/'**，而 `absoluteFilePath()` / `absolutePath()` 相关的字符串常被当成原生路径（'\\'）。
    // 原来这里拿 canonicalPath() 的结果去 `startsWith(root + "\\")` —— 两边分隔符对不上，
    // 恒为 false：**媒体库目录里的文件全被判成越界**，`media_delete` 一个都删不掉。
    // 功能看起来"有守卫、很安全"，实际是把合法操作全挡了。
    // 正解：统一 cleanPath + '/' 比较，且根目录必须真存在（被移走/改名就一律拒绝，宁可不错删）。
    const QString rAbs = QDir(QDir::cleanPath(root)).canonicalPath();
    if (rAbs.isEmpty()) return false;
    const QFileInfo t(target);
    const QString tCanon = QDir(QDir::cleanPath(t.absoluteFilePath())).canonicalPath();
    const QString tAbs = (tCanon.isEmpty() ? QDir::cleanPath(t.absoluteFilePath()) : tCanon).toLower();
    return tAbs.startsWith(rAbs.toLower() + QLatin1Char('/'));
}

// 调度任务结构（内存态，与 JSON 一一对应）。at 是绝对毫秒时间戳。
struct SchedTask { QString id; QString what; qint64 at = 0; qint64 createdMs = 0; };

// 持久化：写失败必须报出来（否则崩溃恢复等于没做）。
static void saveSchedules(const QVector<SchedTask> &tasks)
{
    if (g_schedDir.isEmpty()) return;
    QDir().mkpath(g_schedDir);
    QJsonArray arr;
    for (const auto &t : tasks) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), t.id);
        o.insert(QStringLiteral("what"), t.what);
        o.insert(QStringLiteral("at"), QJsonValue(t.at));
        o.insert(QStringLiteral("createdMs"), QJsonValue(t.createdMs));
        arr.append(o);
    }
    QFile f(g_schedFile);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
        f.close();
    } else {
        fprintf(stderr, "[agent-qt] FAIL: 调度任务持久化失败（写 %s 打不开）—— 本机重启后这批定时任务会丢\n",
                qPrintable(g_schedFile));
    }
}

static QVector<SchedTask> loadSchedules()
{
    QVector<SchedTask> out;
    QFile f(g_schedFile);
    if (!f.open(QIODevice::ReadOnly)) return out;   // 没文件＝还没有任何计划，正常
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isArray()) {
        fprintf(stderr, "[agent-qt] FAIL: 调度文件不是合法 JSON 数组：%s（已忽略，不当成功）\n",
                qPrintable(g_schedFile));
        return out;
    }
    for (const auto &v : doc.array()) {
        const QJsonObject o = v.toObject();
        SchedTask t;
        t.id = o.value(QStringLiteral("id")).toString();
        t.what = o.value(QStringLiteral("what")).toString();
        t.at = o.value(QStringLiteral("at")).toVariant().toLongLong();
        t.createdMs = o.value(QStringLiteral("createdMs")).toVariant().toLongLong();
        if (!t.id.isEmpty() && !t.what.isEmpty() && t.at > 0) out.append(t);
    }
    return out;
}

// 到期是否**真的**关机/重启。
//
// 默认**不真执行**（fail-safe，不是 fail-open）：到点只记日志、任务记录保留。
// 只有装机脚本显式把 STE_QT_ALLOW_SCHED_FIRE 设成 1 才真关机 ——
// 本机验证环境 / 开发机上跑定时，绝不能"一测就重启自己"。
// 这条与项目红线一致：破坏性动作只许在 TEST1 那台真触发。
// 代价：忘了设变量的机器上"设了定时但实际没关"。所以**回执里必须把 fireAllowed 报出来**，
// 让管理端一眼看见"这条定时当前不会真执行"，而不是静默假装成功。
static bool schedFireAllowed()
{
    return qEnvironmentVariableIntValue("STE_QT_ALLOW_SCHED_FIRE") == 1;
}

// 到点回调：删掉对应任务记录 → 执行关机/重启。
// 顺序不能反：先删记录再关机，否则下次启动又把它挂回来，变成"永远关不掉"。
static void fireScheduledTask(const QString &taskId, const QString &what)
{
    qInfo("[agent-qt] ⏰ 定时任务到期 id=%s action=%s", qPrintable(taskId), qPrintable(what));

    // 不允许真执行：保留任务记录（下次到期继续提醒，不静默丢弃），如实说清楚为什么没执行。
    if (!schedFireAllowed()) {
        qWarning("[agent-qt] ⏰ 定时任务 id=%s 已到期，但 STE_QT_ALLOW_SCHED_FIRE≠1，"
                 "**未真执行** %s（默认保护）。装机时把该变量设为 1 才会真正关机/重启。",
                 qPrintable(taskId), qPrintable(what));
        return;
    }

    {
        QVector<SchedTask> cur = loadSchedules();
        for (int i = 0; i < cur.size(); ++i)
            if (cur.at(i).id == taskId) { cur.removeAt(i); break; }
        saveSchedules(cur);
    }
    // 单次定时器触发后不会再自己消失，手动摘掉映射（对象由 Qt 事件循环后续回收）
    g_schedTimerById.remove(taskId);
    enableShutdownPriv();
    const DWORD flags = (what == QStringLiteral("reboot") ? EWX_REBOOT : EWX_SHUTDOWN) | EWX_FORCE;
    const BOOL ok = ExitWindowsEx(flags, SHTDN_REASON_FLAG_PLANNED);
    if (!ok)
        fprintf(stderr, "[agent-qt] FAIL: 定时 %s 的 ExitWindowsEx 失败，错误码 %d\n",
                qPrintable(what), (int)GetLastError());
    // 进程即将被系统杀掉；等一小会儿让日志刷出去
    QCoreApplication::processEvents();
    if (ok) QCoreApplication::exit(0);
}

// 把某个任务挂成单次定时器。timeout 传剩余毫秒。
static void armScheduleTimer(const SchedTask &t, int delayMs)
{
    QTimer *tm = new QTimer();
    tm->setParent(qApp);
    tm->setSingleShot(true);
    tm->start(delayMs);
    const QString id = t.id;
    const QString what = t.what;
    QObject::connect(tm, &QTimer::timeout, [id, what] { fireScheduledTask(id, what); });
    g_schedTimers.append(tm);
    g_schedTimerById.insert(id, tm);   // 登记映射：cancel_schedule 要能精确停掉对应定时器
}

// 启动时从 %LOCALAPPDATA%\xingjikong\schedules.json 把未到期任务重新挂上。
// 任务不能只活在内存里 —— 进程崩溃/重启后必须能自己接上，否则"定时关机"就是个摆设。
static void reschedulePersisted()
{
    const QVector<SchedTask> tasks = loadSchedules();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    int dropped = 0;
    QVector<SchedTask> kept;
    for (const auto &t : tasks) {
        if (t.at <= now) { ++dropped; continue; }   // 过期未执行（当时不在线），不再补做——如实丢弃
        kept.append(t);
        int delay = (int)qMin<qint64>(t.at - now, 2147483647LL);   // QTimer 上限 INT_MAX
        armScheduleTimer(t, delay);
    }
    saveSchedules(kept);   // 过期项清掉，文件保持干净
    if (!tasks.isEmpty())
        qInfo("[agent-qt] 调度恢复：载入 %d 条，重挂 %d 条，丢弃过期 %d 条",
              (int)tasks.size(), (int)kept.size(), dropped);
}


/* ══════════ 批次 1：教室刚需动作（口径照抄 Rust process.rs / engine.rs，别重新发明） ══════════ */

// 三张**硬禁止名单**：写死在代码里，不可配置、不可被下发指令绕过（Rust 版原话）。
// 判定口径（照抄 Rust `is_protected`）：
//   集控自身 —— 去掉 .exe 后 **等于或以它开头**
//   系统关键 —— 去掉 .exe 后 **精确等于**
//   安全软件 —— 去掉 .exe 后 **包含**即命中（360/火绒/卡巴的变体太多）
const char *const kProtectedSelf[] = { "classisland", "xingjikong", "stelarith-agent", "stelarith", nullptr };
const char *const kProtectedSystem[] = {
    "system", "idle", "csrss", "wininit", "winlogon", "lsass", "smss", "services", "svchost",
    "explorer", "dwm", "fontdrvhost", "ctfmon", "runtimebroker", "taskhostw", "searchindexer",
    "spoolsv", "audiodg", "conhost", "wudfhost", "sihost", "shellhost", "startmenuexperiencehost",
    "searchhost", "textinputhost", "applicationframehost", "systemsettings", "lockapp", nullptr
};
const char *const kProtectedSecurity[] = {
    "360", "qhsafe", "360safe", "360tray", "360sd", "huorong", "hrsword", "usysdiag", "hipstray",
    "msmpeng", "nissrv", "mssense", "securityhealth", "defender", "smartscreen", "avp", "avguard",
    "kxfed", "kxetray", "kxe", "qqpcrtp", "qqpctray", "baidusd", "bdagent", "ksafe", "kxescore",
    "kws", "avast", "avg", "avira", "norton", "mcafee", "kaspersky", "eset", "ekrn", "bdservice",
    "safemon", "rsmain", "rsnetsvr", "wrar", "trojan", "hips", nullptr
};

/** 受保护就返回人话类别（写进回执 error），可安全关闭返回空串。 */
QString protectedCategory(const QString &exeName)
{
    QString base = exeName.toLower();
    if (base.endsWith(QStringLiteral(".exe"))) base.chop(4);
    for (const char *const *p = kProtectedSelf; *p; ++p)
        if (base == QLatin1String(*p) || base.startsWith(QLatin1String(*p)))
            return QStringLiteral("集控自身的组成程序");
    for (const char *const *p = kProtectedSystem; *p; ++p)
        if (base == QLatin1String(*p)) return QStringLiteral("系统关键进程");
    for (const char *const *p = kProtectedSecurity; *p; ++p)
        if (base.contains(QLatin1String(*p))) return QStringLiteral("安全软件");
    return QString();
}

/** 枚举所有进程 pid → exe 名。只读快照，不打开任何进程句柄。 */
QHash<quint32, QString> snapshotProcessNames()
{
    QHash<quint32, QString> m;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return m;
    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            m.insert((quint32)pe.th32ProcessID, QString::fromWCharArray(pe.szExeFile));
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return m;
}

/** EnumWindows 回调：收集「有非空标题的顶级窗口」→ (hwnd, pid)。与 Rust 同口径。 */
BOOL CALLBACK enumWindowsCb(HWND hwnd, LPARAM lp)
{
    auto *acc = reinterpret_cast<QVector<QPair<HWND, quint32>> *>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != 0 && GetWindowTextLengthW(hwnd) > 0) acc->append({ hwnd, (quint32)pid });
    return TRUE;
}

QString windowTitleOf(HWND hwnd)
{
    const int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return QString();
    QVector<wchar_t> buf(len + 1, 0);
    const int n = GetWindowTextW(hwnd, buf.data(), len + 1);
    return n > 0 ? QString::fromWCharArray(buf.constData(), n) : QString();
}

/** 进程工作集内存（MB）。取不到返回 0 —— 权限不足是常态，不算失败。 */
int memMbOf(quint32 pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) return 0;
    PROCESS_MEMORY_COUNTERS pmc = {};
    pmc.cb = sizeof(pmc);
    const BOOL ok = GetProcessMemoryInfo(h, &pmc, pmc.cb);
    CloseHandle(h);
    return ok ? (int)(pmc.WorkingSetSize / (1024 * 1024)) : 0;
}

/** 追加一行到被控端日志文件（超过 2MB 直接重开，简单轮转，别无限涨）。 */
void appendLogFile(const QString &line)
{
    if (g_logPath.isEmpty()) return;
    QFile f(g_logPath);
    if (f.size() > 2 * 1024 * 1024) QFile::remove(g_logPath);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        f.write(line.toUtf8());
        f.write("\n");
        f.close();
    }
}

/**
 * 读 exe 同目录的 agent.env（格式：`set KEY=VALUE`，`rem`/`#` 为注释）。
 *
 * **为什么要程序自己读**：开机自启走的是「登录触发器计划任务」。2026-10-03 实测发现
 * 计划任务里再套一层 cmd/bat（`cmd /c start-agent.bat`）**会卡住不退**（任务永远 Running、
 * 被控端根本没被拉起）。自启链路越短越可靠 —— 所以计划任务**直接拉 exe**，
 * 配置由 exe 自己从同目录读，不再依赖 bat 帮忙设环境变量。
 *
 * 只设置**进程内**环境变量，不写系统环境；已存在的真实环境变量优先（便于临时覆盖）。
 * @returns 实际生效的键数
 */
int loadEnvFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return 0;
    const QByteArray raw = f.readAll();

    // 编码兼容：写它的工具不确定（NSIS/记事本/PowerShell 各写各的），这里三种都认。
    //   UTF-16LE + BOM（FF FE）—— NSIS 的 FileWrite 默认就是这个
    //   UTF-8 + BOM（EF BB BF）
    //   裸 UTF-8 / ASCII
    QString text;
    if (raw.size() >= 2 && (quint8)raw.at(0) == 0xFF && (quint8)raw.at(1) == 0xFE) {
        text = QString::fromUtf16(reinterpret_cast<const char16_t *>(raw.constData() + 2),
                                  (raw.size() - 2) / 2);
    } else if (raw.size() >= 3 && (quint8)raw.at(0) == 0xEF && (quint8)raw.at(1) == 0xBB
               && (quint8)raw.at(2) == 0xBF) {
        text = QString::fromUtf8(raw.constData() + 3, raw.size() - 3);
    } else {
        text = QString::fromUtf8(raw);
    }

    int n = 0;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString line : lines) {
        line = line.trimmed();   // 顺带去掉行尾 \r
        if (line.isEmpty()) continue;
        if (line.startsWith(QLatin1Char('#'))) continue;
        if (line.startsWith(QStringLiteral("rem"), Qt::CaseInsensitive)) continue;
        if (line.startsWith(QStringLiteral("set "), Qt::CaseInsensitive)) line = line.mid(4).trimmed();
        // 2026-10-04 修：`set "KEY=VALUE"` 是 cmd 的标准写法 —— 引号包住的是整个 KEY=VALUE，
        // 不是只包 VALUE。不先剥掉这层"整体引号"，解析出来就是：
        //   key = "STE_QT_WS_TOKEN（带前导引号）  val = dev-xxx"（带尾引号）
        // → qputenv 设了一个**没人会去读的变量名**，配置等于完全没生效，
        //   表现为「进程在跑、抓屏正常，但 register 的 token 永远是空，云端不回执」。
        // 自启路径（计划任务直接拉 exe，没有 cmd 帮忙 `call`）全靠这里解析，错了就连不上，
        // 而且不报任何错 —— 是静默失败。
        if (line.size() >= 2 && line.startsWith(QLatin1Char('"')) && line.endsWith(QLatin1Char('"')))
            line = line.mid(1, line.size() - 2).trimmed();
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        const QString key = line.left(eq).trimmed();
        QString val = line.mid(eq + 1).trimmed();
        if (val.size() >= 2 && val.startsWith(QLatin1Char('"')) && val.endsWith(QLatin1Char('"')))
            val = val.mid(1, val.size() - 2);
        if (key.isEmpty()) continue;
        if (!qgetenv(key.toUtf8().constData()).isEmpty()) continue;   // 真环境变量优先
        qputenv(key.toUtf8().constData(), val.toUtf8());
        n++;
    }
    return n;
}

/* ══════════ 托盘：常驻程序的可见入口 ══════════
 * 被控端是常驻程序 —— 老师/管理员必须能**看见它还在跑、连没连上**。
 * 没有托盘就等于一个"看不见的进程"：不知道在不在，出问题只能靠猜。
 * 图标程序化画（不引外部图标文件）：已连＝实心，未连＝空心。
 */
QSystemTrayIcon *g_tray = nullptr;
QAction *g_actStatus = nullptr;

QIcon makeTrayIcon(bool online)
{
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    // 浅色填充 + 深色描边：任务栏托盘底色深浅不定，这样两种底色下都看得见
    p.setPen(QPen(QColor(0x10, 0x10, 0x10), 2));
    p.setBrush(online ? QBrush(QColor(0xF0, 0xF0, 0xF0)) : QBrush(Qt::NoBrush));
    p.drawEllipse(QPointF(16, 16), 9, 9);
    p.end();
    return QIcon(pm);
}

void updateTray(bool online, const QString &detail)
{
    if (!g_tray) return;
    g_tray->setIcon(makeTrayIcon(online));
    const QString uid = g_uid.isEmpty() ? QStringLiteral("(未注册)") : g_uid;
    g_tray->setToolTip(QStringLiteral("星集控 · 被控端\n%1\n%2").arg(uid, detail));
    if (g_actStatus) g_actStatus->setText(QStringLiteral("状态：%1").arg(detail));
}

bool enableShutdownPriv()
{
    HANDLE h = NULL;
    TOKEN_PRIVILEGES tkp = {};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &h))
        return false;
    if (!LookupPrivilegeValueA(NULL, "SeShutdownPrivilege", &tkp.Privileges[0].Luid))
        return false;
    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    return AdjustTokenPrivileges(h, FALSE, &tkp, 0, NULL, NULL) && GetLastError() == ERROR_SUCCESS;
}

ExecOut executeAction(const QString &action, const QJsonObject &params)
{
    ExecOut out;

    if (action == QStringLiteral("lock")) {
        // user32 真实导出名就是 LockWorkStation（无 Native 后缀，那个名字不存在）
        if (LockWorkStation())
            out.result = QStringLiteral("done");
        else
            out.result = QStringLiteral("failed"),
            out.error = QStringLiteral("LockWorkStation 返回 FALSE，错误码 %1").arg((int)GetLastError());
        return out;
    }

    if (action == QStringLiteral("shutdown") || action == QStringLiteral("reboot")) {
        enableShutdownPriv();
        const DWORD flags = (action == QStringLiteral("reboot") ? EWX_REBOOT : EWX_SHUTDOWN) | EWX_FORCE;
        // 真关机/重启。这是破坏性指令，只在 TEST1 上点；本机测试不触发。
        if (ExitWindowsEx(flags, SHTDN_REASON_FLAG_PLANNED))
            out.result = QStringLiteral("done");
        else
            out.result = QStringLiteral("failed"),
            out.error = QStringLiteral("ExitWindowsEx 失败，错误码 %1").arg((int)GetLastError());
        return out;
    }

    if (action == QStringLiteral("input")) {
        const QString kind = params.value(QStringLiteral("kind")).toString();
        const int w = GetSystemMetrics(SM_CXSCREEN);
        const int h = GetSystemMetrics(SM_CYSCREEN);
        INPUT in = {};
        in.type = INPUT_MOUSE;
        if (kind == QStringLiteral("move") || kind == QStringLiteral("down") || kind == QStringLiteral("up")) {
            const int x = qRound(params.value(QStringLiteral("x")).toDouble() * (w - 1));
            const int y = qRound(params.value(QStringLiteral("y")).toDouble() * (h - 1));
            in.mi.dx = (LONG)(x * 65535 / (w - 1));
            in.mi.dy = (LONG)(y * 65535 / (h - 1));
            in.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
            if (kind == QStringLiteral("move"))      in.mi.dwFlags |= MOUSEEVENTF_MOVE;
            else if (kind == QStringLiteral("down")) in.mi.dwFlags |= MOUSEEVENTF_LEFTDOWN;
            else                                     in.mi.dwFlags |= MOUSEEVENTF_LEFTUP;
            if (SendInput(1, &in, sizeof(in)) == 1) out.result = QStringLiteral("done");
            else out.result = QStringLiteral("failed"),
                 out.error = QStringLiteral("SendInput 鼠标失败，错误码 %1").arg((int)GetLastError());
            return out;
        }
        if (kind == QStringLiteral("wheel")) {
            in.mi.dwFlags = MOUSEEVENTF_WHEEL;
            in.mi.mouseData = (DWORD)params.value(QStringLiteral("delta")).toInt();
            if (SendInput(1, &in, sizeof(in)) == 1) out.result = QStringLiteral("done");
            else out.result = QStringLiteral("failed"),
                 out.error = QStringLiteral("SendInput 滚轮失败，错误码 %1").arg((int)GetLastError());
            return out;
        }
        if (kind == QStringLiteral("type")) {
            const QString text = params.value(QStringLiteral("text")).toString();
            bool ok = true;
            for (const QChar c : text) {
                const SHORT v = VkKeyScanW((WCHAR)c.unicode());
                if (v == -1) continue;
                const BYTE vk = (BYTE)(v & 0xFF);
                INPUT ki = {};
                ki.type = INPUT_KEYBOARD;
                ki.ki.wVk = vk;
                if (SendInput(1, &ki, sizeof(ki)) != 1) { ok = false; break; }
                ki.ki.dwFlags = KEYEVENTF_KEYUP;
                if (SendInput(1, &ki, sizeof(ki)) != 1) { ok = false; break; }
            }
            out.result = ok ? QStringLiteral("done") : QStringLiteral("failed");
            if (!ok) out.error = QStringLiteral("SendInput 键盘注入失败，错误码 %1").arg((int)GetLastError());
            return out;
        }
        out.result = QStringLiteral("failed");
        out.error = QStringLiteral("未知 input 子类型：%1").arg(kind);
        return out;
    }

    // 安全验证专用（不破坏）：写个标记文件证明"真执行链路通了"
    if (action == QStringLiteral("exec-test")) {
        const QString path = QDir::temp().filePath(QStringLiteral("ste-exec-ok.txt"));
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            f.write(QStringLiteral("done at %1\n").arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs)).toUtf8());
            f.close();
            out.result = QStringLiteral("done");
        } else {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("写标记文件失败：%1").arg(path);
        }
        return out;
    }

    /* ══════════ 批次 1：教室刚需（探针 / 音量 / 截图 / 开程序 / 进程查看与结束 / 日志尾） ══════════ */

    // 最便宜的探针：确认"指令真走到了被控端"，而不是只看云端说"发出去了"
    if (action == QStringLiteral("ping")) {
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("pong"), true);
        out.data.insert(QStringLiteral("at"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
        return out;
    }

    // 主音量 0-100。用 winmm 的 waveOutSetVolume：零依赖、Rust 版实测可用。
    // 越界**夹紧**（不报错），非数字回落 50；回执里回**实际生效值**，不许静默改了不说。
    if (action == QStringLiteral("set_volume")) {
        int v = 50;
        const QJsonValue jv = params.value(QStringLiteral("value"));
        if (jv.isDouble()) v = jv.toInt();
        else if (jv.isString()) { bool okv = false; const int t = jv.toString().trimmed().toInt(&okv); if (okv) v = t; }
        const int want = v;
        v = qBound(0, v, 100);
        const quint32 lvl = ((quint32)v * 0xFFFFu) / 100u;
        const quint32 packed = (lvl << 16) | lvl;
        waveOutSetVolume(nullptr, packed);
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("volume"), v);
        if (want != v) out.data.insert(QStringLiteral("clampedFrom"), want);
        return out;
    }

    // 桌面截屏落盘（区别于画面流：这个用来留证/回看）
    if (action == QStringLiteral("screenshot")) {
        QScreen *screen = QGuiApplication::primaryScreen();
        if (!screen) { out.result = QStringLiteral("failed"); out.error = QStringLiteral("找不到主屏"); return out; }
        const QPixmap pm = screen->grabWindow(0);
        if (pm.isNull()) { out.result = QStringLiteral("failed"); out.error = QStringLiteral("grabWindow 返回空图"); return out; }
        QDir().mkpath(g_shotDir);
        const QString path = g_shotDir + QStringLiteral("/shot-%1.png").arg(QDateTime::currentMSecsSinceEpoch());
        if (!pm.save(path, "PNG")) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("保存失败：%1").arg(path);
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("path"), QDir::toNativeSeparators(path));
        out.data.insert(QStringLiteral("bytes"), (int)QFileInfo(path).size());
        out.data.insert(QStringLiteral("width"), pm.width());
        out.data.insert(QStringLiteral("height"), pm.height());
        return out;
    }

    // 远程打开软件。白名单约束（照抄 Rust）：默认只允许教育/办公类，
    // 避免"任意程序可被远程拉起"变成攻击面。
    if (action == QStringLiteral("launch_app") || action == QStringLiteral("open_app")) {
        const QString target = params.value(QStringLiteral("target")).toString().trimmed();
        if (target.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("参数缺失：需要 target（进程名或可执行路径）");
            return out;
        }
        const QString allow = qEnvironmentVariable("STELARITH_AGENT_LAUNCH_ALLOWLIST").toLower();
        bool allowed = false;
        const QStringList items = allow.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &item : items)
            if (!item.trimmed().isEmpty() && target.toLower().contains(item.trimmed())) { allowed = true; break; }
        static const char *const kBuiltinSafe[] = {
            "classisland", "xingjikong", "explorer.exe", "msedge", "chrome", "firefox",
            "wechat", "wps", "word", "excel", "powerpnt", "notepad", "cmd.exe", nullptr
        };
        const QString tl = target.toLower();
        if (!allowed)
            for (const char *const *p = kBuiltinSafe; *p; ++p)
                if (tl.contains(QLatin1String(*p))) { allowed = true; break; }
        if (!allowed) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("launch_app: 「%1」不在内置安全名单（classisland/xingjikong/浏览器/Office/记事本…），"
                                       "被拒绝。可配置 STELARITH_AGENT_LAUNCH_ALLOWLIST 放行。").arg(target);
            return out;
        }
        const HINSTANCE r = ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(target.utf16()),
                                          nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(r) <= 32) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("ShellExecuteW 失败，错误码 %1").arg((int)reinterpret_cast<INT_PTR>(r));
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("target"), target);
        return out;
    }

    // 软件运行查看：只列「有主窗口标题」的进程 —— 老师要看的是"教室机上开着什么"，
    // 不是几百个 svchost。只读快照 + 最小权限查询，不提权、不注入。
    if (action == QStringLiteral("process_list")) {
        const int lim = params.value(QStringLiteral("limit")).toInt(kProcessListLimit);
        const int cap = lim > 0 ? qMin(lim, 500) : kProcessListLimit;
        const QHash<quint32, QString> names = snapshotProcessNames();
        if (names.isEmpty()) { out.result = QStringLiteral("failed"); out.error = QStringLiteral("无法获取进程快照"); return out; }
        QVector<QPair<HWND, quint32>> wins;
        EnumWindows(enumWindowsCb, reinterpret_cast<LPARAM>(&wins));
        QJsonArray items;
        bool truncated = false;
        for (const auto &w : wins) {
            const auto it = names.constFind(w.second);
            if (it == names.constEnd() || it->isEmpty()) continue;
            if ((int)items.size() >= cap) { truncated = true; break; }
            QJsonObject o;
            o.insert(QStringLiteral("pid"), (int)w.second);
            o.insert(QStringLiteral("name"), *it);
            o.insert(QStringLiteral("title"), windowTitleOf(w.first));
            o.insert(QStringLiteral("mem_mb"), memMbOf(w.second));
            const QString prot = protectedCategory(*it);
            if (!prot.isEmpty()) o.insert(QStringLiteral("protected"), prot);
            items.append(o);
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("items"), items);
        out.data.insert(QStringLiteral("count"), (int)items.size());
        out.data.insert(QStringLiteral("truncated"), truncated);
        return out;
    }

    // 软件控制：硬禁止名单第一关（命中**连 WM_CLOSE 都不发**）→ 温和关闭（发 WM_CLOSE，软件能存盘）
    // → 最多等 5 秒 → 仍未退出才 TerminateProcess。
    if (action == QStringLiteral("process_stop")) {
        const QJsonValue jpid = params.value(QStringLiteral("pid"));
        const int pid = jpid.isDouble() ? jpid.toInt()
                                        : jpid.toString().trimmed().toInt();
        if (pid <= 0) { out.result = QStringLiteral("failed"); out.error = QStringLiteral("参数缺失：需要 pid"); return out; }
        const QHash<quint32, QString> names = snapshotProcessNames();
        const QString exe = names.value((quint32)pid);
        if (exe.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("找不到 pid=%1 对应的进程（可能已退出）").arg(pid);
            return out;
        }
        const QString prot = protectedCategory(exe);
        if (!prot.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("拒绝结束 %1（pid %2）：命中硬禁止名单「%3」").arg(exe).arg(pid).arg(prot);
            return out;
        }
        HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
        if (!h) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("OpenProcess 失败（错误码 %1），可能权限不足或进程受保护").arg((int)GetLastError());
            return out;
        }
        int closed = 0;
        QVector<QPair<HWND, quint32>> wins;
        EnumWindows(enumWindowsCb, reinterpret_cast<LPARAM>(&wins));
        for (const auto &w : wins)
            if (w.second == (quint32)pid) { PostMessageW(w.first, WM_CLOSE, 0, 0); closed++; }
        if (closed > 0 && WaitForSingleObject(h, 5000) == WAIT_OBJECT_0) {
            CloseHandle(h);
            out.result = QStringLiteral("done");
            out.data.insert(QStringLiteral("pid"), pid);
            out.data.insert(QStringLiteral("name"), exe);
            out.data.insert(QStringLiteral("how"), QStringLiteral("wm_close"));
            out.data.insert(QStringLiteral("detail"), QStringLiteral("已温和关闭（WM_CLOSE，主窗口 %1 个）").arg(closed));
            return out;
        }
        const BOOL killed = TerminateProcess(h, 1);
        const DWORD lastErr = GetLastError();
        CloseHandle(h);
        if (killed) {
            out.result = QStringLiteral("done");
            out.data.insert(QStringLiteral("pid"), pid);
            out.data.insert(QStringLiteral("name"), exe);
            out.data.insert(QStringLiteral("how"), QStringLiteral("terminate"));
            out.data.insert(QStringLiteral("detail"),
                            closed > 0 ? QStringLiteral("温和关闭 5 秒未生效，已强制结束")
                                       : QStringLiteral("该进程没有主窗口，直接强制结束"));
            return out;
        }
        out.result = QStringLiteral("failed");
        out.error = QStringLiteral("TerminateProcess 失败（错误码 %1）").arg((int)lastErr);
        return out;
    }

    // 本机日志尾部：装机后没有 stdout，出故障只能靠它
    if (action == QStringLiteral("log_tail")) {
        if (g_logPath.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("本端未启用日志文件");
            return out;
        }
        QFile f(g_logPath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("读取日志失败：%1").arg(g_logPath);
            return out;
        }
        int want = params.value(QStringLiteral("lines")).toInt(200);
        if (want <= 0) want = 200;
        want = qMin(want, 2000);
        const QStringList all = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        f.close();
        const int from = qMax(0, (int)all.size() - want);
        QJsonArray lines;
        for (int i = from; i < (int)all.size(); ++i) lines.append(all.at(i));
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("path"), QDir::toNativeSeparators(g_logPath));
        out.data.insert(QStringLiteral("lines"), lines);
        out.data.insert(QStringLiteral("count"), (int)lines.size());
        return out;
    }

    // 快捷方式候选：管理端要「从这台机器上挑」，所以这里返回**可选来源**，而不是预置名单。
    // 两类：① 正在运行且有主窗口的程序（同一 exe 只出一条）② 桌面上的文件与文件夹。
    // 纪律：**不把集控自己列进去**（否则会变成"自己开自己"）。
    if (action == QStringLiteral("list_shortcut_candidates")) {
        QJsonArray apps;
        {
            const QHash<quint32, QString> names = snapshotProcessNames();
            QVector<QPair<HWND, quint32>> wins;
            EnumWindows(enumWindowsCb, reinterpret_cast<LPARAM>(&wins));
            QSet<QString> seen;
            for (const auto &w : wins) {
                const auto it = names.constFind(w.second);
                if (it == names.constEnd() || it->isEmpty()) continue;
                const QString exe = *it;
                const QString low = exe.toLower();
                if (low.startsWith(QStringLiteral("stelarith")) || low.startsWith(QStringLiteral("xingjikong")))
                    continue;
                if (seen.contains(low)) continue;
                seen.insert(low);
                QJsonObject o;
                o.insert(QStringLiteral("kind"), QStringLiteral("app"));
                o.insert(QStringLiteral("target"), exe);
                o.insert(QStringLiteral("title"), windowTitleOf(w.first));
                o.insert(QStringLiteral("pid"), (int)w.second);
                apps.append(o);
            }
        }

        QJsonArray files;
        const QString deskPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        {
            QDir d(deskPath);
            const QFileInfoList list =
                d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            int n = 0;
            for (const QFileInfo &fi : list) {
                if (n++ >= 60) break;   // 桌面上东西多时截断，别把回执撑爆
                QJsonObject o;
                o.insert(QStringLiteral("kind"), fi.isDir() ? QStringLiteral("dir") : QStringLiteral("file"));
                o.insert(QStringLiteral("target"), QDir::toNativeSeparators(fi.absoluteFilePath()));
                o.insert(QStringLiteral("title"), fi.fileName());
                files.append(o);
            }
        }

        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("apps"), apps);
        out.data.insert(QStringLiteral("desktop"), files);
        out.data.insert(QStringLiteral("desktopPath"), QDir::toNativeSeparators(deskPath));
        return out;
    }

    /* ══════════ 批次 2：全量补齐（调度器 / 相机 / 媒体 / 远控 / 文件推送） ══════════ */

    // ── 定时关机三件套 ──
    // 参数：at（ISO 8601 绝对时间）或 delay_sec（秒），二选一；what：shutdown|reboot（默认 shutdown）
    // 落盘到 schedules.json，进程重启会自动重挂——所以"定时"不是靠进程活着撑着的假定时。
    if (action == QStringLiteral("schedule_shutdown") || action == QStringLiteral("schedule_reboot")) {
        const QString what = (action == QStringLiteral("schedule_reboot"))
                                 ? QStringLiteral("reboot") : QStringLiteral("shutdown");
        qint64 at = 0;
        const QString atStr = params.value(QStringLiteral("at")).toString().trimmed();
        const QJsonValue dsec = params.value(QStringLiteral("delay_sec"));
        if (!atStr.isEmpty()) {
            // 兼容带 Z 与不带 Z：QT 的 fromString 对 T 分隔 + Z 都能吃
            QDateTime dt = QDateTime::fromString(atStr, Qt::ISODateWithMs);
            if (dt.date() == QDate()) dt = QDateTime::fromString(atStr, Qt::ISODate);
            if (!dt.isValid()) {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("schedule: at 不是合法时间（要 ISO 8601，如 2026-10-03T22:00:00）");
                return out;
            }
            at = dt.toMSecsSinceEpoch();
        } else if (dsec.isDouble()) {
            // 数字型：直接取。此前这里把 `okv` 留在 false（只在字符串分支赋过值）
            // → 条件 `!okv` 恒真 → **传数字的 delay_sec 一律被拒**，只有字符串型才能安排成功。
            // 数字是下发侧最自然的写法（JSON number），这个洞等于"定时开关机"用不了。
            const int sec = dsec.toInt();
            if (sec <= 0) {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("schedule: delay_sec 必须 > 0 的整数秒");
                return out;
            }
            at = QDateTime::currentMSecsSinceEpoch() + (qint64)sec * 1000;
        } else if (dsec.isString() && !dsec.toString().trimmed().isEmpty()) {
            bool okv = false;
            const int sec = dsec.toString().trimmed().toInt(&okv);
            if (!okv || sec <= 0) {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("schedule: delay_sec 必须 > 0 的整数秒");
                return out;
            }
            at = QDateTime::currentMSecsSinceEpoch() + (qint64)sec * 1000;
        } else {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("schedule: 需要 at（ISO 时间）或 delay_sec（秒）其一");
            return out;
        }
        if (at <= QDateTime::currentMSecsSinceEpoch()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("schedule: 时间已在过去，拒绝安排");
            return out;
        }

        // 同一台机只允许一条待执行任务：教室机上是"排一个就排一个"，多个排队语义复杂且易误伤
        QVector<SchedTask> cur = loadSchedules();
        QString replacedId;
        for (int i = 0; i < cur.size(); ++i) {
            const QString oldId = cur.at(i).id;
            // 同步停掉旧定时器，否则会出现"文件里没记录但定时器仍在跑"——到点照样关机
            const auto ti = g_schedTimerById.constFind(oldId);
            if (ti != g_schedTimerById.constEnd() && ti.value()) {
                ti.value()->stop();
                delete ti.value();
                g_schedTimerById.erase(ti);
            }
            replacedId = oldId;
            cur.removeAt(i);
            break;
        }

        SchedTask nt;
        nt.id = QStringLiteral("sch-") + QString::number(QDateTime::currentMSecsSinceEpoch());
        nt.what = what;
        nt.at = at;
        nt.createdMs = QDateTime::currentMSecsSinceEpoch();
        cur.append(nt);
        saveSchedules(cur);
        armScheduleTimer(nt, (int)qMin<qint64>(at - QDateTime::currentMSecsSinceEpoch(), 2147483647LL));

        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("id"), nt.id);
        out.data.insert(QStringLiteral("what"), what);
        out.data.insert(QStringLiteral("at"), QDateTime::fromMSecsSinceEpoch(at).toString(Qt::ISODateWithMs));
        out.data.insert(QStringLiteral("inMs"), at - QDateTime::currentMSecsSinceEpoch());
        out.data.insert(QStringLiteral("persisted"), true);
        out.data.insert(QStringLiteral("fireAllowed"), schedFireAllowed());
        // 明说这条定时**现在就不会真关机器**（未装机开启时）——“报了安排成功”和“真的会关”是两件事
        if (!schedFireAllowed())
            out.data.insert(QStringLiteral("note"),
                            QStringLiteral("到期不会真执行（STE_QT_ALLOW_SCHED_FIRE≠1），装机时需在 agent.env 设为 1"));
        if (!replacedId.isEmpty()) out.data.insert(QStringLiteral("replaced"), replacedId);
        qInfo("[agent-qt] ⏲ 已安排 %s @ %s（%lld 秒后）", qPrintable(what),
              qPrintable(out.data.value(QStringLiteral("at")).toString()),
              (at - QDateTime::currentMSecsSinceEpoch()) / 1000);
        return out;
    }

    // 列出所有待执行定时任务（管理端"看计划"用）。已到期未执行的不会出现在这里。
    if (action == QStringLiteral("list_schedules")) {
        const QVector<SchedTask> tasks = loadSchedules();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QJsonArray items;
        int pending = 0;
        for (const auto &t : tasks) {
            if (t.at <= now) continue;
            QJsonObject o;
            o.insert(QStringLiteral("id"), t.id);
            o.insert(QStringLiteral("what"), t.what);
            o.insert(QStringLiteral("at"), QDateTime::fromMSecsSinceEpoch(t.at).toString(Qt::ISODateWithMs));
            o.insert(QStringLiteral("inMs"), t.at - now);
            o.insert(QStringLiteral("inSec"), (t.at - now) / 1000);
            o.insert(QStringLiteral("createdAt"),
                     QDateTime::fromMSecsSinceEpoch(t.createdMs).toString(Qt::ISODateWithMs));
            items.append(o);
            ++pending;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("items"), items);
        out.data.insert(QStringLiteral("count"), pending);
        out.data.insert(QStringLiteral("file"), QDir::toNativeSeparators(g_schedFile));
        return out;
    }

    // 按 id 取消。找不到就如实报 failed（不算"静默成功"）。
    if (action == QStringLiteral("cancel_schedule")) {
        const QString id = params.value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("cancel_schedule: 需要 id");
            return out;
        }
        QVector<SchedTask> cur = loadSchedules();
        int hit = -1;
        for (int i = 0; i < cur.size(); ++i)
            if (cur.at(i).id == id) { hit = i; break; }
        if (hit < 0) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("找不到 id=%1 的定时任务（可能已执行或已取消）").arg(id);
            return out;
        }
        // 先停定时器再删记录：顺序反了会出现"记录没了但定时器还在跑"，到点照样关机
        const auto ti = g_schedTimerById.constFind(id);
        if (ti != g_schedTimerById.constEnd() && ti.value()) {
            ti.value()->stop();
            delete ti.value();
            g_schedTimerById.erase(ti);
        }
        cur.removeAt(hit);
        saveSchedules(cur);
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("id"), id);
        out.data.insert(QStringLiteral("cancelled"), true);
        out.data.insert(QStringLiteral("remaining"), (int)cur.size());
        return out;
    }

    // ── 相机四件套（走 ffmpeg DirectShow 输入）──
    // 用 ffmpeg 而不是 Qt Multimedia：本机 Qt 6.8.1 装的是最小集，没有 Qt6Multimedia。
    // 找不到 ffmpeg / 没有相机都如实报错，绝不回 done。

    if (action == QStringLiteral("camera_list")) {
        const QString ff = ffmpegBin();
        if (ff.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("找不到 ffmpeg（可设 STE_QT_FFMPEG 指向 ffmpeg.exe）");
            return out;
        }
        // -list_devices 输出是文本；直接读 stdout 抓 `@` 开头的设备名
        QProcess p;
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start(ff, { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                      QStringLiteral("error"), QStringLiteral("-list_devices"),
                      QStringLiteral("-f"), QStringLiteral("dshow"), QStringLiteral("-i"),
                      QStringLiteral("dummy") });
        if (!p.waitForFinished(4000)) {
            p.kill();
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("ffmpeg 枚举设备超时（DirectShow 可能不可用）");
            return out;
        }
        const QString txt = QString::fromUtf8(p.readAll());
        QJsonArray cams, mics;
        for (const QString &line : txt.split(QLatin1Char('\n'))) {
            const int at = line.indexOf(QLatin1Char('@'));
            if (at < 0) continue;
            const QString name = line.mid(at + 1).trimmed();
            if (name.isEmpty()) continue;
            if (name.startsWith(QStringLiteral("videocapture"), Qt::CaseInsensitive))
                cams.append(name);
            else if (name.startsWith(QStringLiteral("audio"), Qt::CaseInsensitive))
                mics.append(name);
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("cameras"), cams);
        out.data.insert(QStringLiteral("microphones"), mics);
        out.data.insert(QStringLiteral("count"), cams.size());
        return out;
    }

    // 拍一张：ffmpeg 从指定设备（默认 video0）抓一帧写 PNG。回执给路径 + 字节数 + base64 缩略图。
    if (action == QStringLiteral("camera_snapshot")) {
        const QString ff = ffmpegBin();
        if (ff.isEmpty()) { out.result = QStringLiteral("failed"); out.error = QStringLiteral("找不到 ffmpeg"); return out; }
        const QString dev = params.value(QStringLiteral("device")).toString(QStringLiteral("video0")).trimmed();
        if (dev.isEmpty()) { out.result = QStringLiteral("failed"); out.error = QStringLiteral("camera_snapshot: device 不能为空"); return out; }
        QDir().mkpath(g_shotDir);
        const QString path = g_shotDir + QStringLiteral("/cam-%1.png").arg(QDateTime::currentMSecsSinceEpoch());
        QProcess p;
        // 通道必须分着收：之前用 MergedChannels，ffmpeg 把"设备不存在"写进 stderr，
        // 结果 readAllStandardError() 永远是空的 → 回执里 reason 是**空字符串**，
        // 排障的人只看到"失败"，看不到为什么失败（失败必须给原因，这条红线上栽过）。
        p.setProcessChannelMode(QProcess::SeparateChannels);
        // -frames:v 1 = 只要一帧；-y 覆盖；抽帧失败（设备不存在/占用）ffmpeg 会非零退出
        p.start(ff, { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                      QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"),
                      QStringLiteral("dshow"), QStringLiteral("-i"), QStringLiteral("video_device=%1").arg(dev),
                      QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-q:v"),
                      QStringLiteral("2"), path });
        if (!p.waitForFinished(8000)) {
            p.kill();
            p.waitForFinished(1500);
            QFile::remove(path);
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("camera_snapshot: ffmpeg 超时（设备 %1）").arg(dev);
            return out;
        }
        if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
            QFile::remove(path);
            const QString err = QString::fromUtf8(p.readAllStandardError()).trimmed().left(300);
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("camera_snapshot: ffmpeg 失败（退出码 %1，设备 %2）%3")
                            .arg(p.exitCode()).arg(dev)
                            .arg(err.isEmpty() ? QStringLiteral("（ffmpeg 没给 stderr，看 ffmpeg 日志）")
                                               : QStringLiteral("：%1").arg(err));
            return out;
        }
        if (!QFileInfo::exists(path) || QFileInfo(path).size() == 0) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("camera_snapshot: ffmpeg 退出 0 但没写出图片（设备 %1 可能不可用）").arg(dev);
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("path"), QDir::toNativeSeparators(path));
        out.data.insert(QStringLiteral("bytes"), (int)QFileInfo(path).size());
        out.data.insert(QStringLiteral("device"), dev);
        return out;
    }

    // 开始录像：后台起 ffmpeg 进程写 mp4。已经在录就如实报 failed（不静默换文件）。
    if (action == QStringLiteral("camera_record_start")) {
        const QString ff = ffmpegBin();
        if (ff.isEmpty()) { out.result = QStringLiteral("failed"); out.error = QStringLiteral("找不到 ffmpeg"); return out; }
        if (g_recProc && g_recProc->state() != QProcess::NotRunning) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("camera_record: 已有一条录像在进行中（%1），请先 record_stop").arg(
                QDir::toNativeSeparators(g_recOutPath));
            return out;
        }
        const QString dev = params.value(QStringLiteral("device")).toString(QStringLiteral("video0")).trimmed();
        const int maxSec = qBound(1, params.value(QStringLiteral("duration_sec")).toInt(300), 3600);
        QDir().mkpath(g_shotDir);
        g_recOutPath = g_shotDir + QStringLiteral("/rec-%1.mp4").arg(QDateTime::currentMSecsSinceEpoch());
        g_recProc = new QProcess();
        g_recProc->setProcessChannelMode(QProcess::MergedChannels);
        g_recProc->start(ff, { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                               QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"),
                               QStringLiteral("dshow"), QStringLiteral("-i"),
                               QStringLiteral("video_device=%1").arg(dev),
                               QStringLiteral("-t"), QString::number(maxSec),
                               QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"),
                               QStringLiteral("ultrafast"), QStringLiteral("-crf"), QStringLiteral("28"),
                               QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                               QStringLiteral("-r"), QStringLiteral("15"), g_recOutPath });
        if (!g_recProc->waitForStarted(4000)) {
            const int code = g_recProc->exitCode();
            delete g_recProc; g_recProc = nullptr;
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("camera_record: ffmpeg 启动失败（退出码 %1，设备 %2）").arg(code).arg(dev);
            return out;
        }
        // 光"进程起来了"不算数：ffmpeg 遇到不存在的 dshow 设备会**自己立刻退出**，
        // 而 waitForStarted() 这种情况照样返回 true → 回执 done、一个字节都没录到（假成功）。
        // 所以真等两秒，看输出文件有没有真的长出来；没有就 kill + 删文件 + 如实报失败。
        QThread::msleep(2000);
        const bool reallyRecording = QFileInfo::exists(g_recOutPath) && QFileInfo(g_recOutPath).size() > 0;
        if (!reallyRecording) {
            g_recProc->kill();
            g_recProc->waitForFinished(3000);
            QFile::remove(g_recOutPath);
            delete g_recProc; g_recProc = nullptr;
            g_recOutPath.clear();
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("camera_record: 2 秒内没有写入任何数据（设备 %1 不存在/被占用？ffmpeg 已退出）—— 不是「正在录」").arg(dev);
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("recording"), true);   // 明示：这条回执是"确认真的在录"，不是"进程开了"
        out.data.insert(QStringLiteral("path"), QDir::toNativeSeparators(g_recOutPath));
        out.data.insert(QStringLiteral("device"), dev);
        out.data.insert(QStringLiteral("maxSec"), maxSec);
        return out;
    }

    // 停止录像。回执给实际字节数——0 字节就是失败，不许报 done。
    if (action == QStringLiteral("camera_record_stop")) {
        if (!g_recProc || g_recProc->state() == QProcess::NotRunning) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("camera_record_stop: 当前没有录像在进行");
            return out;
        }
        g_recProc->terminate();
        if (!g_recProc->waitForFinished(5000)) { g_recProc->kill(); g_recProc->waitForFinished(2000); }
        const int code = g_recProc->exitCode();
        const qint64 bytes = QFileInfo(g_recOutPath).exists() ? QFileInfo(g_recOutPath).size() : 0;
        delete g_recProc; g_recProc = nullptr;
        if (bytes == 0) {
            QFile::remove(g_recOutPath);
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("camera_record_stop: 没有产生有效文件（退出码 %1）").arg(code);
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("path"), QDir::toNativeSeparators(g_recOutPath));
        out.data.insert(QStringLiteral("bytes"), (qint64)bytes);
        return out;
    }

    // ── 媒体四件套（扫媒体库目录；删除走回收站）──
    if (action == QStringLiteral("media_list")) {
        const QString root = params.value(QStringLiteral("dir")).toString().trimmed();
        const QString use = root.isEmpty() ? g_mediaDir : root;
        QDir d(use);
        if (!d.exists()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("media_list: 目录不存在 %1（可设 STE_QT_MEDIA_DIR 或传 dir）")
                            .arg(QDir::toNativeSeparators(use));
            return out;
        }
        static const char *const kMediaExt[] = {
            "mp4", "mkv", "mov", "avi", "wmv", "flv", "webm", "ts",
            "mp3", "wav", "m4a", "aac", "flac", "ogg", "wma", "ape", "opus",
            "pdf", "ppt", "pptx", "doc", "docx", "xls", "xlsx", "txt", nullptr
        };
        const int lim = qMin(params.value(QStringLiteral("limit")).toInt(200), 1000);
        QJsonArray items;
        bool truncated = false;
        const QFileInfoList files = d.entryInfoList(QDir::Files, QDir::Name);
        for (const QFileInfo &fi : files) {
            QString ext = fi.suffix().toLower();
            bool hit = false;
            for (const char *const *p = kMediaExt; *p; ++p)
                if (ext == QLatin1String(*p)) { hit = true; break; }
            if (!hit) continue;
            if ((int)items.size() >= lim) { truncated = true; break; }
            QJsonObject o;
            o.insert(QStringLiteral("name"), fi.fileName());
            o.insert(QStringLiteral("path"), QDir::toNativeSeparators(fi.absoluteFilePath()));
            o.insert(QStringLiteral("bytes"), (qint64)fi.size());
            o.insert(QStringLiteral("sizeMB"), fi.size() / 1048576.0);
            o.insert(QStringLiteral("ext"), ext);
            items.append(o);
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("dir"), QDir::toNativeSeparators(use));
        out.data.insert(QStringLiteral("items"), items);
        out.data.insert(QStringLiteral("count"), (int)items.size());
        out.data.insert(QStringLiteral("truncated"), truncated);
        return out;
    }

    // 删除：走 SHFileOperation 的 FO_DELETE + FO_ALLOWUNDO（进回收站，可恢复）。
    // 目标路径必须在媒体库目录内——防 `..` 越界删到系统区。
    if (action == QStringLiteral("media_delete")) {
        const QString target = params.value(QStringLiteral("path")).toString().trimmed();
        if (target.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("media_delete: 需要 path");
            return out;
        }
        if (!pathInside(g_mediaDir, target)) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("media_delete: 「%1」不在媒体库目录内（%2），拒绝删除")
                            .arg(QDir::toNativeSeparators(target))
                            .arg(QDir::toNativeSeparators(g_mediaDir));
            return out;
        }
        QFileInfo fi(target);
        if (!fi.exists()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("media_delete: 文件不存在 %1").arg(QDir::toNativeSeparators(target));
            return out;
        }
        SHFILEOPSTRUCTW op = {};
        const QString native = QDir::toNativeSeparators(fi.absoluteFilePath());
        // `pFrom` 必须是**双重** null 结尾（路径一个 \0，结构体再一个 \0）。
        // 之前直接把 `native.utf16()` 的指针塞进去：QString 只保证末尾一个 \0，
        // SHFileOperation 于是把第一个 \0 之后的内容当成"下一个要删的文件"接着读 ——
        // 结果是**永远返回错误码 2**，报"可能被占用或权限不足"，
        // 而媒体库里的文件明明一个都没删掉。注释写了规则、代码没照做，才躲不掉这个洞。
        QVector<ushort> pathBuf(native.size() + 2, 0);
        for (int i = 0; i < native.size(); ++i)
            pathBuf[i] = static_cast<ushort>(native.at(i).unicode());
        op.wFunc = FO_DELETE;
        op.pFrom = reinterpret_cast<LPCWSTR>(pathBuf.constData());
        op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
        const int rc = SHFileOperationW(&op);
        if (rc != 0 || op.fAnyOperationsAborted) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("media_delete: 删除失败（错误码 %1）—— 可能被占用或权限不足")
                            .arg(rc);
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("path"), native);
        out.data.insert(QStringLiteral("recycleBin"), true);
        return out;
    }

    // 播放：用 ffplay 后台打开指定媒体（或媒体库里的默认第一个）。
    if (action == QStringLiteral("media_session_start")) {
        const QString fp = ffplayBin();
        if (fp.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("media_session_start: 找不到 ffplay（可设 STE_QT_FFPLAY）");
            return out;
        }
        QString target = params.value(QStringLiteral("path")).toString().trimmed();
        if (target.isEmpty()) {
            QDir d(g_mediaDir);
            const QFileInfoList ls = d.entryInfoList(
                { QStringLiteral("*.mp4"), QStringLiteral("*.mp3") }, QDir::Files, QDir::Name);
            if (!ls.isEmpty()) target = ls.first().absoluteFilePath();
        }
        if (target.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("media_session_start: 没有指定 path，且媒体库里也没有可播放文件");
            return out;
        }
        if (!QFileInfo::exists(target)) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("media_session_start: 文件不存在 %1").arg(QDir::toNativeSeparators(target));
            return out;
        }
        if (g_playProc && g_playProc->state() != QProcess::NotRunning) {
            g_playProc->terminate();
            if (!g_playProc->waitForFinished(3000)) g_playProc->kill();
            delete g_playProc;
            g_playProc = nullptr;
        }
        g_playProc = new QProcess();
        g_playProc->setProcessChannelMode(QProcess::MergedChannels);
        g_playProc->start(fp, { QStringLiteral("-nodisp"), QStringLiteral("-autoexit"), target });
        if (!g_playProc->waitForStarted(3000)) {
            const int code = g_playProc->exitCode();
            delete g_playProc; g_playProc = nullptr;
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("media_session_start: ffplay 启动失败（退出码 %1）").arg(code);
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("path"), QDir::toNativeSeparators(target));
        out.data.insert(QStringLiteral("pid"), (int)g_playProc->processId());
        return out;
    }

    if (action == QStringLiteral("media_session_stop")) {
        if (!g_playProc) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("media_session_stop: 当前没有在播放");
            return out;
        }
        // 「停止」的结果就是**播放停了** —— 不管是被我们停的，还是播放器自己播完退出的
        // （本机没可用输出设备时 ffplay 会秒退）。原来一律回 failed「当前没有在播放」，
        // 老师点了停止却看到"没在播放"，等于把"已经停了"说成了"做失败"。
        const int pid = (int)g_playProc->processId();
        const bool wasRunning = (g_playProc->state() != QProcess::NotRunning);
        if (wasRunning) {
            g_playProc->terminate();
            if (!g_playProc->waitForFinished(3000)) g_playProc->kill();
        }
        delete g_playProc; g_playProc = nullptr;
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("stopped"), true);
        out.data.insert(QStringLiteral("pid"), pid);
        out.data.insert(QStringLiteral("wasRunning"), wasRunning);
        if (!wasRunning)
            out.data.insert(QStringLiteral("note"),
                            QStringLiteral("会话已清理：播放器进程此前已自行退出（播完 / 本机无可用输出设备），不是我们停的"));
        return out;
    }

    // ── 远控模式（真实会话状态，不是假标记）──
    // start：记下会话状态 + 提高抓屏帧率（远控要看得清，2 秒一帧太慢）
    // stop：恢复默认帧率
    if (action == QStringLiteral("remote_control_start")) {
        if (g_remoteControl) {
            out.result = QStringLiteral("done");
            out.data.insert(QStringLiteral("alreadyActive"), true);
            out.data.insert(QStringLiteral("fps"), g_currentFps);
            return out;
        }
        g_remoteControl = true;
        const int want = qBound(1, params.value(QStringLiteral("fps")).toInt(20), 60);
        const int oldFps = g_currentFps;
        g_currentFps = want;
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("active"), true);
        out.data.insert(QStringLiteral("fps"), want);
        out.data.insert(QStringLiteral("fpsBefore"), oldFps);
        out.data.insert(QStringLiteral("sessionSince"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
        qInfo("[agent-qt] 🔴 远控会话开启，抓屏帧率 %d → %d fps", oldFps, want);
        return out;
    }

    if (action == QStringLiteral("remote_control_stop")) {
        if (!g_remoteControl) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("remote_control_stop: 当前没有远控会话在进行");
            return out;
        }
        const int was = g_currentFps;
        g_remoteControl = false;
        g_currentFps = 5;   // 恢复默认
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("active"), false);
        out.data.insert(QStringLiteral("fps"), g_currentFps);
        out.data.insert(QStringLiteral("fpsWas"), was);
        qInfo("[agent-qt] 🟢 远控会话关闭，抓屏帧率恢复 5 fps");
        return out;
    }

    // 接收文件的一个分片。params: seq（第几片，从 0）+ data（base64）
    // 必须按 seq 顺序到达才追加，乱序如实报错（不猜顺序，也不静默丢弃）。
    if (action == QStringLiteral("file_chunk")) {
        if (g_fileRecvTarget.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_chunk: 没有打开的接收会话，请先下发 file_push");
            return out;
        }
        const int seq = params.value(QStringLiteral("seq")).toInt(-1);
        if (seq < 0) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_chunk: seq 非法");
            return out;
        }
        if (seq != g_fileRecvNext) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_chunk: 期望第 %1 片，收到第 %2 片（乱序，拒绝）")
                            .arg(g_fileRecvNext).arg(seq);
            return out;
        }
        const QByteArray data = QByteArray::fromBase64(
            params.value(QStringLiteral("data")).toString().toLatin1());
        if (data.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_chunk: data 为空（base64 解码后 0 字节）");
            return out;
        }
        if (!g_fileRecvFile || !g_fileRecvFile->isOpen()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_chunk: 接收文件句柄已关闭");
            return out;
        }
        const qint64 written = g_fileRecvFile->write(data);
        if (written != data.size()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_chunk: 写入失败（写入 %1 / 期望 %2 字节）")
                            .arg(written).arg(data.size());
            return out;
        }
        g_fileRecvBytes += data.size();
        g_fileRecvNext++;
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("seq"), seq);
        out.data.insert(QStringLiteral("bytes"), written);
        out.data.insert(QStringLiteral("totalBytes"), g_fileRecvBytes);
        return out;
    }

    // 结束接收：校验接收到的字节数与声明的 total_bytes 一致，否则视为损坏。
    if (action == QStringLiteral("file_done")) {
        if (g_fileRecvTarget.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error =QStringLiteral("file_done: 当前没有正在进行的接收会话");
            return out;
        }
        if (g_fileRecvFile) {
            g_fileRecvFile->flush();
            g_fileRecvFile->close();
            delete g_fileRecvFile;
            g_fileRecvFile = nullptr;
        }
        const qint64 expect = params.value(QStringLiteral("total_bytes")).toVariant().toLongLong();
        if (expect <= 0) {
            // 发端点了"取消"（total_bytes=0 就是取消信号，不是"收完整"）。
            // 半截文件**必须删掉**：留着就是这台机器上打不开的残缺文件，中间这段时间谁点开谁中招。
            out.result = QStringLiteral("done");
            out.data.insert(QStringLiteral("cancelled"), true);
            out.data.insert(QStringLiteral("bytes"), g_fileRecvBytes);
            out.data.insert(QStringLiteral("path"), QDir::toNativeSeparators(g_fileRecvTarget));
            QFile::remove(g_fileRecvTarget);
            g_fileRecvTarget.clear();
            g_fileRecvNext = 0;
            g_fileRecvBytes = 0;
            return out;
        }
        if (expect != g_fileRecvBytes) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_done: 字节数不符（收到 %1 / 声明 %2），文件已删除")
                            .arg(g_fileRecvBytes).arg(expect);
            QFile::remove(g_fileRecvTarget);
            g_fileRecvTarget.clear();
            g_fileRecvNext = 0;
            g_fileRecvBytes = 0;
            g_fileRecvFile = nullptr;   // 句柄前面已经关掉了
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("path"), QDir::toNativeSeparators(g_fileRecvTarget));
        out.data.insert(QStringLiteral("bytes"), g_fileRecvBytes);
        out.data.insert(QStringLiteral("chunks"), g_fileRecvNext);
        g_fileRecvTarget.clear();
        g_fileRecvNext = 0;
        g_fileRecvBytes = 0;
        return out;
    }

    // ── file_push：接收远程发来的文件 ──
    // 走独立的消息类型（file_chunk / file_done），见下方 handleControlText 的分支。
    // 这个 action 只是"初始化接收会话"：校验目标路径、清掉旧的半截文件。
    if (action == QStringLiteral("file_push")) {
        const QString fname = params.value(QStringLiteral("name")).toString().trimmed();
        const qint64 total = params.value(QStringLiteral("total_bytes")).toVariant().toLongLong();
        if (fname.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_push: 需要 name（文件名）");
            return out;
        }
        if (!fname.contains(QLatin1Char('.'))) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_push: 文件名 %1 缺少扩展名，拒绝（防无后缀可执行）").arg(fname);
            return out;
        }
        // 目标必须落在收件目录内：只取文件名，丢弃任何路径成分
        const QString clean = QFileInfo(fname).fileName();
        if (clean != fname) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_push: 文件名不能带路径（收到 %1）").arg(fname);
            return out;
        }
        QDir().mkpath(g_recvDir);
        const QString target = g_recvDir + QStringLiteral("/") + clean;
        QFile::remove(target);   // 重名直接覆盖旧文件

        // 开接收句柄（2026-10-04 补上）。原来这两样（收件目录 + 这个句柄）都只有声明没有实现：
        // 结果就是落盘路径跑到当前盘根目录、file_chunk 一律"接收文件句柄已关闭"。
        // 顺带先摘掉上一次没收口的会话 —— 不摘的话两个会话抢同一个句柄，写进去的两批数据会搅一起。
        if (g_fileRecvFile) {
            g_fileRecvFile->close();
            delete g_fileRecvFile;
            g_fileRecvFile = nullptr;
        }
        g_fileRecvTarget = target;
        g_fileRecvBytes = 0;
        g_fileRecvNext = 0;
        g_fileRecvFile = new QFile(target);
        if (!g_fileRecvFile->open(QIODevice::WriteOnly)) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_push: 目标打不开（%1）").arg(target);
            g_fileRecvTarget.clear();
            delete g_fileRecvFile;
            g_fileRecvFile = nullptr;
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("target"), QDir::toNativeSeparators(target));
        out.data.insert(QStringLiteral("totalBytes"), total);
        out.data.insert(QStringLiteral("ready"), true);
        return out;
    }

    out.result = QStringLiteral("failed");
    out.error = QStringLiteral("未知指令：%1").arg(action);
    return out;
}

// 云端下行的文本消息：注册回执 / 指令 / 错误。收到就印出来——被控端看不见指令＝链断了还不自知。
//
// 入向统一：v1 信封与旧扁平格式都化成 (type, payload, id)，
// 这样"云端切了 v1-strict / 退回 legacy"两种情况下本端都还能读懂，不会静默失联。
void handleControlText(const QString &text)
{
    const QByteArray raw = text.toUtf8();
    if (raw.isEmpty()) return;

    if (!raw.startsWith('{')) {
        qInfo("[agent-qt] 收到云端文本（非 JSON）：%s", qPrintable(text.left(200)));
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isObject()) {
        fprintf(stderr, "[agent-qt] FAIL: 云端消息不是合法 JSON：%s\n", qPrintable(text.left(200)));
        return;
    }
    const QJsonObject o = doc.object();
    const bool isV1 = o.contains(QStringLiteral("v"));
    const QJsonObject pay = isV1 ? o.value(QStringLiteral("payload")).toObject() : o;
    const QString type = o.value(QStringLiteral("type")).toString();
    // id 在 v1 里是信封顶层的字符串，旧格式里是顶层的数字；回执要原样带回，云端靠它配对
    const QString id = isV1 ? o.value(QStringLiteral("id")).toString()
                            : QString::number((qint64)o.value(QStringLiteral("id")).toDouble());

    if (type == QStringLiteral("registered") || type == QStringLiteral("registration-ok")) {
        g_registered = true;
        const int hb = pay.value(QStringLiteral("heartbeatMs")).toInt(0);
        const int to = pay.value(QStringLiteral("timeoutMs")).toInt(0);
        if (hb > 0 && hb != g_heartbeatMs) {
            g_heartbeatMs = hb;
            if (g_hbTimer) g_hbTimer->setInterval(g_heartbeatMs);
        }
        if (to > 0) g_timeoutMs = to;
        updateTray(true, QStringLiteral("已连接"));
        qInfo("[agent-qt] ✅ 云端已确认注册 v1（uid=%s，server=%s，心跳 %d ms，超时 %d ms）",
              qPrintable(pay.value(QStringLiteral("uid")).toString()),
              qPrintable(pay.value(QStringLiteral("server")).toString()),
              g_heartbeatMs, g_timeoutMs);
        return;
    }

    if (type == QStringLiteral("error")) {
        // 统一错误通道：云端拒绝必须看得见（fail-silent 红线）。code 是机器可读的，message 是人话。
        fprintf(stderr, "[agent-qt] FAIL: 云端拒绝 → %s：%s\n",
                qPrintable(pay.value(QStringLiteral("code")).toString()),
                qPrintable(pay.value(QStringLiteral("message")).toString()));
        return;
    }

    if (type == QStringLiteral("instruction")) {
        g_instructionSeq++;
        const QString action = pay.value(QStringLiteral("action")).toString();
        const QJsonObject params = pay.value(QStringLiteral("params")).toObject();
        qInfo("[agent-qt] 📥 收到指令 id=%s action=%s（D5 真执行）", qPrintable(id), qPrintable(action));

        // D5：真执行，再如实回执。绝不许"报 done 其实没做"。
        const ExecOut r = executeAction(action, params);
        QJsonObject p;
        p.insert(QStringLiteral("id"), id);
        p.insert(QStringLiteral("action"), action);
        p.insert(QStringLiteral("result"), r.result);
        if (!r.error.isEmpty()) p.insert(QStringLiteral("error"), r.error);
        // 动作的附加数据（进程列表 / 日志行 / 音量实际值 / 截图路径）。
        // 协议规范第十节：**可选字段**，客户端不认就忽略，不算错。
        if (!r.data.isEmpty()) p.insert(QStringLiteral("data"), r.data);
        g_ws->sendTextMessage(makeEnvelope(QStringLiteral("receipt"), p));
        qInfo("[agent-qt] 📤 回执 id=%s result=%s%s",
              qPrintable(id), qPrintable(r.result),
              r.error.isEmpty() ? "" : qPrintable(" error=" + r.error));
        return;
    }

    qInfo("[agent-qt] 收到云端消息 type=%s", qPrintable(type));
}

// 抓一帧 → 本地存图（有 dir 才存）→ 连上了就发出去
void captureAndSend(const QString &outDir)
{
    QElapsedTimer cost;
    cost.start();

    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        fprintf(stderr, "[agent-qt] FAIL: 找不到主屏\n");
        return;
    }

    const QPixmap pm = screen->grabWindow(0);
    if (pm.isNull()) {
        fprintf(stderr, "[agent-qt] FAIL: grabWindow 返回空图\n");
        return;
    }

    QByteArray jpeg;
    {
        QBuffer buffer(&jpeg);
        buffer.open(QIODevice::WriteOnly);
        if (!pm.save(&buffer, "JPEG", 60)) {
            fprintf(stderr, "[agent-qt] FAIL: JPEG 编码失败\n");
            return;
        }
    }

    if (!outDir.isEmpty()) {
        const QString path = outDir + QStringLiteral("/shot-%1.jpg")
                                 .arg(QDateTime::currentMSecsSinceEpoch());
        if (pm.save(path, "JPEG", 60))
            qInfo("[agent-qt] 本地存图 %s", qPrintable(path));
        else
            fprintf(stderr, "[agent-qt] FAIL: 本地存图失败 → %s\n", qPrintable(path));
    }

    const bool online = g_ws && g_ws->state() == QAbstractSocket::ConnectedState;
    if (online) {
        // v1 二进制帧：[1B 版本][2B 大端 headerLen][header JSON][JPEG]，带 seq —— 旧裸帧没有序号，
        // 将来按需拉流、断帧重传、测 fps 全靠它。
        const QByteArray frame = makeFrameBytes(g_uid, g_frameSeq + 1, jpeg);
        const qint64 n = g_ws->sendBinaryMessage(frame);
        g_frameSeq++;
        g_frameBytes += jpeg.size();   // 只累计 JPEG 净荷，跟云端 bytesIn 对账才对得上
        qInfo("[agent-qt] 帧#%d 推送 %lld B 净荷（含帧头 %lld B，累计 %.2f MB，耗时 %lldms）",
              g_frameSeq, (qint64)jpeg.size(), n, g_frameBytes / 1048576.0, cost.elapsed());
    } else {
        qInfo("[agent-qt] 帧#%d 离线（%lld B 未发）—— 云端不在，先只本地抓屏",
              ++g_frameSeq, (qint64)jpeg.size());
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);   // 要托盘 → QApplication（托盘在 QtWidgets 里）

    // Windows GUI 程序里 qInfo 默认走 OutputDebugString，重定向到文件就是空的——
    // 被控端没有日志等于瞎子。这里把 Qt 日志全部接管到 stdout/stderr。
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString &msg) {
        FILE *out = (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
                        ? stderr : stdout;
        fprintf(out, "%s\n", qPrintable(msg));
        fflush(out);
        // 同时落盘：装机后是计划任务拉起、没有 stdout，没日志文件就只能靠猜（log_tail 动作读它）
        appendLogFile(msg);
        if (type == QtFatalMsg)
            abort();
    });

    // 自启时计划任务**直接拉 exe**（不再经 bat）→ 配置得由自己从同目录的 agent.env 读。
    // 2026-10-03 实测：计划任务里套一层 `cmd /c start-agent.bat` 会**卡住不退出**，
    // 任务永远 Running、被控端根本没被拉起。
    const int envN = loadEnvFile(QCoreApplication::applicationDirPath() + QStringLiteral("/agent.env"));
    if (envN > 0) qInfo("[agent-qt] 已从 agent.env 读入 %d 项配置（exe 同目录）", envN);

    // 数据目录放 %LOCALAPPDATA%\xingjikong\（与既有约定一致，覆盖升级天然保留历史）。
    // 截图目录默认**不再**落在当前工作目录 —— 装机后 cwd 是哪全看启动器，落在那儿等于丢了。
    const QString dataDir = qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong");
    QDir().mkpath(dataDir);
    g_shotDir = qEnvironmentVariable("STE_QT_SHOT_DIR", dataDir + QStringLiteral("/shots"));
    if (!g_shotDir.isEmpty()) QDir().mkpath(g_shotDir);
    g_logPath = qEnvironmentVariable("STE_QT_LOG", dataDir + QStringLiteral("/agent-qt.log"));

    // ── 调度与媒体库的持久化路径（2026-10-03 修复）──
    // 这三个变量**只在文件头声明过、从来没赋过值**：
    //   · g_schedDir 为空 → saveSchedules() 进门就 return → schedules.json 永远写不进去，
    //     而 schedule_shutdown 照样回执 done（回执说"安排成功"，实际一个字节都没落盘）；
    //   · g_schedFile 为空 → loadSchedules() 必然读空 → 重启后定时全丢；
    //   · g_mediaDir 为空 → media_list 默认目录是空串 → 恒 failed，media_delete 的越界校验也形同虚设。
    // 这就是"假成功"：动作报了 done，机器却什么都没记住。这里一次性把真路径定死。
    g_schedDir = envOr("STE_QT_SCHED_DIR", dataDir + QStringLiteral("/sched"));
    QDir().mkpath(g_schedDir);
    g_schedFile = g_schedDir + QStringLiteral("/schedules.json");
    g_mediaDir = envOr("STE_QT_MEDIA_DIR", dataDir + QStringLiteral("/media"));
    QDir().mkpath(g_mediaDir);

    // ── file_push 的收件目录（2026-10-04 补上）──
    // 和上面 g_schedDir / g_mediaDir 是同一类缺口：这行只声明过、从来没赋过值，
    // 于是 file_push 算出来的落盘路径是 "/文件名" —— 直接写进当前盘根目录（C:\），
    // 同时 g_shotDir/g_mediaDir 那套目录约定在这里也是断的。收件文件统一进 ~\incoming。
    g_recvDir = envOr("STE_QT_RECV_DIR", dataDir + QStringLiteral("/incoming"));
    QDir().mkpath(g_recvDir);

    // ── 托盘常驻（先建好；后面连接状态一变就往它上面写）──
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        g_tray = new QSystemTrayIcon(&app);
        auto *menu = new QMenu();
        g_actStatus = menu->addAction(QStringLiteral("状态：正在启动…"));
        g_actStatus->setEnabled(false);      // 只显示，不可点
        menu->addSeparator();
        auto *actData = menu->addAction(QStringLiteral("打开数据目录"));
        QObject::connect(actData, &QAction::triggered, &app, [] {
            const QString dir = qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong");
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
        });
        auto *actLog = menu->addAction(QStringLiteral("打开日志目录"));
        QObject::connect(actLog, &QAction::triggered, &app, [] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(g_logPath).absolutePath()));
        });
        menu->addSeparator();
        auto *actQuit = menu->addAction(QStringLiteral("退出被控端"));
        QObject::connect(actQuit, &QAction::triggered, &app, &QApplication::quit);
        g_tray->setContextMenu(menu);
        updateTray(false, QStringLiteral("正在连接…"));
        g_tray->show();
        qInfo("[agent-qt] 托盘已就绪");
    } else {
        fprintf(stderr, "[agent-qt] WARN 系统托盘不可用（Session0？）—— 进程照跑，但没有可见入口\n");
    }

    g_ws = new QWebSocket();
    g_ws->setParent(&app);

    QObject::connect(g_ws, &QWebSocket::connected, &app, [] {
        qInfo("[agent-qt] 已连上云端（等待 register 回执）");
        g_backoffMs = 1000;      // 连上了就重置退避
        sendRegister();
    });

    // 云端踢人时给不给原因，被控端这边都得说清楚，否则运维只能猜。
    // （Qt 6.8 的 aboutToClose() 不带参数，拿不到 close code，所以退到 socket 错误串 + 上下文判断。）
    QObject::connect(g_ws, &QWebSocket::disconnected, &app, [] {
        g_registered = false;
        updateTray(false, QStringLiteral("未连接（重试中）"));
        fprintf(stderr, "[agent-qt] FAIL: 云端断开（%lld 帧已发）\n", g_frameBytes);
        if (!g_registered) {   // 连上了却从没拿到 registered 就被断开 = 握手/令牌没过
            fprintf(stderr, "[agent-qt]   └ 本轮 register 未通过云端校验 —— 先查令牌（STE_QT_WS_TOKEN）与云端 CLOUD_WS_TOKEN 是否一致，再看云端 events.log\n");
        } else {
            fprintf(stderr, "[agent-qt]   └ 网络/服务侧断开，正在退避重连\n");
        }
        scheduleReconnect();
    });

    QObject::connect(g_ws, &QWebSocket::errorOccurred, &app,
                     [](QAbstractSocket::SocketError) {
                         // disconnected 里统一退避重连，这里只报真实原因
                         fprintf(stderr, "[agent-qt] FAIL: 连接出错 —— %s\n",
                                 qPrintable(g_ws->errorString()));
                     });

    QObject::connect(g_ws, &QWebSocket::textMessageReceived, &app, [](const QString &msg) {
        handleControlText(msg);
    });

    // 抓屏**默认不落盘**：每 2 秒一张、一天能堆上 10GB，是隐患。
    // 要留证请用 `screenshot` 动作（按需、走 g_shotDir），或显式设 STE_QT_STORE_FRAMES=1 复现旧行为。
    const QString storeFrames =
        (qEnvironmentVariableIntValue("STE_QT_STORE_FRAMES") == 1) ? g_shotDir : QString();
    if (!storeFrames.isEmpty())
        qInfo("[agent-qt] 注意：STE_QT_STORE_FRAMES=1，每帧都会落盘到 %s", qPrintable(storeFrames));
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &app, [storeFrames] {
        captureAndSend(storeFrames);
    });
    timer.start(kFrameIntervalMs);

    // 心跳定时器：独立于抓屏。抓屏失败/按需拉流不推帧时，云端仍要靠它判在线。
    // 间隔先取环境变量，等云端 registered 下发 heartbeatMs 后再改（协议 v1：参数由云端定，两端不各猜）。
    int hbMs = qEnvironmentVariableIntValue("STE_QT_HEARTBEAT_MS");
    if (hbMs <= 0) hbMs = kHeartbeatIntervalMs;
    g_heartbeatMs = hbMs;
    g_hbTimer = new QTimer(&app);
    QObject::connect(g_hbTimer, &QTimer::timeout, &app, [] { sendHeartbeat(); });
    g_hbTimer->start(g_heartbeatMs);
    qInfo("[agent-qt] 心跳间隔 %d ms（等云端 registered 下发 heartbeatMs 后以其为准；本机当前按 %d ms 无心跳即判离线）",
          g_heartbeatMs, g_timeoutMs);

    connectNow();
    QMetaObject::invokeMethod(&timer, "timeout", Qt::QueuedConnection);

    return app.exec();
}
