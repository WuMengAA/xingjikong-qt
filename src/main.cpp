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

#include "singleinstance.h"
#include "schedule_clock.h"   // 2026-10-06：课表时钟（上下课提醒）
#include "schedule_view.h"    // 2026-10-06：大屏今日课表窗口   // 单实例守卫（与管理端 viewer 同一份实现）
#include "oauthbind.h"        // 2026-10-06：用星璃账号绑定（OAuth + 设备票）
#include "island.h"           // 2026-10-07：被控端灵动岛（顶部居中胶囊 + 缓动 + 交互穿透）

#include <QApplication>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QPainter>
#include <QDesktopServices>
#include <QTimer>
#include <QScreen>
// ⚠️ WebEngine 的拉起时机由"第一次 new QWebEngineView"决定，见 rtcStart()：
// 只要不在 main 里提前 new，Chromium 就不会在进程启动时被拉起来。
// 反过来，如果哪天有人在 main 开头 new 一个 QWebEngineView（哪怕只是为了"预热"），
// 就得回 ~456MB 私有内存 / 78 线程的常驻，跟按需加载互相抵消。
#include <QWebEngineView>
#include <QPixmap>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QBuffer>
#include <QSet>
#include <cstdlib>   // rand()：重连退避抖动（2026-10-08）
#include <QStandardPaths>
#include <QElapsedTimer>
#include <QUrl>
#include <QWebSocket>
#include "cloud_proto.h"       // 协议 v1 信封（makeEnvelope 定义在这，别的 TU 共用）
#include "terminal_console.h"  // 2026-10-06：远程终端（REPL，见该文件注释）
#include "exam_mode.h"         // 2026-10-06：考试模式（全屏拦截 + 黑名单轮询 + 计时）
#include "audio_player.h"      // 2026-10-07：语音对讲播放（waveOut）
#include "broadcast_view.h"    // 2026-10-07：屏幕广播全屏显示
#include "ldc_streamer.h"      // 2026-10-09：WebRTC 去 WebEngine 底层（libdatachannel 推流）
// ── 通知体系新组件（2026-10-09 接线 · DeepSeek Harness 造）──
#include "danmaku.h"           // §1.2 弹幕：字幕式横向滚动
#include "loop_reminder.h"     // §1.2 loopRemind：未确认循环提醒
#include "notification_gate.h" // §6.2 考试模式禁一切通知与监视
#include "marquee_text.h"      // §6.1 灵动岛展开态滚动文本
#include "island_clock.h"      // 最初需求：灵动岛时间/状态显示
#include "spectrum_widget.h"   // §6.3 语音通知 16 位频谱
#include "edge_tts.h"          // §6.4 Edge 语音 TTS（管理端可选）
#include "noise_monitor.h"     // §6.5 噪音检测
#include "microphone.h"        // 语音通话：mic 会话内采集
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QVector>
#include <QFileInfo>
#include <QStorageInfo>   // 2026-10-06 强制自动升级：装之前先看磁盘剩多少
#include <QStringList>
#include <QStandardPaths>
#include <QSet>
#include <QProcess>
#include <QThread>      // QThread::msleep（camera_record 确认"真的在录"时等两秒）
#include <functional>   // 2026-10-07：NotifyWindow 确认/快捷回复回调（std::function）
#include <algorithm>    // 2026-10-09：重复下发去重名单裁剪用 std::sort
// 2026-10-05：OTA 自更新要下载安装包 + 校验 sha256（详见 executeSelfUpdate）。
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QCryptographicHash>
#include <QRegularExpression>
// 2026-10-04：首次运行配置窗（OOBE）——让老师在界面里填配置，而不是手改 agent.env。
#include <QDialog>
#include <QMessageBox>   // 2026-10-05：单例锁重复启动提示
#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
// 2026-10-04：OOBE 绑定班级（消费激活码 POST 站点 /api/device/activate）
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QMouseEvent>   // 2026-10-05：紧急通知点击关闭
#include <QPaintEvent>   // 2026-10-05：全屏通知背景色（setStyleSheet 对顶层 QWidget 无效）
#include <QGraphicsOpacityEffect>   // 2026-10-07：通知窗淡入淡出（#98「要有过渡」）
#include <QPropertyAnimation>
#include <QPainter>
#include <QPainterPath>
#include <windows.h>
#include <tlhelp32.h>   // 进程快照（process_list / process_stop）
#include <psapi.h>      // 进程工作集内存
#include <mmsystem.h>   // waveOutSetVolume（set_volume，Rust 版实测零依赖可用）
#include <shellapi.h>   // ShellExecuteW（launch_app）+ SHFileOperationW（media_delete 进回收站）
#include <sapi.h>       // 2026-10-04：大屏通知 TTS 语音播报（Windows SAPI 5，微软免费离线语音）
#include <cstdio>

// 音量与进程内存信息要显式链接（MSVC 下 pragma 最省事，不必动 CMakeLists）
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shell32.lib")  // SHFileOperationW
#pragma comment(lib, "sapi.lib")     // ISpVoice（通知 TTS）

namespace {

constexpr int kFrameIntervalMs = 2000;   // 2 秒一帧，与 D2 保持一致
// 采集页回收延迟：rtc-stop 后留这么久，没人重新点开就真删（见 scheduleRtcViewReap）
constexpr int kRtcReapDelayMs = 5000;
constexpr int kHeartbeatIntervalMs = 10000; // 心跳缺省值（云端 registered 里给了就以云端为准）
constexpr int kMaxBackoffMs = 8000;      // 退避上限（2026-10-08：从 15s 降到 8s，断网恢复更快）
constexpr int kFirstBackoffMs = 500;     // 首连/未注册时的快速重试间隔（2026-10-08）
constexpr int kProtocolVersion = 1;      // 协议 v1（《星集控-协议规范v1-2026-10-03.md》）
constexpr int kProcessListLimit = 50;    // process_list 默认条数（与 Rust DEFAULT_LIMIT 同量级）

// ⚠️ 应用版本号的**唯一真源**（2026-10-05 统一）：
//   · register 上报给云端的就是它 → 云端据此判断要不要提示 OTA 升级；
//   · 装机包 NSIS 的 DisplayVersion 也必须与它一致（installer.nsi 的 VER 常量）。
//   改版本时**只改这一处** + installer.nsi，别在别处再写一份（历史上就是两处不一致出过岔子：
//   exe 报 0.4.0-v1、安装器写 0.5.0）。
//   ⚠️ 2026-10-06 收敛到 0.6.0：此前这里写 0.5.0、installer.nsi 写 0.5.1，本文件自己的注释
//      还写着"两者必须一致"却没做到 —— 不一致的代价是云端按 0.5.0 判断 OTA，装出来却是 0.5.1。
//     ⚠️ 2026-10-07：命名改 DeepSeek Harness 同款，正式 X.Y.Z / 候选 X.Y.Z-rc.N
//     （rc 号同核心递增、换核心归零、转正剥离后缀；成熟度标记不进版本号，归 ota.json 的
//      mandatory / notes 管）。完整规则见 ../../docs/版本号命名规范-2026-10-07.md。
//     这一行是 CMake 校验用的回落值 —— 正常构建下由 set(AGENT_VERSION ...) 强制拉齐。
constexpr const char *kAppVersion = "0.6.24-rc.6";

// 2026-10-10：灰度渠道持久化（发现①修复）—— 当前生效渠道在 main 启动时解析并落盘 channel.txt，
// 后续任何重启/升级都自动沿用，不再依赖启动器注入 STE_QT_CHANNEL。见 resolvedChannel/persistChannel。
static QString g_channel = QStringLiteral("stable");   // 供 register 上报；main 启动即解析覆盖
QString resolvedChannel();                               // 前向声明（定义在 agentEnvPath 之后）

QWebSocket *g_ws = nullptr;
int g_backoffMs = kFirstBackoffMs;
// 2026-10-08：本机归属的班级码（OOBE 绑定后持久化，register 时带给云端 → 管理端按班分组）。
QString g_classCode;
// 2026-10-09：班级**显示名**（站点 /api/device/activate 回喂的 className，如「1班」）。
// 云端班级索引的 name 默认等于 code；带上它，管理端面板才显示真名而不是 class_01。
QString g_className;
// 2026-10-08：是否曾经注册成功过（决定首连用快速重试还是正常退避）。
bool g_everRegistered = false;
// 语音对讲（2026-10-07）：学生端播放器（waveOut，懒打开——只在收到音频帧时开）
AudioPlayer g_audioPlayer;
bool g_audioHintShown = false;   // "老师正在讲话"提示是否已显示（每会话一次）
int g_audioUpSeq = 0;            // 语音通话上行帧序号（2026-10-09）
// ── 通知体系组件实例（2026-10-09 接线 · DeepSeek Harness）──
DanmakuWidget *g_danmaku = nullptr;          // 弹幕（懒建：实际用弹幕时才建窗口）
LoopReminder g_loopReminder;                 // 循环提醒（无 UI，纯逻辑）
NotificationGate g_notifGate;                // 通知门控（考试模式禁通知）
Microphone g_microphone;                     // 语音通话 mic 采集（会话内开关）
SpectrumWidget *g_spectrum = nullptr;        // §6.3 语音频谱（懒建：有语音才出现）
// 2026-10-08：断线灵动岛去重。退避重连期间每次 disconnected 都会再弹一条「连接中断」，
// 首连快速重试（500ms）下等于每半秒闪一次，用户看到的就是"一直显示连接中断"。
// 规则：同一段离线只弹第一条；云端回 registered（真的又连上了）才把闸门放开。
static bool g_islandOfflineShown = false;
static qint64 g_lastOfflineToastMs = 0;  // 2026-10-08：上一条"连接中断"灵动岛的时间戳（秒级冷却，防抖动时反复弹）
int g_frameSeq = 0;
qint64 g_frameBytes = 0;      // 累计发出的 JPEG 净荷（不含帧头）——与云端 bytesIn 对账用这个
int g_instructionSeq = 0;
bool g_registered = false;     // 云端回了 registered
QString g_uid;                 // 本机设备码（register 时定下来，心跳复用，避免每次重算）
int g_heartbeatSeq = 0;
int g_heartbeatMs = kHeartbeatIntervalMs;   // 实际心跳间隔：云端下发就听云端的
int g_timeoutMs = 30000;                    // 云端判离线阈值（只用于日志说明，不自己判）
QTimer *g_hbTimer = nullptr;   // 心跳定时器（registered 拿到参数后要能改间隔，所以放外面）

// ---- WebRTC 推流状态（内嵌采集页）----
// ⚠️ 这四行曾经在一次误合并里被整段删掉（只删声明、没删使用），编译期满屏
//    "g_rtcView: 未声明的标识符"。改这块时留意：声明和使用必须同时存在。
static QWebEngineView *g_rtcView = nullptr;
static QTimer *g_rtcTick = nullptr;
static bool g_rtcOn = false;
// 采集页回收定时器（2026-10-05 内存优化）：没人看画面这件事在机房里是常态（几十台机器
// 同时挂着被控端，老师只看其中一两台）。原来 rtc-stop 只让页面里跑一个 __reset()，
// QWebEngineView 和它拉起来的 Chromium 渲染进程（QtWebEngineProcess，实测 ~137MB 常驻）
// 一直不回收 → 一年到头内存只涨不降。这里改成"停推 → 延迟几秒 → 真删 view"。
static QTimer *g_rtcReap = nullptr;
// 云端离线起始时刻（0 = 在线）：采集页回收的兜底触发源。
// 为什么不能只靠云端的 rtc-stop 广播：实测管理端进程被强杀（不是正常退订）时，
// 云端不会补发这一下 —— 采集页和它拉起来的 Chromium 渲染进程（~137MB）就一直挂着。
// 而"云端连不上"这个信号是本地就能看出来的，而且连接断了画面本来也到不了任何人眼里的，
// 拿它当回收触发源不会误杀正在看的画面。
static qint64 g_offlineSince = 0;
static bool g_webEngineReady = false;   // 见 ensureWebEngine()：按需拉，用完不卸（卸载由 Qt 决定）
static bool g_rtcPageReady = false;     // 采集页 loadFinished 过没有（setHtml 是异步的）

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

// ── 配置/连接可见性状态（2026-10-04）──
// 目标：配置缺失时"明显报错"，不和"配了但连不上"混为一谈。
enum class TrayState { Connected, Disconnected, Unconfigured };
bool g_configured = true;            // URL+TOKEN 是否齐全（loadEnvFile 后判定）
bool g_unconfigured = false;         // 已进入"未配置"态（不连接/不重试）
bool g_unconfiguredNotified = false; // 未配置气泡只弹一次
bool g_reconnectNow = false;         // 保存新配置后：断线回调里立刻重连（别等退避）
bool g_configDialogOpen = false;     // 配置窗防重入（首运行自动弹 + 手动点可能撞车）

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

// ⚠️ makeEnvelope 的**定义**已经在文件末尾（匿名 namespace 外面）。
// 原先它就写在下面那段匿名 namespace（103–3846 行）里，匿名域符号跨不了 TU ——
// terminal_console.cpp 一调用就 LNK2019。调用点仍在 namespace 内，靠 cloud_proto.h 的声明可见。

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

// 语音通话上行帧（2026-10-09 · 语音通话设计 §4.1）：学生 mic PCM → 云端。
// 与画面帧同 v1 结构，区别在 mime=audio/pcm（云端据此走 duplex 单播回老师）。
QByteArray makeAudioFrameBytes(const QByteArray &pcm)
{
    QJsonObject h;
    h.insert(QStringLiteral("v"), kProtocolVersion);
    h.insert(QStringLiteral("type"), QStringLiteral("frame"));
    h.insert(QStringLiteral("uid"), g_uid);
    h.insert(QStringLiteral("seq"), ++g_audioUpSeq);
    h.insert(QStringLiteral("ts"), QJsonValue(QDateTime::currentMSecsSinceEpoch()));
    h.insert(QStringLiteral("mime"), QStringLiteral("audio/pcm"));
    h.insert(QStringLiteral("bytes"), pcm.size());
    const QByteArray header = QJsonDocument(h).toJson(QJsonDocument::Compact);

    QByteArray out;
    out.reserve(3 + header.size() + pcm.size());
    out.append(char(kProtocolVersion));
    out.append(char((header.size() >> 8) & 0xFF));
    out.append(char(header.size() & 0xFF));
    out.append(header);
    out.append(pcm);
    return out;
}

void scheduleReconnect();

// 定义在后面（托盘区），这里前向声明供 connectNow/sendRegister 提前调用
void updateTray(TrayState st, const QString &detail);
void goUnconfigured();
// 定义在后面（OTA 段）：设置面板里的「立即升级」复用它，所以提前声明。
static void startSelfUpdate(const QString &id, const QJsonObject &params);
// 设置与信息面板（定义在 OOBE 配置窗之前）；托盘菜单要用它。
void openInfoDialog();

void connectNow()
{
    const QString url = qEnvironmentVariable("STE_QT_WS_URL").trimmed();
    if (url.isEmpty()) {
        // 老代码只 return，连重试都不排 —— 配置缺失时进程静默停摆，老师以为装好了。
        // 现在进"未配置"态：可见（图标+日志+气泡），且不重试。
        goUnconfigured();
        return;
    }
    if (qEnvironmentVariable("STE_QT_WS_TOKEN").trimmed().isEmpty()) {
        // 令牌没配：连上去也过不了握手，直接判"未配置"，别做无意义重连。
        goUnconfigured();
        return;
    }
    qInfo("[agent-qt] 连接 %s …", qPrintable(url));
    // Qt 6.8：setUrl()+connectToHost() 已合并为 open(url)
    g_ws->open(QUrl(url));
}

void scheduleReconnect()
{
    // 2026-10-08 根因修复：几十台教室机同时断网时，若都用同一退避序列会在同一时刻一起重连，
    // 把云端瞬间打死 → 又一起断，恶性循环。加 ±20% 抖动，让重连时刻分散开。
    // 首连（从未注册成功过）用快速重试，断网恢复更快。
    const int base = g_everRegistered ? qMin(g_backoffMs, kMaxBackoffMs) : kFirstBackoffMs;
    const double r = (double)rand() / (double)RAND_MAX;        // [0,1)
    const int jitter = int(base * (0.8 + 0.4 * r));            // [0.8,1.2) × base
    QTimer::singleShot(jitter, connectNow);
    qInfo("[agent-qt] %d ms 后重试（退避上限 %d ms，含 ±20%% 抖动%s）",
          jitter, kMaxBackoffMs, g_everRegistered ? "" : "，首连快速重试");
    g_backoffMs = qMin(g_backoffMs * 2, kMaxBackoffMs);
}

/** 本端真实实现的指令全集，**必须与 executeAction 的分支逐条对齐**。
 *  （2026-10-06 契合度改造：以前 register 只报 screen/input/power 三个能力组、
 *   这条表一个都不报，于是云端和管端无从知道这台机器能不能拍照/定时/推文件，
 *   管端只能把按钮对着所有机器开放，点下去才收到一句「未知指令」。）
 * 放成常量数组而不是散在各个 if 里，是为了让"能做什么"只有一处真值来源：
 * 以后 executeAction 加分支，这张表也得跟着加，否则设备会自称会做却做不了（或反之）。 */
static const char *kActionNames[] = {
    "camera_bind", "camera_list", "camera_record_start", "camera_record_stop", "camera_snapshot", "camera_test",
    "cancel_schedule", "file_chunk", "file_done", "file_push",
    "input", "launch_app", "list_schedules", "list_shortcut_candidates",
    "lock", "log_tail", "media_delete", "media_list",
    "media_session_start", "media_session_stop", "notify", "open_app",
    "ping", "process_list", "process_stop", "reboot",
    "remote_control_start", "remote_control_stop", "schedule_reboot", "schedule_shutdown",
    "screenshot", "self_update", "set_volume", "shutdown",
    // 远程终端三条（2026-10-06）。必须报上去：管理端按 caps 决定「终端」按钮亮不亮，
    // 不报 = 管理端永远置灰，功能做完了看得见摸不着。
    // 顺序照旧按字母表末段排，方便以后和别的动作一起校对。
    "terminal_close", "terminal_input", "terminal_open",
    // 考试模式（2026-10-06，设计文档 3.7 第一版）：全屏拦截 + 黑名单轮询 + 计时。
    "exam_mode", "exam_mode_stop",
};
static const int kActionCount = (int)(sizeof(kActionNames) / sizeof(kActionNames[0]));

void sendRegister()
{
    g_uid = envOr("STE_QT_UID", QHostInfo::localHostName());
    const QString &uid = g_uid;
    const QString token = qEnvironmentVariable("STE_QT_WS_TOKEN").trimmed();
    if (token.isEmpty()) {
        // 不许把空令牌发出去白等云端拒（老代码 L194-195 照发）——直接进"未配置"态，可见且停止重试。
        goUnconfigured();
        if (g_ws && g_ws->state() != QAbstractSocket::UnconnectedState) g_ws->close();
        return;
    }
    // caps：告诉云端这台机器能做什么。
    // actions 是给机器比对的（管端据此决定按钮置不置灰），screen/input/power 是给人看的能力组。
    QJsonObject caps;
    caps.insert(QStringLiteral("screen"), true);
    caps.insert(QStringLiteral("input"), true);
    caps.insert(QStringLiteral("power"), true);
    QJsonArray acts;
    // 注：Qt6 的 QJsonArray 没有 reserve()（不是 QVector/QList），直接 append 即可。
    for (int i = 0; i < kActionCount; ++i)
        acts.append(QString::fromLatin1(kActionNames[i]));
    caps.insert(QStringLiteral("actions"), acts);

    QJsonObject p;
    p.insert(QStringLiteral("uid"), uid);
    p.insert(QStringLiteral("token"), token);
    p.insert(QStringLiteral("version"), QString::fromLatin1(kAppVersion));
    p.insert(QStringLiteral("caps"), caps);
    // 2026-10-08：设备展示名（默认电脑名，可用 STE_QT_NAME 覆盖；管理端据此显示「设备名」而非一串 uid）
    p.insert(QStringLiteral("name"), envOr("STE_QT_NAME", QHostInfo::localHostName()));
    // 2026-10-08：本机归属的班级码（OOBE 绑定后持久化）；云端据此把设备按班分组。
    if (!g_classCode.isEmpty()) p.insert(QStringLiteral("classCode"), g_classCode);
    // 2026-10-09：班级显示名 —— 云端班级索引 name 默认 = code，带上它管理端才显示「1班」。
    if (!g_className.isEmpty()) p.insert(QStringLiteral("className"), g_className);

    // 2026-10-10：灰度渠道 —— 设备声明自己所在的更新通道（stable / gray），云端据此下发对应通道版本。
    //   改用「持久化后的当前渠道」g_channel（main 启动时已 resolvedChannel()+persistChannel() 落盘），
    //   这样即便计划任务 action / OTA 重启助手没注入 STE_QT_CHANNEL，灰度也跨重启/升级保留（发现①修复）。
    const QString channel = g_channel;
    p.insert(QStringLiteral("channel"), channel);

    // 2026-10-06：设备指纹（账号绑定路径需要）。
    // 如果 token 是站点签的设备票（7 段 v1.…），云端验票时会拿这个 fp 比对；
    // 旧静态令牌路径不校验 fp，这个字段会被忽略。不填也不报错（兼容旧云端）。
    // 指纹统一由 oauthbind.cpp 的 computeFingerprint() 出：站点签设备票时算的是同一个值，
    // 这里另抄一份（哪怕算法现在一样）迟早会漂移 —— 一漂移就是"绑定成功却连不上"。
    p.insert(QStringLiteral("fp"), computeFingerprint());

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

/* ══ 2026-10-07 恶性BUG：关机/重启指令会一直发、机器一直重启 ══════════════════
 *
 * 原写法的顺序是「先 ExitWindowsEx 关机 → 再 sendActionReceipt 回执」。
 * 机器一关进程就没了，**回执物理上发不出去** ⇒ 云端永远判这条"未回执" ⇒
 * 下次设备重连时把 shutdown 补发一次 ⇒ 又关 ⇒ 又死 ⇒ 无限循环。
 * 现象就是老师点一次关机，机器反复重启、"被控软件一连上就立刻重启"。
 *
 * 三层止血（缺一层都会在某个版本组合下复发）：
 *   ① 回执先发、关机延后 2.5 秒（见 executeAction 的 shutdown/reboot 分支）——
 *      否则"发了回执"和"真关了"中间那段是彻底黑洞，对账永远差一条；
 *   ② 本机落盘记录"已执行过的破坏性指令 id"，重复下发直接跳过 ——
 *      挡的是还没升级到 no_resend 那套逻辑的**老版本云端**补发过来的同一条；
 *   ③ 云端 store.js 已把 shutdown / reboot / schedule_* / self_update 标 no_resend，
 *      根本不进补发队列（regression-terminal-no-resend.mjs 11 项断言）。
 *
 * 只挡"已经执行过"的**同一条 id**，不挡新指令：老师想再关一次，下新单即可。
 */
// ⚠️ 2026-10-09 语义扩大：不再只管关机/重启这类"执行即终止进程"的指令，
//    而是**所有**已执行过的 instruction id —— 云端重连补发是按"未回执"来的
//    （store.js pendingFor，只排除 no_resend 的终止类），非终止类动作一旦
//    "执行了但回执没送到"（重启/断网/进程被杀），重连就会被补发、被二次执行 ——
//    正是用户报的"被控端重启后又把最后一次操作做了一遍"。
static QSet<QString> g_terminalDone;   // 已执行过的指令 id（内存态，随开随读）
static QString       g_terminalDoneFile;
static QString       g_lastInstrId;    // 当前这条 instruction 的 id，executeAction 要用
// 记录上限：超出就按 id 数值保留最新的那批（云端 id 是自增整数，越大越新）
static constexpr int kDoneKeep = 500;

static QString terminalDonePath()
{
    if (g_terminalDoneFile.isEmpty()) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (dir.isEmpty()) g_terminalDoneFile = QStringLiteral("./terminal-done.json");  // 兜底也别让关机卡住
        else {
            QDir().mkpath(dir);
            g_terminalDoneFile = dir + QStringLiteral("/terminal-done.json");
        }
    }
    return g_terminalDoneFile;
}

/** 读回上次记下的"已经执行过"的破坏性指令 id。读不了就当空 —— 绝不能因为它挡住关机。 */
static void loadTerminalDone()
{
    QFile f(terminalDonePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    const QJsonArray arr = doc.isObject() ? doc.object().value(QStringLiteral("ids")).toArray()
                                          : doc.array();
    for (const QJsonValue &v : arr) if (v.isString()) g_terminalDone.insert(v.toString());
}

/** 记一笔并**立即**落盘：关机随时可能发生，缓冲区里的东西来不及写就白记了。 */
static void markTerminalDone(const QString &id)
{
    if (id.isEmpty()) return;
    g_terminalDone.insert(id);
    // 上限裁剪：只保留最新的 kDoneKeep 条（云端 id 自增，数值越大越新）
    if (g_terminalDone.size() > kDoneKeep * 2) {
        QStringList ids = g_terminalDone.values();
        std::sort(ids.begin(), ids.end(), [](const QString &a, const QString &b) {
            return a.toLongLong() > b.toLongLong();
        });
        QSet<QString> keep;
        for (int i = 0; i < kDoneKeep && i < ids.size(); ++i) keep.insert(ids[i]);
        g_terminalDone = keep;
    }
    QFile f(terminalDonePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qInfo("[agent-qt] ⚠ 破坏性指令记录写不进（%s）—— 重复下发只能靠云端侧兜", qPrintable(f.errorString()));
        return;
    }
    QJsonArray arr;
    for (const QString &s : g_terminalDone) arr.append(s);
    QJsonObject o;
    o.insert(QStringLiteral("ids"), arr);
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    f.close();
}

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

// ── 摄像头绑定（2026-10-09）──────────────────────────────────────────────
// 教室机上常有多枚摄像头（含 OBS 这类虚拟摄像头），抓拍/录制到底用哪一枚必须**本机说了算**
// （只有本机能枚举）。所以绑定存在被控端：管理端「监控中心」选一枚 → camera_bind 落到这里；
// 之后 camera_snapshot / camera_record_start 不带 device 时默认用它。
// 存成 `%LOCALAPPDATA%/xingjikong/cameradev.txt`（与 classcode.txt 同款，不动 agent.env）。
static QString cameraDevPath()
{
    return qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong/cameradev.txt");
}
static QString boundCamera()   // 空 = 未绑定（调用方回落 video0 / 设备名）
{
    QFile f(cameraDevPath());
    if (!f.open(QIODevice::ReadOnly)) return QString();
    const QString d = QString::fromUtf8(f.readAll()).trimmed();
    f.close();
    return d;
}
static bool saveBoundCamera(const QString &dev)
{
    const QString d = dev.trimmed();
    if (d.isEmpty()) { QFile::remove(cameraDevPath()); return true; }   // 传空 = 解绑
    QDir().mkpath(QFileInfo(cameraDevPath()).absolutePath());
    QFile f(cameraDevPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(d.toUtf8());
    f.close();
    return true;
}
/** 解析 `ffmpeg -list_devices true -f dshow -i dummy` 的输出。
 *  实测格式（ffmpeg 9.0，2026-10-09）：
 *      [in#0 @ 0x…] "OBS Virtual Camera" (none)
 *      [in#0 @ 0x…]   Alternative name "@device_sw_{…}"
 *      [in#0 @ 0x…] "麦克风 (High Definition Audio Device)" (audio)
 *  ⚠️ 设备名在**首对引号**里，类别在尾部括号里。别再抓行里第一个 '@' ——
 *     那是 `[in#0 @ 0x…]` 的地址标记，抓它解析必然为空（旧实现的 bug）。
 *  类别：video=摄像头；audio=麦克风；**none=虚拟摄像头（如 OBS），也算摄像头**。 */
static void parseDshowDevices(const QString &txt, QJsonArray &cams, QJsonArray &mics)
{
    static const QRegularExpression re(QStringLiteral("\\]\\s*\"([^\"]+)\"\\s*\\(([A-Za-z]+)\\)"));
    for (const QString &line : txt.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch m = re.match(line);
        if (!m.hasMatch()) continue;
        const QString name = m.captured(1).trimmed();
        const QString kind = m.captured(2).toLower();
        if (name.isEmpty()) continue;
        if (kind == QLatin1String("audio")) mics.append(name);
        else                                cams.append(name);
    }
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
 * 读数据目录的 agent.env（格式：`set KEY=VALUE`，`rem`/`#` 为注释）。
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

QIcon makeTrayIcon(TrayState st)
{
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    // 三态：已连=实心 / 断线=空心 / 未配置=红色空心+一道斜杠。
    // 未配置刻意"不靠颜色单独表达"（深浅托盘底都能辨），一眼区别于普通断线。
    const bool unconfig = (st == TrayState::Unconfigured);
    const QColor stroke = unconfig ? QColor(0xC0, 0x30, 0x30) : QColor(0x10, 0x10, 0x10);
    p.setPen(QPen(stroke, 2));
    p.setBrush(st == TrayState::Connected ? QBrush(QColor(0xF0, 0xF0, 0xF0)) : QBrush(Qt::NoBrush));
    p.drawEllipse(QPointF(16, 16), 9, 9);
    if (unconfig) {
        p.setPen(QPen(stroke, 3));
        p.drawLine(QPointF(7.5, 24.5), QPointF(24.5, 7.5));
    }
    p.end();
    return QIcon(pm);
}

// 人话说明"缺了哪几项"（tooltip / 气泡 / 日志共用一处，避免三处措辞不一致）
QString unconfiguredReason()
{
    QStringList miss;
    if (qEnvironmentVariable("STE_QT_WS_URL").trimmed().isEmpty())
        miss << QStringLiteral("STE_QT_WS_URL（云端地址）");
    if (qEnvironmentVariable("STE_QT_WS_TOKEN").trimmed().isEmpty())
        miss << QStringLiteral("STE_QT_WS_TOKEN（设备令牌）");
    return miss.isEmpty() ? QStringLiteral("配置不完整")
                          : QStringLiteral("缺少 ") + miss.join(QStringLiteral("、"));
}

void updateTray(TrayState st, const QString &detail)
{
    if (!g_tray) return;
    g_tray->setIcon(makeTrayIcon(st));
    const QString uid = g_uid.isEmpty() ? QStringLiteral("(未注册)") : g_uid;
    g_tray->setToolTip(QStringLiteral("星集控 · 被控端\n%1\n%2").arg(uid, detail));
    if (g_actStatus) g_actStatus->setText(QStringLiteral("状态：%1").arg(detail));
}

// 未配置气泡只弹一次（g_unconfiguredNotified 保证不反复弹）。
// **必须在托盘 show() 之后调用**（setVisible 之前弹不出来）；Session0/无托盘时 g_tray 为空、自然跳过，
// 此时靠"图标 + 日志"独立表达未配置。
void notifyUnconfiguredOnce()
{
    if (!g_tray || g_unconfiguredNotified) return;
    g_unconfiguredNotified = true;
    g_tray->showMessage(
        QStringLiteral("星集控 · 未配置"),
        QStringLiteral("本机尚未接入集控：%1。\n右键托盘图标 →「配置…」填写后即可接入（也可手改 agent.env（配置目录，即数据目录））。")
            .arg(unconfiguredReason()),
        QSystemTrayIcon::Warning, 10000);
}

// 进入"未配置"态：告警只报一次，且**必须走 Qt 日志（qWarning）** ——
// 它会经 qInstallMessageHandler → appendLogFile 落盘；fprintf 只进 stderr、不进日志文件，事后排查看不到。
void goUnconfigured()
{
    if (!g_unconfigured) {
        g_unconfigured = true;
        // 用 QDebug 流式而非 qWarning("%s", qPrintable(...))：后者经 toLocal8Bit 会把中文
        // 转成 GBK 本地码页，落盘(UTF-8)后变乱码；流式全程保持 Unicode。
        qWarning().noquote()
            << QStringLiteral("[agent-qt] FAIL: 未配置 —— %1；本机尚未接入集控，不连接云端、不重试。"
                              "右键托盘图标 →「配置…」填写 STE_QT_WS_URL / STE_QT_WS_TOKEN，"
                              "或把 agent.env 放到配置目录后重启。")
                   .arg(unconfiguredReason());
    }
    // 托盘可能尚未创建（启动早期调用）—— updateTray/notify 会在 g_tray 为空时安全跳过。
    updateTray(TrayState::Unconfigured,
               QStringLiteral("未配置：%1（右键托盘 →「配置…」）").arg(unconfiguredReason()));
    notifyUnconfiguredOnce();
}

/* ══════════ 首次运行配置窗（OOBE）（2026-10-04） ══════════
 * 背景：装完第一次打开时，老师面对的是一个托盘图标 + 一个要手改的 agent.env 文本文件，
 * 门槛太高（要认得 `set KEY=VALUE`、要找对目录、要知道令牌从哪来）。
 * 这里给一个界面：三个字段填完点「保存并连接」，写回数据目录的 agent.env 并立即重连。
 */

// 单行清理：去掉可能被粘进来的换行（否则会污染配置文件、注入多余行）。
QString cleanLine(const QString &s)
{
    QString t = s;
    t.remove(QLatin1Char('\r'));
    t.remove(QLatin1Char('\n'));
    return t.trimmed();
}

// agent.env 的路径 —— **唯一真源**：读、写、备份、提示文案全从这里出，别处不许自己拼字符串。
//
// ⚠️ 2026-10-07 从「exe 同目录」改到「数据目录」。原先返回 applicationDirPath()，也就是
// `C:\Program Files\Stelarith\agent.env` —— 那是**程序目录**，不是数据目录，两个后果：
//   ① 教室机以老师/学生账户跑时，Program Files 对普通账户**没有写权限** ⇒ 配置向导点「保存」
//      看着存了（QFile::write 返回 true 前就静默失败），重启又变回「未配置」；
//   ② OTA 换包要决定"备份谁"，备份程序目录里的配置文件本身就是个错的位置。
// 数据目录 %LOCALAPPDATA%\xingjikong\ 与日志/截图/调度/OTA 状态同根 —— 普通账户可写、
// 升级天然保留、换包碰不到。老机器上已有的旧配置由 migrateAgentEnvFromInstallDir() 搬运。
QString agentEnvPath()
{
    static QString cached;                                    // 回退/迁移只算一次
    if (!cached.isEmpty()) return cached;
    const QString dir = qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong");
    if (!QFileInfo::exists(dir) && !QDir().mkpath(dir)) {
        // 连数据目录都建不出来（权限异常/只读盘）⇒ 回退老行为，宁可沿用装目录也别让程序连不上云端
        cached = QCoreApplication::applicationDirPath() + QStringLiteral("/agent.env");
        qWarning() << QStringLiteral("[agent-qt] 数据目录建不出来（%1）—— agent.env 回退到安装目录：%2").arg(dir, cached);
        return cached;
    }
    cached = dir + QStringLiteral("/agent.env");
    return cached;
}

// ── 2026-10-10：OTA 灰度渠道持久化（发现①修复）────────────────────────────
// 渠道原本只由启动环境变量 STE_QT_CHANNEL 指定；但计划任务 action 与 OTA 自更新的
// 重启助手(ota-relaunch.bat)默认不注入该变量 → 一旦重启/升级就拿不到 → 回落 stable，
// 灰度机悄悄掉回稳定通道。这里把「当前生效渠道」落盘到数据目录 channel.txt：
//   启动优先读环境变量 → 其次读 channel.txt → 都没有才 stable。
// 首次以 gray 拉起后，之后任何重启/升级都自动沿用 gray（main 启动时 persistChannel 写回）。
// ⚠️ 这里**不能**依赖 agentEnvPath()（它在文件更后面定义），直接拼数据目录。
QString channelFilePath()
{
    return qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong/channel.txt");
}

QString resolvedChannel()
{
    const QString env = qEnvironmentVariable("STE_QT_CHANNEL").trimmed();
    if (!env.isEmpty()) return env;
    QFile f(channelFilePath());
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString v = QString::fromUtf8(f.readAll()).trimmed();
        f.close();
        if (!v.isEmpty()) return v;
    }
    return QStringLiteral("stable");
}

// 把当前生效渠道写回 channel.txt（幂等：内容相同则不动盘）
void persistChannel(const QString &ch)
{
    const QString p = channelFilePath();
    QFile f(p);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString cur = QString::fromUtf8(f.readAll()).trimmed();
        f.close();
        if (cur == ch) return;
    }
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        f.write(ch.toUtf8());
        f.write("\n");
        f.close();
    }
}

/**
 * 老版本（≤0.6.22）把 agent.env 写在 exe 同目录，这里在**首次启动时**把它搬到数据目录。
 * 三条硬规矩：
 *   ① 幂等 —— 目标位置已经在（新装/已搬过）就直接返回，绝不重复搬；
 *   ② **只复制、不删除**原文件 —— 装机面板生成的脚本、老师手改的副本可能还指着那个路径，
 *      删掉就等于凭空制造一台"配置丢了"的机器；
 *   ③ 搬不动就当没这回事（老位置照样读得到），绝不让迁移失败把启动挡在门外。
 */
void migrateAgentEnvFromInstallDir()
{
    const QString dst = agentEnvPath();
    if (QFileInfo::exists(dst)) return;   // ① 幂等：已经是新位置
    const QString src = QCoreApplication::applicationDirPath() + QStringLiteral("/agent.env");
    if (!QFileInfo::exists(src)) return;  // 全新装机 / 本来就没配过：没什么可搬
    if (!QFile::copy(src, dst)) return;   // ③ 失败不计，老位置仍可正常读到
    qInfo().noquote() << QStringLiteral("[agent-qt] 已把旧的 agent.env 搬进数据目录 → %1（原文件保留不动）").arg(dst);
}

/**
 * 把界面上的配置写回 agent.env（数据目录），并**直接更新进程环境变量**。
 *
 * ⚠️ 关键坑：写完**绝不能**指望 loadEnvFile() 把新值刷进进程。
 * loadEnvFile 的规则是"真环境变量优先、不覆盖"（`if (!qgetenv(key).isEmpty()) continue;`）。
 * 进程里 STE_QT_WS_URL/TOKEN 很可能已经存在（哪怕值是旧的），于是保存后 loadEnvFile 会
 * 原封不动保留旧值 —— 表现得像"保存成功，但连的、注册的还是老配置"这个静默陷阱。
 * 所以这里用 qputenv 把新值**直接覆盖**（qputenv 会替换同名变量）。
 *
 * 编码：UTF-8 无 BOM。纯 ASCII 输入 → 文件字节级就是纯 ASCII（读端三种编码都认）。
 * 格式：`set KEY=VALUE`（不带引号），与 deploy/agent.env、读端解析器一致。
 * @returns 文件是否写入成功
 */
bool saveAgentEnv(const QString &url, const QString &token, const QString &uid, const QString &name)
{
    // 4~5 行：1 行 ASCII 注释 + 3 个必需键（STE_QT_UID/NAME 留空也写一行，便于人看/手改）
    // 设备显示名（STE_QT_NAME）若不显式给，则保留进程内已有的（OAuth 绑定一般不改显示名，
    // 避免每次重绑把用户在手动表单里设的名字冲掉）；显式给则覆盖。
    const QString existingName = qEnvironmentVariable("STE_QT_NAME").trimmed();
    const QString finalName = name.trimmed().isEmpty() ? existingName : name.trimmed();
    QString text;
    text += QStringLiteral("rem Stelarith control agent - local config (auto-generated, safe to edit)\r\n");
    text += QStringLiteral("set STE_QT_WS_URL=") + url + QStringLiteral("\r\n");
    text += QStringLiteral("set STE_QT_WS_TOKEN=") + token + QStringLiteral("\r\n");
    text += QStringLiteral("set STE_QT_UID=") + uid + QStringLiteral("\r\n");
    if (!finalName.isEmpty())
        text += QStringLiteral("set STE_QT_NAME=") + finalName + QStringLiteral("\r\n");

    QFile f(agentEnvPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning().noquote() << QStringLiteral("[agent-qt] FAIL: 无法写入配置 %1 —— %2")
                                    .arg(agentEnvPath(), f.errorString());
        return false;
    }
    f.write(text.toUtf8());   // UTF-8 无 BOM
    f.close();

    // 直接覆盖进程内环境变量（不管之前有没有旧值），这样保存即刻生效、无需重启。
    qputenv("STE_QT_WS_URL", url.toUtf8());
    qputenv("STE_QT_WS_TOKEN", token.toUtf8());
    qputenv("STE_QT_UID", uid.toUtf8());   // 空值=清除，sendRegister 会回退到电脑名
    if (!finalName.isEmpty()) qputenv("STE_QT_NAME", finalName.toUtf8());

    qInfo().noquote() << QStringLiteral("[agent-qt] 已保存配置 → %1（%2 行，UTF-8 无 BOM）")
                         .arg(agentEnvPath()).arg(finalName.isEmpty() ? 4 : 5);
    return true;
}

// 2026-10-08：班级码持久化（独立于 agent.env 的 4 行格式，单独存一个文件，避免破坏既有解析）。
static QString classCodePath()
{
    return qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong/classcode.txt");
}
static void saveClassCode(const QString &code)
{
    if (code.isEmpty()) return;
    g_classCode = code.trimmed();
    QFile f(classCodePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(g_classCode.toUtf8());
        f.close();
        qInfo().noquote() << QStringLiteral("[agent-qt] 已保存班级码 → %1").arg(classCodePath());
    } else {
        qWarning().noquote() << QStringLiteral("[agent-qt] ⚠️ 无法保存班级码：%1").arg(f.errorString());
    }
}
static void loadClassCode()
{
    QFile f(classCodePath());
    if (f.open(QIODevice::ReadOnly)) {
        const QString code = QString::fromUtf8(f.readAll()).trimmed();
        f.close();
        if (!code.isEmpty()) { g_classCode = code; qInfo().noquote() << QStringLiteral("[agent-qt] 已载入班级码 %1").arg(code); }
    }
}

// 2026-10-09：班级显示名持久化（与 classCode 分开存；同款理由：不动 agent.env 的 4 行格式）。
static QString classNamePath()
{
    return qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong/classname.txt");
}
static void saveClassName(const QString &name)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) return;   // 空值不覆盖已有（站点没回名时别把旧的抹掉）
    g_className = n;
    QFile f(classNamePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(g_className.toUtf8());
        f.close();
    } else {
        qWarning().noquote() << QStringLiteral("[agent-qt] ⚠️ 无法保存班级名：%1").arg(f.errorString());
    }
}
static void loadClassName()
{
    QFile f(classNamePath());
    if (f.open(QIODevice::ReadOnly)) {
        const QString name = QString::fromUtf8(f.readAll()).trimmed();
        f.close();
        if (!name.isEmpty()) { g_className = name; qInfo().noquote() << QStringLiteral("[agent-qt] 已载入班级名 %1").arg(name); }
    }
}

/* ══════════ 班级绑定（2026-10-04：OOBE 里消费激活码，把本机绑到班级） ══════════
 * 背景：管理员生成一次性、绑班级的接入码（XJK-XXXX-XXX）；教室端在配置向导里
 * 填进去，保存时调站点 `POST /api/device/activate?code=…&uid=…` 消费，
 * 站点落库 device_bindings（uid→class）。这就是"被控端绑定界面"。
 * 站点地址从云端 ws 地址推导：ws://host:8788/ws/agent → http://host:8090
 * （站点与云端同机部署；端口默认 8090，可用 STE_QT_SITE_PORT 覆盖）。
 * 消费失败不阻断保存——绑定是可选项，配错了以后还能在面板改。
 */

// 云端 ws 地址 → 站点 http 基址（nullptr = 推导不出）
// 站点（website）地址的**唯一真源**（2026-10-06 加）。
//
// 为什么必须有它：以前站点地址是从云端 ws 地址**推导**出来的
// （ws://host:8788/ws/agent → http://host:8090）。在生产部署下这条推导两样都错：
//   ① 域名错：云端在 control.245959623.xyz，站点在 www.245959623.xyz，不是同一个子域；
//   ② 端口错：站点公网走 443（Cloudflare 回源），8090 是内网端口，公网根本不通。
// 实测两条地址的差别（这是"被控端连不上 website"的根因）：
//   https://control.245959623.xyz:8090/api/device/activate  → 超时，连不上
//   https://www.245959623.xyz/api/device/activate           → HTTP 400（通了，400 只是没带参数）
// 站点是被控端绑定班级 / 走账号体系的入口，地址必须是一等配置，不能靠猜。
// 自建站点（别的学校自己搭）请显式设 STE_QT_SITE_URL 覆盖。
static constexpr const char *kDefaultSiteUrl = "https://www.245959623.xyz";

/** 内网自建场景判断：只有这种时候"站点与云端同机"才成立，才允许推导。 */
static bool isPrivateHost(const QString &host)
{
    if (host.isEmpty()) return false;
    if (host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0) return true;
    if (host == QLatin1String("127.0.0.1") || host == QLatin1String("::1")) return true;
    // 172.16.0.0–172.31.255.255 才是私网；这里只做前缀粗判（内网自建场景够用），
    // 公网 IP 撞 172. 开头的情况由"显式设 STE_QT_SITE_URL"兜住。
    return host.startsWith(QLatin1String("10."))
           || host.startsWith(QLatin1String("192.168."))
           || host.startsWith(QLatin1String("172."));
}

QString siteBaseFromWsUrl(const QString &wsUrl)
{
    // ① 显式配置优先（自建站点 / 本机联调）
    const QString envSite = qEnvironmentVariable("STE_QT_SITE_URL").trimmed();
    if (!envSite.isEmpty()) return envSite;

    // ② 云端地址指向**内网**时，"站点与云端同机"这个前提才成立，允许推导
    //    （且不拼 8090：自建场景站点端口用 STE_QT_SITE_PORT 显式给，不给就走默认 443/80）
    const QUrl u(wsUrl.trimmed());
    if (u.isValid() && !u.host().isEmpty() && isPrivateHost(u.host())) {
        const QString port = qEnvironmentVariable("STE_QT_SITE_PORT").trimmed();
        const QString scheme = (u.scheme().compare(QStringLiteral("wss"), Qt::CaseInsensitive) == 0)
                                   ? QStringLiteral("https") : QStringLiteral("http");
        return port.isEmpty() ? QStringLiteral("%1://%2").arg(scheme, u.host())
                              : QStringLiteral("%1://%2:%3").arg(scheme, u.host(), port);
    }

    // ③ 其余一律用生产站点常量 —— 公网部署下推导出来的地址必然是错的
    return QString::fromUtf8(kDefaultSiteUrl);
}

// 消费接入码（异步）。回调里报结果；本机 uid 用于绑定。
void consumeActivationCodeAsync(const QString &siteBase, const QString &code, const QString &uid)
{
    auto *mgr = new QNetworkAccessManager();
    QUrl url(siteBase + QStringLiteral("/api/device/activate"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("code"), code.trimmed());
    q.addQueryItem(QStringLiteral("uid"), uid.trimmed().isEmpty() ? QHostInfo::localHostName() : uid.trimmed());
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    // POST 空 body；消费端点同时接受 query 与 body（query 优先）
    QNetworkReply *reply = mgr->post(req, QByteArray("{}"));

    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, mgr] {
        reply->deleteLater();
        mgr->deleteLater();
        const QByteArray body = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            // 成功：{"ok":true,"classId":"…","className":"…","classCode":"…","uid":"…"}
            QJsonParseError pe;
            const QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
            const QJsonObject o = (pe.error == QJsonParseError::NoError) ? doc.object() : QJsonObject();
            const QString cls = o.value(QStringLiteral("className")).toString().trimmed();
            // 2026-10-09：把**站点权威班级码**回存，替掉先前存进去的一次性接入码 ——
            //   否则云端班级索引的键会是那串 XJK-xxxx-xxx（不稳定、重发还会换新）。
            //   站点回 classCode（真源 class_id，如 class_01）；老站点不回则回退 classId。
            QString authoritative = o.value(QStringLiteral("classCode")).toString().trimmed();
            if (authoritative.isEmpty()) authoritative = o.value(QStringLiteral("classId")).toString().trimmed();
            const bool changed = !authoritative.isEmpty() && authoritative != g_classCode;
            if (changed) saveClassCode(authoritative);
            if (!cls.isEmpty()) saveClassName(cls);
            qInfo().noquote() << QStringLiteral("[agent-qt] ✅ 已绑定班级：%1（班级码 %2）")
                                     .arg(cls.isEmpty() ? QStringLiteral("（返回无班级名）") : cls,
                                          authoritative.isEmpty() ? QStringLiteral("（未回）") : authoritative);
            // 权威码/名换过 → 补发一次 register，让云端立刻改掉键与显示名（不必等下次重连）。
            if ((changed || !cls.isEmpty()) && g_ws
                && g_ws->state() == QAbstractSocket::ConnectedState) {
                sendRegister();
                qInfo().noquote() << QStringLiteral("[agent-qt] 已补发 register（带上权威班级码/名）");
            }
        } else {
            // 失败：404 码无效/过期；403 密钥错；其它
            qWarning().noquote() << QStringLiteral("[agent-qt] ⚠️ 班级绑定失败（HTTP %1）：%2")
                                        .arg(status).arg(QString::fromUtf8(body).trimmed().left(200));
        }
    });
}

// 班级绑定的**唯一入口**（2026-10-09 抽出来）。
// 为什么抽：这段逻辑原来只长在「保存并连接」按钮里，于是**「用星璃账号绑定」这条路压根不走班级绑定**
// —— 用户填了班级码、点了账号绑定，班级就是不绑（本轮补的缺口）。两条路各写一遍必然漂移，
// 这就是第一次漂移的实例，所以只留一份实现。
//
// 一个输入框吃两种取值，不让用户去分辨自己手里是哪一种：
//   · **班级码**（云端班级索引的键，6-16 位字母数字或 -_）→ 直接持久化，
//     下次 register 带上 classCode → 云端按班归组（云端 registry.js 的班级索引）。
//   · **接入码**（XJK-XXXX-XXX，管理员生成、一次性、带 TTL）→ 先持久化 + 调站点
//     `/api/device/activate` 消费；站点回喂**权威班级码**（classCode = 真源 class_id）与
//     班级名，回调里回存并补发 register，替掉先前那串一次性接入码。
//     ⚠️ 站点 `stelarith_classes.code` 存的是**中文名**（如"1班"），不合云端键格式，
//        所以权威键取 `class_id`（`class_01`），显示名走 className（2026-10-09 三端贯通）。
static void applyClassBinding(const QString &rawCode, const QString &wsUrl, const QString &uid)
{
    const QString code = rawCode.trimmed();
    if (code.isEmpty()) return;

    saveClassCode(code);   // 持久化 → 下一次 register 带上 classCode

    // 只有接入码需要去站点消费；班级码没有可消费的东西（站点也不认识它）。
    if (!code.startsWith(QStringLiteral("XJK-"), Qt::CaseInsensitive)) {
        qInfo().noquote() << QStringLiteral("[agent-qt] 已记录班级码 %1（register 时带给云端按班归组）").arg(code);
        return;
    }

    const QString siteBase = siteBaseFromWsUrl(wsUrl);
    if (siteBase.isEmpty()) {
        qWarning().noquote() << QStringLiteral("[agent-qt] ⚠️ 无法从云端地址推导站点地址，跳过接入码消费（班级码已存，register 仍会带给云端）");
        return;
    }
    const QString u = uid.trimmed().isEmpty() ? QHostInfo::localHostName() : uid.trimmed();
    qInfo().noquote() << QStringLiteral("[agent-qt] 绑定接入码 → %1/api/device/activate (uid=%2)").arg(siteBase, u);
    consumeActivationCodeAsync(siteBase, code, u);
}

// 打开"配置向导"。首运行（未配置）自动调一次；托盘菜单「配置…」也走它。
// 保存成功 → 立即用新配置重连；点「以后再说」→ 原样保持未配置态（不丢可见告警）。
/* ══════════════════════════════════════════════════════════════════════════
 * 升级提示 + 设置与信息面板（2026-10-06）
 *
 * 「谁告诉本机有新版本」：云端在 registered 回执里带 latestVersion / updateUrl /
 * updateSha256（清单来自云端磁盘上的 ota.json）。没发布过版本时这些字段是 null ——
 * 那是**"未知"**，不是"已是最新"，界面上必须分得清，否则运维会以为升级链路是好的。
 *
 * 本机只做三件事：① 比版本号 ② 提示（托盘气泡 + 菜单里多一条 + 面板里一行）
 * ③ 用户点「立即升级」时**复用已有的 self_update 链路**（下载 → sha256 校验 →
 *   重启助手 → 静默安装），不另写第二套升级逻辑。
 * ══════════════════════════════════════════════════════════════════════════ */

QString g_latestVer, g_updateUrl, g_updateSha, g_updateNotes;
bool g_updateChecked = false;       // 有没有从云端拿到过版本信息
bool g_updateMandatory = false;     // 2026-10-06：云端 ota.json 的 mandatory（true=强制自动升级）
qint64 g_lastOnlineAt = 0;          // 最近一次连上云端（强制升级回滚判定用：从没连通就不许误判成"新版本坏了"）
QAction *g_actUpdate = nullptr;     // 托盘里"发现新版本…"那一条（无更新时隐藏）

/** 比版本号（semver 子集，2026-10-07 对齐 DeepSeek Harness 同款 X.Y.Z[-(alpha|rc).N]）：
 *  a>b → 1，a<b → -1，相等 → 0。
 *  ① 先比核心三段 X.Y.Z 的数字（缺段补 0 —— 0.10.0 > 0.9.9 这种字符串比会反的地方）；
 *  ② 核心打平后，先比**阶段**："alpha" < "rc" < 正式版（跟 semver 的预发布标识符一致，
 *     阶段名不同就不再比号 —— alpha.5 也小于 rc.1）；
 *  ③ 同核心同阶段才比号（rc.2 > rc.1、alpha.3 > alpha.1）。
 *  例：0.6.23-alpha.1 < 0.6.23-rc.1 < 0.6.23；0.6.23-rc.1 > 0.6.22；0.6.23 < 0.6.24-rc.1。
 *  兼容：非本标准的老后缀（"0.4.0-v1"、"-beta"）照旧当空气，行为跟以前一致。
 *  这套语义的参考实现和断言在 scripts/test-version-naming.mjs（工作区根）—— 改这里之前
 *  先跑一遍那个脚本，它挂了就说明两边语义分叉了。
 *
 *  为什么必须认后缀：候选版也要能被 OTA 识别成「有更新」。旧实现把后缀当空气，
 *  0.6.23 与 0.6.23-rc.1 会判成相等 ⇒ 托盘的「发现新版本」整段失灵。 */
int compareVersion(const QString &a, const QString &b)
{
    // 拆成「核心段」+「阶段 + 阶段号」：阶段是 alpha / rc，号是 -(alpha|rc).N 里的 N；
    // 没有合法后缀（含 -v1/-beta 这些老后缀）时 rank 保持 RANK_RELEASE、号 = -1，下面当"正式版"处理。
    // rank：alpha=0 < rc=1 < 正式=2（同 semver 的 stage 序，正式版最大）。
    enum { RANK_ALPHA = 0, RANK_RC = 1, RANK_RELEASE = 2 };
    auto parse = [](const QString &s, int *rankOut, int *numOut) -> QStringList {
        QString t = s.trimmed();
        // ⚠️ 无阶段的初值必须是 RANK_RELEASE(2)，不能图省事写 0 —— 0 是 RANK_ALPHA。
        // 踩过：初值 0 + parse 里写 -1 两套并存，核心打平后「正式版 < 候选版」，
        // 于是 rc 转正发正式版时 compareVersion(云端, 本机) 判成 -1 ⇒ 托盘不提示升级（B1/B2/B17 三条断言抓的）。
        *rankOut = RANK_RELEASE;
        *numOut = -1;
        const int dash = t.indexOf(QLatin1Char('-'));
        if (dash > 0) {
            const QString suf = t.mid(dash + 1);                 // 形如 "rc.3" / "alpha.2"
            int dot = suf.indexOf(QLatin1Char('.'));
            if (dot > 0) {
                const QString name = suf.left(dot);
                const QString num = suf.mid(dot + 1);
                bool ok = false;
                const int n = num.toInt(&ok);
                if (ok && n >= 1) {
                    if (name == QLatin1String("alpha")) *rankOut = RANK_ALPHA;
                    else if (name == QLatin1String("rc")) *rankOut = RANK_RC;
                    if (*rankOut >= 0) *numOut = n;
                }
            }
            t = t.left(dash);                                    // 后缀摘掉，核心段交给下面比
        }
        return t.split(QLatin1Char('.'));
    };
    auto seg = [](const QStringList &parts, int i) -> int {
        if (i >= parts.size()) return 0;                          // 段数不够 → 补 0
        int n = 0; bool got = false;
        for (const QChar c : parts.at(i)) {
            if (c.isDigit()) { n = n * 10 + c.digitValue(); got = true; }
            else if (got) break;                                  // 数字之后的东西（如 "-beta"）忽略
        }
        return got ? n : 0;
    };

    int ra = RANK_RELEASE, rb = RANK_RELEASE, na = -1, nb = -1;
    const QStringList sa = parse(a, &ra, &na), sb = parse(b, &rb, &nb);
    for (int i = 0; i < 3; ++i) {
        const int x = seg(sa, i), y = seg(sb, i);
        if (x != y) return x > y ? 1 : -1;
    }
    // 核心打平：先分阶段（alpha < rc < 正式），同阶段再分号。
    if (ra != rb) return ra > rb ? 1 : -1;
    if (na < 0 || nb < 0) return 0;
    return na == nb ? 0 : (na > nb ? 1 : -1);
}

// 这两个定义在下文（强制升级段 / OTA 段）；这里提前声明好让"发现新版本"直接触发自动升级。
static void startSelfUpdate(const QString &id, const QJsonObject &params);
static void maybeAutoUpdate();

/** 消化云端给的升级信息：记下来 + 该提示就提示 + 该自动装就自动装。 */
void applyUpdateInfo(const QJsonObject &pay)
{
    g_latestVer   = pay.value(QStringLiteral("latestVersion")).toString().trimmed();
    g_updateUrl   = pay.value(QStringLiteral("updateUrl")).toString().trimmed();
    g_updateSha   = pay.value(QStringLiteral("updateSha256")).toString().trimmed();
    g_updateNotes = pay.value(QStringLiteral("updateNotes")).toString().trimmed();
    g_updateChecked = true;
    // mandatory 取值三种：true / false / null（没发布过版本）。
    // 只有 **true** 才算"云端要求强制升级"—— null 是"未知"，绝不能当成强制。
    g_updateMandatory = pay.value(QStringLiteral("updateMandatory")).toBool();

    if (g_latestVer.isEmpty()) {
        // 如实说：云端还没发布过版本（ota.json 不存在）。这不是"已是最新"。
        qInfo("[agent-qt] 升级检查：云端尚未发布过版本（无 ota.json）→ 本机 v%s 继续跑", kAppVersion);
        return;
    }
    if (compareVersion(g_latestVer, QString::fromLatin1(kAppVersion)) > 0) {
        const bool canUpgrade = !g_updateUrl.isEmpty() && g_updateSha.length() == 64;
        if (g_updateMandatory) {
            qInfo("[agent-qt] ⬆ 云端 v%s 标注为**强制升级**（本机 v%s，可升=%s）—— 到点自动装，不用老师点",
                  qPrintable(g_latestVer), kAppVersion, canUpgrade ? "yes" : "no");
            // 延迟 8 秒再动手：让 registered 握手彻底收尾，别在云端还在发别的东西时抢带宽。
            QTimer::singleShot(8000, [] { maybeAutoUpdate(); });
        } else {
            qInfo("[agent-qt] ⬆ 发现新版本 v%s（本机 v%s）%s —— 右键托盘 →「设置与信息…」可升级",
                  qPrintable(g_latestVer), kAppVersion,
                  canUpgrade ? "" : "（⚠ 清单缺 url/sha256，先别升，让管理员补 ota.json）");
        }
        if (g_actUpdate) {
            g_actUpdate->setVisible(true);
            g_actUpdate->setText(g_updateMandatory
                                     ? QStringLiteral("将自动升级到 v%1（云端强制）").arg(g_latestVer)
                                     : QStringLiteral("发现新版本 v%1（点此查看）").arg(g_latestVer));
        }
        if (g_tray) {
            g_tray->showMessage(
                QStringLiteral("星集控 · 被控端"),
                g_updateMandatory
                    ? QStringLiteral("云端要求升级到 v%1（当前 v%2）。\n这台机器会在联网后自动安装，不用手动点——"
                                     "装的过程中被控端会短暂重启，一两分钟内回来。")
                          .arg(g_latestVer, QString::fromLatin1(kAppVersion))
                    : QStringLiteral("有可用的新版本 v%1（当前 v%2）。\n右键托盘图标 →「设置与信息…」即可升级。")
                          .arg(g_latestVer, QString::fromLatin1(kAppVersion)),
                QSystemTrayIcon::Information, 12000);
        }
    } else {
        qInfo("[agent-qt] 升级检查：已是最新（云端 v%s，本机 v%s）",
              qPrintable(g_latestVer), kAppVersion);
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * 强制自动升级（2026-10-06）
 *
 * 以前的行为：云端标 mandatory 也没用 —— 只有托盘提示 + 面板里一个「立即升级」按钮，
 * 必须有人在教室机右键点一下才装。**全校教室机没人点 = 永远升不上来**，
 * 于是 mandatory 只是一个好看的开关。现在补齐成一条完整链路：
 *
 *   云端 ota.json.mandatory=true
 *     → register 回执带 updateMandatory
 *     → 本机判定"到点了"就**自己下载、自己校验、自己静默装**（老师全程不用碰）
 *     → 装完新版本自己起来；起不来（或起来注册不上）就把升级前的旧 exe 放回去
 *
 * 三道保命设计（缺一道都不敢让机器无人值守换自己）：
 *   ① **退避**：失败一次等 3 分钟，再失败 15 分钟 / 1 小时 / 4 小时 / 12 小时封顶。
 *      网络抖一下不至于半夜反复拉包。
 *   ② **回滚**：装之前先备一份当前 exe（留在数据目录，不随安装目录被覆盖）+
 *      写 pending 标记 + 删掉"最近一次启动成功"标记。
 *   ③ **看门狗**：回滚判定写在**升级前生成**的 ota-relaunch.bat 里 ——
 *      它体内是**旧版本的字节**，所以哪怕新 exe 一启动就崩，狗还在，照样能把机器救回来。
 *      （只看 agent 自己是不行的：新 exe 崩了，它自己也已经没了。）
 *
 * 判定"新版本起没起来"只看一件事：**有没有连上云端注册成功**（register 成功即写 ota-last-ok.txt）。
 * 网络本来就断的机器不算新版本坏了 —— 见过不到云端的机器被误判回滚，白降一版。
 * 所以 `g_lastOnlineAt == 0`（从没连通过）时永不回滚。
 *
 * 紧急刹车（强制升级最要紧的东西）：数据目录里放一个 `ota-pause`（空文件）即暂停自动升级。
 * 某个版本真把机器搞挂时，运维能不卸包、不重编，往机器上扔一个文件就止血。
 * ══════════════════════════════════════════════════════════════════════════ */

// 查"上一个下载任务还在不在跑"。**不能用变量声明**：g_otaBusy 的真身在后头的匿名 namespace
// 里，在前面再来一次同名的 `static bool g_otaBusy;` 会被判成两个实体（C2086 重定义）。
// 用一个函数绕开作用域——函数声明不引入变量实体。
static bool otaIsBusy();

// 回滚判定窗口：升级后新版本这么多秒内没注册成功 → 回滚
static const int kOtaRollbackMs = 240 * 1000;
// 装之前要求的数据盘余量（安装包 ~70MB，取 4 倍冗余：下载 + NSIS 解压 + 备份）
static const qint64 kOtaMinFreeBytes = 400LL * 1024 * 1024;

// ⚠️ 全部落在**数据目录**，不落在安装目录：NSIS 装完会换安装目录里的文件，
//    放这儿的备份和标记才不会被一起换掉（2026-10-06 亲测这个坑：放安装目录=没有备份）。
static QString otaBaseDir()
{
    static QString cached;
    if (cached.isEmpty())
        cached = qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong/ota");
    return cached;
}
static QString otaStatePath()   { return otaBaseDir() + QStringLiteral("/ota-state.json"); }
static QString otaOkPath()      { return otaBaseDir() + QStringLiteral("/ota-last-ok.txt"); }
static QString otaPrevPath()    { return otaBaseDir() + QStringLiteral("/prev/stelarith-agent-qt.exe"); }
static QString otaPendingPath() { return otaBaseDir() + QStringLiteral("/ota-pending.json"); }
static QString otaPausePath()   { return otaBaseDir() + QStringLiteral("/ota-pause"); }

static QJsonObject otaLoadState()
{
    QJsonObject s;
    QFile f(otaStatePath());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument d = QJsonDocument::fromJson(f.readAll());
        if (d.isObject()) s = d.object();
    }
    return s;
}

static void otaSaveState(const QJsonObject &s)
{
    QDir().mkpath(otaBaseDir());
    QFile f(otaStatePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("[agent-qt] ⚠ 写不下 ota-state.json（%s）—— 重试退避会退化成每次都装", qPrintable(otaStatePath()));
        return;
    }
    f.write(QJsonDocument(s).toJson(QJsonDocument::Indented));
    f.flush();
}

/** 下一次尝试前要等多久：失败越多等越久（3 分 → 15 分 → 1 时 → 4 时 → 12 时封顶）。 */
static qint64 otaRetryDelayMs(int failCount)
{
    static const qint64 table[] = {
        3  * 60 * 1000LL,   // 第 1 次失败：网络抖一下，三分钟后再说
        15 * 60 * 1000LL,   // 第 2 次：短时间重来也没用，等一刻钟
        1  * 60 * 60 * 1000LL,
        4  * 60 * 60 * 1000LL,
        12 * 60 * 60 * 1000LL   // 封顶：一整天最多闹这么几次
    };
    if (failCount <= 0) return 0;
    return table[failCount - 1 > 4 ? 4 : failCount - 1];
}

/** 记一次失败：把"下次最早几点能试"一起算好写进状态文件。 */
static void otaNoteFailure(const QString &why)
{
    QJsonObject st = otaLoadState();
    const int n = st.value(QStringLiteral("failCount")).toInt(0) + 1;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    st.insert(QStringLiteral("failCount"), n);
    st.insert(QStringLiteral("lastResult"), QStringLiteral("failed"));
    st.insert(QStringLiteral("lastError"), why);
    st.insert(QStringLiteral("lastVer"), g_latestVer);
    st.insert(QStringLiteral("lastAt"), (double)now);
    st.insert(QStringLiteral("nextAt"), (double)(now + otaRetryDelayMs(n)));
    otaSaveState(st);
    qWarning("[agent-qt] ⏳ 自动升级失败（连续 %d 次：%s）→ %lld 分钟后再试",
             n, qPrintable(why), otaRetryDelayMs(n) / 60000LL);
}

/** 记一次成功：清空失败计数（下次有网就立刻再试，不用再等退避）。 */
static void otaNoteSuccess()
{
    QJsonObject st = otaLoadState();
    st.insert(QStringLiteral("failCount"), 0);
    st.insert(QStringLiteral("lastResult"), QStringLiteral("ok"));
    st.insert(QStringLiteral("lastVer"), QString::fromLatin1(kAppVersion));
    st.insert(QStringLiteral("lastAt"), (double)QDateTime::currentMSecsSinceEpoch());
    st.insert(QStringLiteral("nextAt"), (double)0);
    otaSaveState(st);
}

static bool otaPaused()
{
    return QFile::exists(otaPausePath());
}

/** 备份当前 exe 到数据目录（回滚的命根子）。失败返回空串 —— 见调用处宁可不装。 */
static QString otaBackupExe()
{
    const QString src = QCoreApplication::applicationFilePath();
    const QString dst = otaPrevPath();
    QDir().mkpath(QFileInfo(dst).absolutePath());
    QFile::remove(dst);
    if (!QFile::copy(src, dst)) {
        qWarning("[agent-qt] ⚠ 备份当前 exe 失败（%s → %s）", qPrintable(src), qPrintable(dst));
        return QString();
    }
    if (QFileInfo(dst).size() != QFileInfo(src).size()) {
        qWarning("[agent-qt] ⚠ 备份大小对不上（%lld vs %lld），不认这个备份",
                 (long long)QFileInfo(dst).size(), (long long)QFileInfo(src).size());
        QFile::remove(dst);
        return QString();
    }
    // 备份是给回滚用的，权限要跟原件一致；设不上也不致命（安装器以管理员身份覆盖时会重设）。
    QFile::setPermissions(dst, QFile::ReadOwner | QFile::WriteOwner);
    qInfo("[agent-qt] 💾 已备份当前 exe（%lld 字节）用于回滚 → %s",
          (long long)QFileInfo(dst).size(), qPrintable(dst));
    return dst;
}

static void otaWritePending(const QString &prevExe, const QString &targetVer)
{
    QJsonObject pend;
    pend.insert(QStringLiteral("target"), targetVer);
    pend.insert(QStringLiteral("prevExe"), prevExe);
    pend.insert(QStringLiteral("ts"), (double)QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(otaBaseDir());
    QFile f(otaPendingPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("[agent-qt] ⚠ 写不下 pending 标记（%s）—— 看门狗分不清新旧版本", qPrintable(otaPendingPath()));
        return;
    }
    f.write(QJsonDocument(pend).toJson(QJsonDocument::Indented));
    f.flush();
}

/** 新版本起来并注册成功 → 标记 + 清掉 pending（清不掉就会被人造的启动失败误回滚）。 */
static void otaMarkStartedOk()
{
    QDir().mkpath(otaBaseDir());
    QFile f(otaOkPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QStringLiteral("v%1 %2\n")
                    .arg(QString::fromLatin1(kAppVersion),
                         QDateTime::currentDateTime().toString(Qt::ISODate))
                    .toUtf8());
        f.flush();
    }
    if (QFile::exists(otaPendingPath())) {
        QFile::remove(otaPendingPath());
        qInfo("[agent-qt] ✅ 新版本 v%s 已连上云端，本次升级确认成功（回滚标记已清）", kAppVersion);
    }
    otaNoteSuccess();
}

/** 回滚：把旧 exe 放回去并拉起来。当前进程（新版本）随后退出。 */
static void otaRollback()
{
    const QString prev = otaPrevPath();
    const QString exe  = QCoreApplication::applicationFilePath();
    if (!QFile::exists(prev)) {
        qWarning("[agent-qt] ⚠ 回滚备份不在（%s）—— 只能让这台机器保持现状，等下次升级 retry", qPrintable(prev));
        QFile::remove(otaPendingPath());
        otaNoteFailure(QStringLiteral("回滚备份不存在"));
        return;
    }
    if (QFileInfo(prev).size() == 0) {
        qWarning("[agent-qt] ⚠ 回滚备份是 0 字节（%s），拒绝用空壳覆盖", qPrintable(prev));
        QFile::remove(otaPendingPath());
        otaNoteFailure(QStringLiteral("回滚备份为 0 字节"));
        return;
    }
    qWarning("[agent-qt] 🔙 回滚：用 %s 覆盖 %s，再把它拉起来", qPrintable(prev), qPrintable(exe));
    // 覆盖自己正在跑的 exe 一定失败（文件被占），所以交出一个独立 cmd 去干：
    // 杀进程 → 等 3 秒（自己也退干净）→ 拷回旧版 → 清 pending → 拉起旧版。
    const QString cmd = QStringLiteral(
        "taskkill /F /IM stelarith-agent-qt.exe >nul 2>&1 && "
        "ping -n 4 127.0.0.1 >nul && "
        "copy /Y \"%1\" \"%2\" >nul && "
        "del /f /q \"%3\" && "
        "start \"\" \"%2\"");
    QProcess::startDetached(QStringLiteral("cmd.exe"),
                            {QStringLiteral("/c"), cmd.arg(prev, exe, otaPendingPath())});
    otaNoteFailure(QStringLiteral("新版本未能在 %d 秒内连上云端，已回滚").arg(kOtaRollbackMs / 1000));
    QTimer::singleShot(1500, [] { QCoreApplication::quit(); });   // 让位给刚拉起来的旧版
}

/** 该不该现在自动装：全部满足才动手，任何一条不满足都**如实说为什么**不装。 */
static void maybeAutoUpdate()
{
    if (!g_updateMandatory) return;                       // ① 只有云端明确要求强制才自动装
    if (g_unconfigured) {                                 // ② 没配好就别自作主张（升完照样连不上）
        qInfo("[agent-qt] 强制升级跳过：本机还没配好云端（未配置）");
        return;
    }
    if (otaIsBusy()) {                                    // ③ 上一次下载还没完
        qInfo("[agent-qt] 强制升级跳过：上一个更新任务还在跑");
        return;
    }
    if (g_updateUrl.isEmpty() || g_updateSha.length() != 64) {   // ④ 清单不完整 = 不许升
        qWarning("[agent-qt] 强制升级跳过：清单缺 url 或 sha256（不让管理员补 ota.json 就别硬升）");
        return;
    }
    if (compareVersion(g_latestVer, QString::fromLatin1(kAppVersion)) <= 0) return;   // ⑤ 已经是最新

    if (otaPaused()) {                                    // ⑥ 人工紧急刹车
        qWarning("[agent-qt] ⛔ 强制升级已在本机暂停（存在 %s）—— 管理员排查用，删掉这个文件即恢复",
                 qPrintable(otaPausePath()));
        return;
    }

    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const qint64 nextAt = otaLoadState().value(QStringLiteral("nextAt")).toVariant().toLongLong();
        if (nextAt > now) {
            qInfo("[agent-qt] ⏳ 强制升级还没到点：%lld 秒后再试（上次结果 %s）",
                  (nextAt - now) / 1000, qPrintable(otaLoadState().value(QStringLiteral("lastError")).toString()));
            return;
        }
    }
    {
        QStorageInfo si(qEnvironmentVariable("LOCALAPPDATA"));
        si.refresh();
        if (si.bytesAvailable() < kOtaMinFreeBytes) {      // ⑦ 盘快满了：宁可不升，也别装到一半没地方
            qWarning("[agent-qt] ⚠ 强制升级跳过：数据盘只剩 %lld 字节（要求 ≥ %lld）",
                     (long long)si.bytesAvailable(), (long long)kOtaMinFreeBytes);
            return;
        }
    }

    qInfo("[agent-qt] 🔒 云端要求强制升级：v%s → v%s，开始自动下载安装（无需人工操作）",
          kAppVersion, qPrintable(g_latestVer));
    QJsonObject p;
    p.insert(QStringLiteral("url"), g_updateUrl);
    p.insert(QStringLiteral("sha256"), g_updateSha);
    p.insert(QStringLiteral("version"), g_latestVer);
    startSelfUpdate(QStringLiteral("auto"), p);
}

/* ── 开机自启 / 桌面快捷方式（2026-10-09 · 用户清单第②条）────────────────────
 * 被控端是**受管终端**：自启 = 安装器建的「登录触发器计划任务」（RunLevel Highest，
 * 才拿得到锁屏/关机/抓屏所需权限）；快捷方式在**公共桌面**。两者改动都要管理员，
 * 所以点开关时用 runas 提权（弹 UAC；用户拒绝就如实报错、绝不假装成功）。
 * ⚠️ 管理端走的是另一套（HKCU Run + 当前用户桌面，免提权）—— 两端机制不同是用户选定的。
 */
static const char *kAgentTaskName = "StelarithAgentQt";

static bool runElevatedWait(const QString &file, const QString &params, int timeoutMs = 60000)
{
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";                 // 触发 UAC；用户点「否」时 ShellExecuteExW 返回 FALSE
    sei.lpFile = reinterpret_cast<LPCWSTR>(file.utf16());
    sei.lpParameters = reinterpret_cast<LPCWSTR>(params.utf16());
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) {
        qWarning("[agent-qt] 提权执行失败（UAC 被拒？）：%s", qPrintable(file));
        return false;
    }
    if (!sei.hProcess) return true;
    WaitForSingleObject(sei.hProcess, timeoutMs);
    DWORD code = 1;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    return code == 0;
}

static bool agentAutostartEnabled()
{
    QProcess p;
    p.start(QStringLiteral("schtasks"),
            {QStringLiteral("/Query"), QStringLiteral("/TN"), QString::fromLatin1(kAgentTaskName)});
    if (!p.waitForFinished(4000)) { p.kill(); return false; }
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

static bool agentSetAutostart(bool on)
{
    return runElevatedWait(QStringLiteral("schtasks.exe"),
        QStringLiteral("/Change /TN %1 /%2")
            .arg(QString::fromLatin1(kAgentTaskName),
                 on ? QStringLiteral("ENABLE") : QStringLiteral("DISABLE")));
}

static QString agentPublicDesktopLnk()
{
    const QString pub = qEnvironmentVariable("PUBLIC");
    return pub.isEmpty() ? QString() : (pub + QStringLiteral("/Desktop/星集控被控端.lnk"));
}

static bool agentDesktopShortcutExists()
{
    const QString p = agentPublicDesktopLnk();
    return !p.isEmpty() && QFile::exists(p);
}

static bool agentSetDesktopShortcut(bool on)
{
    const QString lnk = agentPublicDesktopLnk();
    if (lnk.isEmpty()) return false;
    const QString exe  = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString work = QDir::toNativeSeparators(QFileInfo(exe).absolutePath());
    QString ps;
    if (on)
        ps = QStringLiteral("$s=(New-Object -ComObject WScript.Shell).CreateShortcut('%1');"
                            "$s.TargetPath='%2';$s.WorkingDirectory='%3';$s.IconLocation='%2,0';$s.Save()")
                 .arg(QDir::toNativeSeparators(lnk), exe, work);
    else
        ps = QStringLiteral("Remove-Item -LiteralPath '%1' -Force -ErrorAction SilentlyContinue")
                 .arg(QDir::toNativeSeparators(lnk));
    return runElevatedWait(QStringLiteral("powershell.exe"),
        QStringLiteral("-NoProfile -ExecutionPolicy bypass -Command \"%1\"").arg(ps));
}

/** 设置与信息：一眼看清"我是谁、连着谁、什么版本、能不能升"。只读 + 两个动作按钮。 */
void openInfoDialog()
{
    static bool infoOpen = false;      // 防重入：托盘菜单连点两下别叠出三个窗
    if (infoOpen) return;
    infoOpen = true;

    QDialog dlg;
    dlg.setWindowTitle(QStringLiteral("星集控 · 被控端 设置与信息"));
    dlg.setMinimumWidth(560);

    auto *root = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout();
    root->addLayout(form);

    const QString cur = QString::fromLatin1(kAppVersion);

    form->addRow(QStringLiteral("当前版本"), new QLabel(QStringLiteral("v%1").arg(cur)));

    // 升级状态：没查到 / 云端没发布 / 有新版本 / 已是最新 —— 四种态分开写，别含糊
    QString latestText;
    const bool newer = g_updateChecked && !g_latestVer.isEmpty()
                       && compareVersion(g_latestVer, cur) > 0;
    if (!g_updateChecked)            latestText = QStringLiteral("还没查到（等云端回执；没连上时会一直显示这项）");
    else if (g_latestVer.isEmpty())  latestText = QStringLiteral("云端还没发布过版本");
    else if (newer)                  latestText = QStringLiteral("有新版本 v%1 可升级").arg(g_latestVer);
    else                             latestText = QStringLiteral("已是最新（云端 v%1）").arg(g_latestVer);
    auto *latestLbl = new QLabel(latestText);
    if (newer) latestLbl->setStyleSheet(QStringLiteral("color:#d08a00;font-weight:bold;"));
    form->addRow(QStringLiteral("升级状态"), latestLbl);

    form->addRow(QStringLiteral("设备代号"), new QLabel(g_uid.isEmpty() ? QStringLiteral("(未注册)") : g_uid));
    form->addRow(QStringLiteral("云端地址"), new QLabel(qEnvironmentVariable("STE_QT_WS_URL")));
    form->addRow(QStringLiteral("连接状态"),
                 new QLabel(g_registered ? QStringLiteral("已连接（云端已确认）")
                                         : QStringLiteral("未连接 / 正在重连")));
    form->addRow(QStringLiteral("数据目录"),
                 new QLabel(qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong")));
    form->addRow(QStringLiteral("日志文件"), new QLabel(g_logPath));

    if (!g_updateNotes.isEmpty()) {
        auto *notes = new QLabel(g_updateNotes);
        notes->setWordWrap(true);
        form->addRow(QStringLiteral("版本说明"), notes);
    }

    // ── 强制自动升级（2026-10-06）：把"谁说了算""上次干得怎么样"摆到台面上 ──
    // 强制升级最怕两件事：老师不知道机器在自动换程序；出了问题没法一眼看出是哪一版。
    QLabel *otaAutoLbl = nullptr;
    {
        const QJsonObject st = otaLoadState();
        const bool paused = otaPaused();
        const int fc = st.value(QStringLiteral("failCount")).toInt(0);
        const QString lastRes = st.value(QStringLiteral("lastResult")).toString();

        auto *modeLbl = new QLabel(g_updateMandatory
                                       ? QStringLiteral("强制：到点自动装，不用老师点（装完重启一次）")
                                       : QStringLiteral("可选：只有点「立即升级」才装"));
        if (g_updateMandatory) modeLbl->setStyleSheet(QStringLiteral("color:#c0392b;font-weight:bold;"));
        form->addRow(QStringLiteral("升级方式"), modeLbl);

        otaAutoLbl = new QLabel(paused
                                    ? QStringLiteral("已暂停（停机排查用；删掉 %1 即恢复）").arg(otaPausePath())
                                    : QStringLiteral("到点自动装"));
        if (paused) otaAutoLbl->setStyleSheet(QStringLiteral("color:#c0392b;"));
        form->addRow(QStringLiteral("自动升级"), otaAutoLbl);

        QString lastText;
        if (lastRes.isEmpty()) {
            lastText = QStringLiteral("（这台机器还没自动升级过）");
        } else if (lastRes == QLatin1String("ok")) {
            lastText = QStringLiteral("成功（已升到 v%1）").arg(st.value(QStringLiteral("lastVer")).toString());
        } else {
            lastText = QStringLiteral("失败 ×%1：%2")
                           .arg(fc).arg(st.value(QStringLiteral("lastError")).toString());
        }
        form->addRow(QStringLiteral("上次自动升级"), new QLabel(lastText));

        const qint64 nextAt = st.value(QStringLiteral("nextAt")).toVariant().toLongLong();
        form->addRow(QStringLiteral("下次自动升级"),
                     new QLabel(nextAt > QDateTime::currentMSecsSinceEpoch()
                                    ? QDateTime::fromMSecsSinceEpoch(nextAt).toString(QStringLiteral("MM-dd HH:mm"))
                                    : QStringLiteral("立刻（有网就试）")));
    }

    // ── 开机自启 / 桌面快捷方式（2026-10-09 · 用户清单第②条）──
    // 开关直接反映系统真实状态（查计划任务 / 看公共桌面），改完复查一遍 ——
    // 提权被拒时不能"界面勾上了、其实没改"。
    {
        auto *chkAuto = new QCheckBox(QStringLiteral("开机自动启动（登录时拉起）"));
        chkAuto->setChecked(agentAutostartEnabled());
        chkAuto->setToolTip(QStringLiteral("管理登录触发器计划任务 %1；改动需要管理员权限（会弹 UAC）。")
                                .arg(QString::fromLatin1(kAgentTaskName)));
        QObject::connect(chkAuto, &QCheckBox::toggled, &dlg, [chkAuto](bool on) {
            if (!agentSetAutostart(on))
                QMessageBox::warning(chkAuto, QStringLiteral("开机自启"),
                                     QStringLiteral("没改成：多半是 UAC 被拒绝，或安装时没建过该计划任务。"));
            chkAuto->blockSignals(true);
            chkAuto->setChecked(agentAutostartEnabled());
            chkAuto->blockSignals(false);
        });
        form->addRow(QStringLiteral("开机自启"), chkAuto);

        auto *chkLnk = new QCheckBox(QStringLiteral("在公共桌面放快捷方式"));
        chkLnk->setChecked(agentDesktopShortcutExists());
        chkLnk->setToolTip(QStringLiteral("公共桌面（所有登录用户都能看到）；改动需要管理员权限。"));
        QObject::connect(chkLnk, &QCheckBox::toggled, &dlg, [chkLnk](bool on) {
            if (!agentSetDesktopShortcut(on))
                QMessageBox::warning(chkLnk, QStringLiteral("桌面快捷方式"),
                                     QStringLiteral("没改成：多半是 UAC 被拒绝。"));
            chkLnk->blockSignals(true);
            chkLnk->setChecked(agentDesktopShortcutExists());
            chkLnk->blockSignals(false);
        });
        form->addRow(QStringLiteral("桌面快捷"), chkLnk);
    }

    auto *btnRow = new QHBoxLayout();
    root->addLayout(btnRow);

    auto *btnUpgrade = new QPushButton(QStringLiteral("立即升级"));
    const bool canUpgrade = newer && !g_updateUrl.isEmpty() && g_updateSha.length() == 64;
    btnUpgrade->setEnabled(canUpgrade);
    if (!canUpgrade) {
        btnUpgrade->setToolTip(QStringLiteral(
            "现在没有可升级的版本：要么云端还没发布（ota.json），要么清单里缺 url/sha256。\n"
            "这两项都得由管理员在云端补，本机不猜、不自己找包。"));
    }
    QObject::connect(btnUpgrade, &QPushButton::clicked, &dlg, [] {
        QJsonObject p;
        p.insert(QStringLiteral("url"), g_updateUrl);
        p.insert(QStringLiteral("sha256"), g_updateSha);
        p.insert(QStringLiteral("version"), g_latestVer);
        // 走的就是云端下发 self_update 用的同一条链路（含四条护栏）
        qInfo("[agent-qt] 用户在设置面板点了「立即升级」→ v%s", qPrintable(g_latestVer));
        startSelfUpdate(QStringLiteral("manual"), p);
    });
    QObject::connect(btnUpgrade, &QPushButton::clicked, &dlg, &QDialog::accept);

    // 强制升级的紧急刹车：**不卸包、不重编、不改云端**，往数据目录扔一个空文件就停手。
    // 某个版本真把全校教室机搞挂时，这是现场唯一来得及做的止血动作。
    auto *btnPause = new QPushButton(otaPaused() ? QStringLiteral("恢复自动升级")
                                                 : QStringLiteral("暂停自动升级"));
    btnPause->setToolTip(QStringLiteral("暂停只在本机生效：写入 %1 之后，这台机器不再自动升级，"
                                        "但别人照常升。删掉该文件即恢复。")
                             .arg(otaPausePath()));
    QObject::connect(btnPause, &QPushButton::clicked, &dlg, [btnPause, otaAutoLbl] {
        if (otaPaused()) {
            QFile::remove(otaPausePath());
            qInfo("[agent-qt] 操作员恢复了本机自动升级");
            btnPause->setText(QStringLiteral("暂停自动升级"));
            if (otaAutoLbl) otaAutoLbl->setText(QStringLiteral("到点自动装"));
        } else {
            QDir().mkpath(otaBaseDir());
            QFile f(otaPausePath());
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write(QStringLiteral("paused by operator %1\n")
                            .arg(QDateTime::currentDateTime().toString(Qt::ISODate)).toUtf8());
                f.flush();
            }
            qWarning("[agent-qt] ⛔ 操作员暂停了本机自动升级（%s）", qPrintable(otaPausePath()));
            btnPause->setText(QStringLiteral("恢复自动升级"));
            if (otaAutoLbl)
                otaAutoLbl->setText(QStringLiteral("已暂停（删掉 %1 即恢复）").arg(otaPausePath()));
        }
    });

    auto *btnLog = new QPushButton(QStringLiteral("打开日志目录"));
    QObject::connect(btnLog, &QPushButton::clicked, &dlg, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(g_logPath).absolutePath()));
    });

    auto *btnClose = new QPushButton(QStringLiteral("关闭"));
    QObject::connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::reject);

    btnRow->addWidget(btnUpgrade);
    btnRow->addWidget(btnPause);
    btnRow->addWidget(btnLog);
    btnRow->addStretch();
    btnRow->addWidget(btnClose);

    dlg.exec();
    infoOpen = false;
}

void openConfigDialog()
{
    if (g_configDialogOpen) return;   // 防重入
    g_configDialogOpen = true;
    qInfo().noquote() << QStringLiteral("[agent-qt] 打开配置窗口");

    QDialog dlg;
    dlg.setWindowTitle(QStringLiteral("星集控 · 配置向导"));
    dlg.setMinimumWidth(480);

    auto *root = new QVBoxLayout(&dlg);

    auto *intro = new QLabel(QStringLiteral(
        "这台电脑还没有接入学校的集控系统。\n\n"
        "最简单的方式：点下面的「用星璃账号绑定」——浏览器里登录你的星璃账号、点同意，"
        "这台机器就自动接入，不用填任何东西。\n\n"
        "如果你有管理员给的地址和令牌，也可以手动填下面三项。"));
    intro->setWordWrap(true);
    root->addWidget(intro);

    // 就地提示（校验失败/绑定结果/测试结果都写这里，**不弹二次窗口**）
    // 必须在「用星璃账号绑定」按钮之前定义——绑定回调里要用到它。
    auto *err = new QLabel();
    err->setWordWrap(true);
    err->setStyleSheet(QStringLiteral("color:#c03030;"));
    err->hide();
    root->addWidget(err);

    // ── 「用星璃账号绑定」按钮（2026-10-06：零手填路径）──
    // 点了之后：开浏览器 → 登录授权 → 自动换设备票 → 写 agent.env → 重连。
    // 全程不用手填地址/令牌/设备名。
    auto *bindBtn = new QPushButton(QStringLiteral("用星璃账号绑定（推荐）"));
    bindBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:#5b8def;color:white;border:none;padding:10px 16px;"
        "border-radius:6px;font-size:14px;font-weight:600}"
        "QPushButton:hover{background:#4a7ddf}"
        "QPushButton:disabled{background:#aaa}"));
    root->addWidget(bindBtn);

    auto *bindHint = new QLabel(QStringLiteral(
        "点了之后会打开浏览器，登录你的星璃账号、点同意，这台机器就自动接入集控。"));
    bindHint->setWordWrap(true);
    bindHint->setStyleSheet(QStringLiteral("color:#888;"));
    root->addWidget(bindHint);

    // 班级输入框**先声明、后创建**（真正的 new 在下面的手动表单里）：
    // OAuth 成功回调要用到它，而那个 lambda 定义在这行**上面** ——
    // C++ 里「后面才声明的局部变量」在 lambda 的捕获列表里够不到（实测 C2065）。
    // 这里只占个位，回调真正执行时（用户点完绑定）它的值早就赋好了。
    QLineEdit *codeEdit = nullptr;
    // 设备显示名（STE_QT_NAME）先声明、后创建：OAuth 成功回调要用它，而那个 lambda 定义在本行上面，
    // C++ lambda 捕获列表够不到后面才声明的局部变量（见 codeEdit 上方的同款注释）。
    QLineEdit *nameEdit = nullptr;

    // 「用星璃账号绑定」的信号槽：OAuthBind 是独立对象，对话框关了也不能被析构（绑定时对话框还在）
    auto *oauth = new OAuthBind(&dlg);   // parent=dlg，对话框关闭时自动析构

    QObject::connect(bindBtn, &QPushButton::clicked, &dlg, [&, bindBtn, bindHint, oauth, err] {
        if (oauth->busy()) return;
        bindBtn->setEnabled(false);
        bindHint->setStyleSheet(QStringLiteral("color:#666;"));
        bindHint->setText(QStringLiteral("正在打开浏览器…登录完成后会自动回来。"));
        err->hide();
        // 站点基址交给 OAuthBind（唯一真源是 siteBaseFromWsUrl，见它上面的注释）
        oauth->begin(siteBaseFromWsUrl(qEnvironmentVariable("STE_QT_WS_URL")));
    });

    // 绑定成功：写 agent.env + 立即重连 + 关窗（与手动保存走同一条重连路）
    QObject::connect(oauth, &OAuthBind::succeeded, &dlg, [&, bindBtn](const QString &wsUrl, const QString &ticket, const QString &uid, const QString &owner) {
        bindBtn->setText(QStringLiteral("已绑定（%1）").arg(owner));

        // 把设备票写进 agent.env（token 字段放票，与旧静态令牌同字段、云端按段数区分）
        if (!saveAgentEnv(wsUrl, ticket, uid, nameEdit ? nameEdit->text() : QString())) {
            err->setStyleSheet(QStringLiteral("color:#c03030;"));
            err->setText(QStringLiteral("绑定成功但写配置失败（目录无写权限？）。请用管理员身份重装后再试。"));
            err->show();
            bindBtn->setEnabled(true);
            return;
        }
        // 2026-10-09：**账号绑定这条路也要绑班** —— 用户在下面填了班级码就直接生效。
        // 原来只有「保存并连接」会读那个输入框，点「用星璃账号绑定」时它被完全忽略，
        // 于是"用账号接入"的设备永远不带班级码，管理端按班分组里根本看不到它。
        // ⚠️ 必须放在重连之前：g_classCode 先落好，紧接着的 register 才带得上。
        applyClassBinding(codeEdit->text(), wsUrl, uid);

        // 保存成功 → 立刻用新配置重连
        g_configured = true;
        g_unconfigured = false;
        g_unconfiguredNotified = false;
        g_registered = false;
        g_backoffMs = 1000;
        if (g_ws && g_ws->state() != QAbstractSocket::UnconnectedState) {
            g_reconnectNow = true;
            g_ws->close();
        } else {
            updateTray(TrayState::Disconnected, QStringLiteral("正在用绑定配置连接…"));
            connectNow();
        }
        dlg.accept();
    });

    // 绑定失败：恢复按钮 + 显示原因（不关窗，让用户可以再试或改用手动填）
    QObject::connect(oauth, &OAuthBind::failed, &dlg, [&, bindBtn, bindHint](const QString &reason) {
        bindBtn->setEnabled(true);
        bindBtn->setText(QStringLiteral("用星璃账号绑定（重试）"));
        bindHint->setStyleSheet(QStringLiteral("color:#c03030;"));
        bindHint->setText(QStringLiteral("绑定失败：%1").arg(reason));
    });

    // 手动填的分隔线
    auto *sep = new QLabel(QStringLiteral("── 或者手动填写（管理员给了你地址和令牌时用）──"));
    sep->setStyleSheet(QStringLiteral("color:#bbb;text-align:center;"));
    sep->setAlignment(Qt::AlignCenter);
    root->addWidget(sep);

    auto *form = new QFormLayout();
    root->addLayout(form);

    const QString hintStyle = QStringLiteral("color:#888;");

    // —— 云端地址 ——
    auto *urlEdit = new QLineEdit(qEnvironmentVariable("STE_QT_WS_URL").trimmed());
    // ⚠️ 示例地址必须是**外网真正连得上**的那个（2026-10-09 修）：
    //    这里原写 `ws://10.0.0.5:8788/ws/agent`，而 8788 **只在内网/本机可达**，
    //    cloudflared 回源它、对外只暴露 443 ⇒ 外网机填 8788 必连不上，
    //    装机的人会以为是令牌错了，在错误方向上排查半天。
    urlEdit->setPlaceholderText(QStringLiteral("例如 wss://control.245959623.xyz/ws/agent"));
    form->addRow(QStringLiteral("云端地址"), urlEdit);
    auto *urlHint = new QLabel(QStringLiteral(
        "就是集控服务器在哪，管理员给你的一串地址，照抄即可。"
        "走公网是 wss:// 开头、**不带端口**；只有在同一内网自建时才用 ws://<内网IP>:8788/ws/agent。"));
    urlHint->setWordWrap(true);
    urlHint->setStyleSheet(hintStyle);
    form->addRow(QString(), urlHint);

    // —— 设备令牌（打码显示 + 可切换明文核对）——
    auto *tokenEdit = new QLineEdit(qEnvironmentVariable("STE_QT_WS_TOKEN").trimmed());
    tokenEdit->setEchoMode(QLineEdit::Password);
    tokenEdit->setPlaceholderText(QStringLiteral("管理员提供的设备令牌"));
    auto *tokenRow = new QWidget();
    auto *tokenLay = new QHBoxLayout(tokenRow);
    tokenLay->setContentsMargins(0, 0, 0, 0);
    tokenLay->addWidget(tokenEdit, 1);
    auto *showCb = new QCheckBox(QStringLiteral("显示"));
    tokenLay->addWidget(showCb);
    form->addRow(QStringLiteral("设备令牌"), tokenRow);
    auto *tokenHint = new QLabel(QStringLiteral(
        "相当于这台电脑接入集控的\"门禁卡\"，一机一串，由管理员发放。"
        "默认打码显示，勾「显示」可核对是否抄错。"));
    tokenHint->setWordWrap(true);
    tokenHint->setStyleSheet(hintStyle);
    form->addRow(QString(), tokenHint);

    // —— 设备名（可选）——
    auto *uidEdit = new QLineEdit(qEnvironmentVariable("STE_QT_UID").trimmed());
    uidEdit->setPlaceholderText(QHostInfo::localHostName());
    form->addRow(QStringLiteral("设备码（可选）"), uidEdit);
    auto *uidHint = new QLabel(QStringLiteral(
        "这台电脑在集控里的唯一代号（用于按班归组等）。留空就用电脑本名，一般不用改。"));
    uidHint->setWordWrap(true);
    uidHint->setStyleSheet(hintStyle);
    form->addRow(QString(), uidHint);

    // —— 设备显示名（可选，2026-10-10 加）——
    // 落 STE_QT_NAME，与上面的「设备码」(STE_QT_UID) 区分：码是唯一代号，显示名是给人看的友好名。
    // 管理端据此显示「设备名」；留空回落电脑本名。改名后保存即生效、无需重启。
    nameEdit = new QLineEdit(qEnvironmentVariable("STE_QT_NAME").trimmed());
    nameEdit->setPlaceholderText(QHostInfo::localHostName());
    form->addRow(QStringLiteral("设备显示名（可选）"), nameEdit);
    auto *nameHint = new QLabel(QStringLiteral(
        "管理面板里看到的这台电脑的名字（如「一班讲台机」）。留空就用电脑本名。"));
    nameHint->setWordWrap(true);
    nameHint->setStyleSheet(hintStyle);
    form->addRow(QString(), nameHint);

    // —— 班级（可选）—— 2026-10-04 立；2026-10-09 起「账号绑定」那条路也吃它
    //    （两个按钮共用同一份绑定逻辑，见 applyClassBinding）
    //    ⚠️ 这里只赋值，**不重新声明** —— 指针已在上面（OAuth 回调之前）声明过，见那段注释。
    codeEdit = new QLineEdit();
    codeEdit->setPlaceholderText(QStringLiteral("班级码，或 XJK-XXXX-XXX 接入码"));
    form->addRow(QStringLiteral("班级（可选）"), codeEdit);
    auto *codeHint = new QLabel(QStringLiteral(
        "「保存并连接」与「用星璃账号绑定」两个按钮都会带上它："
        "接入码（XJK-…）会去站点换班级，班级码直接上报云端按班归组。"
        "拿不准就留空，之后在管理面板里也能绑。"));
    codeHint->setWordWrap(true);
    codeHint->setStyleSheet(hintStyle);
    form->addRow(QString(), codeHint);

    // 就地提示（校验失败/测试结果都写这里，**不弹二次窗口**）
    auto *boxes = new QDialogButtonBox();
    auto *testBtn = boxes->addButton(QStringLiteral("测试连接"), QDialogButtonBox::ActionRole);
    auto *saveBtn = boxes->addButton(QStringLiteral("保存并连接"), QDialogButtonBox::AcceptRole);
    auto *laterBtn = boxes->addButton(QStringLiteral("以后再说"), QDialogButtonBox::RejectRole);
    root->addWidget(boxes);

    // 勾「显示」→ 明文，取消 → 回到打码
    QObject::connect(showCb, &QCheckBox::toggled, tokenEdit, [tokenEdit](bool on) {
        tokenEdit->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
    });

    // 「以后再说」= 直接关掉，不做任何改动（未配置态的托盘告警照旧）
    QObject::connect(laterBtn, &QPushButton::clicked, &dlg, [&dlg] { dlg.reject(); });

    // 「测试连接」：只为验证地址可达（令牌要等保存后才能真正接入，故只报"能否连上"）
    QObject::connect(testBtn, &QPushButton::clicked, &dlg, [&, testBtn] {
        const QString url = cleanLine(urlEdit->text());
        if (url.isEmpty()) {
            err->setStyleSheet(QStringLiteral("color:#c03030;"));
            err->setText(QStringLiteral("请先填写「云端地址」，再点测试。"));
            err->show();
            return;
        }
        const QUrl u(url);
        if (!u.isValid() || u.host().isEmpty()) {
            err->setStyleSheet(QStringLiteral("color:#c03030;"));
            err->setText(QStringLiteral("这个地址看起来不对，请检查是否形如 ws://主机:端口/ws/agent。"));
            err->show();
            return;
        }
        err->setStyleSheet(QStringLiteral("color:#666;"));
        err->setText(QStringLiteral("正在测试连接…"));
        err->show();
        testBtn->setEnabled(false);

        auto *probe = new QWebSocket();
        probe->setParent(&dlg);   // 关窗后自动销毁，避免回调里引用已析构的控件
        auto *t = new QTimer(probe);
        t->setSingleShot(true);
        QObject::connect(t, &QTimer::timeout, probe, [&, probe, t, testBtn] {
            probe->abort();
            err->setStyleSheet(QStringLiteral("color:#c03030;"));
            err->setText(QStringLiteral("连接失败：等待超时。请确认地址正确、网络通畅、集控服务器已启动。"));
            testBtn->setEnabled(true);
        });
        QObject::connect(probe, &QWebSocket::connected, probe, [&, probe, t, testBtn] {
            t->stop();
            err->setStyleSheet(QStringLiteral("color:#1a7f37;"));
            err->setText(QStringLiteral("地址可达：已连上服务器。（能否成功接入还要看令牌，保存后看托盘图标。）"));
            testBtn->setEnabled(true);
            probe->close();
        });
        QObject::connect(probe, &QWebSocket::errorOccurred, probe,
                         [&, probe, t, testBtn](QAbstractSocket::SocketError) {
                             if (!t->isActive()) return;   // 已被 connected/timeout 分支处理
                             t->stop();
                             err->setStyleSheet(QStringLiteral("color:#c03030;"));
                             err->setText(QStringLiteral("连接失败：连不上这个地址，请核对地址与服务器是否已启动。"));
                             testBtn->setEnabled(true);
                         });
        t->start(5000);
        probe->open(u);
    });

    // 「保存并连接」：校验 → 写文件 + qputenv → 立即重连 → 关窗
    QObject::connect(saveBtn, &QPushButton::clicked, &dlg, [&] {
        const QString url = cleanLine(urlEdit->text());
        const QString token = cleanLine(tokenEdit->text());
        const QString uid = cleanLine(uidEdit->text());
        if (url.isEmpty() || token.isEmpty()) {
            err->setStyleSheet(QStringLiteral("color:#c03030;"));
            err->setText(QStringLiteral("请先填写「云端地址」和「设备令牌」——这两项是接入集控必需的，缺一不可。"));
            err->show();
            return;   // 原地提示，不关窗、不弹二次窗口
        }
        if (!saveAgentEnv(url, token, uid, nameEdit ? nameEdit->text() : QString())) {
            err->setStyleSheet(QStringLiteral("color:#c03030;"));
            err->setText(QStringLiteral("写入配置文件失败（可能是程序目录没有写权限）。请用管理员身份重装后再试。"));
            err->show();
            return;
        }
        // 保存成功 → 立刻用新配置重连，并把可见状态从"未配置"恢复
        g_configured = true;
        g_unconfigured = false;
        g_unconfiguredNotified = false;
        g_registered = false;
        g_backoffMs = 1000;
        if (g_ws && g_ws->state() != QAbstractSocket::UnconnectedState) {
            g_reconnectNow = true;   // 断线回调里立刻重连，不等退避
            g_ws->close();
        } else {
            updateTray(TrayState::Disconnected, QStringLiteral("正在用新配置连接…"));
            connectNow();
        }

        // 填了班级码 / 接入码 → 走统一的班级绑定入口（见 applyClassBinding 的注释）
        applyClassBinding(cleanLine(codeEdit->text()), url, uid);

        dlg.accept();
    });

    dlg.exec();
    g_configDialogOpen = false;
    qInfo().noquote() << QStringLiteral("[agent-qt] 配置窗口已关闭");
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

/* ══════════ 2026-10-04 · 大屏通知（乙阶段 2：notify 动作 + 灵动岛/居中弹窗/全屏 + TTS） ══════════ */

// TTS 朗读（Windows SAPI 5 · 微软免费离线语音）。失败**不抛**、只记日志 ——
// 通知弹窗是主链路，朗读只是增强；SAPI 不可用时（极端环境）通知照常显示。
//
// ⚠️ 2026-10-09 改**异步**（用户反馈"tts 会拖慢通知显示"）：
//    原来用同步 Speak（SPF_DEFAULT），一次朗读会把主线程整个卡住（一句几百 ms ~ 数秒）。
//    而调用点是"先把通知上屏、紧接着朗读"—— 通知窗口虽然 show() 了，主线程却卡在朗读里出不来，
//    界面要等朗读结束才真正画出来，看起来就是"通知迟迟不弹/显示被拖慢"。
//    改法：常驻一个 ISpVoice（**不能每次 Speak 完就 Release** —— 释放会把正在异步朗读的声音掐掉），
//    用 SPF_ASYNC 立即返回、把朗读交给 SAPI 自己的后台线程；再加 SPF_PURGEBEFORESPEAK，
//    新的一条顶掉还没播完的旧的一条，避免多条通知叠着念成一锅粥。
//    COM 由 Qt 的 Windows 平台插件在主线程初始化（原同步版本能跑即是证明），此处不重复 CoInitialize。
static ISpVoice *g_spVoice = nullptr;

static void speakText(const QString &text)
{
    if (text.trimmed().isEmpty()) return;
    if (!g_spVoice) {
        HRESULT hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice,
                                      (void **)&g_spVoice);
        if (FAILED(hr) || !g_spVoice) {
            g_spVoice = nullptr;   // 失败别留半个指针，下次进来重试
            qWarning().noquote() << QStringLiteral("[agent-qt] TTS 不可用（SAPI 初始化失败 HR=0x%1），通知不朗读，仅显示")
                                        .arg((uint)hr, 0, 16);
            return;
        }
    }
    // UTF-8 → 宽字符；SAPI 默认读中文需要系统装中文语音（Win10 自带 Huihui 等）
    const QString say = text.left(120);   // 防长广播把朗读拖到天荒地老
    const int wlen = say.length() + 1;
    std::wstring wstr(wlen, L'\0');
    say.toWCharArray(&wstr[0]);
    const HRESULT hr = g_spVoice->Speak(wstr.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
    if (FAILED(hr))
        qWarning().noquote() << QStringLiteral("[agent-qt] TTS Speak 失败 HR=0x%1（通知已显示，朗读跳过）")
                                    .arg((uint)hr, 0, 16);
}

// ── 灵动岛统一入口（2026-10-07 · #98）──────────────────────────────────
// 用户 2026-10-07 定下："被控端一切通知都经过灵动岛显示，弹窗则居中，全屏则放大全屏，
// 要有过渡。"
// 以前被控端的"轻量提示"有三种写法（托盘气泡 / NotifyWindow 的 island 形态 / 直接弹窗），
// 各弹各的、位置互相打架，教室大屏上还会叠成一摞。现在统一走 IslandOverlay：
//   · 顶部居中胶囊、Collapsed(56)↔Expanded(168) 弹性缓动、出现/消失都带动画；
//   · 默认鼠标穿透（WA_TransparentForMouseEvents）—— 不挡后面的课件/白板；
//   · 需要"用户点一下"的（如收到文件想看清楚）才 setInteractive(true) 临时收走点击。
// 需要真·弹窗的（要确认 / 要输入文本）仍然走 NotifyWindow，不硬塞进灵动岛。
// form（#98）：Capsule=顶部胶囊（默认）；Centered=屏幕正中卡片；Fullscreen=铺满整屏。
// 调用方按"这条内容要不要停下来看清"来选，别什么都往全屏上堆。
static void islandShow(const QString &title, const QString &desc = QString(),
                       const QString &icon = QString(), int ms = 5000, bool interactive = false,
                       const QString &openPath = QString(),
                       IslandOverlay::Form form = IslandOverlay::Form::Capsule)
{
    IslandOverlay *isl = IslandOverlay::instance();
    if (openPath.isEmpty()) isl->clearOpenPath();
    else                    isl->setOpenPath(openPath);
    // 交互态（可点）清掉旧的 openPath，免得下一条提示点一下去翻上一份文件。
    isl->setInteractive(interactive);
    isl->showIsland(title, desc, icon, ms, form);
}

// 托盘气泡兜底：灵动岛是贴桌面顶层的胶囊，被控端缩进托盘 / 教室投影投在别处 /
// 用户正在全屏课件里，那一小条容易整个漏掉。像"文件落到本机了"这种结果型事件，
// 再往 Windows 通知中心推一条才兜得住（tray 没起来就静默跳过，不报错）。
static void trayNotify(const QString &title, const QString &body,
                       QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information,
                       int ms = 8000)
{
    if (!g_tray) return;
    g_tray->showMessage(title, body.isEmpty() ? title : body, icon, ms);
}

// 大屏通知窗口：无边框 + 置顶 + Tool（不进任务栏）。
// 三种形态（用户 2026-10-04 拍板）：
//   · popup     —— 居中弹窗（圆角卡片）；同时经托盘 showMessage 进 Windows 通知中心留存
//   · island    —— 顶部灵动岛（细长圆角胶囊，顶部居中，自动收起）
//   · fullscreen—— 全屏遮罩（大字居中，紧急通知）
// 单例：同一时间只显示一个（新通知顶掉旧的），防止堆叠霸屏。
class NotifyWindow : public QWidget
{
public:
    enum Kind { Popup, Island, Fullscreen };

    // severity：remind(绿) / inform(黄) / urgent(红)；仅对 fullscreen 生效（其它形态背景本就是深色半透明）。
    // emergency：紧急通知 —— 不自动关闭、强制置顶（WindowStaysOnTopAlways），点击任意处手动关闭。
    // needConfirm：普通通知也要确认（2026-10-07 · 通知确认 + 快捷回复）；
    //             显示「确认」按钮 + 快捷回复按钮行，点击后回调 onConfirm(reply)。
    //             reply 为 "确认" 或用户点的快捷话术（如"收到"）。
    static void showNotice(Kind kind, const QString &title, const QString &content, int seconds, bool tts,
                           const QString &severity = QString(), bool emergency = false,
                           bool needConfirm = false, const QStringList &replies = {},
                           std::function<void(const QString &reply)> onConfirm = nullptr)
    {
        // 单例：先关掉旧的（同一时间只一个通知窗口）
        if (g_notify) {
            // 新通知顶掉旧的：旧的**淡出**再走（直接 hide() 会硬切，教室大屏上很扎眼）
            g_notify->fadeOutClose();
            g_notify->deleteLater();
        }
        g_notify = new NotifyWindow(kind, title, content, seconds, tts, severity, emergency,
                                    needConfirm, replies, onConfirm);
        g_notify->showWindow();
    }

private:
    explicit NotifyWindow(Kind kind, const QString &title, const QString &content, int seconds, bool tts,
                          const QString &severity, bool emergency,
                          bool needConfirm = false, const QStringList &replies = {},
                          std::function<void(const QString &reply)> onConfirm = nullptr)
        : m_kind(kind), m_title(title), m_content(content), m_seconds(seconds), m_tts(tts),
          m_severity(severity), m_emergency(emergency), m_needConfirm(needConfirm),
          m_replies(replies), m_onConfirm(onConfirm)
    {
        // 所有通知统一置顶（Qt6 只有 WindowStaysOnTopHint 一种置顶标志，没有 Always 变体）。
        // 紧急通知的差异化靠"不自动关闭 + 点击才关"表达，不靠更强的置顶强度。
        // ⚠️ WindowDoesNotAcceptFocus（2026-10-09 加）：从窗口标志这一层就拒绝拿焦点，
        //    光靠"不调 activateWindow()"不够 —— 置顶窗口在某些情况下仍会被系统聚焦。
        //    Qt::Tool = 不进任务栏；StaysOnTop = 压在最上层。三者合起来才是"只展示不打扰"。
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool
                       | Qt::WindowDoesNotAcceptFocus);
        setAttribute(Qt::WA_DeleteOnClose);
        if (kind == Fullscreen) {
            // 全屏遮罩：palette 画纯色背景（WA_TranslucentBackground + paintEvent 在 Qt 6 不稳定）
            QColor c;
            if (severity == QLatin1String("urgent"))      c = QColor(180, 20, 20);
            else if (severity == QLatin1String("inform")) c = QColor(200, 140, 10);
            else if (severity == QLatin1String("remind")) c = QColor(20, 140, 60);
            else                                            c = QColor(0, 0, 0);
            setAutoFillBackground(true);
            QPalette pal = palette();
            pal.setColor(QPalette::Window, c);
            setPalette(pal);
        } else {
            // 灵动岛/弹窗：圆角+透明背景
            setAttribute(Qt::WA_TranslucentBackground);
        }

        // 长度限制：标题/正文各限长，超长截断加省略号——防止长广播把固定高度的窗撑爆
        // （2026-10-04 用户要求"长度限制"；显示与 TTS 都读同一份截断文本，口径一致）
        static const int kTitleCap = 24;
        static const int kContentCap = 64;
        const QString t = (m_title.length() > kTitleCap)
                              ? m_title.left(kTitleCap) + QStringLiteral("…")
                              : m_title;
        QString c = m_content.trimmed();
        if (c.length() > kContentCap)
            c = c.left(kContentCap) + QStringLiteral("…");
        m_combined = c.isEmpty() ? t : t + QStringLiteral("：") + c;

        auto *lay = new QVBoxLayout(this);
        lay->setContentsMargins(24, 16, 24, 16);
        m_label = new QLabel(m_combined, this);
        m_label->setAlignment(Qt::AlignCenter);
        m_label->setWordWrap(false);
        // 字号随长度自适应（短则大，长则缩）—— 复刻旧插件 BuildSingleLineFontSize 思路
        m_label->setStyleSheet(QStringLiteral(
            "font-weight:700; color:#ffffff; background:transparent;"
            "font-size:%1px;").arg(fontSizeFor(m_combined.length())));
        lay->addWidget(m_label);

        // 紧急通知：底部"确认"按钮（2026-10-05：需求文档要求确认按钮）
        // 确认 + 快捷回复（2026-10-07）：needConfirm 时显示「确认」按钮；
        // 有 replies 时再加一排快捷回复话术按钮。点击 → onConfirm(reply) → 关闭。
        // 紧急通知（emergency）也走这里（此前只有确认按钮，现在同样带回调上报）。
        if (m_emergency || m_needConfirm) {
            lay->addSpacing(16);
            auto *btnLay = new QHBoxLayout();
            btnLay->addStretch();
            auto mkBtn = [this, lay](const QString &text, const QString &reply) {
                auto *b = new QPushButton(text, this);
                b->setMinimumSize(140, 48);
                b->setStyleSheet(QStringLiteral(
                    "QPushButton { font-size:18px; font-weight:700; color:#ffffff;"
                    " background:rgba(255,255,255,80); border:2px solid rgba(255,255,255,180);"
                    " border-radius:24px; padding:0 32px;}"
                    "QPushButton:hover { background:rgba(255,255,255,140);}"
                    "QPushButton:pressed { background:rgba(255,255,255,180);}"));
                QObject::connect(b, &QPushButton::clicked, this, [this, reply]() {
                    if (m_onConfirm) m_onConfirm(reply);
                    close();
                });
                return b;
            };
            btnLay->addWidget(mkBtn(QStringLiteral("\u2713 \u786e\u8ba4"), QStringLiteral("\u786e\u8ba4")));
            // 快捷回复：一排预设话术（如"收到"/"好的"/"稍后处理"）
            for (const QString &r : m_replies) {
                if (r.trimmed().isEmpty()) continue;
                btnLay->addWidget(mkBtn(r, r));
            }
            btnLay->addStretch();
            lay->addLayout(btnLay);
        }

        // 淡入（用户 2026-10-07："要有过渡"）。
        // 以前这里是"直接显示"——教室大屏上通知是"啪"地砸出来，紧接着一颗弹窗又"啪"地消失，
        // 讲台上的学生注意力全被这个硬切带走。QPropertyAnimation 在 QtCore 里（不用加依赖），
        // 220ms 跟手、不拖沓。
        auto *eff = new QGraphicsOpacityEffect(this);
        setGraphicsEffect(eff);
        auto *fin = new QPropertyAnimation(eff, "opacity", this);
        fin->setDuration(220);
        fin->setStartValue(0.0);
        fin->setEndValue(1.0);
        fin->start();
    }

    /** 带淡出的关窗（用户要求"要有过渡"）：动画放完再真关，避免"啪一下不见"。 */
    void fadeOutClose()
    {
        if (!isVisible()) { close(); return; }
        QGraphicsOpacityEffect *eff = qobject_cast<QGraphicsOpacityEffect *>(graphicsEffect());
        if (!eff) { close(); return; }
        auto *fout = new QPropertyAnimation(eff, "opacity", this);
        fout->setDuration(220);
        fout->setStartValue(1.0);
        fout->setEndValue(0.0);
        // 动画对象随窗一起析构；真关窗在 finished 里做（此时窗口还在，视觉连续）
        QObject::connect(fout, &QPropertyAnimation::finished, this, [this]() { close(); });
        fout->start();
    }

    ~NotifyWindow() { if (g_notify == this) g_notify = nullptr; }

    void showWindow()
    {
        QScreen *screen = QGuiApplication::primaryScreen();
        if (!screen) { close(); return; }
        const QRect geo = screen->geometry();

        if (m_kind == Fullscreen) {
            setGeometry(geo);
            // 全屏遮罩 + 居中大字。严重度配色（2026-10-05 补）：
            //   remind  绿 —— 提醒；inform  琥珀 —— 通知；urgent 红 —— 紧急；
            //   未知/缺省回黑底（安全默认，避免把任意串直接插进 QSS）。
            // 全屏背景由构造函数 palette 处理
            setStyleSheet(QStringLiteral("border:none;"));
        } else if (m_kind == Island) {
            // 灵动岛：顶部居中细长胶囊（≈ 高度 64px、宽度随内容 60% 屏宽）
            const int w = qMin(geo.width() * 6 / 10, 720);
            const int h = 64;
            setGeometry(geo.x() + (geo.width() - w) / 2, geo.y() + 24, w, h);
            setStyleSheet(QStringLiteral(
                "background:rgba(20,20,20,215); border-radius:%1px; border:none;").arg(h / 2));
            m_label->setStyleSheet(m_label->styleSheet() + QStringLiteral(" font-size:22px;"));
        } else {
            // popup 居中弹窗：圆角卡片，宽 40% 屏、高 140
            const int w = qMin(geo.width() * 2 / 5, 520);
            const int h = 140;
            setGeometry(geo.x() + (geo.width() - w) / 2, geo.y() + (geo.height() - h) / 2, w, h);
            setStyleSheet(QStringLiteral(
                "background:rgba(30,30,30,215); border-radius:16px; border:1px solid #666;"));
        }

        // ⚠️ 这里**绝不能 activateWindow()**（2026-10-09 修：老师反馈"每次弹通知桌面跳一下"）。
        //    activateWindow() 会把**键盘焦点**从老师/学生正在用的程序上抢走 —— 讲课时弹一条
        //    通知，正在打的字就断了、输入法上下文也跳掉，教室里看着就是"桌面抖一下"。
        //    通知是**展示性**的：WindowStaysOnTopHint 已经保证它压在最上层看得见，
        //    raise() 再保证 Z 序在顶 —— 焦点必须留在用户当前正在操作的窗口上。
        //    ⚠️ WindowDoesNotAcceptFocus 只挡键盘焦点，**不挡鼠标点击**：
        //       紧急通知"点一下才关"（mousePressEvent）照常工作，所以可以放心加。
        show();
        raise();

        // TTS 朗读（调用方要求时）：读截断后的完整内容（标题：正文），与显示一致
        if (m_tts) speakText(m_combined);

        // 到点自动收起；0/缺省 → 自适应（短 5s、长内容按字数抬高，复刻旧插件公式）
        const int durMs = m_seconds > 0
                              ? qMin(m_seconds, 3600) * 1000
                              : qMax(5000.0, 2500.0 + m_combined.length() * 120.0);
        if (m_emergency) return;   // 紧急通知不自动消失，点击才关（见 mousePressEvent）
        QTimer::singleShot(durMs, this, &NotifyWindow::fadeOutClose);
    }

    // fullscreen 遮罩底色：按严重度映射（仅这三档白名单 + 黑底回落）。
    // 用 rgba(0,0,0,200) 风格保持与旧版一致的半透明密度，只换色相。
    static QString fullscreenBackground(const QString &severity)
    {
        if (severity == QLatin1String("urgent")) return QStringLiteral("rgba(180,20,20,210)");
        if (severity == QLatin1String("inform")) return QStringLiteral("rgba(200,140,10,205)");
        if (severity == QLatin1String("remind")) return QStringLiteral("rgba(20,140,60,205)");
        return QStringLiteral("rgba(0,0,0,200)");   // 缺省/未知 → 黑底（与旧版一致）
    }

    static int fontSizeFor(int len)
    {
        // 短 → 特大（48px）；长 → 逐步缩小，最低 18px
        if (len <= 8) return 48;
        if (len <= 16) return 36;
        if (len <= 32) return 28;
        if (len <= 64) return 22;
        return 18;
    }

    Kind m_kind;
    QString m_title;
    QString m_content;
    QString m_combined;
    int m_seconds;
    bool m_tts;
    QString m_severity;   // remind / inform / urgent（仅 fullscreen 生效）
    bool m_emergency;     // 紧急：不自动关 + 强制置顶 + 点击关闭
    // 确认 + 快捷回复（2026-10-07）
    bool m_needConfirm = false;
    QStringList m_replies;                        // 快捷回复话术（可空）
    std::function<void(const QString &reply)> m_onConfirm;
    QLabel *m_label = nullptr;
    static NotifyWindow *g_notify;

protected:

    // paintEvent: stylesheet background is ineffective on top-level QWidget (Qt 6)
    void paintEvent(QPaintEvent *) override
    {
        if (m_kind == Fullscreen) { QWidget::paintEvent(nullptr); return; }
        QPainter p(this);
        if (m_kind == Island) {
            QPainterPath path;
            path.addRoundedRect(rect().toRectF(), 32, 32);
            p.fillPath(path, QColor(20, 20, 20, 215));
        } else {
            QPainterPath path;
            path.addRoundedRect(rect().toRectF(), 16, 16);
            p.fillPath(path, QColor(30, 30, 30, 215));
            p.setPen(QColor(0x66, 0x66, 0x66));
            p.drawRoundedRect(rect().adjusted(1, 1, -1, -1).toRectF(), 16, 16);
        }
    }

    // 紧急通知：必须人工确认 —— 点击任意处手动关闭（不自动消失）。
    void mousePressEvent(QMouseEvent *ev) override
    {
        // 紧急通知点哪都能关，但同样走淡出（用户 2026-10-07："要有过渡"）
        if (m_emergency) { ev->accept(); fadeOutClose(); return; }
        QWidget::mousePressEvent(ev);
    }
};

NotifyWindow *NotifyWindow::g_notify = nullptr;

// 前向声明（2026-10-07）：notify 确认/快捷回复回调要在定义前用（二次回执）
static void sendActionReceipt(const QString &id, const QString &action, const QString &result,
                              const QString &error, const QJsonObject &data);

// 解析 params（kind/title/content/seconds/notice_id/flags）并弹窗。
// 返回 "" = 成功；非空 = 错误（调用方转 failed 回执）。
// id：本条指令的 instruction id（确认/快捷回复时发二次回执用，可为空）。
static QString notifyFromParams(const QJsonObject &params, const QString &id = QString())
{
    const QString kind = params.value(QStringLiteral("kind")).toString().trimmed().toLower();
    const QString title = params.value(QStringLiteral("title")).toString().trimmed();
    if (title.isEmpty())
        return QStringLiteral("notify 缺 title");
    const QString content = params.value(QStringLiteral("content")).toString();
    // ── 考试模式门控（2026-10-09 接线 · §6.2）：考试激活期间一切通知静默丢弃 ──
    // 不弹、不响、不记 error —— 学生机在考试中收到任何通知都应像没收到一样。
    const bool gated = !g_notifGate.allow(
        kind == QLatin1String("danmaku") ? NotificationGate::Danmaku
        : kind == QLatin1String("loop") ? NotificationGate::LoopRemind
        : NotificationGate::Island);
    if (gated) return QStringLiteral("exam-active-gated");
    const int seconds = params.value(QStringLiteral("seconds")).toInt(0);
    // flags.speech → TTS 朗读（缺省不读，避免每教室都响）
    const QJsonObject flags = params.value(QStringLiteral("flags")).toObject();
    const bool tts = flags.value(QStringLiteral("speech")).toBool(false)
                     || flags.value(QStringLiteral("speech_enabled")).toBool(false);
    // flags.severity → 全屏配色（2026-10-05 补）：站点已发对字段，被控端此前不读。
    // 站点白名单已收敛为 remind/inform/urgent 三档；未知值一律回落 remind（不做任意串插进样式）。
    const QString severity = flags.value(QStringLiteral("severity")).toString().trimmed().toLower();
    // flags.emergency_confirm → 紧急锁定：不自动关 + 强制置顶（2026-10-05 补）
    // 站点此前在 params 顶层也发过 emergency_confirm（broadcast.ts 的注入分支），此处一并兜底。
    const bool emergency = flags.value(QStringLiteral("emergency_confirm")).toBool(false)
                           || flags.value(QStringLiteral("topmost")).toBool(false)
                           || params.value(QStringLiteral("emergency_confirm")).toBool(false);
    // 确认 + 快捷回复（2026-10-07 · 通知被控端确认快捷回复）：
    // flags.confirm → 需要学生点确认；flags.replies → 快捷回复话术列表（如 ["收到","好的"]）。
    const bool needConfirm = flags.value(QStringLiteral("confirm")).toBool(false)
                             || params.value(QStringLiteral("confirm")).toBool(false);
    QStringList replies;
    const QJsonArray repArr = flags.value(QStringLiteral("replies")).toArray();
    if (!repArr.isEmpty()) {
        for (const QJsonValue &v : repArr) replies.append(v.toString());
    } else {
        // 默认快捷回复（不传 replies 时给一组通用话术）
        replies = { QStringLiteral("\u6536\u5230"), QStringLiteral("\u597d\u7684"),
                    QStringLiteral("\u7a0d\u540e\u5904\u7406") };
    }

    NotifyWindow::Kind k = NotifyWindow::Popup;
    if (kind == QStringLiteral("island")) k = NotifyWindow::Island;
    else if (kind == QStringLiteral("fullscreen")) k = NotifyWindow::Fullscreen;
    else k = NotifyWindow::Popup;   // popup 及未知值一律居中弹窗（安全默认）

    // ── 弹幕（2026-10-09 接线 · §1.2/§6.1）：kind=danmaku → 字幕式横向滚动 ──
    if (kind == QLatin1String("danmaku")) {
        if (!g_danmaku) g_danmaku = new DanmakuWidget();   // 懒建：只在真用弹幕时才开窗口
        g_danmaku->push(content.isEmpty() ? title : (title + QStringLiteral("：") + content));
        // 同样进托盘通知留存（与 popup 行为一致，别丢静默列表）
        if (g_tray) {
            g_tray->showMessage(title, content.isEmpty() ? title : content,
                                QSystemTrayIcon::Information, 10000);
        }
        return QString();
    }

    // ── 循环提醒（2026-10-09 接线 · §1.2 loopRemind）：flags.loop / params.repeat ──
    // 展示超时未确认 → 间隔重现；只有学生点确认才终止。id 用 notice_id 或 title（幂等刷新）。
    const bool loop = flags.value(QStringLiteral("loop")).toBool(false)
                      || params.value(QStringLiteral("repeat")).toBool(false)
                      || params.value(QStringLiteral("loopRemind")).toBool(false);
    if (loop) {
        const QString loopId = params.value(QStringLiteral("notice_id")).toString(
            params.value(QStringLiteral("id")).toString());
        LoopReminder::Config cfg;
        cfg.intervalSec = params.value(QStringLiteral("intervalSec")).toInt(
                              flags.value(QStringLiteral("interval_sec")).toInt(60));
        // 确认回调：学生点「确认」→ 终止循环（唯一正常退出路径），并二次回执云端
        std::function<void(const QString &)> loopConfirm = [loopId, id](const QString &reply) {
            g_loopReminder.confirm(loopId);
            if (!id.isEmpty()) {
                QJsonObject d;
                d.insert(QStringLiteral("confirmed"), true);
                d.insert(QStringLiteral("reply"), reply);
                sendActionReceipt(id, QStringLiteral("notify"), QStringLiteral("confirmed"),
                                  QString(), d);
            }
        };
        // 立即展示第一轮：并入灵动岛（确认按钮现在在**岛本体**上），点「确认」→ loopConfirm
        {
            QStringList loopActs;
            loopActs << QStringLiteral("确认");
            for (const QString &rr : replies)
                if (!rr.trimmed().isEmpty() && !loopActs.contains(rr)) loopActs << rr;
            IslandOverlay::instance()->setSeverity(severity);
            IslandOverlay::instance()->showWithActions(title, content, QStringLiteral("bell"),
                                                       IslandOverlay::Form::Centered, loopActs,
                                                       loopConfirm, 0);
        }
        if (tts)
            speakText(content.trimmed().isEmpty() ? title
                                                  : (title + QStringLiteral("：") + content));
        if (g_tray) {
            g_tray->showMessage(title, content.isEmpty() ? title : content,
                                QSystemTrayIcon::Information, 10000);
        }
        return QString();
    }

    // 确认/快捷回复回调：把结果作为**二次回执**发给云端（原 notify 回执已发 done，
    // 这条补充 confirmed+reply，云端 recordResult 按 id 配对；管理端据此显示"已确认"）。
    std::function<void(const QString &reply)> onConfirm;
    if (needConfirm || emergency) {
        onConfirm = [id](const QString &reply) {
            if (id.isEmpty()) return;
            QJsonObject d;
            d.insert(QStringLiteral("confirmed"), true);
            d.insert(QStringLiteral("reply"), reply);
            sendActionReceipt(id, QStringLiteral("notify"), QStringLiteral("confirmed"), QString(), d);
        };
    }

    // ── #98：形态分流（2026-10-09）────────────────────────────────────────
    // 用户要的是"弹窗则居中，全屏则放大全屏"。所以**纯展示类**的通知走灵动岛：
    //   popup      → 灵动岛 Centered（屏幕正中卡片）
    //   fullscreen → 灵动岛 Fullscreen（铺满整屏，带放大过渡）
    //   island     → 灵动岛 Capsule（顶部胶囊）
    // ⚠️ 需要交互的（要确认 / 有快捷回复 / 紧急"点一下才关"）**仍走 NotifyWindow**：
    //    灵动岛现在没有按钮，硬塞过去等于把"确认"这个能力悄悄砍掉 —— 那是功能倒退。
    // ── 时长：>0 用给定秒数；0/缺省 → 按字数自适应（约 5 秒起）────────────────────
    // ⚠️ 2026-10-09 修 bug：原来直接把 seconds*1000 传给灵动岛，而灵动岛按
    //    "durationMs>0 才自动收"判定 ⇒ seconds=0 时永远不消失、一直置顶。
    //    这里把"0=自适应"的契约在**传参前**兑现，公式复刻 NotifyWindow 那份
    //    （短 5s、长内容按字数抬升），避免两处口径漂移。
    const int durMs = seconds > 0
        ? qMin(seconds, 3600) * 1000
        : int(qMax(5000.0, 2500.0 + (title + QStringLiteral("：") + content).length() * 120.0));

    // ── 形态分流：**一律经灵动岛**（2026-10-09 起「确认」也并入岛本体，不再走 NotifyWindow）──
    //   popup → Centered（正中卡片）｜island → Capsule（顶部胶囊）｜fullscreen → Fullscreen
    IslandOverlay::Form f = IslandOverlay::Form::Capsule;
    if (k == NotifyWindow::Fullscreen)     f = IslandOverlay::Form::Fullscreen;
    else if (k == NotifyWindow::Popup)     f = IslandOverlay::Form::Centered;

    // ⚠️ 2026-10-09 修 bug「全屏通知没有颜色」：配色档必须**无条件先设**。
    //    原来只在"确认/紧急"那条 else 分支里调 setSeverity ⇒ **纯展示的全屏通知**
    //    （kind=fullscreen 且不需要确认）压根没设 severity，全屏永远是默认黑底，
    //    remind/inform/urgent 三档配色全被吞掉。而且不设还会残留上一条的档。
    //    （severity 只对 Fullscreen 生效，Centered/Capsule 走各自的固定配色，不受影响。）
    IslandOverlay::instance()->setSeverity(severity);

    const bool needsInteraction = needConfirm || emergency;
    if (!needsInteraction) {
        islandShow(title, content, QStringLiteral("bell"), durMs, false, QString(), f);
    } else {
        // 确认 / 紧急：按钮行（「确认」+ 快捷回复），点哪枚回哪句。
        // ⚠️ 胶囊那档太矮放不下按钮 ⇒ 升到 Centered（全屏仍是 Fullscreen）。
        if (f == IslandOverlay::Form::Capsule) f = IslandOverlay::Form::Centered;
        QStringList acts;
        acts << QStringLiteral("确认");
        for (const QString &rr : replies)
            if (!rr.trimmed().isEmpty() && !acts.contains(rr)) acts << rr;
        IslandOverlay::instance()->showWithActions(title, content, QStringLiteral("bell"),
                                                   f, acts, onConfirm, 0);
    }

    // TTS（调用方要求时）：读「标题：正文」，与旧 NotifyWindow 的行为保持一致
    if (tts)
        speakText(content.trimmed().isEmpty() ? title
                                              : (title + QStringLiteral("：") + content));

    // Windows 通知中心留存（托盘在才有）—— 换呈现不该丢掉静默列表
    if (g_tray) {
        g_tray->showMessage(title, content.trimmed().isEmpty() ? title : content,
                            QSystemTrayIcon::Information, 10000);
    }
    return QString();   // 成功
}

/* ══════════════════════════════════════════════════════════════════════════
 * OTA 自更新（self_update 指令 · 2026-10-05）
 *
 * 链路：云端下发 self_update{url,sha256,version} → 本机下载 → 校验 sha256
 *       → 派"重启助手" → 静默执行安装包（Inno /VERYSILENT，保留 /S 兼容旧 NSIS 包）→ 助手把新版拉起来。
 *
 * 为什么"自己换自己"还能活：安装包第一步就会 Stop-Process 掉本进程，
 *   所以**必须先派一个独立的重启助手**（等若干秒 → 启动安装目录下的 exe），
 *   再启动安装包。助手是独立 cmd 进程，不受本进程被杀影响。
 *
 * 安全护栏（缺一不可，缺就不升）：
 *   ① URL 只认 http(s)；② sha256 必须是 64 位小写十六进制（禁止无校验更新）；
 *   ③ 下载体积上限 kOtaMaxBytes，超限即中止并删档；④ 落盘只在 ota 目录内的 .exe。
 *
 * ⚠️ 诚实声明：下载/校验/触发安装这条链路**无法在本机端到端验证**
 *   （一执行就会把本进程换掉）。故本实现只保证"护栏与逻辑正确、编译通过"，
 *   真实升级必须在教室机灰度验证后再全网铺开。
 * 应急开关：设 STE_QT_OTA_DISABLE=1 可在某台机器上硬关掉自更新（默认开）。
 * ══════════════════════════════════════════════════════════════════════════ */

static const qint64 kOtaMaxBytes = 300LL * 1024 * 1024;   // 安装包体积上限 300MB

static QNetworkAccessManager *g_otaNam = nullptr;
static QNetworkReply *g_otaReply = nullptr;
static QFile *g_otaFile = nullptr;
static QCryptographicHash *g_otaHash = nullptr;
static QString g_otaId, g_otaSha, g_otaVer, g_otaPath;
static qint64 g_otaGot = 0;
static bool g_otaBusy = false;
// 见前面 otaIsBusy 的说明：强制升级的触发判定在文件更前面，只能靠这个函数看到这个标志。
static bool otaIsBusy() { return g_otaBusy; }

// 统一回执出口（被控端→云端）。断链时不静默：如实记一条，避免"以为回了其实没回"。
static void sendActionReceipt(const QString &id, const QString &action, const QString &result,
                              const QString &error = QString(), const QJsonObject &data = QJsonObject())
{
    if (!g_ws || g_ws->state() != QAbstractSocket::ConnectedState) {
        qInfo("[agent-qt] 回执欲发但当前未连接（id=%s result=%s），如实记录", qPrintable(id), qPrintable(result));
        return;
    }
    QJsonObject p;
    p.insert(QStringLiteral("id"), id);
    p.insert(QStringLiteral("action"), action);
    p.insert(QStringLiteral("result"), result);
    if (!error.isEmpty()) p.insert(QStringLiteral("error"), error);
    if (!data.isEmpty()) p.insert(QStringLiteral("data"), data);
    g_ws->sendTextMessage(makeEnvelope(QStringLiteral("receipt"), p));
    qInfo("[agent-qt] 📤 回执 id=%s action=%s result=%s%s",
          qPrintable(id), qPrintable(action), qPrintable(result),
          error.isEmpty() ? "" : qPrintable(" error=" + error));
}

static void otaOnFinished();   // 前向声明（下载结束回调）

// 启动一次自更新。**异步**：立即回执 started，下载完成后再回 installing/failed。
static void startSelfUpdate(const QString &id, const QJsonObject &params)
{
    const QString action = QStringLiteral("self_update");

    if (qEnvironmentVariableIsSet("STE_QT_OTA_DISABLE")) {
        sendActionReceipt(id, action, QStringLiteral("failed"),
                          QStringLiteral("本机已设 STE_QT_OTA_DISABLE，拒绝自更新"));
        return;
    }
    if (g_otaBusy) {
        sendActionReceipt(id, action, QStringLiteral("failed"), QStringLiteral("已有一个更新任务在进行中"));
        return;
    }

    const QString url = params.value(QStringLiteral("url")).toString().trimmed();
    const QString sha = params.value(QStringLiteral("sha256")).toString().trimmed().toLower();
    const QString ver = params.value(QStringLiteral("version")).toString().trimmed();

    // 护栏①：只认 http(s)（挡掉 file:// / 本地路径注入）
    if (!url.startsWith(QStringLiteral("http://")) && !url.startsWith(QStringLiteral("https://"))) {
        sendActionReceipt(id, action, QStringLiteral("failed"),
                          QStringLiteral("url 必须是 http(s) 地址：%1").arg(url));
        otaNoteFailure(QStringLiteral("url 不合法"));
        return;
    }
    // 护栏②：必须有合法 sha256 —— 没有校验的"更新"等于任意代码执行，一律拒绝
    static const QRegularExpression hex64(QStringLiteral("^[0-9a-f]{64}$"));
    if (!hex64.match(sha).hasMatch()) {
        sendActionReceipt(id, action, QStringLiteral("failed"),
                          QStringLiteral("sha256 必须是 64 位小写十六进制（拒绝无校验更新）"));
        otaNoteFailure(QStringLiteral("sha256 不合法"));
        return;
    }

    // 落地目录：%LOCALAPPDATA% 下的 xingjikong/ota（注意：注释行末尾不能是反斜杠，
    // 否则 C/C++ 会当成续行符把下一行吞掉 —— 这里踩过一次，记下）
    const QString dir = qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong/ota");
    QDir().mkpath(dir);
    QString base = QUrl(url).fileName();
    // 护栏④：只当安装包用，且只落在 ota 目录里的 .exe
    if (base.isEmpty() || !base.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
        base = QStringLiteral("stelarith-agent-update.exe");
    g_otaPath = dir + QLatin1Char('/') + base;

    g_otaFile = new QFile(g_otaPath);
    if (!g_otaFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        sendActionReceipt(id, action, QStringLiteral("failed"),
                          QStringLiteral("无法写入下载文件：%1").arg(g_otaPath));
        otaNoteFailure(QStringLiteral("无法写入下载目录（盘满了？）"));
        delete g_otaFile; g_otaFile = nullptr;
        return;
    }

    g_otaId = id; g_otaSha = sha; g_otaVer = ver; g_otaGot = 0; g_otaBusy = true;
    if (g_otaHash) delete g_otaHash;
    g_otaHash = new QCryptographicHash(QCryptographicHash::Sha256);
    if (!g_otaNam) g_otaNam = new QNetworkAccessManager();

    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("StelarithAgentQt/%1").arg(kAppVersion));

    // 先回执"已受理"：管理端据此确认指令被接下（长下载不至于看起来"没反应"）
    sendActionReceipt(id, action, QStringLiteral("started"), QString(),
                      QJsonObject{{QStringLiteral("stage"), QStringLiteral("downloading")},
                                  {QStringLiteral("version"), ver}, {QStringLiteral("url"), url}});
    qInfo("[agent-qt] ⬇ OTA 开始下载 %s → %s（期望 sha256=%s）",
          qPrintable(url), qPrintable(g_otaPath), qPrintable(sha));

    g_otaReply = g_otaNam->get(req);

    // 注意：这些 lambda **无捕获**，只读命名空间级全局量，避免函数返回后引用悬空。
    QObject::connect(g_otaReply, &QNetworkReply::readyRead, [] {
        if (!g_otaReply || !g_otaFile) return;
        const QByteArray chunk = g_otaReply->readAll();
        g_otaGot += chunk.size();
        if (g_otaGot > kOtaMaxBytes) { g_otaReply->abort(); return; }  // 护栏③
        g_otaFile->write(chunk);
        g_otaHash->addData(chunk);
    });
    QObject::connect(g_otaReply, &QNetworkReply::finished, [] { otaOnFinished(); });
}

// 下载结束：关文件、验 sha256、通过则派助手 + 执行静默安装；否则如实回 failed。
static void otaOnFinished()
{
    const QString action = QStringLiteral("self_update");
    QNetworkReply *reply = g_otaReply;
    if (!reply) return;
    const QNetworkReply::NetworkError nerr = reply->error();
    const QString nerrStr = reply->errorString();
    reply->deleteLater();
    g_otaReply = nullptr;
    if (g_otaFile) { g_otaFile->close(); g_otaFile->deleteLater(); g_otaFile = nullptr; }
    g_otaBusy = false;

    const QString id = g_otaId, ver = g_otaVer, sha = g_otaSha, path = g_otaPath;
    const qint64 got = g_otaGot;
    const QString gotSha = g_otaHash ? QString::fromLatin1(g_otaHash->result().toHex()) : QString();
    if (g_otaHash) { delete g_otaHash; g_otaHash = nullptr; }

    // 三种失败都要进"退避账"：强制升级失败的机器不能立刻又装一遍。
    if (got > kOtaMaxBytes) {
        QFile::remove(path);
        sendActionReceipt(id, action, QStringLiteral("failed"),
                          QStringLiteral("安装包超过体积上限（%1 字节）").arg(kOtaMaxBytes));
        otaNoteFailure(QStringLiteral("安装包超过体积上限"));
        return;
    }
    if (nerr != QNetworkReply::NoError) {
        QFile::remove(path);
        sendActionReceipt(id, action, QStringLiteral("failed"),
                          QStringLiteral("下载失败：%1").arg(nerrStr));
        otaNoteFailure(QStringLiteral("下载失败：").append(nerrStr).left(160));
        return;
    }
    if (gotSha != sha) {
        QFile::remove(path);
        sendActionReceipt(id, action, QStringLiteral("failed"),
                          QStringLiteral("sha256 校验不符（期望 %1 实得 %2），已删除下载文件").arg(sha, gotSha));
        otaNoteFailure(QStringLiteral("sha256 校验不符"));
        return;
    }
    qInfo("[agent-qt] ✅ OTA 校验通过（%lld 字节，sha256=%s），准备安装", (long long)got, qPrintable(gotSha));

    // ①-0 动安装器之前：先做"回滚三件套"。顺序不能反（装完就没人有机会备份了）。
    //   · 删掉 ota-last-ok.txt，否则看门狗会拿**上一版的**成功标记冒充新版本起来了。
    //   · 备份当前 exe → 写 pending（看门狗靠它知道"这次升级该验谁的账"）
    //   · 备份失败就直接不装：宁可升不上，也不能留一台没有退路的机器。
    QFile::remove(otaOkPath());
    const QString prev = otaBackupExe();
    if (prev.isEmpty()) {
        QFile::remove(path);
        sendActionReceipt(id, action, QStringLiteral("failed"),
                          QStringLiteral("备份当前 exe 失败，拒绝无回滚地覆盖自己"));
        otaNoteFailure(QStringLiteral("备份当前 exe 失败（拒绝无回滚升级）"));
        return;
    }
    otaWritePending(prev, ver);

    // ① 先派重启助手：等安装器把文件换完，再把新版拉起来（本进程随后会被安装器杀掉）
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString helper = exeDir + QStringLiteral("/ota-relaunch.bat");
    {
        QFile h(helper);
        if (h.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            // 纯 ASCII + CRLF：cmd.exe 按 GBK 解析 .bat，中文注释会吞行（同 install-autostart 的教训）
            //
            // ⚠️ 这份脚本是**升级前**由旧版本写下的，之后不会被新版本改写 ——
            //    所以它体内是**旧代码的字节**：新版本一启动就崩也不影响它把机器救回来。
            //    只看 agent 自己是不够的（新 exe 崩了它自己也已经没了），这道狗必须在进程外。
            const QString content =
                QStringLiteral("@echo off\r\n"
                               "rem Stelarith OTA guard (auto-generated by the PRE-upgrade build).\r\n"
                               "rem Pure ASCII on purpose: cmd.exe parses .bat as GBK.\r\n"
                               "rem 1) give the installer time to replace files\r\n"
                               "ping -n 31 127.0.0.1 >nul\r\n"
                               "rem 2) start the new agent, then see whether it ever reached the cloud\r\n"
                               "start \"\" \"%3\"\r\n"
                               "ping -n 91 127.0.0.1 >nul\r\n"
                               "if exist \"%1\" goto done\r\n"
                               "rem 3) it never registered -> restore the pre-upgrade exe and relaunch it\r\n"
                               "del /f /q \"%1\" >nul 2>&1\r\n"
                               "copy /Y \"%2\" \"%3\" >nul\r\n"
                               "start \"\" \"%3\"\r\n"
                               ":done\r\n"
                               "del /f /q \"%~f0\"\r\n")
                    .arg(otaOkPath(), otaPrevPath(), exeDir + QStringLiteral("/stelarith-agent-qt.exe"));
            h.write(content.toUtf8());
            h.flush();
            h.close();
        } else {
            qInfo("[agent-qt] ⚠ OTA 重启助手写不出（%s）—— 升级后需靠下次登录拉起", qPrintable(helper));
        }
    }
    if (QFile::exists(helper))
        QProcess::startDetached(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), helper});

    // ② 启动安装包。它先 Stop-Process 掉本进程，再覆盖文件。
    //   静默参数同时给 NSIS(/S) 与 Inno(/VERYSILENT)：旧版 NSIS 包认 /S，
    //   新版 Inno 包认 /VERYSILENT；两者都静默，过渡期不弹窗。
    const bool launched = QProcess::startDetached(
        path, QStringList{QStringLiteral("/S"), QStringLiteral("/VERYSILENT"),
                          QStringLiteral("/SUPPRESSMSGBOXES"), QStringLiteral("/NORESTART")});
    if (!launched) {
        sendActionReceipt(id, action, QStringLiteral("failed"),
                          QStringLiteral("安装包启动失败：%1").arg(path));
        return;
    }
    // 这是本进程能发出的**最后一条回执**（随后被安装器替换）。
    // 最终是否成功，以"设备重连后 register 上报的新 version"为准。
    sendActionReceipt(id, action, QStringLiteral("installing"), QString(),
                      QJsonObject{{QStringLiteral("stage"), QStringLiteral("installing")},
                                  {QStringLiteral("version"), ver},
                                  {QStringLiteral("path"), path}});
    qInfo("[agent-qt] 🚀 OTA 已启动安装包（/S + /VERYSILENT），重启助手已派发；本进程即将被替换");
}

// ── 班级监控上传（设计文档 3.6 第一版，2026-10-07）──
// record_stop 后把本地 rec-*.mp4 POST 到云端 /api/recordings/upload（Bearer 设备令牌）。
// 异步执行：QNetworkAccessManager 是 QObject，放在这里创建并负责自己生命周期。
void uploadRecordingAsync(const QString &mp4Path)
{
    if (!QFileInfo::exists(mp4Path)) return;
    const QString wsUrl = qEnvironmentVariable("STE_QT_WS_URL").trimmed();
    if (wsUrl.isEmpty()) { qWarning("[rec] 上传跳过：没有 STE_QT_WS_URL"); return; }
    // ws://host:port/ws/agent → http://host:port/api/recordings/upload?uid=...
    QString httpUrl = wsUrl;
    httpUrl.replace(QLatin1String("wss://"), QLatin1String("https://"))
           .replace(QLatin1String("ws://"), QLatin1String("http://"));
    const int wsIdx = httpUrl.indexOf(QLatin1String("/ws/"));
    if (wsIdx > 0) httpUrl.truncate(wsIdx);
    httpUrl += QStringLiteral("/api/recordings/upload?uid=") + g_uid;

    auto *mgr = new QNetworkAccessManager();
    QNetworkRequest req((QUrl(httpUrl)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    const QString token = qEnvironmentVariable("STE_QT_WS_TOKEN").trimmed();
    if (!token.isEmpty()) req.setRawHeader("Authorization", "Bearer " + token.toUtf8());

    QFile *file = new QFile(mp4Path);
    if (!file->open(QIODevice::ReadOnly)) {
        qWarning("[rec] 上传失败：打不开 %s", qPrintable(mp4Path));
        delete file; delete mgr;
        return;
    }
    QNetworkReply *reply = mgr->post(req, file);
    QObject::connect(reply, &QNetworkReply::finished, [reply, file, mgr, mp4Path]() {
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (code == 200) {
            qInfo("[rec] 📹 录制已上传云端：%s", qPrintable(mp4Path));
        } else {
            qWarning("[rec] 上传失败（HTTP %d）：%s", code, qPrintable(mp4Path));
        }
        reply->deleteLater();
        file->deleteLater();
        mgr->deleteLater();
    });
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
        // ① 去重：老版本云端（还没有 no_resend 那套逻辑）重连时会把同一条再补发一次，
        //    照单全收就是"关了又关、重启了又重启"。命中就直接跳过、如实回执，不重复执行。
        if (g_terminalDone.contains(g_lastInstrId)) {
            const QString dup = QStringLiteral("本机已执行过该指令（指令 id %1），按重复下发保护跳过%2")
                                    .arg(g_lastInstrId,
                                         action == QStringLiteral("reboot") ? QStringLiteral("重启") : QStringLiteral("关机"));
            qInfo("[agent-qt] ⛔ %s", qPrintable(dup));
            out.result = QStringLiteral("failed");
            out.error  = dup;
            return out;
        }

        // ② 先落记录：这条 id 就算机器马上关了，下次（老版云端补发）也认。
        markTerminalDone(g_lastInstrId);

        // ③ 回执由调用方在本函数返回后发出（第 4206 行附近），所以这里**只排期、不执行**，
        //    留 2.5 秒给回执把 socket 写出去 —— 顺序反了回执就永远发不出去（恶性BUG 根因）。
        const DWORD flags = (action == QStringLiteral("reboot") ? EWX_REBOOT : EWX_SHUTDOWN) | EWX_FORCE;
        const bool isReboot = (action == QStringLiteral("reboot"));
        out.result = QStringLiteral("done");
        out.error.clear();
        out.data.insert(QStringLiteral("shutting_down"), true);
        out.data.insert(QStringLiteral("action"), action);
        QTimer::singleShot(2500, [flags, isReboot]() {
            enableShutdownPriv();
            if (ExitWindowsEx(flags, SHTDN_REASON_FLAG_PLANNED))
                qInfo("[agent-qt] ⏻ 已执行%s（EWX 返回 TRUE）", isReboot ? "重启" : "关机");
            else
                // 失败要带系统原话：只写"关机失败"会把四种病藏成一个样，误判过一次
                qWarning("[agent-qt] ⏻ %s 失败（ExitWindowsEx 错误码 %u）—— 多半是没开关机权限或不是管理员起的",
                         isReboot ? "重启" : "关机", (unsigned)GetLastError());
        });
        return out;
    }

    if (action == QStringLiteral("input")) {
        const QString kind = params.value(QStringLiteral("kind")).toString();
        // 2026-10-07：三键。管理端会把 button 一起带过来（left/right/middle）。
        // 旧版不管按的是哪个键，down/up 一律写 MOUSEEVENTF_LEFTDOWN/LEFTUP ——
        // 右键点不出右键菜单、中键的自动滚也全废，而且"按住右键拖动"会被当成左键拖。
        const QString btn = params.value(QStringLiteral("button")).toString();
        const DWORD flagDown = (btn == QStringLiteral("right"))  ? MOUSEEVENTF_RIGHTDOWN
                             : (btn == QStringLiteral("middle")) ? MOUSEEVENTF_MIDDLEDOWN
                             : MOUSEEVENTF_LEFTDOWN;
        const DWORD flagUp = (btn == QStringLiteral("right"))  ? MOUSEEVENTF_RIGHTUP
                           : (btn == QStringLiteral("middle")) ? MOUSEEVENTF_MIDDLEUP
                           : MOUSEEVENTF_LEFTUP;

        // ⚠️ 绝对坐标的基准是**整个虚拟桌面**，不是主屏。
        //   MOUSEEVENTF_VIRTUALDESK 的 dx/dy 按虚拟桌面归一化，而老写法拿
        //   SM_CXSCREEN / SM_CYSCREEN（只要主屏的尺寸）去换算 —— 教室机接投影仪 +
        //   副屏时（虚拟桌面比主屏宽一截，左上还有负偏移）算出来的落点整体偏出去，
        //   越靠右偏得越狠。这就是"我点这儿、光标跑到那儿"的根因。
        const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

        INPUT in = {};
        in.type = INPUT_MOUSE;
        if (kind == QStringLiteral("move") || kind == QStringLiteral("down") || kind == QStringLiteral("up")) {
            // 归一化坐标夹到 [0,1]：越界就别发，免得把光标甩到桌面外去（会被夹回角上）
            const double nx = qBound(0.0, params.value(QStringLiteral("x")).toDouble(), 1.0);
            const double ny = qBound(0.0, params.value(QStringLiteral("y")).toDouble(), 1.0);
            const int px = vx + qRound(nx * (vw - 1));
            const int py = vy + qRound(ny * (vh - 1));
            in.mi.dx = (LONG)((px - vx) * 65535 / (vw - 1));
            in.mi.dy = (LONG)((py - vy) * 65535 / (vh - 1));
            in.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
            if (kind == QStringLiteral("move"))      in.mi.dwFlags |= MOUSEEVENTF_MOVE;
            else if (kind == QStringLiteral("down")) in.mi.dwFlags |= flagDown;
            else                                     in.mi.dwFlags |= flagUp;
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
        // ⚠️ `-list_devices` **必须带布尔值 `true`**：只写 `-list_devices` 会把紧跟的 `-f`
        //    当成它的值吃掉，ffmpeg 于是直接去开 dummy，**一条设备都不打印**（2026-10-09 实测真因）。
        //    另外别再挂 `-loglevel error` —— 设备列表是 info 级输出的，挂上就全被吞。
        QProcess p;
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start(ff, { QStringLiteral("-hide_banner"), QStringLiteral("-list_devices"),
                      QStringLiteral("true"), QStringLiteral("-f"),
                      QStringLiteral("dshow"), QStringLiteral("-i"),
                      QStringLiteral("dummy") });
        if (!p.waitForFinished(4000)) {
            p.kill();
            p.waitForFinished(1500);
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("ffmpeg 枚举设备超时（DirectShow 可能不可用）");
            return out;
        }
        const QString txt = QString::fromUtf8(p.readAll());
        QJsonArray cams, mics;
        parseDshowDevices(txt, cams, mics);
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("cameras"), cams);
        out.data.insert(QStringLiteral("microphones"), mics);
        out.data.insert(QStringLiteral("count"), cams.size());
        out.data.insert(QStringLiteral("boundDevice"), boundCamera());   // "" = 未绑定
        return out;
    }

    // 绑定摄像头：管理端「监控中心」选定一枚后落在这里，之后抓拍/录制默认用它。
    // device 传空 = 解绑（回落默认）。绑定存本机（只有本机能枚举），不落 agent.env。
    if (action == QStringLiteral("camera_bind")) {
        const QString dev = params.value(QStringLiteral("device")).toString().trimmed();
        if (!saveBoundCamera(dev)) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("camera_bind: 无法写入绑定文件 %1")
                            .arg(QDir::toNativeSeparators(cameraDevPath()));
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("boundDevice"), dev);
        out.data.insert(QStringLiteral("note"), dev.isEmpty()
                                                      ? QStringLiteral("已解绑，抓拍/录制回落默认设备")
                                                      : QStringLiteral("已绑定，抓拍/录制默认用它"));
        return out;
    }

    // 摄像头自检：按绑定设备（或指定 device）真抓一帧，只回"能不能用 + 耗时 + 原因"。
    // 与 camera_snapshot 的区别：这是**诊断**用途（管理端「测试」按钮），不管画面好看不好看。
    if (action == QStringLiteral("camera_test")) {
        const QString ff = ffmpegBin();
        if (ff.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("找不到 ffmpeg（可设 STE_QT_FFMPEG）");
            return out;
        }
        QString dev = params.value(QStringLiteral("device")).toString().trimmed();
        const bool fromBind = dev.isEmpty();
        if (dev.isEmpty()) dev = boundCamera();
        if (dev.isEmpty()) dev = QStringLiteral("video0");

        QDir().mkpath(g_shotDir);
        const QString path = g_shotDir + QStringLiteral("/_camtest-%1.png").arg(QDateTime::currentMSecsSinceEpoch());
        QProcess p;
        p.setProcessChannelMode(QProcess::SeparateChannels);
        const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
        // ⚠️ 输入串是 `video=<设备名>`。**不是** `video_device=` —— 后者 ffmpeg 直接报
        //    "Malformed dshow input string"（2026-10-09 实测，旧实现对相机永远打不开）。
        p.start(ff, { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                      QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"),
                      QStringLiteral("dshow"), QStringLiteral("-i"), QStringLiteral("video=%1").arg(dev),
                      QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-q:v"),
                      QStringLiteral("2"), path });
        const bool finished = p.waitForFinished(9000);
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - t0;
        if (!finished) {
            p.kill();
            p.waitForFinished(1500);
            QFile::remove(path);
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("自检超时（%1ms，设备 %2）：设备被占用或不出帧").arg(elapsed).arg(dev);
            out.data.insert(QStringLiteral("device"), dev);
            out.data.insert(QStringLiteral("elapsedMs"), (int)elapsed);
            return out;
        }
        const qint64 bytes = QFileInfo::exists(path) ? QFileInfo(path).size() : 0;
        if (p.exitCode() != 0 || bytes == 0) {
            QFile::remove(path);
            const QString err = QString::fromUtf8(p.readAllStandardError()).trimmed().left(300);
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("自检失败（退出码 %1，设备 %2）：%3")
                            .arg(p.exitCode()).arg(dev)
                            .arg(err.isEmpty() ? QStringLiteral("ffmpeg 未给 stderr") : err);
            out.data.insert(QStringLiteral("device"), dev);
            out.data.insert(QStringLiteral("elapsedMs"), (int)elapsed);
            return out;
        }
        QFile::remove(path);   // 自检不留垃圾
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("device"), dev);
        out.data.insert(QStringLiteral("fromBinding"), fromBind);
        out.data.insert(QStringLiteral("elapsedMs"), (int)elapsed);
        out.data.insert(QStringLiteral("bytes"), (qint64)bytes);
        out.data.insert(QStringLiteral("ok"), true);
        return out;
    }

    // 拍一张：ffmpeg 从指定设备（默认 video0）抓一帧写 PNG。回执给路径 + 字节数 + base64 缩略图。
    if (action == QStringLiteral("camera_snapshot")) {
        const QString ff = ffmpegBin();
        if (ff.isEmpty()) { out.result = QStringLiteral("failed"); out.error = QStringLiteral("找不到 ffmpeg"); return out; }
        // 设备：显式 device > 本机绑定的摄像头 > video0（2026-10-09）
        QString dev = params.value(QStringLiteral("device")).toString().trimmed();
        if (dev.isEmpty()) dev = boundCamera();
        if (dev.isEmpty()) dev = QStringLiteral("video0");
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
                      QStringLiteral("dshow"), QStringLiteral("-i"), QStringLiteral("video=%1").arg(dev),
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
        // 设备：显式 device > 本机绑定的摄像头 > video0（2026-10-09）
        QString dev = params.value(QStringLiteral("device")).toString().trimmed();
        if (dev.isEmpty()) dev = boundCamera();
        if (dev.isEmpty()) dev = QStringLiteral("video0");
        const int maxSec = qBound(1, params.value(QStringLiteral("duration_sec")).toInt(300), 3600);
        QDir().mkpath(g_shotDir);
        g_recOutPath = g_shotDir + QStringLiteral("/rec-%1.mp4").arg(QDateTime::currentMSecsSinceEpoch());
        g_recProc = new QProcess();
        g_recProc->setProcessChannelMode(QProcess::MergedChannels);
        g_recProc->start(ff, { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                               QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"),
                               QStringLiteral("dshow"), QStringLiteral("-i"),
                               QStringLiteral("video=%1").arg(dev),
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
        // 班级监控（设计文档 3.6 第一版）：录制完成后自动上传到云端 recordings/，
        // 供管理端列表 + 回放。上传失败不改变回执（本地文件仍在，可手工取）。
        uploadRecordingAsync(g_recOutPath);
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
        // 学生端知情（设计文档 6.5 隐私边界）：远控=屏幕正被老师查看，学生必须知道。
        // 灵动岛形态非打扰式提示（#98：统一走灵动岛），不遮操作。
        // 时长给的比原来的 3 秒长一些：远控一整节课都在，学生错过前 3 秒就再没提示了。
        islandShow(QStringLiteral("屏幕正在被查看"),
                   QStringLiteral("老师正在远程查看这台电脑的屏幕"), QStringLiteral("eye"), 8000);
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("active"), true);
        out.data.insert(QStringLiteral("fps"), want);
        out.data.insert(QStringLiteral("fpsBefore"), oldFps);
        out.data.insert(QStringLiteral("sessionSince"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
        qInfo("[agent-qt] 🔴 远控会话开启，抓屏帧率 %d → %d fps（已提示学生知情）", oldFps, want);
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
        // Truncate 显式带上：只给 WriteOnly 时，撞上"文件存在但处于删除挂起状态"
        // （刚被 QFile::remove 掉、而句柄还没被系统真正释放）会直接失败。
        if (!g_fileRecvFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            // 失败必须带**系统的原话**回来。以前只报"目标打不开（路径）"，
            // 于是目录不存在 / 没权限 / 被别的进程占着 / 删不掉 —— 四种病长得一模一样，
            // 每次都得从零猜一遍（2026-10-07 就因为这句含糊的话误判成"目录没建"）。
            const QString why = g_fileRecvFile->errorString();
            delete g_fileRecvFile;
            g_fileRecvFile = nullptr;
            // 同名打不开就换个名字再试一次：收件目录里撞名的往往是上一次没收口的残留，
            // 换名能让这次推送**成功**，而不是让用户对着一句"打不开"干瞪眼。
            const QString alt = g_recvDir + QStringLiteral("/") +
                                QFileInfo(clean).completeBaseName() + QStringLiteral("-") +
                                QString::number(QDateTime::currentSecsSinceEpoch()) +
                                QStringLiteral(".") + QFileInfo(clean).suffix();
            g_fileRecvFile = new QFile(alt);
            if (!g_fileRecvFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("file_push: 目标打不开（%1）：%2；换名 %3 也失败：%4")
                                .arg(target, why, alt, g_fileRecvFile->errorString());
                delete g_fileRecvFile;
                g_fileRecvFile = nullptr;
                g_fileRecvTarget.clear();
                return out;
            }
            qInfo("[agent] file_push 原名打不开（%s），已换名落盘 %s",
                  qPrintable(why), qPrintable(alt));
            g_fileRecvTarget = alt;
            g_fileRecvBytes = 0;
            g_fileRecvNext = 0;
            out.result = QStringLiteral("done");
            out.data.insert(QStringLiteral("target"), QDir::toNativeSeparators(alt));
            out.data.insert(QStringLiteral("totalBytes"), total);
            out.data.insert(QStringLiteral("ready"), true);
            // #92：接收端提醒。以前文件推送下来是**静默落盘**——学生根本不知道下了什么，
            // 老师问"作业收到了吗"还得去翻收件目录。这里在灵动岛上点一句，
            // 并且开交互（可点开看清楚文件名），而不是每次都弹窗打断上课。
            islandShow(QStringLiteral("收到文件"), QDir::toNativeSeparators(alt),
                       QStringLiteral("download"), 6000, true, QDir::toNativeSeparators(alt));
            trayNotify(QStringLiteral("收到文件"),
                       QFileInfo(alt).fileName(), QSystemTrayIcon::Information, 10000);
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("target"), QDir::toNativeSeparators(target));
        out.data.insert(QStringLiteral("totalBytes"), total);
        out.data.insert(QStringLiteral("ready"), true);
        islandShow(QStringLiteral("收到文件"), QDir::toNativeSeparators(target),
                   QStringLiteral("download"), 6000, true, QDir::toNativeSeparators(target));
        trayNotify(QStringLiteral("收到文件"), QFileInfo(target).fileName(),
                   QSystemTrayIcon::Information, 10000);
        return out;
    }

    // 2026-10-04（乙阶段 2）：大屏通知（notify）——popup 居中弹窗 / island 灵动岛 / fullscreen 全屏遮罩，
    // 可配 TTS 朗读。站点广播（broadcast.ts）改走云端 WS 指令通道后，教室大屏不再依赖 CIMS。
    if (action == QStringLiteral("notify")) {
        const QString err = notifyFromParams(params);
        if (!err.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = err;
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("notice_id"),
                        params.value(QStringLiteral("notice_id")).toVariant().toJsonValue());
        out.data.insert(QStringLiteral("kind"),
                        params.value(QStringLiteral("kind")).toString(QStringLiteral("popup")));
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

// ===== WebRTC 推流：内藏一个 QWebEngineView 跑采集页（canvas.captureStream）=====
// 不用 getDisplayMedia：那要用户手势授权，教室机无人值守必死。
static const char *kRtcPage = R"RTCPAGE(<!doctype html><html><head><meta charset="utf-8"></head>
<body style="margin:0;background:#000"><canvas id="cv" width="640" height="400"></canvas>
<script>
(function () {
  var cv = document.getElementById('cv');
  var ctx = cv.getContext('2d');
  var pc = null;
  // sentOffer/offeredAt/answeredAt：用来自愈"offer 发出去没人理"这件事。
  // 以前采集页只为一个 pc 发一次 offer，一旦它发早了（那时没人订阅，云端直接丢）就再也不重发，
  // 之后怎么订阅都是黑屏，而且日志上看不出任何异常。
  var st = { state: 'init', frames: 0, err: '', sentOffer: false, offeredAt: 0, answeredAt: 0, sdplen: -1, icePending: 0, dupAnswer: 0 };
  window.__st = st;
  window.__sigq = [];
  function sig(kind, sdp, cand) { window.__sigq.push({ kind: kind, sdp: sdp || '', candidate: cand || '' }); }
  // 远端（管理端）信令**只喂 pc，绝不回送**：__signal 收的是对端推来的 answer / ice，
  // 要是再把它塞回 sigq 由 rtcTickOnce 发回云端，就是两端无限互发 candidate 的死循环。
  var pendingIce = [];
  window.__signal = function (o) {
    if (!pc) { st.err = 'signal-nopc'; return; }
    if (o.kind === 'answer') {
      if (!o.sdp) { st.err = 'answer-nosdp'; return; }
      // answer 必须**幂等**：一个 pc 只吃得下一次 setRemoteDescription(answer)。
      // 喂第二次时 signalingState 已经是 stable，Chromium 直接抛
      // "Called in wrong state: stable"，而且这一抛之后 pc 的状态刷不回去
      // （真出现过 sdplen=-1、gd/cc 全乱），整条流就废了。
      // 重复 answer 的来源是真实存在的：管理端一退订再重订（重协商），
      // 被控端就会为同一个 pc 收到两份 answer。
      if (pc.signalingState !== 'have-local-offer') {
        st.dupAnswer = (st.dupAnswer || 0) + 1;
        return;
      }
      st.answeredAt = Date.now();
      pc.setRemoteDescription({ type: 'answer', sdp: o.sdp }).then(function () {
        st.sdplen = o.sdp.length;
        // 候选可能比 answer 先到（网络乱序/云端转发快于 answer 落地）。
        // 那时候 remoteDescription 还是 null，addIceCandidate 直接抛
        // InvalidStateError —— 这个错只在页面里，C++ 侧一个字都收不到，
        // 两端日志都"正常"，实际谁也没连上。所以先攒着，answer 落地再一起喂。
        var q = pendingIce; pendingIce = []; st.icePending = 0;
        q.forEach(function (c) {
          try { pc.addIceCandidate(c); } catch (e) { st.err = 'iceflush:' + e; }
        });
        if (pc.connectionState === 'connected') st.state = 'streaming';
        st.gather = pc.iceGatheringState; st.conn = pc.iceConnectionState;
      }).catch(function (e) { st.err = 'answer:' + e; st.state = 'failed'; });
    } else if (o.kind === 'ice') {
      // 对端推来的是 JSON 字符串（我们发它的本端 candidate 也是 JSON.stringify 出来的），
      // addIceCandidate 只接受对象，直接传字符串会报
      // "not of type 'RTCIceCandidateInit'" —— 而这条报错只落在页面里，C++ 侧完全看不见。
      var c = (typeof o.candidate === 'string') ? JSON.parse(o.candidate) : o.candidate;
      if (!c || !c.candidate) { st.err = 'ice-empty'; return; }
      if (!pc.remoteDescription) { pendingIce.push(c); st.icePending = pendingIce.length; return; }
      pc.addIceCandidate(c).catch(function (e) { st.err = 'ice:' + e; });
    }
  };
  window.__drain = function () { var a = window.__sigq; window.__sigq = []; return a; };
  window.__pushFrame = function (b64) {
    try {
      var img = new Image();
      img.onload = function () { try { ctx.drawImage(img, 0, 0, cv.width, cv.height); st.frames++; } catch (e) { st.err = 'draw:' + e; } };
      img.onerror = function () { st.err = 'decode-fail'; };
      img.src = 'data:image/jpeg;base64,' + b64;
    } catch (e) { st.err = 'push:' + e; }
  };
  window.__startStream = function () {
    try {
      if (st.state === 'negotiating' || st.state === 'streaming') return st.state;
      // 不配 iceServers：只收 host candidate。被控端与管理端在同一校园网/内网，够用；
      // 且不受外网 STUN 通不通影响（教室机演示时 STUN 一慢，ICE 收集就卡住，画面上不来）。
      pc = new RTCPeerConnection();
      var stream = cv.captureStream(15);
      stream.getTracks().forEach(function (t) { try { pc.addTrack(t, stream); } catch (e) { st.err = 'addtrack:' + e; } });
      st.state = 'negotiating';
      st.gather = pc.iceGatheringState;
      st.conn = pc.iceConnectionState;
      st.senders = pc.getSenders().length;
      st.tracks = pc.getSenders().reduce(function (n, s) { return n + s.track.length; }, 0);
      pc.ontrack = function () { st.state = 'streaming'; };
      // 只有候选串非空才算一个真候选：Chromium 在某些阶段会给 candidate 对象但 .candidate 是空串，
      // 那种空 candidate 发过去既没用还会让对端 addIceCandidate 直接报错
      pc.onicecandidate = function (ev) {
        // 必须 JSON.stringify 成**字符串**：这里直接把 ev.candidate（一个 RTCIceCandidate
        // 对象）塞进 sigq，出页面时就变成了个不透明的 QVariant，C++ 侧
        // o.value("candidate").toString() 对对象返回空串 → 包里压根插不上 candidate 键
        // → 云端只看到 {"kind":"ice"} → 对端收不到候选、ICE 永远完不成，两端日志都像没事。
        if (ev && ev.candidate && ev.candidate.candidate) sig('ice', '', JSON.stringify(ev.candidate));
      };
      pc.onconnectionstatechange = function () {
        if (pc.connectionState === 'connected') st.state = 'streaming';
        if (pc.connectionState === 'failed') { st.err = 'pc-failed'; st.state = 'failed'; }
      };
      pc.createOffer().then(function (o) { return pc.setLocalDescription(o); })
        .then(function () {
            st.gather = pc.iceGatheringState;
            st.conn = pc.iceConnectionState;
            st.sentOffer = true;
            st.offeredAt = Date.now();
            sig('offer', pc.localDescription.sdp, '');
        })
        .catch(function (e) { st.err = 'offer:' + e; st.state = 'failed'; });
    } catch (e) { st.err = 'start:' + e; st.state = 'failed'; }
    return st.state;
  };
  window.__reset = function () {
    try { if (pc) pc.close(); } catch (e) {}
    pendingIce = []; st.icePending = 0;
    st.state = 'init'; st.err = ''; st.sentOffer = false; st.offeredAt = 0; st.answeredAt = 0; st.sdplen = -1;
  };
  // 自愈：offer 发出去 8 秒还没等到 answer，就重建 pc 重新 offer。
  // 触发条件是"确实没人接"，不是无脑重发 —— 有人看的时候不会白烧 CPU。
  window.__tickRetry = function () {
    if (!pc || st.answeredAt) return;
    if (!st.sentOffer || Date.now() - st.offeredAt <= 8000) return;
    // __startStream 见到 negotiating/streaming 就直接 return，所以这里必须先清干净再重来
    try { if (pc) pc.close(); } catch (e) {}
    pc = null; st.state = 'init'; st.err = ''; st.sentOffer = false;
    window.__startStream();
  };
  // 云端回执说"这条信令没送到对端"时立刻重来，不用等超时
  window.__forceReoffer = function () { if (pc) st.offeredAt = 0; };
  // 诊断出口：pc / sigq 是闭包内的，页面全局作用域访问不到，只能从这里拿
  window.__diag = function () {
    return JSON.stringify(st) + ' | q=' + window.__sigq.length + ' | pc=' + (pc ? 'y' : 'n')
         + ' | gd=' + (pc ? pc.iceGatheringState : '-') + ' | cc=' + (pc ? pc.iceConnectionState : '-')
         + ' | ss=' + (pc ? pc.signalingState : '-')
         + ' | sdp=' + (pc && pc.localDescription ? pc.localDescription.sdp.length : -1);
  };
})();
</script></body></html>)RTCPAGE";

// 本端信令（offer / ice）发回云端，云端中继给管理端。
static void rtcSendSignal(const QString &kind, const QString &sdp, const QString &cand)
{
    if (!g_ws || g_ws->state() != QAbstractSocket::ConnectedState) {
        qWarning("[rtc] FAIL 信令没发出去（socket 不在线）kind=%s", qPrintable(kind));
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("kind"), kind);
    if (!sdp.isEmpty()) body.insert(QStringLiteral("sdp"), sdp);
    if (!cand.isEmpty()) body.insert(QStringLiteral("candidate"), cand);
    QJsonObject pay;
    pay.insert(QStringLiteral("payload"), body);
    pay.insert(QStringLiteral("from"), QStringLiteral("agent"));
    g_ws->sendTextMessage(makeEnvelope(QStringLiteral("rtc-") + kind, pay));
}

// 每 200ms：抓屏 → JPEG → 灌进采集页画布 → canvas 出帧 → 取回本端信令
static int g_rtcDiagTick = 0;
// 云端回执说"这条信令没送到对端"时置位，下一个 tick 就重新 offer（不用干等 8 秒超时）
static bool g_rtcForceReoffer = false;
static void rtcStop(const QString &why);   // 定义在下面（rtcStart/rtcStop 那一段），这里要给 tick 用

static void rtcTickOnce()
{
    if (!g_rtcOn || !g_rtcView) return;
    // 页面还在 setHtml 的异步加载窗口里：这会儿 runJavaScript 全是对着 about:blank 执行，
    // 一律静默失败（__startStream / __diag / __drain 都不存在）。等 loadFinished 起第一枪就行，
    // 这里直接跳过 —— 顺手把以前"每 tick 对着空白页刷一堆空转调用"也省了。
    if (!g_rtcPageReady) return;

    // 云端离线看门狗：跟云端断了这么久还没连回来，就当没人看画面，停推 + 回收采集页。
    // 判据不用"多久没收到对端信令"—— 画面稳定后对端本来就不发东西了，那样数会误杀正在看的画面；
    // 改用"跟云端断多久"，断了的这条链路本来就送不到任何一个人眼里，回收没有代价。
    // 阈值可用 STE_RTC_IDLE_MS 覆盖，方便排障时调小值快验。
    if (g_offlineSince) {
        const qint64 off = QDateTime::currentMSecsSinceEpoch() - g_offlineSince;
        // 空 = 用默认 20 秒；显式写 0 = 关掉这个兜底（别让默认值把开关吃掉）
        const QByteArray envIdle = qgetenv("STE_RTC_IDLE_MS");
        const qint64 limit = envIdle.isEmpty() ? 20000 : QString::fromLocal8Bit(envIdle).toLongLong();
        if (limit <= 0) return;
        if (off > limit) {
            rtcStop(QStringLiteral("与云端断开 %1 秒，画面送不出去，先收采集页").arg(off / 1000));
            return;
        }
    }

    if (++g_rtcDiagTick % 10 == 0) {   // 每 2 秒捞一次采集页内部状态
        g_rtcView->page()->runJavaScript(
            QStringLiteral("window.__diag()"),
            [](const QVariant &v) {
                qWarning("[rtc] tick %s", qPrintable(v.toString()));
            });
    }
    QScreen *sc = QGuiApplication::primaryScreen();
    if (!sc) { qWarning("[rtc] FAIL 没拿到屏幕，停推"); g_rtcOn = false; return; }
    // __startStream 已经在 loadFinished 里起过第一枪（幂等：已在 negotiating/streaming 就直接 return），
    // 这里再兜一次是给"重连后页面还在、但推流被 __reset 打断"的情况收尾。
    g_rtcView->page()->runJavaScript(QStringLiteral("window.__startStream();"));
    // 自愈先跑：上一次 offer 没等到 answer（多半是发出时没人订阅、被云端丢了）就重来一次
    if (g_rtcForceReoffer) {
        g_rtcForceReoffer = false;
        g_rtcView->page()->runJavaScript(QStringLiteral("window.__forceReoffer();"));
    }
    g_rtcView->page()->runJavaScript(QStringLiteral("window.__tickRetry();"));
    // 取信令（offer / ice）排在抓屏**之前**：抓屏偶发 GetDIBits 失败返回空图，
    // 原来这一步排在抓屏之后，一次失败就把整轮信令吞掉 → offer 永远发不出去，
    // 页面里 sigq 越堆越多，日志上还看不出任何异常。
    // 不能拿信号量等回调：回调要靠主线程事件循环派发，在 timer 回调里阻塞就是死等。
    g_rtcView->page()->runJavaScript(QStringLiteral("window.__drain()"), [](const QVariant &v) {
        if (!v.canConvert<QJsonArray>()) return;
        const QJsonArray arr = v.toJsonArray();
        for (const QJsonValue &it : arr) {
            const QJsonObject o = it.toObject();
            const QString kind = o.value(QStringLiteral("kind")).toString();
            if (kind == QLatin1String("offer")) {
                rtcSendSignal(QStringLiteral("offer"), o.value(QStringLiteral("sdp")).toString(), QString());
            } else if (kind == QLatin1String("ice")) {
                rtcSendSignal(QStringLiteral("ice"), QString(), o.value(QStringLiteral("candidate")).toString());
            } else if (kind == QLatin1String("err")) {
                qWarning("[rtc] FAIL 采集页报错：%s", qPrintable(o.value(QStringLiteral("msg")).toString()));
            }
        }
    });
    const QPixmap pm = sc->grabWindow();
    if (pm.isNull()) { qWarning("[rtc] FAIL 抓屏是空图"); return; }
    QByteArray buf;
    QBuffer dev(&buf);
    if (!dev.open(QIODevice::WriteOnly)) { qWarning("[rtc] FAIL 缓冲打不开"); return; }
    pm.save(&dev, "JPEG", 55);
    dev.close();
    g_rtcView->page()->runJavaScript(
        QStringLiteral("window.__pushFrame('%1')").arg(QString::fromUtf8(buf.toBase64())));
}

// 把 QString 转义成 JS 字符串字面量。QString 没有 toJson，只能手拼。
// 每一步单独赋值：MSVC 下 QStringLiteral 宏内部带 '+」，裸在链式表达式里会把加法解析搅乱。
// 把 QString 变成 JS 里的**字符串表达式**。
//
// 这里踩过一个很难查的坑：以前的版本只转义反斜杠和双引号，然后拼成 "..." 字面量。
// SDP 里每一行结尾都是 CRLF，裸换行一旦进到 JS 字符串字面量里就是非法 token ——
// runJavaScript 抛 SyntaxError 而且**完全静默**（页面控制台什么都不打，C++ 侧一个错都收不到），
// 表现就是"信令发到云端、云端也说 ok 中继了，但对端 pc 根本没消费过"，永远连不上。
// base64 只含 [A-Za-z0-9+/=]，物理上不可能产生语法错，所以统一走 atob()。
static QString rtcJsB64(const QString &v)
{
    return QStringLiteral("atob('%1')").arg(QString::fromLatin1(v.toUtf8().toBase64()));
}

// 管理端回过来的 answer / ice，喂给采集页的 RTCPeerConnection
static void rtcFeedSignal(const QString &kind, const QString &sdp, const QString &cand)
{
    if (!g_rtcView) return;
    const QString a = rtcJsB64(kind);
    const QString b = rtcJsB64(sdp);
    const QString c = rtcJsB64(cand);
    const QString p1 = QStringLiteral("window.__signal({kind:");
    const QString p2 = QStringLiteral(",sdp:");
    const QString p3 = QStringLiteral(",candidate:");
    const QString p4 = QStringLiteral("});");
    const QString js = p1 + a + p2 + b + p3 + c + p4;
    g_rtcView->page()->runJavaScript(js);
}

// 真删采集页。用 deleteLater 而不是 delete：正在往事件循环里排的 runJavaScript 回调
// 还攥着 page 指针，当场 delete 会打在半路上。
static void releaseRtcView()
{
    if (!g_rtcView) return;
    g_rtcView->close();
    g_rtcView->deleteLater();
    g_rtcView = nullptr;
    qInfo("[rtc] 采集页已回收（Chromium 渲染进程随之退出）");
}

// 延迟回收：给"刚停又马上要看"留一段窗口，避免 rtc-stop/rtc-start 抖动时反复拉起/销毁
// Chromium（重建一次要几百毫秒，还会在云端留下一次 offer 重协商）。
static void scheduleRtcViewReap()
{
    if (!g_rtcReap) {
        g_rtcReap = new QTimer();
        g_rtcReap->setSingleShot(true);
        g_rtcReap->setInterval(kRtcReapDelayMs);
        QObject::connect(g_rtcReap, &QTimer::timeout, []() { releaseRtcView(); });
    }
    g_rtcReap->stop();
    g_rtcReap->start();
}

static void cancelRtcViewReap()
{
    if (g_rtcReap && g_rtcReap->isActive()) g_rtcReap->stop();
}

/**
 * 按需把 WebEngine 拉起来（2026-10-05 内存优化）。
 *
 * 为什么不能干脆去掉 QWebEngineView 改用纯 C++ WebRTC：被控端这套采集页（kRtcPage）
 * 靠的就是 Chromium 自带的 RTCPeerConnection + canvas.captureStream，自己拿 libwebrtc
 * 重写一遍是几千行的事，也谈不上"就地优化"。
 *
 * 能省下的是**进程常驻**那一块：Chromium 一旦起过就在主进程里留着（私有内存 ~456MB、
 * 78 线程，Qt 删掉 view 也卸不掉）。机房几十台机器整天没人看画面，这份常驻是纯浪费。
 * 所以把"拉起来"这件事推迟到**真的有人要看**的时刻：rtcStart() 里现拉。
 *
 * setHtml 的页面生命周期由 QWebEngineProfile::defaultProfile() 管（keep-alive 项默认 30 秒，
 * 可在 profile 上再压，但那是后端调），所以这里只管进程，不用管页面。
 *
 * 失败必须**大声报**：拉不起来就等于 RTC 不可用，不能像以前那样静默黑屏。
 */
static bool ensureWebEngine()
{
    if (g_webEngineReady) return true;
    // 这两行必须在 createWebEngineProcess() 之前：都是喂给 Chromium 的启动参数。
    // ⚠️ 第一行是 WebRTC 的命门：不关沙箱，ICE 收集会永远停在 "new"，且**不报任何错**。
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
            "--no-sandbox --disable-features=WebRtcHideLocalIpsWithMdns");
    // 被控端跑在教室机/本机这类受控 Windows 上，多渲染进程的内存开销没必要摊（我们就一个页面）
    qputenv("QTWEBENGINE_DISABLE_SANDBOX", "1");

    // ⚠️ 这里**不再**调 QtWebEngineQuick::initialize()。
    // 实测（2026-10-05）：在 Qt6 动态构建 + Widgets 程序里运行时调它，进程会卡死在启动期
    //（只剩 2 个线程、11MB、不连网络、不写日志、完全无输出），属于启动期死锁。
    // 运行时的 Chromium 拉起由 QWebEngineView 本身触发就够（Qt 在 WebEngineWidgets 的
    // 插件里做了这件事），这个函数是给**静态构建**做 pre-link 用的。
    // 结论：什么都不用调；真正要保证的只有"环境变量早于第一次 new QWebEngineView"（上面两行）。
    g_webEngineReady = true;
    qInfo("[rtc] WebEngine 已按需就绪（Chromium 进程会在首次建 view 时启动）");
    return true;
}

static void rtcStart()
{
    // ── 2026-10-09 换 other webrtc：优先 libdatachannel（ldc_streamer），
    // 不拉起 Chromium（瘦身 137MB 常驻）。STE_USE_LDC_RTC=0 回退旧 WebEngine 路。──
    const bool useLdc = qgetenv("STE_USE_LDC_RTC") != "0";
    if (useLdc) {
        if (g_rtcOn) return;   // 已在推
        g_offlineSince = 0;
        cancelRtcViewReap();
        g_rtcOn = true;
        LdcStreamer &s = LdcStreamer::instance();
        // 信令出站：ldc 的 offer/candidate → 云端（与旧路同协议 rtc-* 消息）
        QObject::connect(&s, &LdcStreamer::signalOffer, &s, [](const QByteArray &sdp) {
            rtcSendSignal(QStringLiteral("offer"), QString::fromUtf8(sdp), QString());
        }, Qt::UniqueConnection);
        QObject::connect(&s, &LdcStreamer::signalCandidate, &s, [](const QByteArray &cand) {
            rtcSendSignal(QStringLiteral("ice"), QString(), QString::fromUtf8(cand));
        }, Qt::UniqueConnection);
        QObject::connect(&s, &LdcStreamer::failed, &s, [](const QString &reason) {
            qWarning("[rtc-ldc] FAIL %s", qPrintable(reason));
        }, Qt::UniqueConnection);
        s.start(200);   // 5fps 抓屏推流
        qInfo("[rtc-ldc] 推流已开（libdatachannel，无 Chromium）");
        return;
    }
    if (g_rtcOn && g_rtcView) return;
    g_offlineSince = 0;           // 重新推流 = 还在线上，离线看门狗解除
    cancelRtcViewReap();          // 上一轮还在倒计时就别回收了，直接复用现有的 view
    if (!ensureWebEngine()) {     // 有人要看画面了，这会儿才把 Chromium 拉起来
        qWarning("[rtc] 没有 WebEngine，放弃推流");
        return;
    }
    if (!g_rtcView) {
        // 必须 show 一次（只 show 再 hide 会让 Chromium unmap，出黑帧）；放屏幕外不影响用户
        g_rtcView = new QWebEngineView();
        g_rtcView->setAttribute(Qt::WA_DeleteOnClose, false);
        g_rtcView->show();
        g_rtcView->setGeometry(-3000, -3000, 640, 400);
        g_rtcPageReady = false;
        g_rtcView->page()->setHtml(QString::fromUtf8(kRtcPage), QUrl(QStringLiteral("http://127.0.0.1/agent.html")));
    }
    g_rtcOn = true;
    if (!g_rtcTick) {
        g_rtcTick = new QTimer();
        g_rtcTick->setInterval(200);
        QObject::connect(g_rtcTick, &QTimer::timeout, []() { rtcTickOnce(); });
    }
    g_rtcTick->start();
    // ⚠️ 这里**不**再直接 runJavaScript("window.__startStream()")：
    // page->setHtml() 是异步的，刚 new 出来的 view 还在 about:blank 上，函数根本不存在，
    // 调用会静默抛 "Uncaught TypeError: window.__startStream is not a function"（C++ 侧收不到任何错）。
    // 以前日志里那行 TypeError 就是这么来的 —— 靠 rtcTickOnce() 每 200ms 兜一遍蒙对了，
    // 但窗口期里 __drain/__diag 全都空转，首帧要等好几百毫秒才出。
    // 现在显式等 loadFinished 再起第一枪，并把这个"页面就绪"状态记下来（rtcTickOnce 用它省掉重试）。
    g_rtcPageReady = false;
    QObject::connect(g_rtcView->page(), &QWebEnginePage::loadFinished, g_rtcView->page(),
                     [](bool ok) {
                         if (!g_rtcView) return;
                         g_rtcPageReady = true;
                         if (!ok) {
                             qWarning("[rtc] FAIL 采集页加载失败 —— 推流起不来");
                             return;
                         }
                         g_rtcView->page()->runJavaScript(QStringLiteral("window.__startStream();"));
                         // 诊断：把采集页内部状态和 RTCPeerConnection 可用性捞出来
                         //（推不出来时必须看得到原因，不许静默）
                         g_rtcView->page()->runJavaScript(
                             QStringLiteral("JSON.stringify(window.__st) + ' | PC=' + (typeof RTCPeerConnection)"),
                             [](const QVariant &v) {
                                 qWarning("[rtc] diag %s", qPrintable(v.toString()));
                             });
                         qInfo("[rtc] 采集页就绪，推流已开");
                     });
}

static void rtcStop(const QString &why)
{
    if (!g_rtcOn) return;
    g_rtcOn = false;
    // 2026-10-09 ldc 路：停 libdatachannel 推流（无 Chromium 可回收）
    if (qgetenv("STE_USE_LDC_RTC") != "0") {
        LdcStreamer::instance().stop();
        qInfo("[rtc-ldc] 推流已停：%s", qPrintable(why));
        return;
    }
    if (g_rtcTick) g_rtcTick->stop();
    if (g_rtcView) g_rtcView->page()->runJavaScript(QStringLiteral("window.__reset();"));
    qInfo("[rtc] 推流已停：%s", qPrintable(why));
    // 采集页先留着 5 秒，没人重新点开就回收（省的是那 137MB 的 Chromium 渲染进程）
    scheduleRtcViewReap();
}

/**
 * 被控端**单独的一刀**：管理员要开终端 → 这台机器旁边的人必须点头。
 *
 * 为什么不开箱即用：能跑命令就等于能把这台机器折腾到要重装，教室里是老师/学生的机器。
 * 所以第一次开会话弹一个模态确认（会话开着之后的输入不再问，免得敲一条问一次），
 * 拒绝/超时不点 = 关掉 —— 无人值守时没人点，默认就是"不许进来"。
 */
static void openTerminal(const QString &id, const QJsonObject &params)
{
    TerminalConsole &t = TerminalConsole::inst();

    if (t.hasSession()) {
        sendActionReceipt(id, QStringLiteral("terminal_open"), QStringLiteral("failed"),
                          QStringLiteral("已经有终端会话开着，同一台机器同时只放一个"));
        return;
    }

    TermReq req;
    req.sid = params.value(QStringLiteral("sid")).toString();
    req.shell = params.value(QStringLiteral("shell")).toString().trimmed().toLower();
    if (req.shell.isEmpty()) req.shell = QStringLiteral("cmd");
    // 只放行 cmd / powershell：别的 shell（如直接起 wscript）不认，避免这条路被当后门用
    if (req.shell != QStringLiteral("cmd") && req.shell != QStringLiteral("powershell")) {
        sendActionReceipt(id, QStringLiteral("terminal_open"), QStringLiteral("failed"),
                          QStringLiteral("不支持的 shell：") + req.shell);
        return;
    }
    req.cols = params.value(QStringLiteral("cols")).toInt(100);
    req.rows = params.value(QStringLiteral("rows")).toInt(30);
    req.idleMs = params.value(QStringLiteral("idleMs")).toInt(60000);
    req.cmdMs = params.value(QStringLiteral("cmdMs")).toInt(30000);

    const QByteArray autoAllow = qgetenv("STE_TERM_AUTO_ALLOW");
    bool allowed;
    if (!autoAllow.isEmpty()) {
        // 只给自动化/无人值守灰度用：设了它就不弹窗，一律放行。
        // 默认（不设）一定弹 —— 能跑 shell 就是能把机器拆了，不能没人点头就开门。
        allowed = (autoAllow != "0");
        qWarning("[term] ⚠️ STE_TERM_AUTO_ALLOW 已设（=%s），跳过本机确认：这条链路上没人看着",
                 autoAllow.constData());
    } else {
        QMessageBox box(QMessageBox::Question,
                        QStringLiteral("远程终端请求"),
                        QStringLiteral("有人要从管理端连这台机器的命令行（能执行任意命令）。\n允许吗？"),
                        QMessageBox::Yes | QMessageBox::No,
                        nullptr);
        box.setButtonText(QMessageBox::Yes, QStringLiteral("允许"));
        box.setButtonText(QMessageBox::No,  QStringLiteral("拒绝"));
        // 模态阻塞：这段时间内 socket 消息也会被挡，但只等人点一下，可以接受
        allowed = (box.exec() == QMessageBox::Yes);
    }
    if (!allowed) {
        qInfo("[term] 管理员请求被本机拒绝（id=%s）", qPrintable(id));
        sendActionReceipt(id, QStringLiteral("terminal_open"), QStringLiteral("failed"),
                          QStringLiteral("本机拒绝了远程终端请求"));
        t.close(req.sid, QStringLiteral("denied"));
        return;
    }

    QString err;
    if (!t.open(req, &err)) {
        sendActionReceipt(id, QStringLiteral("terminal_open"), QStringLiteral("failed"), err);
        return;
    }
    // 会话已经开好并会持续往外吐帧，这条回执只表示"点过头了"，别让它盖住后面的数据流
    sendActionReceipt(id, QStringLiteral("terminal_open"), QStringLiteral("started"),
                      QStringLiteral("本机已允许，终端会话已开"));
}

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
        g_everRegistered = true;            // 2026-10-08：注册成功 → 退避重置为快速重试，且后续走正常退避
        g_backoffMs = kFirstBackoffMs;
        g_islandOfflineShown = false;       // 2026-10-08：真的又连上了，断线提示的闸门放开（下一次断线再弹一条）
        const int hb = pay.value(QStringLiteral("heartbeatMs")).toInt(0);
        const int to = pay.value(QStringLiteral("timeoutMs")).toInt(0);
        if (hb > 0 && hb != g_heartbeatMs) {
            g_heartbeatMs = hb;
            if (g_hbTimer) g_hbTimer->setInterval(g_heartbeatMs);
        }
        if (to > 0) g_timeoutMs = to;
        g_lastOnlineAt = QDateTime::currentMSecsSinceEpoch();
        // 连上云端 = 这一版是活的（强制升级就靠这一个信号判定"新版本起没起来"）。
        // 注意要在 applyUpdateInfo **之前**执行：applyUpdateInfo 里可能立刻触发自动升级，
        // 而升级前的成功标记必须带着"我当前这一版是好的"这个事实走。
        otaMarkStartedOk();
        updateTray(TrayState::Connected, QStringLiteral("已连接"));
        qInfo("[agent-qt] ✅ 云端已确认注册 v1（uid=%s，server=%s，心跳 %d ms，超时 %d ms）",
              qPrintable(pay.value(QStringLiteral("uid")).toString()),
              qPrintable(pay.value(QStringLiteral("server")).toString()),
              g_heartbeatMs, g_timeoutMs);
        // 升级提示（2026-10-06）：云端把 ota.json 里的最新版本随回执带回，
        // 这里比一次、该提示就提示（比不出来/没发布过都如实说，不假装"已最新"）。
        applyUpdateInfo(pay);
        return;
    }

    if (type == QStringLiteral("rtc-start")) {
        qInfo("[rtc] 云端要开推流（有人在看）"); rtcStart();
        return;
    }
    if (type == QStringLiteral("rtc-stop")) {
        qInfo("[rtc] 云端要停推流（没人看了）"); rtcStop(QStringLiteral("云端 rtc-stop"));
        return;
    }
    // 云端把本端信令的投递结果回给本端（ok/to/detail）：这是唯一能证明"信令到底送没送到"的信号。
    // 以前接到就丢，offer 被云端丢了也就没人知道 —— 只能靠猜。
    if (type == QStringLiteral("rtc-relayed")) {
        const bool ok = pay.value(QStringLiteral("ok")).toBool();
        const QString rtype = pay.value(QStringLiteral("type")).toString();
        if (!ok) {
            qWarning("[rtc] FAIL 云端回执：%s 没送到（%s）→ 下个 tick 重发 offer",
                     rtype.toUtf8().constData(),
                     pay.value(QStringLiteral("detail")).toString().toUtf8().constData());
            g_rtcForceReoffer = true;
        } else {
            qInfo("[rtc] 云端回执：%s 已投给 %s 个对端",
                  rtype.toUtf8().constData(),
                  pay.value(QStringLiteral("to")).toString().toUtf8().constData());
        }
        return;
    }
    if (type == QStringLiteral("rtc-offer")) {
        rtcFeedSignal(QStringLiteral("offer"), pay.value(QStringLiteral("sdp")).toString(), QString());
        return;
    }
    if (type == QStringLiteral("rtc-answer")) {
        // 2026-10-09 ldc 路：answer 喂 libdatachannel（非 ldc 路走旧 rtcFeedSignal）
        if (qgetenv("STE_USE_LDC_RTC") != "0") {
            LdcStreamer::instance().onSignal(QStringLiteral("answer"),
                                             pay.value(QStringLiteral("sdp")).toString().toUtf8());
        } else {
            rtcFeedSignal(QStringLiteral("answer"), pay.value(QStringLiteral("sdp")).toString(), QString());
        }
        return;
    }
    if (type == QStringLiteral("rtc-ice")) {
        // 2026-10-09 ldc 路：candidate 喂 libdatachannel
        if (qgetenv("STE_USE_LDC_RTC") != "0") {
            LdcStreamer::instance().onSignal(QStringLiteral("candidate"),
                                             pay.value(QStringLiteral("candidate")).toString().toUtf8());
        } else {
            rtcFeedSignal(QStringLiteral("ice"), QString(), pay.value(QStringLiteral("candidate")).toString());
        }
        return;
    }
    if (type == QStringLiteral("error")) {
        // 统一错误通道：云端拒绝必须看得见（fail-silent 红线）。code 是机器可读的，message 是人话。
        // 错误码体系（2026-10-06）：ecode = 数字码（AU1001/CM4001…），code = 语义码兼容。
        const QString ecode = pay.value(QStringLiteral("ecode")).toString();
        fprintf(stderr, "[agent-qt] FAIL: 云端拒绝 → %s/%s：%s\n",
                qPrintable(ecode.isEmpty() ? QStringLiteral("-") : ecode),
                qPrintable(pay.value(QStringLiteral("code")).toString()),
                qPrintable(pay.value(QStringLiteral("message")).toString()));
        return;
    }

    // 语音对讲（2026-10-07）：老师开始/结束讲话 —— 重置"正在讲话"提示标记。
    // （提示的实际显示在收到第一帧音频时触发；stop 后下次 start 再提示。）
    // 2026-10-09 语音通话：duplex=true → 学生开 mic 采集回传（只回发起老师）。
    if (type == QStringLiteral("audio.start")) {
        g_audioHintShown = false;
        const bool duplex = pay.value(QStringLiteral("duplex")).toBool(false)
                            || pay.value(QStringLiteral("params")).toObject()
                                   .value(QStringLiteral("duplex")).toBool(false);
        if (duplex && !g_microphone.running()) {
            // 会话内采集：硬上限 120s 自动停（防挂起窃听，安全边界见语音通话设计 §3）
            g_microphone.start(120000);
            qInfo("[agent-qt] 🔊 语音通话开始（duplex，学生 mic 已开启，回传老师）");
        }
        qInfo("[agent-qt] 🔊 老师开始讲话（语音对讲）");
        return;
    }
    if (type == QStringLiteral("audio.stop")) {
        g_audioHintShown = true;   // 结束后不再提示
        g_audioPlayer.closePlayer();
        if (g_microphone.running()) {
            g_microphone.stop();
            qInfo("[agent-qt] 🔊 语音通话结束（学生 mic 已关闭）");
        }
        qInfo("[agent-qt] 🔊 老师结束讲话（语音对讲已静音）");
        return;
    }

    // 屏幕广播（2026-10-07）：学生端提示"正在接收老师屏幕"。
    // 实际显示在收到第一帧时；stop 关闭窗口恢复。
    if (type == QStringLiteral("broadcast.start")) {
        qInfo("[agent-qt] 📺 老师开始屏幕广播");
        return;
    }
    if (type == QStringLiteral("broadcast.stop")) {
        BroadcastView::inst().stop();
        qInfo("[agent-qt] 📺 老师结束屏幕广播，已恢复");
        return;
    }

    if (type == QStringLiteral("instruction")) {
        g_instructionSeq++;
        g_lastInstrId = id;
        // 破坏性指令的去重名单懒加载：不依赖启动顺序，读失败也照样能收到指令
        if (g_terminalDoneFile.isEmpty()) loadTerminalDone();
        const QString action = pay.value(QStringLiteral("action")).toString();
        const QJsonObject params = pay.value(QStringLiteral("params")).toObject();
        qInfo("[agent-qt] 📥 收到指令 id=%s action=%s（D5 真执行）", qPrintable(id), qPrintable(action));

        // ── 重复下发保护（2026-10-09 扩到**所有**指令）──────────────────────────
        // 云端在设备重连时按"已下发未回执"补发（store.js pendingFor），只排除标了
        // no_resend 的终止类。非终止类动作只要"执行了但回执没送到"，重连就会被补发
        // 一次 ⇒ 二次执行（用户报的"被控端重启后又做了一遍最后一次操作"）。
        // 这里按 instruction id 去重：同一条 id 只认第一次，之后一律跳过并如实回执。
        // 只挡"同一条 id"，不挡新指令 —— 老师想再来一次下新单（新 id）即可。
        if (!id.isEmpty() && g_terminalDone.contains(id)) {
            qInfo("[agent-qt] ⛔ 指令 id=%s 已执行过，按重复下发保护跳过", qPrintable(id));
            sendActionReceipt(id, action, QStringLiteral("skipped"),
                              QStringLiteral("本机已执行过该指令（重复下发保护）"));
            return;
        }
        if (!id.isEmpty()) markTerminalDone(id);   // 先落记录：哪怕这条会让进程消失也认

        // self_update 是**异步**动作（要下载）：不走下面的同步 executeAction，
        // 由 startSelfUpdate 自己分阶段回执（started → installing/failed）。
        if (action == QStringLiteral("self_update")) {
            startSelfUpdate(id, params);
            return;
        }

        // notify 确认 + 快捷回复（2026-10-07）：要走异步分支拿 instruction id，
        // 学生点「确认」或快捷回复时发二次回执（confirmed + reply）给云端。
        if (action == QStringLiteral("notify")) {
            const QString err = notifyFromParams(params, id);
            if (!err.isEmpty()) sendActionReceipt(id, action, QStringLiteral("failed"), err);
            else sendActionReceipt(id, action, QStringLiteral("done"));
            return;
        }

        // ── 远程终端（2026-10-06）：也只有它一个动作会**持续往外吐帧**，所以单独走分支 ──
        // 开会话必须经被控端旁边的人点头 —— 能跑 shell 就等于能控制这台机器，不能无人值守。
        if (action == QStringLiteral("terminal_open")) {
            openTerminal(id, params);
            return;
        }
        if (action == QStringLiteral("terminal_input")) {
            TerminalConsole::inst().input(params.value(QStringLiteral("sid")).toString(),
                                          params.value(QStringLiteral("keys")).toString());
            sendActionReceipt(id, action, QStringLiteral("done"));
            return;
        }
        if (action == QStringLiteral("terminal_close")) {
            TerminalConsole::inst().close(params.value(QStringLiteral("sid")).toString(),
                                         QStringLiteral("remote"));
            sendActionReceipt(id, action, QStringLiteral("done"));
            return;
        }

        // 考试模式（2026-10-06，设计文档 3.7 第一版；2026-10-09 进程管控改黑名单）：
        // 全屏拦截 + 只对黑名单进程下手 + 计时。
        // exam_mode_stop 手动结束（管理员收卷）。到点自动结束由 ExamMode 内部触发。
        // ⚠️ exam_mode 是**破坏性**动作，这里是全校/全班级的一次性影响面。
        //    2026-10-09 事故后务必保留 ExamMode 内部那三重内核闸门，
        //    别为了"更严"把黑名单改回白名单 —— 白名单杀到 critical 进程就是蓝屏重启。
        if (action == QStringLiteral("exam_mode")) {
            const int minutes = params.value(QStringLiteral("minutes")).toInt();
            // ── 2026-10-09 改黑名单（学校机房"反复重启"事故后的纠偏）──
            // 语义反转：只杀黑名单里点名的进程，其余一概不碰。
            //   旧字段名叫 whitelist 时，被控端会把"名单之外"的全部杀掉 ⇒ 杀到 critical 的
            //   svchost ⇒ CRITICAL_PROCESS_DIED(0xEF) 蓝屏 ⇒ 重启 ⇒ 自启 ⇒ 再杀 ⇒ 反复重启。
            // 这里**只读 blacklist**：万一还有老版本管理端在发 whitelist，
            // 被控端会读到空黑名单 ⇒ 完全不杀任何进程（fail-safe 的降级，不会把机器再弄崩）。
            QStringList blacklist;
            const QJsonArray arr = params.value(QStringLiteral("blacklist")).toArray();
            for (const QJsonValue &v : arr) blacklist.append(v.toString());
            const bool enforcing = ExamMode::inst().start(minutes, blacklist);
            // 2026-10-09 通知门控同步：考试激活 → 一切通知/弹幕/循环提醒静默
            g_notifGate.setExamActive(true);
            // #98：考试模式也走灵动岛（顶部一条，考试期间不摊一个窗在桌面上）
            islandShow(QStringLiteral("考试模式已开始"),
                       QStringLiteral("倒计时 %1 分钟").arg(minutes)
                       + (enforcing ? QString() : QStringLiteral("（未启用进程终止）")),
                       QStringLiteral("pencil"), 6000);
            sendActionReceipt(id, action, QStringLiteral("started"),
                              enforcing ? QString() : ExamMode::inst().reason());
            return;
        }
        if (action == QStringLiteral("exam_mode_stop")) {
            const bool was = ExamMode::inst().stop(QStringLiteral("manual"));
            // 2026-10-09 通知门控同步：考试结束 → 放行通知
            g_notifGate.setExamActive(false);
            islandShow(QStringLiteral("考试模式已结束"), QStringLiteral("已恢复操作"),
                       QStringLiteral("pencil"), 3000);
            sendActionReceipt(id, action, was ? QStringLiteral("done") : QStringLiteral("not_active"));
            return;
        }

        // D5：真执行，再如实回执。绝不许"报 done 其实没做"。
        const ExecOut r = executeAction(action, params);
        sendActionReceipt(id, action, r.result, r.error, r.data);
        return;
    }

    qInfo("[agent-qt] 收到云端消息 type=%s", qPrintable(type));
}

// 抓一帧 → 本地存图（有 dir 才存）→ 连上了就发出去
void captureAndSend(const QString &outDir)
{
    QElapsedTimer cost;
    cost.start();

    // 云端不在就别抓了（2026-10-05）。
    // 抓屏是这条链路里最贵的一步：grabWindow 要跨进程取整个屏幕位图，再整张 QPixmap 压 JPEG；
    // 而抓完的结果**一帧都送不出去**（sendBinaryMessage 对着断开的 socket 直接返回 0）。
    // 原来这里照样每 2 秒抓一张、照样编码、只在最后 if(online) 里丢掉，日志刷满
    // "帧#N 离线（B 未发）" —— 机房几十台机器一起空转就是这个量级。
    // 特别注意：这个提前返回要排在 grabWindow 和 JPEG 编码**之前**，否则省不下来。
    const bool online = g_ws && g_ws->state() == QAbstractSocket::ConnectedState;
    if (!online) {
        // 每 30 次（约 1 分钟）留一行心跳式的离线提示：全静默会让"云端挂了"这件事在日志里消失
        static int skipped = 0;
        if (++skipped % 30 == 1)
            qInfo("[agent-qt] 云端不在，跳过抓屏（累计跳过 %d 次）", skipped);
        return;
    }

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

    // 走到这里一定是 online（离线分支已在函数开头提前返回）。
    // v1 二进制帧：[1B 版本][2B 大端 headerLen][header JSON][JPEG]，带 seq —— 旧裸帧没有序号，
    // 将来按需拉流、断帧重传、测 fps 全靠它。
    const QByteArray frame = makeFrameBytes(g_uid, g_frameSeq + 1, jpeg);
    const qint64 n = g_ws->sendBinaryMessage(frame);
    g_frameSeq++;
    g_frameBytes += jpeg.size();   // 只累计 JPEG 净荷，跟云端 bytesIn 对账才对得上
    qInfo("[agent-qt] 帧#%d 推送 %lld B 净荷（含帧头 %lld B，累计 %.2f MB，耗时 %lldms）",
          g_frameSeq, (qint64)jpeg.size(), n, g_frameBytes / 1048576.0, cost.elapsed());
}

} // namespace

/* ---------- 协议 v1 封装（本文件唯一的 makeEnvelope 定义） ----------
 * 为什么挪到这里：原定义就在上面那段匿名 namespace 里，匿名域符号**跨不了 TU**，
 * 而远程终端 terminal_console.cpp 要发 v1 信封 —— 一调用就 LNK2019。
 * 声明在 cloud_proto.h（调用点靠它可见），定义必须在全局，版本值只认 CloudProto::kVersion。
 */
QString makeEnvelope(const QString &type, const QJsonObject &payload, const QString &id)
{
    QJsonObject o;
    o.insert(QStringLiteral("v"), CloudProto::kVersion);
    o.insert(QStringLiteral("type"), type);
    o.insert(QStringLiteral("id"), id);
    o.insert(QStringLiteral("ts"), QJsonValue(QDateTime::currentMSecsSinceEpoch()));
    o.insert(QStringLiteral("payload"), payload);
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);   // 要托盘 → QApplication（托盘在 QtWidgets 里）

    // ── 日志 / 数据目录（2026-10-06 提到 main 最前面）──────────────────────
    // ⚠️ 必须**早于单例守卫**（守卫就在下面几行）：守卫拦下第二个实例时会调
    //    appendLogFile() 记录"为什么被拦"，而 appendLogFile 在 g_logPath 为空时
    //    **直接 return 丢弃**（见其实现）。原先这段排在守卫之后，后果是
    //    "确实被拦了，但日志里一行都没有" —— 老师机上只看到一个弹窗，事后查不到原因，
    //    跟"程序没起来"完全同一副样子。这是 fail-silent，红线。
    //    现在提到最前面：任何一次启动（哪怕立刻退出）都能留证。
    // 数据目录放 %LOCALAPPDATA%\xingjikong\（与既有约定一致，覆盖升级天然保留历史）。
    // 截图目录默认**不再**落在当前工作目录 —— 装机后 cwd 是哪全看启动器，落在那儿等于丢了。
    const QString dataDir = qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong");
    QDir().mkpath(dataDir);
    g_shotDir = qEnvironmentVariable("STE_QT_SHOT_DIR", dataDir + QStringLiteral("/shots"));
    if (!g_shotDir.isEmpty()) QDir().mkpath(g_shotDir);
    g_logPath = qEnvironmentVariable("STE_QT_LOG", dataDir + QStringLiteral("/agent-qt.log"));

    // ── 单例锁（2026-10-05 · 用户要求"防多进程启动"，同日修好失效 bug）──────
    // 场景：登录自启计划任务 + 装机引导 + 老师手点 start-agent.bat，多个入口叠加，
    // 实测出现过 2 个实例在跑 —— 重复连云端抢连接、日志互相覆盖。
    // ⚠️ 老实现只用了 `Local\StelarithAgentQt_Singleton`，**已经失效**（2026-10-05 实测报回）：
    //    Local\ 是会话内命名空间，计划任务跑在 Session 0、手动启动在交互会话，各认各的 → 双开照跑。
    //    现在 Global\ 优先（跨会话）+ Local\ 回退 + 文件锁兜底，实现见 src/singleinstance.h/.cpp
    //    （与管理端 viewer 同一份，改的时候两端一起改）。
    // guard 必须活到 main 结束：析构才放手。
    SingleInstanceGuard g_single;
    QString g_singleWhy;
    if (!g_single.acquire(L"StelarithAgentQt_Singleton", L"agent.lock", &g_singleWhy)) {
        // 拿不到 → 提示 + 退出（不留进程、不连云端）。提示留着：老师机上是有人看到的。
        // ⚠️ 顺序不能反：QMessageBox::information 是**模态阻塞**的，要一直等到有人点确定；
        //    原来把 appendLogFile 写在弹窗之后，结果是"弹窗没人点 → 日志一行都没有"，
        //    事后只能看到"进程起来过又没了"，查不到为什么（2026-10-06 排这一趟才看清）。
        //    **必须先落盘再弹窗**：哪怕老师一直不点，原因也已经留在日志里了。
        appendLogFile(QStringLiteral("[agent-qt] 已有另一个被控端在跑（%1）→ 本次启动退出").arg(g_singleWhy));
        QMessageBox::information(nullptr,
            QStringLiteral("星集控 · 被控端"),
            QStringLiteral("被控端已在运行（另一个实例正在工作）。\n本次启动自动退出，请勿重复启动。\n（%1）")
                .arg(g_singleWhy));
        return 0;
    }

    // 关键：常驻托盘程序**不能**在"最后一个窗口关闭时退出"。
    // 默认 quitOnLastWindowClosed=true —— 配置窗是本进程唯一的窗口，老师一点「以后再说」/
    // 保存关窗，整个被控端就跟着退了（托盘也一起没），比不配置还糟。必须关掉这个默认行为。
    app.setQuitOnLastWindowClosed(false);

    // Windows GUI 程序里 qInfo 默认走 OutputDebugString，重定向到文件就是空的——
    // 被控端没有日志等于瞎子。这里把 Qt 日志全部接管到 stdout/stderr。

    // ⚠️ 这里原来有一行 qputenv("QTWEBENGINE_CHROMIUM_FLAGS", ...)，2026-10-05 挪进
    //    ensureWebEngine() 了 —— 因为 Chromium 现在是**按需加载**（不是进程启动就起），
    //    环境变量必须紧贴"拉起 Chromium"那一刻设，放进程开头反而可能与实际启动时机脱节。
    //    那一行的作用（不关沙箱 → ICE 永远停在 "new"、黑屏且无报错）见那里的注释。
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

    // （dataDir / g_shotDir / g_logPath 的初始化**已移到 main() 最前面** —— 必须早于单例守卫，
    //   否则守卫拦下第二次启动时那行"为什么被拦"的日志会被 appendLogFile 静默丢弃。见上面那段。）

    // 自启时计划任务**直接拉 exe**（不再经 bat）→ 配置得由自己从 agentEnvPath()（数据目录）读。
    // 2026-10-03 实测：计划任务里套一层 `cmd /c start-agent.bat` 会**卡住不退出**，
    // 任务永远 Running、被控端根本没被拉起。
    migrateAgentEnvFromInstallDir();   // 老机器：把安装目录里的旧配置搬进数据目录（幂等，无则不动）
    const int envN = loadEnvFile(agentEnvPath());
    if (envN > 0) qInfo("[agent-qt] 已从 agent.env 读入 %d 项配置（%s）", envN, qPrintable(agentEnvPath()));
    loadClassCode();   // 2026-10-08：载入持久化的班级码，register 时带给云端
    loadClassName();   // 2026-10-09：载入班级显示名（同上，register 一起带给云端）

    // 2026-10-10：解析并持久化灰度渠道（发现①修复）—— 见 resolvedChannel / persistChannel。
    // 落盘 channel.txt 后，后续任何重启 / OTA 升级都自动沿用当前通道，无需启动器注入环境变量。
    g_channel = resolvedChannel();
    persistChannel(g_channel);
    qInfo("[agent-qt] 更新渠道 = %s（env→channel.txt→stable 解析）", qPrintable(g_channel));

    // ── 配置校验（2026-10-04）：缺 URL 或 TOKEN = 未配置，必须可见地报出来（图标+日志+气泡），
    //    不再"进程在跑、静默重试"让老师误以为装好了。此刻托盘还没建，goUnconfigured 只负责落盘告警；
    //    托盘建好后会再刷一次"未配置"图标并弹一次气泡。 ──
    g_configured = !qEnvironmentVariable("STE_QT_WS_URL").trimmed().isEmpty()
                   && !qEnvironmentVariable("STE_QT_WS_TOKEN").trimmed().isEmpty();
    if (!g_configured) goUnconfigured();

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
        // 首次运行配置窗的入口：老师在界面里填云端地址/令牌/设备名，写回数据目录的 agent.env。
        auto *actConfig = menu->addAction(QStringLiteral("配置…"));
        QObject::connect(actConfig, &QAction::triggered, &app, [] { openConfigDialog(); });
        // 设置与信息（2026-10-06）：一眼看清 版本 / uid / 云端地址 / 连接状态 / 能不能升级
        auto *actInfo = menu->addAction(QStringLiteral("设置与信息…"));
        QObject::connect(actInfo, &QAction::triggered, &app, [] { openInfoDialog(); });
        // 今日课表（2026-10-06）：教室大屏显示全天课程，双击全屏；读 ClassIsland 档案
        auto *actSched = menu->addAction(QStringLiteral("今日课表…"));
        QObject::connect(actSched, &QAction::triggered, &app, [] {
            const QString profilePath = qEnvironmentVariable(
                "STE_QT_PROFILE",
                QStringLiteral("%1/ClassIsland/data/Profiles/Default.json")
                    .arg(qEnvironmentVariable("LOCALAPPDATA")));
            auto *win = new ScheduleViewWindow();
            win->setAttribute(Qt::WA_DeleteOnClose);
            win->setProfilePath(profilePath);
            win->show();
        });
        // 有新版时才出现的一条：默认隐藏，收到 registered 里的 latestVersion 后按需显示
        g_actUpdate = menu->addAction(QStringLiteral("发现新版本…"));
        g_actUpdate->setVisible(false);
        QObject::connect(g_actUpdate, &QAction::triggered, &app, [] { openInfoDialog(); });
        // 未配置时"去哪儿配"的落点：直接打开数据目录（agent.env 就在那儿，2026-10-07 起不再放 exe 同目录）
        auto *actEnv = menu->addAction(QStringLiteral("打开配置目录"));
        QObject::connect(actEnv, &QAction::triggered, &app, [] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(agentEnvPath()).absolutePath()));
        });
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
        if (g_unconfigured) {
            // 配置校验时已落过盘；这里把图标刷成"未配置"，气泡留到 show() 之后再弹
            updateTray(TrayState::Unconfigured,
                       QStringLiteral("未配置：%1（右键托盘 →「配置…」）").arg(unconfiguredReason()));
        } else {
            updateTray(TrayState::Disconnected, QStringLiteral("正在连接…"));
        }
        g_tray->show();             // 先设图标再 show，避免 "No Icon set" 警告
        notifyUnconfiguredOnce();   // show 之后再弹气泡（托盘可见才弹得出来）
        qInfo("[agent-qt] 托盘已就绪");

        // ── 灵动岛空闲态常驻（2026-10-07 用户要求）──
        // 「无任务、通知则收回顶部，显示半胶囊+横杠箭头」：那条半胶囊本身就是入口，
        // 所以开机就挂上 —— 否则得等第一条通知出现过才见得到它，等于没有。
        // 放在托盘块里：有托盘 = 有桌面会话，Session0 那种无界面环境不摆窗口。
        // 想要回"用完即隐"的老样子，设 STE_QT_ISLAND_IDLE=0。
        IslandOverlay *isl = IslandOverlay::instance();
        isl->setIdleEnabled(envOr("STE_QT_ISLAND_IDLE", QStringLiteral("1")) != QStringLiteral("0"));
        isl->ensureIdle();
    } else {
        fprintf(stderr, "[agent-qt] WARN 系统托盘不可用（Session0？）—— 进程照跑，但没有可见入口\n");
    }

    // ── 通知体系接线（2026-10-09 · DeepSeek Harness）─────────────────────
    // 循环提醒：展示由 notifyFromParams 的 loop 分支直接走 NotifyWindow（含确认按钮，
    // 点确认 → loopConfirm 回调 → g_loopReminder.confirm 终止）。这里只收终止日志。
    QObject::connect(&g_loopReminder, &LoopReminder::expired, &app,
        [](const QString &id, const QString &reason) {
            qInfo("[agent-qt] 循环提醒已终止 %s（%s）", id.toUtf8().constData(),
                  reason.toUtf8().constData());
        });
    // 语音通话：mic 采集帧 → v1 音频帧发回云端（云端 duplex 单播回发起老师）
    QObject::connect(&g_microphone, &Microphone::pcmReady, &app,
        [](const QByteArray &pcm) {
            if (!g_ws || g_ws->state() != QAbstractSocket::ConnectedState) return;
            g_ws->sendBinaryMessage(makeAudioFrameBytes(pcm));
        });
    QObject::connect(&g_microphone, &Microphone::hardStop, &app, [] {
        qInfo("[agent-qt] 🔊 语音通话硬上限到达，学生 mic 自动关闭（安全边界）");
    });
    // 考试模式切换 → 通知门控同步（考试激活 → 一切通知静默；结束 → 放行）
    // ExamMode 是单例：hook 它的状态切换（start/stop 都在 exam_mode.cpp 内触发信号——
    // 目前没有信号，改由 main.cpp 在两个指令处理点同步，见 exam_mode 指令分支）。
    Q_UNUSED(g_notifGate);

    g_ws = new QWebSocket();
    g_ws->setParent(&app);

    QObject::connect(g_ws, &QWebSocket::connected, &app, [] {
        qInfo("[agent-qt] 已连上云端（等待 register 回执）");
        g_backoffMs = 1000;      // 连上了就重置退避
        g_offlineSince = 0;      // 离线看门狗重新计
        sendRegister();
    });

    // 云端踢人时给不给原因，被控端这边都得说清楚，否则运维只能猜。
    // （Qt 6.8 的 aboutToClose() 不带参数，拿不到 close code，所以退到 socket 错误串 + 上下文判断。）
    QObject::connect(g_ws, &QWebSocket::disconnected, &app, [] {
        g_registered = false;
        // 采集页回收兜底的起点只认**第一次**断连：每退避重连失败一次就把起点刷新一次的话，
        // 倒计时会被无限往后推，看门狗永远走不到头（实测断线 26 秒、重连失败 3 次就没触发过）。
        if (!g_offlineSince) g_offlineSince = QDateTime::currentMSecsSinceEpoch();
        // 未配置态：不改图标、不重试（连接本就没意义，别把"未配置"覆盖回"未连接（重试中）"）
        if (g_unconfigured) return;
        // 保存新配置后主动断开的这一跳：立刻用新配置重连，不排退避
        if (g_reconnectNow) {
            g_reconnectNow = false;
            updateTray(TrayState::Disconnected, QStringLiteral("正在用新配置连接…"));
            QTimer::singleShot(0, connectNow);
            return;
        }
        updateTray(TrayState::Disconnected, QStringLiteral("未连接（重试中）"));
        // 断线是运维最需要立刻看见的一件事（机器明明开着却没人管）。
        // 走灵动岛：顶部胶囊一条，3 秒后自己淡掉，不打断正在讲课的人。
        // 2026-10-08 去重：退避重连期间这一段离线只弹第一条；且与上一条离线提示间隔 < 20s
        // 不重弹，避免断线抖动（连上→又断）时胶囊反复出现、看着像"一直显示连接中断"。
        const qint64 nowOffline = QDateTime::currentMSecsSinceEpoch();
        if (!g_islandOfflineShown && nowOffline - g_lastOfflineToastMs > 20000) {
            g_islandOfflineShown = true;
            g_lastOfflineToastMs = nowOffline;
            islandShow(QStringLiteral("连接中断"), QStringLiteral("正在自动重连…"), QStringLiteral("triangle-alert"), 3000);
        }
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
    // 语音对讲（2026-10-07）：云端转发来的音频帧（v1 二进制帧，mime=audio/pcm）。
    // 与画面帧区分：画面帧是 agent→云端→viewer 的单向（被控端不接收）；这里只收 audio。
    QObject::connect(g_ws, &QWebSocket::binaryMessageReceived, &app, [](const QByteArray &msg) {
        // v1 帧：[1B 版本][2B headerLen][header JSON][payload]；header.mime 标 audio/pcm
        if (msg.size() < 4 || (uchar)msg[0] != 1) return; // 非 v1 帧忽略
        const int hLen = ((uchar)msg[1] << 8) | (uchar)msg[2];
        if (hLen <= 0 || hLen > 4096 || msg.size() < 3 + hLen) return;
        const QByteArray head = msg.mid(3, hLen);
        const QJsonDocument doc = QJsonDocument::fromJson(head);
        if (!doc.isObject()) return;
        const QString mime = doc.object().value(QStringLiteral("mime")).toString();
        const QByteArray payload = msg.mid(3 + hLen);
        if (mime == QLatin1String("audio/pcm")) {
            // 语音对讲（设计文档 3.3.6）：老师讲话学生可见（每会话一次，不刷屏）
            if (!g_audioHintShown) {
                g_audioHintShown = true;
                // #98：同样改走灵动岛（顶部胶囊，不摊在桌面正中挡课件）
                islandShow(QStringLiteral("老师正在讲话"),
                           QStringLiteral("老师正在通过语音对讲广播"), QStringLiteral("mic"), 4000);
            }
            if (!g_audioPlayer.isOpen()) g_audioPlayer.open();
            g_audioPlayer.write(payload);
            // §6.3 语音频谱：对讲帧喂 16 位频谱（懒建窗口，只在有语音时出现）
            if (!g_spectrum) {
                g_spectrum = new SpectrumWidget();
                g_spectrum->resize(240, 48);
                g_spectrum->move(20, 20);
                g_spectrum->setLightMode(false);
            }
            g_spectrum->pushPcm(payload);
            g_spectrum->show();
            return;
        }
        if (mime == QLatin1String("image/jpeg")) {
            // 屏幕广播（2026-10-07）：老师屏幕全屏显示
            BroadcastView::inst().showFrame(payload);
            return;
        }
        // 其他 mime 忽略（画面帧等）
    });
    // 远程终端出帧通道（云端一推 terminal_data 就经这里出去；没连上会如实记 error 不假装成功）
    TerminalConsole::inst().setSocket(g_ws);

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

    // ── 强制升级的回滚看门狗（2026-10-06）────────────────────────────────
    // 还有一次升级没被"确认成功"摆在台面上（pending 标记还在）→ 起个定时器：
    // 新版本这么多秒内没连上云端（且这台机器确实连通过）就把旧版放回去。
    // 没连通过的机器**不回滚**：网络断了不该把升级判成失败、白降一版。
    if (QFile::exists(otaPendingPath())) {
        qWarning("[agent-qt] ⚠ 有一次升级还没确认成功（%s）—— %d 秒内没连上云端就回滚",
                 qPrintable(otaPendingPath()), kOtaRollbackMs / 1000);
        QTimer::singleShot(kOtaRollbackMs, [] {
            if (g_registered || g_lastOnlineAt == 0) return;    // 起来了 / 从没连通 → 都不算新版本坏了
            qWarning("[agent-qt] ⚠ 当前版本已运行 %d 秒仍连不上云端 → 回滚到上一版",
                     kOtaRollbackMs / 1000);
            otaRollback();
        });
    }

    // ── 课表时钟（2026-10-06 加）：上下课提醒 ────────────────────────────
    // 读 ClassIsland 档案（默认 %LOCALAPPDATA%\ClassIsland\data\Profiles\Default.json），
    // 每 30 秒轮询当前时间；到上课/下课边界触发大屏通知（复用 showNotice）。
    // 档案路径可配 STE_QT_PROFILE；无档案/无课表时静默（日志留一行，不打扰教室）。
    {
        const QString profilePath = qEnvironmentVariable(
            "STE_QT_PROFILE",
            QStringLiteral("%1/ClassIsland/data/Profiles/Default.json")
                .arg(qEnvironmentVariable("LOCALAPPDATA")));
        auto *clock = new ScheduleClock(profilePath, &app);
        QObject::connect(clock, &ScheduleClock::periodStarted, &app,
            [](const QString& name, const QTime& at) {
                NotifyWindow::showNotice(NotifyWindow::Popup, QStringLiteral("上课啦"),
                           QStringLiteral("%1 · %2").arg(name, at.toString("HH:mm")),
                           6, true, QStringLiteral("remind"));
                qInfo().noquote() << "[agent-qt] 上课提醒已弹: " << name;
            });
        QObject::connect(clock, &ScheduleClock::periodEnded, &app,
            [](const QString& name, const QTime& at) {
                NotifyWindow::showNotice(NotifyWindow::Popup, QStringLiteral("下课啦"),
                           QStringLiteral("%1 · %2").arg(name, at.toString("HH:mm")),
                           6, true, QStringLiteral("remind"));
                qInfo().noquote() << "[agent-qt] 下课提醒已弹: " << name;
            });
        clock->start();
        qInfo().noquote() << "[agent-qt] 课表时钟已启动，档案: " << profilePath;
    }

    // ── 首次运行（未配置）自动弹一次配置向导 ──
    // 判据：走到这里 g_unconfigured=true ⇔ agent.env 不存在 或 URL/令牌为空（loadEnvFile 后判定）。
    // 用 singleShot 延后到事件循环起来、窗口就绪之后再 exec，绝不在构造函数里直接 exec 阻塞启动。
    // 只在有托盘/桌面时弹（Session0 服务态没有桌面，弹窗会无人可关、卡死进程）。
    // 老师点「以后再说」即可跳过；关掉后未配置态的托盘告警照旧保留。
    if (g_unconfigured && g_tray) {
        qInfo().noquote() << QStringLiteral("[agent-qt] 检测到未配置，稍后自动打开配置向导（可点「以后再说」跳过）");
        QTimer::singleShot(700, &app, [] { openConfigDialog(); });
    }

    const int rc = app.exec();
    // 释放单例锁（2026-10-05）：正常退出才放，进程结束前交给系统/OS 清理兜底。
    // 老实现这行 CloseHandle(g_singletonMutex) 引用的变量早删了，改成守卫自己的 release()。
    g_single.release();
    return rc;
}
