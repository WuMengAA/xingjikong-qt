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
#include "smtc_monitor.h"
#include "media/MediaTools.h"
#include "media/MediaSession.h"  // 2026-10-22 §10 第二刀：录像/播放会话控制
#include "media/CameraActions.h" // 2026-10-23 §10 第 3 刀：相机六件套分支体
#include "media/ThumbnailReporter.h" // 2026-10-23 #459：班级域缩略图上报（class-event{kind:thumbnail}）
#include "core/ProcessUtil.h"    // 2026-10-22 §10 第二刀：通用进程等待（跨域复用）   // 2026-10-22 §10 第 1 刀：媒体工具层     // 2026-10-22：系统媒体识别（SMTC），起工作线程 + 元数据
#include "core/ExecOut.h"     // 2026-10-23 §10 第 3 刀：指令执行结果（跨 TU 共享）
#include "core/AgentSettings.h" // 2026-10-23 §10 第 4 刀：本地功能开关（原嵌在匿名域内，跨不了 TU）

#include <QApplication>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QPainter>
#include <QDesktopServices>
#include <QTimer>
#include <QEventLoop>
#include <QScreen>
#include <QPixmap>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QBuffer>
#include <QSet>
#include <cstdlib>   // rand()：重连退避抖动（2026-10-08）
#include <QRandomGenerator>   // 退避抖动改走 CSPRNG（rand() 未播种 ⇒ 全机同一序列，2026-10-13）
#include <QStandardPaths>
#include <QSettings>        // P0-1（2026-10-14）：本地功能开关持久化（QCoreApplication 名/组织已在 main 设置）
#include <QElapsedTimer>
#include <QUrl>
#include <QWebSocket>
#include <QTextEdit>    // P3：长期记录对话框（QTextEdit 不在 QWidgets 总含头里）
#include <QCheckBox>    // P3：播放时段配置对话框
#include "cloud_proto.h"       // 协议 v1 信封（makeEnvelope 定义在这，别的 TU 共用）
#include "vnc_bridge.h"       // 2026-10-18：远控桥（主 WS vnc-data ↔ 本地 VNC，纯字节）
#ifdef STE_USE_LIBVNCSERVER
#include "vnc_server.h"         // 2026-10-18：in-agent VNC Server（libvncserver 封装，方案 A）
#endif
#include "terminal_console.h"  // 2026-10-06：远程终端（REPL，见该文件注释）
#include "exam_mode.h"         // 2026-10-06：考试模式（全屏拦截 + 黑名单轮询 + 计时）
#include "proc_guard.h"        // 2026-10-13：进程终止安全闸门（与 exam_mode 共用同一道门）
#include "audio_player.h"      // 2026-10-07：语音对讲播放（waveOut）
#include "opus_codec.h"        // 2026-10-21（P1）：语音 Opus 编解码（shared/audio，两端同源）
#include "broadcast_view.h"    // 2026-10-07：屏幕广播全屏显示
// ── 通知体系新组件（2026-10-09 接线 · DeepSeek Harness 造）──
#include "danmaku.h"           // §1.2 弹幕：字幕式横向滚动
#include "loop_reminder.h"     // §1.2 loopRemind：未确认循环提醒
#include "notification_gate.h" // §6.2 考试模式禁一切通知与监视
#include "edge_tts.h"          // §6.4 Edge 语音 TTS（管理端可选）
#include "noise_monitor.h"     // §6.5 噪音检测
#include "microphone.h"        // 语音通话：mic 会话内采集
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextStream>   // P3：长期记录对话框文本汇总
#include <QHash>
#include <QVector>
#include <QFileInfo>
#include <QStorageInfo>   // 2026-10-06 强制自动升级：装之前先看磁盘剩多少
#include <QStringList>
#include <QStandardPaths>
#include <QSslSocket>   // 2026-10-13：启动时 TLS 后端自检（TLS 挂掉时故障完全静默）
#include <QSslError>    // 2026-10-14：sslErrors 回调里要具体错误原因
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
#include "plugin/pluginhost.h"   // P2：插件宿主（进程外插件 ABI）
// 2026-10-20：首次启动引导（OOBE Wizard）——QML 向导 + 绑定数据面（详见 oobe_wizard.h）。
#include "oobe_wizard.h"
#include <QQuickWidget>          // 向导宿主（与 AgentShell 同款：QQuickWidget 顶格承载 QML）
#include <QPointer>              // 向导窗口单例（窗口被关掉后指针自动置空）
#include <QQmlContext>
#include <QQmlEngine>
#include "modules/modulehost.h"  // 2026-10-16：内置模块层（编译进 agent 的官方模块，与进程外插件共用门控）
#include "modules/usbmodule.h"
#include "modules/voicehubmodule.h"  // 2026-10-17：VoiceHub 联动（只读展示权威广播状态）
#include "modules/broadcastclubmodule.h"  // 2026-10-23：广播社成员独立身份登录通道
#include <QMessageBox>         // P2：插件市场对话框
#include <QListWidget>         // P2：插件市场列表
#include <QUrlQuery>
#include <QMouseEvent>   // 2026-10-05：紧急通知点击关闭
#include <QPaintEvent>   // 2026-10-05：全屏通知背景色（setStyleSheet 对顶层 QWidget 无效）
#include <QGraphicsOpacityEffect>   // 2026-10-07：通知窗淡入淡出（#98「要有过渡」）
#include <QPropertyAnimation>
#include <QPainter>
#include <QPainterPath>
#include <windows.h>
#include "winlauncher.h"   // 2026-10-19 需求①：子进程静默启动（杜绝任何场景弹出 cmd 窗）
#include <tlhelp32.h>   // 进程快照（process_list / process_stop）
#include <psapi.h>      // 进程工作集内存
#include <mmsystem.h>   // waveOutSetVolume（set_volume，Rust 版实测零依赖可用）
#include <shellapi.h>   // ShellExecuteW（launch_app）+ SHFileOperationW（media_delete 进回收站）
#include <sapi.h>       // 2026-10-04：大屏通知 TTS 语音播报（Windows SAPI 5，微软免费离线语音）
#include "system_volume.h"  // 2026-10-11：系统主音量（Core Audio/WASAPI，独立编译单元）
#include <cstdio>

// 前向声明：进程图标（定义在文件后半段，process_list 要用）
static QString windowIconBase64(HWND hwnd);
/**
 * 取窗口图标 → 16×16 PNG 的 **base64**（给管理端「正在运行」列表显示图标用）。
 *
 * 为什么必须由被控端出图标：进程跑在这台教室机上，管理端本机没有它的 exe，
 * 无从取图标；只能这边从窗口句柄取了编码回传。
 *
 * 取不到（服务进程 / 无窗口 / 跨权限）返回空串 —— 管理端会画一个首字方块占位，
 * 不至于"有的有图标有的没有"看着像渲染坏了。
 */
static QString windowIconBase64(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) return QString();

    HICON ico = nullptr;
    // 顺序：任务栏那个"大图标" → 小图标 → 窗口类注册时带的图标。
    // 一律走 SendMessageTimeout 而不是 SendMessage：目标窗口若已挂起（无响应），
    // 直接 SendMessage 会把抓屏这条线程一起卡死在那里。
    DWORD_PTR r = 0;
    if (!SendMessageTimeoutW(hwnd, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, 60, &r) || r == 0) {
        r = 0;
        SendMessageTimeoutW(hwnd, WM_GETICON, ICON_SMALL2, 0, SMTO_ABORTIFHUNG, 60, &r);
    }
    ico = reinterpret_cast<HICON>(r);
    if (!ico) ico = reinterpret_cast<HICON>(GetClassLongPtr(hwnd, GCLP_HICON));
    if (!ico) return QString();

    ICONINFO ii;
    memset(&ii, 0, sizeof(ii));
    if (!GetIconInfo(ico, &ii)) return QString();

    QString out;
    do {
        HBITMAP hbm = ii.hbmColor;
        if (!hbm) break;                       // 纯掩码图标（1bpp），不做，直接放弃

        BITMAP bm;
        memset(&bm, 0, sizeof(bm));
        if (!GetObjectW(hbm, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) break;

        HDC dc = CreateCompatibleDC(nullptr);
        if (!dc) break;
        BITMAPINFOHEADER bi;
        memset(&bi, 0, sizeof(bi));
        bi.biSize = sizeof(bi);
        bi.biWidth = bm.bmWidth;
        bi.biHeight = -bm.bmHeight;            // 负值 = 自上而下，省一次翻转
        bi.biPlanes = 1;
        bi.biBitCount = 32;
        bi.biCompression = BI_RGB;

        const int stride = bm.bmWidth * 4;
        QByteArray buf(stride * bm.bmHeight, 0);
        if (!GetDIBits(dc, hbm, 0, (UINT)bm.bmHeight, buf.data(),
                       reinterpret_cast<BITMAPINFO *>(&bi), DIB_RGB_COLORS)) {
            DeleteDC(dc);
            break;
        }
        DeleteDC(dc);

        // GDI 给的是 BGRA 字节序，小端下正好就是 Qt 的 Format_ARGB32 内存布局
        QImage img(reinterpret_cast<const uchar *>(buf.constData()),
                   bm.bmWidth, bm.bmHeight, QImage::Format_ARGB32);
        if (img.isNull()) break;
        // 缩到 16×16 再编码：图标经 JSON 回传，原图 32/48/256 都有，
        // 几十条进程原样回传会把一条消息撑到几百 KB。
        img = img.scaled(16, 16, Qt::KeepAspectRatio, Qt::SmoothTransformation).copy();

        QByteArray png;
        QBuffer bb(&png);
        if (!bb.open(QIODevice::WriteOnly)) break;
        if (!img.save(&bb, "PNG")) break;
        bb.close();
        out = QString::fromLatin1(png.toBase64());
    } while (false);

    // GetIconInfo 造出来的两张位图必须自己删，否则每个进程漏两个 GDI 句柄
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    return out;
}


// 音量与进程内存信息要显式链接（MSVC 下 pragma 最省事，不必动 CMakeLists）
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shell32.lib")  // SHFileOperationW
#pragma comment(lib, "sapi.lib")     // ISpVoice（通知 TTS）
#pragma comment(lib, "ole32.lib")    // CoCreateInstance / CoInitializeEx（Core Audio）

// setSystemMasterVolume 由 system_volume.h 声明、system_volume.cpp 实现。
// 刻意独立成编译单元：main.cpp 已经 5500+ 行，再把 WASAPI/MMDevice 那一大套
// SDK 头灌进来，只会让它更容易撞 C1060（编译器堆空间不足）。

// ── 跨 TU 共享的状态（2026-10-22 · AGENTS.md §10 第二刀）──────────────────
// 这几条**必须在匿名 namespace 之外**：匿名域符号是内部链接，跨 TU 会 LNK2019。
// （项目里 makeEnvelope 踩过同一个坑，见文件末尾那段的注释。）
//
// ⚠️ 2026-10-23 第四刀：录像/播放的 6 个状态（g_recProc / g_recOutPath /
//    g_playProc / g_mediaTick / g_mediaAutoStop / g_mediaStopping）已**收归
//    src/media/MediaSession.cpp**（文件内 static + 访问器）。相机分支第三刀抽走、
//    媒体会话分支第四刀抽走之后，本文件对它们已无使用点。
//    剩下这两条属「存储/路径」域，被 media / screenshot / 抓屏 三处共用，
//    等存储域那一刀再处理。
QString g_mediaDir;                // 媒体库目录（media_list/media_session 默认扫这里）
QString g_shotDir;                 // 截图/存图目录（screenshot 动作与抓屏都用它）

namespace {

constexpr int kFrameIntervalMs = 2000;   // 2 秒一帧，与 D2 保持一致
constexpr int kHeartbeatIntervalMs = 10000; // 心跳缺省值（云端 registered 里给了就以云端为准）
// 2026-10-14 修（审计 P2）：云端下发的心跳间隔以前只要 >0 就无条件采纳，
// 异常值（笔误写成 3600000 = 1 小时之类）会让心跳变得极稀 → 云端 30s 判离线 →
// 重连 → 又收到同一份配置 → 循环「连上又断」。现在钳制在这个区间内。
constexpr int kHeartbeatMinMs = 1000;    // 下界：再密就白耗流量
constexpr int kHeartbeatMaxMs = 60000;   // 上界：再稀就会被云端当离线
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
constexpr const char *kAppVersion = "0.6.47-rc.3";

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
// P1（2026-10-21）：语音编解码实例。**惰性初始化**（首次用到才开）——没开过语音的机器
// 不该白占两个 Opus 上下文；编码器 16 kbps 起步，与方案 §5 P1 的档位一致。
static OpusCodec g_opusEnc;      // 学生 mic 上行编码
static OpusCodec g_opusDec;      // 老师下行解码
// ── 通知体系组件实例（2026-10-09 接线 · DeepSeek Harness）──
// 弹幕（2026-10-15 组件5）：全局指针 g_danmaku 已删，改 danmaku.h 的单例访问器
// danmakuWidget()/danmakuWidgetPeek() —— 通知路径（下方 kind=danmaku）与学生祝福
// 路径（IslandOverlay::sendBlessing）共享同一个弹幕层，全局指针会指岔。
LoopReminder g_loopReminder;                 // 循环提醒（无 UI，纯逻辑）
NotificationGate g_notifGate;                // 通知门控（考试模式禁通知）
Microphone g_microphone;                     // 语音通话 mic 采集（会话内开关）
// §6.3 语音频谱（2026-10-13）：不再有独立 SpectrumWidget 窗口 —— 频谱由灵动岛在
// **展开态**画（IslandOverlay::feedSpectrum）。原 `g_spectrum`（无 parent 的顶层窗口）已删。
// 2026-10-08：断线灵动岛去重。退避重连期间每次 disconnected 都会再弹一条「连接中断」，
// 首连快速重试（500ms）下等于每半秒闪一次，用户看到的就是"一直显示连接中断"。
// 规则：同一段离线只弹第一条；云端回 registered（真的又连上了）才把闸门放开。
static bool g_islandOfflineShown = false;
static qint64 g_lastOfflineToastMs = 0;  // 2026-10-08：上一条"连接中断"灵动岛的时间戳（秒级冷却，防抖动时反复弹）
static qint64 g_lastOfflineAt = 0;       // 2026-10-12：最近一次断线的时刻（0=在线或首连）。registered 时用来报「已恢复」
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

// 前向声明：调度器回调在文件后面用到它，避免 C++ 的"用到才定义"顺序问题。
bool enableShutdownPriv();

// ══ 批次 2（全量补齐）新增的全局状态 ══
QVector<QTimer *> g_schedTimers;   // 定时关机：每个任务一个单次定时器（触发即执行 ExitWindowsEx）
QHash<QString, QTimer *> g_schedTimerById;   // 任务 id → 定时器（取消时必须停掉，否则到点仍会执行）
QString g_schedFile;               // 调度持久化文件（崩溃恢复后重挂未到期任务）
QString g_schedDir;                // 调度文件所在目录（写文件要 mkpath）
QString g_recvDir;                 // file_push 落盘目录
bool g_remoteControl = false;      // remote_control_start/stop 的真实会话状态
int g_currentFps = 5;              // 动态帧率：当前抓屏频率（次/分钟）—— 远控开会时提高
// ── 远控上行治理配套旗标（2026-10-19 修复①③，见下方背压闸与看门狗）──────────
qint64 g_vncSessionStartMs = 0;    // 本次远控会话起服时刻（首帧宽限 + 看门狗起步宽限都用它）
qint64 g_lastVncDownMs = 0;        // 最近一次收到管理端 vnc-data down 字节（键鼠/请求/5s 保活）
bool g_vncMegaDone = false;        // 本会话是否已豁免过一个 ≥200KB 整屏帧（一次性）
int g_frameDiffMs = -1;            // 上一帧耗时（毫秒），动态帧率控制器用它判断画面是否稳定

// A6（2026-10-11）：噪音检测采集器。懒加载单例 —— 没收到 noise_monitor_start 前不 new，
// 不占麦克风（教室机麦克风可能同时被语音对讲用，见 noise_monitor.cpp 顶部注释）。
NoiseMonitor *g_noise = nullptr;

// file_push 接收会话状态：一个文件一段会话（file_push 初始化 → 多次 file_chunk 追加 → file_done 收尾）
QString g_fileRecvTarget;          // 目标文件绝对路径（file_push 时定下）
QFile *g_fileRecvFile = nullptr;   // 接收文件句柄（WriteOnly）
int g_fileRecvNext = 0;            // 下一个期望的分片序号（必须按序，乱序即失败）
qint64 g_fileRecvBytes = 0;        // 累计已写入字节（file_done 时与声明值比对）
QString g_fileRecvName;            // 接收文件名（进度卡显示用，file_push 时定下）
qint64 g_fileRecvTotal = 0;        // 声明的总字节（file_done 比对 + 进度条分母）
// 2026-10-11：接收会话"最后一次有动静"的时刻（毫秒）。用来清半截文件 ——
// file_push 一进来就把目标 Truncate 成 0 字节，若对方半途断线再也不来 file_done，
// 这个空文件就永久留在收件目录里（用户报的"推完的文件是空的"多半就是撞上它）。
qint64 g_fileRecvLastMs = 0;
static const qint64 kFileRecvStaleMs = 120000;   // 120 秒没动静即判死会话

// 人性化的体积文案（给"收到文件"提示用）：1024 进制，只到 MB 为止
static QString humanBytes(qint64 n)
{
    if (n < 1024) return QString::number(n) + QStringLiteral(" B");
    if (n < 1024 * 1024)
        return QString::number((double)n / 1024.0, 'f', 1) + QStringLiteral(" KB");
    return QString::number((double)n / 1024.0 / 1024.0, 'f', 1) + QStringLiteral(" MB");
}

// 死会话清理：关句柄 + 删半截文件 + 复位状态。心跳里每轮调一次。
// 不做这件事的后果是收件目录里堆一堆打不开的 0 字节残骸，且下次 file_push
// 会撞名（虽然有换名兜底，但目录还是被污染）。
static void reapFileRecvIfStale()
{
    if (g_fileRecvTarget.isEmpty()) return;
    if (g_fileRecvLastMs <= 0) { g_fileRecvLastMs = QDateTime::currentMSecsSinceEpoch(); return; }
    if (QDateTime::currentMSecsSinceEpoch() - g_fileRecvLastMs < kFileRecvStaleMs) return;

    qInfo("[agent-qt] 文件接收会话超时（%lld 秒没动静），清理半截文件：%s（已收 %lld 字节）",
          (long long)(kFileRecvStaleMs / 1000), qPrintable(g_fileRecvTarget),
          (long long)g_fileRecvBytes);
    if (g_fileRecvFile) { g_fileRecvFile->close(); delete g_fileRecvFile; g_fileRecvFile = nullptr; }
    QFile::remove(g_fileRecvTarget);
    g_fileRecvTarget.clear();
    g_fileRecvNext = 0;
    g_fileRecvBytes = 0;
    g_fileRecvLastMs = 0;
    // #369：超时清理与 file_done 的成功/取消/字节不符三条路径一致 —— 把常驻「文件传输」
    // 进度卡撤掉。否则半截文件清了、卡却没清，学生机那张灵动岛卡永久残留（"长时间占用通知
    // 资源"的根因：接收会话 120s 没动静被回收，但岛卡无人收口）。
    IslandOverlay::instance()->clearFileTransfer();
}

// ── 配置/连接可见性状态（2026-10-04）──
// 目标：配置缺失时"明显报错"，不和"配了但连不上"混为一谈。
enum class TrayState { Connected, Disconnected, Unconfigured };
bool g_configured = true;            // URL+TOKEN 是否齐全（loadEnvFile 后判定）
bool g_unconfigured = false;         // 已进入"未配置"态（不连接/不重试）
bool g_unconfiguredNotified = false; // 未配置气泡只弹一次

// ── P0-1（2026-10-14）：本地功能开关 ───────────────────────────────────────
// 把被控端已存在的各项能力（通知/语音播报/噪音监测/灵动岛常驻/浅色主题）做成可勾选的开关，
// 落 QSettings（组织 Stelarith / 应用 Stelarith Agent，注册表 HKCU）。默认全开，
// 各子系统的入口点（speakText / notifyFromParams / noise_monitor_start / 灵动岛初始化）统一读这里。
// 这样"设置项"才真的"调用设置项"，而不是只摆几个开机自启开关（用户 2026-10-14 反思）。
// ⚠️ 2026-10-23（AGENTS.md §10 第四刀地基）：AgentSettings 已下沉到 src/core/AgentSettings.h。
//    原因：它原先**嵌套在本文件匿名 namespace（251 行起）内**、函数又全带 static ⇒ 内部链接，
//    跨不了 TU；media_session_start 分支下沉到 src/media/ 之后，那边必须能读 playScheduleEnabled。
//    唯一变换 static → inline（头文件定义的标准要求），函数体与键名逐字未动。
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

// 远控 vnc-data 上行帧（2026-10-18 · 统一方案 A）：本地 VNC 产出的 RFB 字节 → 云端。
// 与画面帧同 v1 信封，区别：type=vnc-data / mime=application/vnc / direction=up / seq 自增
// （云端按 uid 原样中继到订阅该设备的管理端，不重写信封）。
static quint64 g_vncUpSeq = 0;
QByteArray makeVncFrameBytes(const QByteArray &rfb)
{
    QJsonObject h;
    h.insert(QStringLiteral("v"), kProtocolVersion);
    h.insert(QStringLiteral("type"), QStringLiteral("vnc-data"));
    h.insert(QStringLiteral("uid"), g_uid);
    h.insert(QStringLiteral("seq"), QJsonValue(static_cast<qint64>(++g_vncUpSeq)));
    h.insert(QStringLiteral("ts"), QJsonValue(QDateTime::currentMSecsSinceEpoch()));
    h.insert(QStringLiteral("mime"), QStringLiteral("application/vnc"));
    h.insert(QStringLiteral("direction"), QStringLiteral("up"));
    h.insert(QStringLiteral("bytes"), rfb.size());
    const QByteArray header = QJsonDocument(h).toJson(QJsonDocument::Compact);

    QByteArray out;
    out.reserve(3 + header.size() + rfb.size());
    out.append(char(kProtocolVersion));                 // 1B 版本
    out.append(char((header.size() >> 8) & 0xFF));      // 2B 大端 headerLen
    out.append(char(header.size() & 0xFF));
    out.append(header);
    out.append(rfb);
    return out;
}

// 语音通话上行帧（2026-10-09 · 语音通话设计 §4.1）：学生 mic → 云端。
// 与画面帧同 v1 结构，区别在 mime（云端据此走 duplex 单播回老师）。
// 2026-10-21（P1）：载荷从裸 PCM 换成 Opus 包 —— mime=audio/opus 标记编码类型。
// ⚠️ 必须**按 mime 区分而不是按会话态猜**：滚动升级期间新旧端共存，
//    旧管理端只认 audio/pcm（裸 PCM），新管理端两种都能解。
QByteArray makeAudioFrameBytes(const QByteArray &payload, bool opus)
{
    QJsonObject h;
    h.insert(QStringLiteral("v"), kProtocolVersion);
    h.insert(QStringLiteral("type"), QStringLiteral("frame"));
    h.insert(QStringLiteral("uid"), g_uid);
    h.insert(QStringLiteral("seq"), ++g_audioUpSeq);
    h.insert(QStringLiteral("ts"), QJsonValue(QDateTime::currentMSecsSinceEpoch()));
    // ⚠️ QStringLiteral 只接受字面量（宏展开成 u"" 运算符），不能塞三元表达式。
    h.insert(QStringLiteral("mime"),
             opus ? QStringLiteral("audio/opus") : QStringLiteral("audio/pcm"));
    h.insert(QStringLiteral("bytes"), payload.size());
    const QByteArray header = QJsonDocument(h).toJson(QJsonDocument::Compact);

    QByteArray out;
    out.reserve(3 + header.size() + payload.size());
    out.append(char(kProtocolVersion));
    out.append(char((header.size() >> 8) & 0xFF));
    out.append(char(header.size() & 0xFF));
    out.append(header);
    out.append(payload);
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
    // ⚠️ 2026-10-13：原来用 rand()，而**全仓没有任何 srand()**（MSVC 下等价 srand(1)）⇒
    //    每台教室机拿到**完全相同的随机序列**，断线后算出同一个 jitter、同一毫秒一起重连，
    //    正是上面这段注释要避免的"一起重连把云端瞬间打死 → 又一起断"的雪崩 —— 抖动白加了。
    //    改走 QRandomGenerator::system()（Windows 上 BCryptGenRandom）。
    const double r = QRandomGenerator::system()->generateDouble();   // [0,1)
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
    // 噪音 / 自习（2026-10-13）：noise_monitor_* 是底层采集开关（只测不录）；
    // study_mode_* 是上层模式（周期测噪 + 连续超阈自动录一段摄像头并上传）。
    // ⚠️ noise_monitor_* 之前一直漏登记 —— 设备实际能做却自称不会，本轮补上。
    "noise_monitor_start", "noise_monitor_stop",
    "study_mode_start", "study_mode_stop",
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
// 2026-10-12 修「20-30 分钟自动重连」：实测云端 events.log 里 N7 是「设备掉线」
// （TCP 断）而非「心跳超时踢」→ 中间设备（Cloudflare 隧道/防火墙）对**空闲连接**
// 的 idle timeout 静默断。应用层 heartbeat（10s 一条文本）在隧道看来可能不算
// 活跃流量，故每次心跳**同时发一条 WS 协议级 Ping 帧**（RFC6455，纯网络活动），
// 让中间设备认为连接活跃，不触发 idle 回收。云端 node-ws 会自动回 Pong，无需改。
void sendHeartbeat()
{
    if (!g_registered) return;   // 还没通过云端校验，先别发（发了会被踢）
    if (!g_ws || g_ws->state() != QAbstractSocket::ConnectedState) return;

    // 协议级 Ping 帧：对抗隧道/防火墙对空闲长连接的 idle timeout。
    // 应用层 heartbeat 是"业务消息"，隧道可能不把它当活跃证据；Ping 是纯协议
    // 帧，任何 WS 实现都处理，必然产生双向网络活动，连接不会被中间设备判闲。
    // ⚠️ 2026-10-13：QWebSocket::ping() 返回 **void**、也不抛异常（Qt 用信号报错）——
    //    原来的 try/catch 是空壳、纯误导（它让人以为"捕获了异常"）。直接调用即可；
    //    写失败 / 连接断由 disconnected / errorOccurred 兜底重连。
    g_ws->ping();

    QJsonObject p;
    p.insert(QStringLiteral("seq"), ++g_heartbeatSeq);
    const QString hb = makeEnvelope(QStringLiteral("heartbeat"), p);
    const qint64 n = g_ws->sendTextMessage(hb);
    if (n <= 0)
        qWarning().noquote() << QStringLiteral("[agent-qt] FAIL: 心跳发送失败（写了 %1 字节）—— 云端可能已断").arg(n);
    else
        qInfo("[agent-qt] 💓 心跳 #%d（%lld ms，interval=%d ms）",
              g_heartbeatSeq, QDateTime::currentMSecsSinceEpoch(), g_heartbeatMs);
}

// 真执行一个指令。返回 result＝"done" / "failed"（绝不许报 done 其实没做）。
// D5 起：lock/shutdown/reboot 真的做；input 真的注入鼠标键盘。
// ⚠️ 2026-10-23（AGENTS.md §10 第 3 刀）：ExecOut 已提到 src/core/ExecOut.h。
//    原因：它原在本文件匿名 namespace 内，匿名域类型**跨不了 TU**，
//    相机分支抽到 src/media/CameraActions.cpp 后那边拿不到这个类型。

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
        // 2026-10-13：级别从 qInfo 升到 qWarning —— 这条写不进 = 重启后 shutdown/reboot
        // 记录全丢 = 重复下发无限重启的根因，不能让它在监控里当普通信息溜过去。
        qWarning("[agent-qt] ⚠ 破坏性指令记录写不进（%s）—— 重启后可能重复执行",
                 qPrintable(f.errorString()));
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


// ⚠️ 2026-10-22（AGENTS.md §10 第 1 刀）：ffmpeg 定位 / 摄像头绑定 / 设备解析 /
//    错误归类已下沉到 src/media/MediaTools.{h,cpp}，调用一律改 MediaTools:: 前缀。
//    相机四件套的分支体还在下面的 executeAction 里，是下一刀的目标。

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
        qWarning().noquote() << QStringLiteral("[agent-qt] FAIL: 调度任务持久化失败（写 %1 打不开）—— 本机重启后这批定时任务会丢").arg(g_schedFile);
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
        qWarning().noquote() << QStringLiteral("[agent-qt] FAIL: 调度文件不是合法 JSON 数组：%1（已忽略，不当成功）").arg(g_schedFile);
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
        qWarning().noquote() << QStringLiteral("[agent-qt] FAIL: 定时 %1 的 ExitWindowsEx 失败，错误码 %2").arg(what).arg((int)GetLastError());
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
    if (pid == 0) return TRUE;
    // 非阻塞取标题长度：挂起（无响应）窗口用 GetWindowTextLengthW 会卡死整条枚举，
    // 必须改用 SendMessageTimeout + SMTO_ABORTIFHUNG（与 windowIconBase64 取图标同一策略）。
    DWORD_PTR len = 0;
    if (!SendMessageTimeoutW(hwnd, WM_GETTEXTLENGTH, 0, 0, SMTO_ABORTIFHUNG, 60, &len) || len == 0)
        return TRUE;
    acc->append({ hwnd, (quint32)pid });
    return TRUE;
}

// 进程快照 + 可见窗口枚举的短时效缓存：管理端开「软件」弹窗会连发
// process_list 与 list_shortcut_candidates 两条，原本各自独立枚举一遍窗口
// （每条还要逐窗口非阻塞取标题）。两条间隔通常 <100ms，缓存 1.5s 让第二条
// 直接复用，省掉一次完整枚举，避免拖慢弹窗响应。
struct WinCache {
    qint64 at = 0;
    QHash<quint32, QString> names;
    QVector<QPair<HWND, quint32>> wins;
};
static WinCache g_winCache;
constexpr qint64 kWinCacheTtlMs = 1500;

static const WinCache &visibleWindowsCached()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!g_winCache.names.isEmpty() && now - g_winCache.at < kWinCacheTtlMs)
        return g_winCache;
    g_winCache.names = snapshotProcessNames();
    g_winCache.wins.clear();
    if (!g_winCache.names.isEmpty())
        EnumWindows(enumWindowsCb, reinterpret_cast<LPARAM>(&g_winCache.wins));
    g_winCache.at = now;
    return g_winCache;
}

QString windowTitleOf(HWND hwnd)
{
    // 非阻塞取标题：GetWindowTextW 对挂起（无响应）窗口会卡住整条枚举，
    // 必须改用 SendMessageTimeout + ABORTIFHUNG（与 enumWindowsCb 一致）。
    wchar_t buf[1024] = { 0 };
    DWORD_PTR n = 0;
    if (!SendMessageTimeoutW(hwnd, WM_GETTEXT, (WPARAM)(sizeof(buf) / sizeof(wchar_t)),
                             (LPARAM)buf, SMTO_ABORTIFHUNG, 60, &n) || n == 0)
        return QString();
    return QString::fromWCharArray(buf, (int)n);
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
    // ⚠️ 2026-10-19 改为**轮转**（原来超过 2MB 直接 QFile::remove 整个日志）：
    //   旧实现的后果是真机排查时日志被整段清空 —— 本次远控崩溃前「抓屏后端=dxgi」
    //   「DuplicateOutput OK」「抓屏线程退出」等关键行全部失踪，只能靠 WER dump 反推。
    //   现在保留 1 份历史（agent-qt.log.1），排查窗口翻倍且不会一次全丢。
    if (f.size() > 2 * 1024 * 1024) {
        const QString prev = g_logPath + QStringLiteral(".1");
        QFile::remove(prev);
        QFile::rename(g_logPath, prev);
    }
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
        QStringLiteral("本机尚未接入集控：%1。\n右键托盘图标 →「配置…」填好后即可接入。")
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
// ⚠️ 路径**必须**跟着 agentEnvPath() 走（而不是自己拼 LOCALAPPDATA）：那份实现已处理
//    「目录建不出 → 回退安装目录 + qWarning」；自己拼的话 LOCALAPPDATA 一旦为空（服务账户/
//    Session0）就会写到 C:/xingjikong/，写失败还**静默无声**，渠道悄悄丢（fail-silent 红线）。
//    agentEnvPath() 定义在**本函数之前**（约 1023 行），可直接调用。
QString channelFilePath()
{
    // 与 agent.env 同目录：配置在哪，渠道就在哪，不会出现两者分裂。
    return QFileInfo(agentEnvPath()).path() + QStringLiteral("/channel.txt");
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
        return;
    }
    // 写不出必须喊出来：渠道没持久化 = 重启/升级后悄悄回落 stable，
    // 现场表现和"灰度机自己退出灰度"一模一样，不写日志事后根本查不到（fail-silent 红线）。
    qWarning("[agent-qt] ⚠ channel.txt 写不出（%s：%s）—— 渠道不会持久化，重启/升级后可能回落 stable",
             qPrintable(p), qPrintable(f.errorString()));
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

/** 向导（二维码/绑定码）打哪儿？
 *
 * 与 siteBaseFromWsUrl（站点，教程/激活码）区分开：**绑定这座桥的两端都在云端**——
 *   GET /api/device/bind-qr、bind-status、POST /api/device/activate 全部由云端实现
 *   （2026-10-20 新建，见 Stelarith-cloud/src/bind-qr.js + index.js）。
 * 所以向导必须打「与 WS 同源」的地址，否则会打到站点上（那里没有这几个端点，
 * 向导只能一路降级到"输入绑定码"）。
 *
 * 判定顺序：
 *   ① STE_QT_BIND_URL 显式覆盖（把绑定接口挪去站点/别的服务时用）
 *   ② 从 STE_QT_WS_URL 推同源地址（ws→http / wss→https，端口沿用）：公网与内网都对
 *   ③ 都没有才退回生产站点常量（此时向导会降级到输码，至少不是白屏）
 */
QString bindBaseFromWsUrl(const QString &wsUrl)
{
    const QString explicitBase = qEnvironmentVariable("STE_QT_BIND_URL").trimmed();
    if (!explicitBase.isEmpty()) return explicitBase;

    const QUrl u(wsUrl.trimmed());
    if (u.isValid() && !u.host().isEmpty()) {
        const QString scheme = (u.scheme().compare(QStringLiteral("wss"), Qt::CaseInsensitive) == 0)
                                   ? QStringLiteral("https") : QStringLiteral("http");
        const int port = u.port();
        const bool defaultPort = (port <= 0)
            || (scheme == QLatin1String("https") && port == 443)
            || (scheme == QLatin1String("http") && port == 80);
        return defaultPort ? QStringLiteral("%1://%2").arg(scheme, u.host())
                           : QStringLiteral("%1://%2:%3").arg(scheme, u.host()).arg(port);
    }
    return QString::fromUtf8(kDefaultSiteUrl);
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
//   · **接入码**（XJK-XXXX-XXX，管理员生成、一次性、带 TTL）→ 也打**云端**的
//     `/api/device/activate`；云端代为向站点兑换，并把站点 `class_id` 纳进班级目录归一
//     （2026-10-20 班级真源统一，见 Stelarith-cloud/src/class-dir.js）。
//     ⚠️ 统一之前这里打的是站点（www.*），于是同一个班在云端记 `cls-xxxx`、在站点记
//        `class_01` —— 两套码并存，重启后还能长出第三套。现在设备端只认识一个地址。
//     ⚠️ 自建站点 / 站点直连（云端没升级）时，把 `STE_QT_BIND_URL` 显式指到站点即可，
//        走的还是这条唯一的代码路径，不再有第二条分支。
static void applyClassBinding(const QString &rawCode, const QString &wsUrl, const QString &uid)
{
    const QString code = rawCode.trimmed();
    if (code.isEmpty()) return;

    saveClassCode(code);   // 持久化 → 下一次 register 带上 classCode

    // 只有接入码需要去兑换；班级码没有可消费的东西。
    if (!code.startsWith(QStringLiteral("XJK-"), Qt::CaseInsensitive)) {
        qInfo().noquote() << QStringLiteral("[agent-qt] 已记录班级码 %1（register 时带给云端按班归组）").arg(code);
        return;
    }

    // 唯一入口：云端（含站点的兑换由云端代劳，见 exchangeSiteActivationCode）
    const QString bindBase = bindBaseFromWsUrl(wsUrl);
    if (bindBase.isEmpty()) {
        qWarning().noquote() << QStringLiteral("[agent-qt] ⚠️ 没有可用的绑定服务地址，跳过接入码兑换（班级码已存，register 仍会带给云端）");
        return;
    }
    const QString u = uid.trimmed().isEmpty() ? QHostInfo::localHostName() : uid.trimmed();
    qInfo().noquote() << QStringLiteral("[agent-qt] 兑换接入码 → %1/api/device/activate (uid=%2)").arg(bindBase, u);
    consumeActivationCodeAsync(bindBase, code, u);
}

/* ══════════ 首次启动引导（OOBE Wizard）（2026-10-20） ══════════
 * 用户要求：首次打开走一遍「欢迎 → 选绑定方式 → 扫码/输码 → 确认班级」，全程只出现
 * 「班级 / 绑定码 / 扫码」这类词；任何一步失败都要有友好提示（不崩、不白屏）；
 * 必须能「稍后配置」——跳过后进未绑定/离线主界面，且**不再反复打扰**。
 *
 * 分工（单一真源）：
 *   · 界面与状态机：qml/oobe/（StackView 四步）+ src/oobe_wizard.*（网络与状态）
 *   · 落盘与重连：本文件 —— boundSucceeded → saveClassCode/saveClassName + 补发 register
 *   · 「有没有问过用户」：数据目录 oobe_done.txt（bound / skipped 都算问过）
 * ⚠️ 为什么不复用旧配置窗：那个是填云端地址/令牌的技术表单；向导只解决"绑班级"，
 *    两件事的用户面完全不同（配置窗保留，托盘「配置…」仍可进）。
 */

/** 向导"已问过"标记：bound / skipped 都写，避免每次开机都弹一遍。 */
QString oobeFlagPath()
{
    return QFileInfo(agentEnvPath()).path() + QStringLiteral("/oobe_done.txt");
}

bool oobeSettled()
{
    return QFileInfo::exists(oobeFlagPath());
}

void markOobeSettled(const char *how)
{
    QFile f(oobeFlagPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning("[agent-qt] ⚠ 引导标记写不出（%s）—— 下次启动可能再弹一次",
                 qPrintable(f.errorString()));
        return;
    }
    f.write(QString::fromUtf8(how).toUtf8());
    f.write("\n");
    f.close();
}

// 单例：托盘连点 / 自动弹与手动点同时发生时不叠窗（与 AgentShell::openPanel 同款理由）。
static QPointer<QDialog> s_oobeDlg;

/**
 * 打开首次启动引导。
 *
 * 为什么窗口里套 QQuickWidget 而不是 QQuickView：与 AgentShell 完全同款（同一套 QML
 * 加载口径：从 exe 旁的 qml/ 读盘），且 QDialog 能拿到正常的标题栏/任务栏行为，
 * 老师不会遇到"窗口跑到岛后面、点不到"这种 QQuickView 常见问题。
 */
void openOobeWizard()
{
    if (s_oobeDlg) { s_oobeDlg->raise(); s_oobeDlg->activateWindow(); return; }

    auto *dlg = new QDialog(nullptr);
    s_oobeDlg = dlg;
    dlg->setWindowTitle(QStringLiteral("星集控 · 首次配置"));
    QScreen *sc = QGuiApplication::primaryScreen();
    const QRect av = sc ? sc->availableGeometry() : QRect(0, 0, 1024, 720);
    const int w = qMin(900, qMax(620, int(av.width() * 0.8)));
    const int h = qMin(680, qMax(540, int(av.height() * 0.88)));
    dlg->resize(w, h);
    dlg->move(av.x() + (av.width() - w) / 2, av.y() + (av.height() - h) / 2);

    auto *lay = new QVBoxLayout(dlg);
    lay->setContentsMargins(0, 0, 0, 0);

    auto *wiz = new OobeWizard(dlg);
    wiz->setMachineCode(g_uid);
    // 向导走"与云端同源"（二维码/绑定端点都在云端），见 bindBaseFromWsUrl
    const QString bindBase = bindBaseFromWsUrl(qEnvironmentVariable("STE_QT_WS_URL"));
    wiz->setSiteBase(bindBase);
    // 现场排障必须看得见"打的是谁"：二维码扫不出来时，第一个要问的就是这个地址
    qInfo().noquote() << QStringLiteral("[oobe] 绑定服务地址：%1").arg(bindBase);

    auto *view = new QQuickWidget(dlg);
    view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    view->engine()->rootContext()->setContextProperty(QStringLiteral("oobe"), wiz);
    lay->addWidget(view);
    // QML 出问题必须可见（fail-silent 是红线）：日志里留下 QML ERROR 原文。
    QObject::connect(view, &QQuickWidget::statusChanged, view, [view](QQuickWidget::Status st) {
        if (st != QQuickWidget::Error) return;
        for (const QQmlError &e : view->errors())
            qWarning("[oobe] QML ERROR: %s", qPrintable(e.toString()));
    });
    view->setSource(QUrl::fromLocalFile(QCoreApplication::applicationDirPath()
                                        + QStringLiteral("/qml/oobe/OobeWizard.qml")));

    // 绑定成功：落盘 + 让云端**立刻**按班归组（不等下次重连）。
    QObject::connect(wiz, &OobeWizard::boundSucceeded, dlg,
                     [](const QString &classCode, const QString &className) {
        if (!classCode.isEmpty()) saveClassCode(classCode);
        if (!className.isEmpty()) saveClassName(className);
        markOobeSettled("bound");
        if (g_ws && g_ws->state() == QAbstractSocket::ConnectedState) {
            sendRegister();
            qInfo("[oobe] 已补发 register（带上班级码/名）");
        } else {
            g_backoffMs = 1000;
            connectNow();      // 绑了班但云端不知道 = 白绑，立刻按新配置连一次
        }
    });
    // 向导结束（完成 / 跳过）：关窗。跳过的机器进「未绑定/离线」主界面，不阻断启动。
    // ⚠️ 关窗前必须**先卸 QML**：QML 里的绑定持有 oobe 指针，若它与 OobeWizard 同时销毁，
    //    销毁顺序会让绑定在已析构对象上求值（探针实测：关窗刷一屏 TypeError ... of null）。
    QObject::connect(wiz, &OobeWizard::wizardFinished, dlg, [dlg, view](bool bound) {
        if (!bound) markOobeSettled("skipped");
        qInfo("[oobe] 引导结束（%s）", bound ? "已绑定" : "用户选择稍后配置");
        view->setSource(QUrl());                       // 断掉所有对 oobe 的绑定
        dlg->close();
        dlg->deleteLater();                            // 下次「绑定引导…」重建（带最新配置）
    });
    QObject::connect(dlg, &QObject::destroyed, [] { s_oobeDlg = nullptr; });

    dlg->show();
    dlg->raise();
    dlg->activateWindow();
    qInfo("[oobe] 已打开首次启动引导（qml/oobe/OobeWizard.qml）");
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

QString g_latestVer, g_updateUrl, g_updateSha, g_updateNotes;bool g_updateChecked = false;       // 有没有从云端拿到过版本信息
bool g_updateMandatory = false;     // 2026-10-06：云端 ota.json 的 mandatory（true=强制自动升级）
// 2026-10-17：已主动提示过的版本（周期性复查会反复读到同一版本，只提示一次）
static QString g_otaPromptedVer;
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
            // 2026-10-17：非强制也要能自己装上去 —— 延迟后再判安全窗口（见 maybeAutoUpdate）。
            // 不在这里直接装：刚弹完提示就重启设备，观感太差。
            // 延迟做成可配（STE_QT_OTA_AUTO_DELAY_SEC，默认 30s）：探针要在几十秒内跑完一轮，
            // 写死 30s 会让"判据到底生效没"变得难以取证。
            {
                int delaySec = qEnvironmentVariableIntValue("STE_QT_OTA_AUTO_DELAY_SEC");
                if (delaySec <= 0) delaySec = 30;
                delaySec = qBound(1, delaySec, 3600);
                QTimer::singleShot(delaySec * 1000, [] { maybeAutoUpdate(); });
            }
        }
        // 2026-10-17：同一个版本**只主动提示一次**（周期性复查每 30 分钟会再来一遍；
        // 每次都弹岛/弹托盘就成了骚扰）。强制升级那条路不受影响 —— maybeAutoUpdate
        // 自带 ota-state 退避与 ota-pause 刹车，该重试还会重试。
        const bool firstPrompt = (g_otaPromptedVer != g_latestVer);
        if (firstPrompt) g_otaPromptedVer = g_latestVer;

        // P0-3（2026-10-14）：托盘气泡容易错过 ⇒ 同时经灵动岛主动推送（带 changelog + 操作按钮）。
        // 非强制更新不打扰太久：20s 后自动收起；点了「立即更新」复用既有 self_update 链路。
        if (firstPrompt && g_tray && IslandOverlay::instance()) {
            const QString desc = g_updateNotes.isEmpty()
                ? QStringLiteral("当前 v%1，点「立即更新」即可静默升级。")
                      .arg(QString::fromLatin1(kAppVersion))
                : g_updateNotes;
            QStringList acts{ QStringLiteral("立即更新"), QStringLiteral("稍后") };
            IslandOverlay::instance()->showWithActions(
                QStringLiteral("发现新版本 v%1").arg(g_latestVer), desc,
                QStringLiteral("download"), IslandOverlay::Form::Centered, acts,
                [](const QString &a) {
                    if (a == QStringLiteral("立即更新")) {
                        QJsonObject p;
                        p.insert(QStringLiteral("url"), g_updateUrl);
                        p.insert(QStringLiteral("sha256"), g_updateSha);
                        p.insert(QStringLiteral("version"), g_latestVer);
                        startSelfUpdate(QStringLiteral("island"), p);   // 与管理端同一条 self_update 链路
                    }
                }, 20000, false);
        }
        if (firstPrompt && g_actUpdate) {
            g_actUpdate->setVisible(true);
            g_actUpdate->setText(g_updateMandatory
                                     ? QStringLiteral("将自动升级到 v%1（云端强制）").arg(g_latestVer)
                                     : QStringLiteral("发现新版本 v%1（点此查看）").arg(g_latestVer));
        }
        if (firstPrompt && g_tray) {
            g_tray->showMessage(
                QStringLiteral("星集控 · 被控端"),
                g_updateMandatory
                    ? QStringLiteral("云端要求升级到 v%1（当前 v%2）。\n联网后自动安装，不用手动点——"
                                     "过程中会短暂重启，一两分钟回来。")
                          .arg(g_latestVer, QString::fromLatin1(kAppVersion))
                    : QStringLiteral("有新版本 v%1（当前 v%2）。\n右键托盘图标 →「设置与信息…」即可升级。")
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
// OTA 健康快照上报（M3，2026-10-20）—— 定义在 sendEvent 之后（本文件靠后），
// 这里只提前声明：失败/成功/暂停/周期复查四个点都要调它。
static void otaReportHealth(const QString &why);

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
    // M3（2026-10-20）：失败是 OTA 最该被云端看见的状态 —— 退避 ladder 每升一级
    // 快照就变一次（failCount/nextAt），变化才发，不会刷屏。
    otaReportHealth(QStringLiteral("failure"));
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
    // ⚠️ 2026-10-18：成功必须把上一次的错误**清掉**。
    //   只写 lastResult=ok 而留着旧 lastError ⇒ 面板/排障时永远看到一条早已不成立的
    //   「备份当前 exe 失败」，把"其实没事"误判成"OTA 坏了"（本轮实测踩到）。
    st.remove(QStringLiteral("lastError"));
    otaSaveState(st);
    // M3（2026-10-20）：升级确认成功（新版本注册上了云端）也上报一次 ——
    // failCount 归零 + lastError 清空，快照与"失败中"是两个人，运营侧能看到闭环。
    otaReportHealth(QStringLiteral("success"));
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

    // ⚠️ 2026-10-18：目标位置可能被上一版留着（或被杀软/别的实例占着句柄）。
    //   原来这里无视 remove() 的返回值就往下走 ⇒ 后面的 copy 必然失败，日志却只有一句
    //   "备份失败"，分不清是"删不掉"还是"拷不动"。现在删不掉就改名挪开，并分别记账。
    if (QFile::exists(dst) && !QFile::remove(dst)) {
        const QString aside = dst + QStringLiteral(".stale");
        QFile::remove(aside);
        if (!QFile::rename(dst, aside)) {
            qWarning("[agent-qt] ⚠ 旧备份既删不掉也挪不开（%s）", qPrintable(dst));
            return QString();
        }
        qWarning("[agent-qt] ⚠ 旧备份删不掉，已挪到 %s", qPrintable(aside));
    }

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
        qWarning("[agent-qt] ⚠ 回滚备份不在（%s）—— 只能让这台设备保持现状，等下次升级 retry", qPrintable(prev));
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
    // 2026-10-21（审查 #10）：taskkill 的 /IM 原来是硬编码 stelarith-agent-qt.exe ——
    //   exe 被重命名（学校定制/重打包）时 taskkill 失败 ⇒ && 链断 ⇒ 后续 copy/start
    //   全不执行，回滚静默失效。改为从 applicationFilePath() 取当前进程的文件名拼。
    const QString exeName = QFileInfo(exe).fileName();
    const QString cmd = QStringLiteral(
        "taskkill /F /IM \"%1\" >nul 2>&1 && "
        "ping -n 4 127.0.0.1 >nul && "
        "copy /Y \"%2\" \"%3\" >nul && "
        "del /f /q \"%4\" && "
        "start \"\" \"%3\"");
    // #368 需求①：用静默 detached 拉起，杜绝回滚脚本弹出 cmd 黑窗
    stelarithStartHiddenDetached(QStringLiteral("cmd.exe"),
                                 {QStringLiteral("/c"), cmd.arg(exeName, prev, exe, otaPendingPath())});
    otaNoteFailure(QStringLiteral("新版本未能在 %d 秒内连上云端，已回滚").arg(kOtaRollbackMs / 1000));
    QTimer::singleShot(1500, [] { QCoreApplication::quit(); });   // 让位给刚拉起来的旧版
}

/** 该不该现在自动装：全部满足才动手，任何一条不满足都**如实说为什么**不装。 */
/* 桌面空闲时长（毫秒）—— 判"安全窗口"用（2026-10-17 · 修 OTA 不生效第三层）。
 * 用 GetLastInputInfo 而不是自己挂钩子：它是系统级"最后一次键鼠输入"，
 * 不依赖本进程有没有焦点，也不受被控端藏在托盘影响。
 * ⚠️ dwTime 是 32 位 GetTickCount 值 ⇒ 用 32 位差值（无符号自然回绕正确），
 *    不要拿 GetTickCount64 去减它（跨 49 天会算出天文数字）。
 * 取不到（远程/无输入会话）时返回 0 = 当作"正在使用"，宁可不升。 */
static qint64 desktopIdleMs()
{
    LASTINPUTINFO li{};
    li.cbSize = sizeof(LASTINPUTINFO);
    if (!GetLastInputInfo(&li)) return 0;
    const DWORD now = GetTickCount();
    return qint64(DWORD(now - li.dwTime));
}

static void maybeAutoUpdate()
{
    // ① 强制更新照旧"到点就装"；**非强制改判安全窗口**（2026-10-17 修）。
    //    病灶：mandatory=false 只弹提示 ⇒ 教室机无人值守、没人点「立即更新」
    //    ⇒ 版本永远卡在旧版。实测取证：stable 清单停在 0.6.37，而在线设备的日志
    //    一直只有"发现新版本…右键托盘可升级"，没有一个人会去右键。
    //    补法：非强制在 **非考试 + 桌面空闲 ≥ N 分钟** 时自动装 —— 老师在用电脑时
    //    绝不重启它；空闲 = 没人在看课件，被控端重启一两分钟不打断课堂。
    //    参数 STE_QT_OTA_AUTO_IDLE_MIN：默认 10（分钟），填 0 = 关掉非强制自动装。
    if (!g_updateMandatory) {
        const QString envIdle = qEnvironmentVariable("STE_QT_OTA_AUTO_IDLE_MIN").trimmed();
        const int idleMin = envIdle.isEmpty() ? 10 : envIdle.toInt();
        if (idleMin <= 0) return;                          // 管理员显式关掉了非强制自动装
        if (ExamMode::inst().active()) {
            // 考试期间绝不重启被控端（会打断考试计时/拦截逻辑），等考完再说。
            // ⚠️ 这条**不受** STE_QT_OTA_AUTO_FORCE 影响：考试优先级高于维护便利。
            qInfo("[agent-qt] ⏸ 非强制升级 v%s 暂缓：考试模式进行中",
                  qPrintable(g_latestVer));
            return;
        }
        // 跳过空闲判据：只给「管理员维护窗口」与「探针取证」用。
        // 常态化设它 = 正在用电脑时也会被重启，所以每次跳过都打 warning 留痕。
        if (qEnvironmentVariable("STE_QT_OTA_AUTO_FORCE") == QLatin1String("1")) {
            qWarning("[agent-qt] ⚠ STE_QT_OTA_AUTO_FORCE=1 ⇒ 跳过空闲判据（维护窗口/探针专用）");
        } else {
            const qint64 idle = desktopIdleMs();
            if (idle < qint64(idleMin) * 60000LL) {
                qInfo("[agent-qt] ⏸ 非强制升级 v%s 等到空闲：桌面只空闲 %lld 秒（要求 ≥ %d 分钟）",
                      qPrintable(g_latestVer), (long long)(idle / 1000), idleMin);
                return;
            }
            qInfo("[agent-qt] ⬆ 非强制升级 v%s → v%s：桌面已空闲 %lld 秒、非考试 ⇒ 自动装"
                  "（STE_QT_OTA_AUTO_IDLE_MIN 可调，0=关）",
                  kAppVersion, qPrintable(g_latestVer), (long long)(idle / 1000));
        }
    }
    if (g_unconfigured) {                                 // ② 没配好就别自作主张（升完照样连不上）
        qInfo("[agent-qt] 升级跳过：本机还没配好云端（未配置）");
        return;
    }
    if (otaIsBusy()) {                                    // ③ 上一次下载还没完
        qInfo("[agent-qt] 升级跳过：上一个更新任务还在跑");
        return;
    }
    if (g_updateUrl.isEmpty() || g_updateSha.length() != 64) {   // ④ 清单不完整 = 不许升
        qWarning("[agent-qt] 升级跳过：清单缺 url 或 sha256（不让管理员补 ota.json 就别硬升）");
        return;
    }
    if (compareVersion(g_latestVer, QString::fromLatin1(kAppVersion)) <= 0) return;   // ⑤ 已经是最新

    if (otaPaused()) {                                    // ⑥ 人工紧急刹车
        qWarning("[agent-qt] ⛔ 升级已在本机暂停（存在 %s）—— 管理员排查用，删掉这个文件即恢复",
                 qPrintable(otaPausePath()));
        // M3（2026-10-20）：暂停/恢复是管理员**从外部**拨的开关（往数据目录扔/删一个文件），
        // 端侧无从"感知事件"，只能靠快照比对：paused 翻转即上报一次（变化才发）。
        otaReportHealth(QStringLiteral("paused"));
        return;
    }

    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const qint64 nextAt = otaLoadState().value(QStringLiteral("nextAt")).toVariant().toLongLong();
        if (nextAt > now) {
            qInfo("[agent-qt] ⏳ 升级还没到点：%lld 秒后再试（上次结果 %s）",
                  (nextAt - now) / 1000, qPrintable(otaLoadState().value(QStringLiteral("lastError")).toString()));
            return;
        }
    }
    {
        QStorageInfo si(qEnvironmentVariable("LOCALAPPDATA"));
        si.refresh();
        if (si.bytesAvailable() < kOtaMinFreeBytes) {      // ⑦ 盘快满了：宁可不升，也别装到一半没地方
            qWarning("[agent-qt] ⚠ 升级跳过：数据盘只剩 %lld 字节（要求 ≥ %lld）",
                     (long long)si.bytesAvailable(), (long long)kOtaMinFreeBytes);
            return;
        }
    }

    qInfo("[agent-qt] 🔒 %s：v%s → v%s，开始自动下载安装（无需人工操作）",
          g_updateMandatory ? "云端要求强制升级" : "空闲窗口自动升级",
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

// 2026-10-13 登记项：主线程 QProcess::waitForFinished 会把**整个事件循环**停掉 ——
// 心跳不发、推帧停摆、WS pong 回不了；camera_test 最长等 9 秒，弱机教室机上就是一次肉眼可见的假死。

static bool agentAutostartEnabled()
{
    QProcess p;
    stelarithHideChildConsole(p);   // #368 需求①：schtasks 查询静默，不弹控制台窗
    p.start(QStringLiteral("schtasks"),
            {QStringLiteral("/Query"), QStringLiteral("/TN"), QString::fromLatin1(kAgentTaskName)});
    if (!ProcessUtil::waitFinished(p, 4000)) { p.kill(); return false; }
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
    if (!g_updateChecked)            latestText = QStringLiteral("还没查到（等云端回执）");
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
                                       ? QStringLiteral("强制：到点自动装（装完重启一次）")
                                       : QStringLiteral("可选：只有点「立即升级」才装"));
        if (g_updateMandatory) modeLbl->setStyleSheet(QStringLiteral("color:#c0392b;font-weight:bold;"));
        form->addRow(QStringLiteral("升级方式"), modeLbl);

        otaAutoLbl = new QLabel(paused
                                    ? QStringLiteral("已暂停（删掉 %1 即恢复）").arg(otaPausePath())
                                    : QStringLiteral("到点自动装"));
        if (paused) otaAutoLbl->setStyleSheet(QStringLiteral("color:#c0392b;"));
        form->addRow(QStringLiteral("自动升级"), otaAutoLbl);

        QString lastText;
        if (lastRes.isEmpty()) {
            lastText = QStringLiteral("（这台设备还没自动升级过）");
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
        chkAuto->setToolTip(QStringLiteral("管理登录触发器计划任务 %1；改动需管理员权限（弹 UAC）。")
                                .arg(QString::fromLatin1(kAgentTaskName)));
        QObject::connect(chkAuto, &QCheckBox::toggled, &dlg, [chkAuto](bool on) {
            if (!agentSetAutostart(on))
                QMessageBox::warning(chkAuto, QStringLiteral("开机自启"),
                                     QStringLiteral("没改成：多半是 UAC 被拒，或安装时没建该计划任务。"));
            chkAuto->blockSignals(true);
            chkAuto->setChecked(agentAutostartEnabled());
            chkAuto->blockSignals(false);
        });
        form->addRow(QStringLiteral("开机自启"), chkAuto);

        auto *chkLnk = new QCheckBox(QStringLiteral("在公共桌面放快捷方式"));
        chkLnk->setChecked(agentDesktopShortcutExists());
        chkLnk->setToolTip(QStringLiteral("公共桌面（所有登录用户可见）；改动需管理员权限。"));
        QObject::connect(chkLnk, &QCheckBox::toggled, &dlg, [chkLnk](bool on) {
            if (!agentSetDesktopShortcut(on))
                QMessageBox::warning(chkLnk, QStringLiteral("桌面快捷方式"),
                                     QStringLiteral("没改成：多半是 UAC 被拒。"));
            chkLnk->blockSignals(true);
            chkLnk->setChecked(agentDesktopShortcutExists());
            chkLnk->blockSignals(false);
        });
        form->addRow(QStringLiteral("桌面快捷"), chkLnk);
    }

    // ── P0-1（2026-10-14）：功能开关 —— 把已存在的能力做成可勾选、落 QSettings ──
    // 之前「设置与信息」只有开机自启/桌面快捷两项，用户反映"设置太简单、不调用设置项"。
    // 这里把通知/语音播报/噪音监测/灵动岛常驻/浅色主题都暴露出来，勾选即生效（部分实时作用于灵动岛）。
    {
        auto *grpLbl = new QLabel(QStringLiteral("功能开关"));
        grpLbl->setStyleSheet(QStringLiteral("font-weight:bold;margin-top:8px;"));
        form->addRow(grpLbl, new QWidget());   // 分组标题占一行

        auto mkToggle = [&](const QString &label, bool on, const std::function<void(bool)> &apply) {
            auto *cb = new QCheckBox(label);
            cb->setChecked(on);
            QObject::connect(cb, &QCheckBox::toggled, &dlg, [apply](bool v) { apply(v); });
            form->addRow(cb);
            return cb;
        };
        mkToggle(QStringLiteral("通知与弹窗"), AgentSettings::notifyEnabled(),
                 [](bool v) { AgentSettings::setNotifyEnabled(v); });
        mkToggle(QStringLiteral("语音播报（通知朗读）"), AgentSettings::voiceEnabled(),
                 [](bool v) { AgentSettings::setVoiceEnabled(v); });
        mkToggle(QStringLiteral("噪音监测"), AgentSettings::noiseEnabled(),
                 [](bool v) { AgentSettings::setNoiseEnabled(v); });
        mkToggle(QStringLiteral("灵动岛空闲常驻"), AgentSettings::islandIdle(),
                 [](bool v) {
                     AgentSettings::setIslandIdle(v);
                     if (IslandOverlay::instance()) IslandOverlay::instance()->setIdleEnabled(v);
                 });
        mkToggle(QStringLiteral("浅色主题"), AgentSettings::lightMode(),
                 [](bool v) {
                     AgentSettings::setLightMode(v);
                     if (IslandOverlay::instance()) IslandOverlay::instance()->setLightMode(v);
                 });
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
    btnPause->setToolTip(QStringLiteral("只在本机生效：写入 %1 后本机不再自动升级，"
                                        "别人照常。删掉该文件即恢复。")
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

/* ══════════════════════════════════════════════════════════════════════════
 * P0-2（2026-10-14）：配置自检 —— env 自动判断 + 一键修复
 * 四项检查：配置完整 / 云端已连接 / 已注册 / 班级已绑。失败项给「一键修复」按钮
 * （去配置 / 绑班 → 打开配置向导；未连 / 未注册 → 立即重连），不弹多层窗。
 * 首运行（未配置）由 main 自动调一次；托盘菜单「配置自检…」也走它。
 * ══════════════════════════════════════════════════════════════════════════ */
void openConfigDialog();   // 前向声明：本函数在下方定义，runSelfCheck 的「一键修复」会调它
void openPluginStore();     // 前向声明：P2 插件市场对话框
void openNoiseLogDialog();  // 前向声明：P3 噪音长期记录查看
void openStudyLogDialog();  // 前向声明：P3 自习模式长期记录查看
void openPlayScheduleDialog(); // 前向声明：P3 播放时段配置

// P3 前置声明：下方对话框 / lambda 在定义前用到这些静态辅助函数（定义在 executeAction 之前）
static QString dataRoot();
static void appendJsonl(const QString &subdir, const QJsonObject &o);
static void logNoiseSample(double db, bool active, int seq, bool study, bool ok);
static qint64 logStudyStart(double th, int need, int recSec);
static void logStudyStop(qint64 startTs, int alertCount);
static PluginHost *g_pluginHost = nullptr;   // P2：插件宿主（进程外插件 ABI）；定义在 openPluginStore 之前以便对话框使用
void runSelfCheck()
{
    static bool selfCheckOpen = false;     // 防重入
    if (selfCheckOpen) return;
    selfCheckOpen = true;

    QDialog dlg;
    dlg.setWindowTitle(QStringLiteral("星集控 · 配置自检"));
    dlg.setMinimumWidth(520);

    auto *root = new QVBoxLayout(&dlg);
    auto *intro = new QLabel(QStringLiteral(
        "自动检查本机接入状态。红色项表示需要处理后才能被管理端正常管控。"));
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto *list = new QVBoxLayout();
    root->addLayout(list);

    // 四项检查（同步读当前状态，便宜；重连态以 QWebSocket 实时状态为准）
    struct Check { QString name; bool ok; QString fix; };
    QVector<Check> checks;
    checks.append({ QStringLiteral("配置完整（云端地址 + 设备令牌）"),
                    g_configured, QStringLiteral("去配置 / 绑定") });
    const bool connected = g_ws && g_ws->state() == QAbstractSocket::ConnectedState;
    checks.append({ QStringLiteral("已连接云端"), connected, QStringLiteral("重试连接") });
    checks.append({ QStringLiteral("已注册（云端已确认）"), g_registered, QStringLiteral("重试连接") });
    checks.append({ QStringLiteral("班级已绑定（管理端按班可见）"),
                    !g_classCode.isEmpty(), QStringLiteral("绑定班级") });

    QVector<QPushButton *> fixBtns;
    for (const Check &c : checks) {
        auto *row = new QHBoxLayout();
        auto *mark = new QLabel(c.ok ? QStringLiteral("✅") : QStringLiteral("❌"));
        mark->setStyleSheet(c.ok ? QStringLiteral("color:#1a7f37;") : QStringLiteral("color:#c0392b;"));
        auto *name = new QLabel(c.name);
        row->addWidget(mark);
        row->addWidget(name, 1);
        if (!c.ok) {
            auto *fix = new QPushButton(c.fix);
            fixBtns.append(fix);
            row->addWidget(fix);
        }
        list->addLayout(row);
    }

    auto *btnRow = new QHBoxLayout();
    root->addLayout(btnRow);
    auto *recheck = new QPushButton(QStringLiteral("重新检测"));
    auto *close = new QPushButton(QStringLiteral("关闭"));
    btnRow->addWidget(recheck);
    btnRow->addStretch();
    btnRow->addWidget(close);

    // 一键修复：配置缺 / 班级缺 → 打开配置向导（OAuth 零手填即接入）；否则立即重连。
    auto doFix = [&]() {
        if (!g_configured || g_classCode.isEmpty()) {
            openConfigDialog();
        } else {
            g_backoffMs = 1000;
            if (g_ws && g_ws->state() != QAbstractSocket::UnconnectedState) {
                g_reconnectNow = true; g_ws->close();
            } else {
                updateTray(TrayState::Disconnected, QStringLiteral("正在重连…"));
                connectNow();
            }
        }
        dlg.accept();
    };
    for (QPushButton *b : fixBtns)
        QObject::connect(b, &QPushButton::clicked, &dlg, doFix);
    // 重新检测：先放开门禁再重开本窗（嵌套模态对话框，安全）
    QObject::connect(recheck, &QPushButton::clicked, &dlg, [&] {
        selfCheckOpen = false;
        dlg.accept();
        runSelfCheck();
    });
    QObject::connect(close, &QPushButton::clicked, &dlg, &QDialog::reject);

    dlg.exec();
    selfCheckOpen = false;
}

// ── P2：插件市场（第三方能力窗口）────────────────────────────────────────────
// 你 P0 反思里点名的「第三方能力窗口」：浏览云端插件商店、安装/启用/停用/卸载，
// 并内置「安装本地示例」演示闭环。安装 = 下载 zip → 解压到 plugins/<id>/ → 启用启动。
void openPluginStore()
{
    if (!g_pluginHost) { QMessageBox::information(nullptr, QStringLiteral("插件市场"),
                                                  QStringLiteral("插件宿主未启动")); return; }
    QDialog dlg;
    dlg.setWindowTitle(QStringLiteral("插件市场 · 第三方能力"));
    dlg.resize(680, 480);
    auto *list = new QListWidget(&dlg);
    auto *detail = new QLabel(&dlg);
    detail->setWordWrap(true); detail->setMinimumHeight(70); detail->setText(QStringLiteral("选择左侧插件查看说明"));
    auto *btnRefresh  = new QPushButton(QStringLiteral("刷新"), &dlg);
    auto *btnInstall  = new QPushButton(QStringLiteral("安装/启用选中"), &dlg);
    auto *btnDisable  = new QPushButton(QStringLiteral("停用选中"), &dlg);
    auto *btnUninstall= new QPushButton(QStringLiteral("卸载选中"), &dlg);
    auto *btnSample   = new QPushButton(QStringLiteral("安装本地示例(echo)"), &dlg);
    auto *btnClose    = new QPushButton(QStringLiteral("关闭"), &dlg);

    auto *nam = new QNetworkAccessManager(&dlg);
    QString registryUrl = qEnvironmentVariable("STE_PLUGIN_REGISTRY");
    if (registryUrl.isEmpty()) registryUrl = QStringLiteral("https://control.245959623.xyz/api/plugins");

    QHash<QString, QJsonObject> g_storeEntries;   // id -> 云端条目
    QHash<QString, bool> g_installed;

    auto render = [&]() {
        list->clear();
        const QJsonArray local = g_pluginHost->snapshot();
        QHash<QString, QJsonObject> localById;
        for (const QJsonValue &v : local) {
            QJsonObject o = v.toObject();
            localById.insert(o.value(QStringLiteral("id")).toString(), o);
        }
        QStringList ids;
        for (auto it = g_storeEntries.begin(); it != g_storeEntries.end(); ++it) ids << it.key();
        for (const QString &id : localById.keys()) if (!ids.contains(id)) ids << id;
        for (const QString &id : ids) {
            QJsonObject cloud = g_storeEntries.value(id);
            QJsonObject loc = localById.value(id);
            const bool inst = localById.contains(id);
            const bool en = loc.value(QStringLiteral("enabled")).toBool();
            const bool run = loc.value(QStringLiteral("running")).toBool();
            const QString name = cloud.value(QStringLiteral("name")).toString(loc.value(QStringLiteral("name")).toString(id));
            const QString ver = cloud.value(QStringLiteral("version")).toString(loc.value(QStringLiteral("version")).toString());
            const QString author = cloud.value(QStringLiteral("author")).toString();
            const QString status = inst ? (en ? (run ? QStringLiteral("● 运行中") : QStringLiteral("○ 已装未运行")) : QStringLiteral("× 已禁用")) : QStringLiteral("— 未安装");
            auto *item = new QListWidgetItem(
                QStringLiteral("[%1] %2 v%3 · %4 · %5").arg(id, name, ver, author, status));
            item->setData(Qt::UserRole, id);
            list->addItem(item);
        }
    };

    auto selectedId = [&]() -> QString {
        QListWidgetItem *it = list->currentItem();
        return it ? it->data(Qt::UserRole).toString() : QString();
    };

    auto fetchCloud = [&]() {
        QNetworkRequest req((QUrl(registryUrl)));
        QNetworkReply *rep = nam->get(req);
        QObject::connect(rep, &QNetworkReply::finished, &dlg, [rep, &g_storeEntries, &render]() {
            const QByteArray data = rep->readAll();
            rep->deleteLater();
            if (rep->error() != QNetworkReply::NoError) { render(); return; }   // 离线也能看本地已装
            const QJsonArray arr = QJsonDocument::fromJson(data).array();
            for (const QJsonValue &v : arr) {
                QJsonObject o = v.toObject();
                g_storeEntries.insert(o.value(QStringLiteral("id")).toString(), o);
            }
            render();
        });
    };

    QObject::connect(btnRefresh, &QPushButton::clicked, &dlg, [&]() { fetchCloud(); render(); });
    QObject::connect(btnInstall, &QPushButton::clicked, &dlg, [&]() {
        const QString id = selectedId(); if (id.isEmpty()) return;
        if (!g_installed.value(id)) {
            const QString url = g_storeEntries.value(id).value(QStringLiteral("downloadUrl")).toString();
            if (url.isEmpty()) { QMessageBox::warning(&dlg, QStringLiteral("插件市场"),
                QStringLiteral("该插件暂无可下载包（需先在商店后台上传）")); return; }
            QNetworkRequest req((QUrl(url)));
            QNetworkReply *rep = nam->get(req);
            QObject::connect(rep, &QNetworkReply::finished, &dlg, [rep, id, &dlg]() {
                const QByteArray data = rep->readAll();
                rep->deleteLater();
                if (rep->error() != QNetworkReply::NoError) { QMessageBox::warning(&dlg, QStringLiteral("插件市场"), QStringLiteral("下载失败")); return; }
                const QString tmp = QDir::tempPath() + QStringLiteral("/stelarith-plugin-%1.zip").arg(id);
                { QFile f(tmp); if (f.open(QIODevice::WriteOnly)) { f.write(data); f.close(); } }
                const QString dir = g_pluginHost->pluginDir() + QLatin1Char('/') + id;
                QDir().mkpath(dir);
                // #368 需求①：进程内 powershell 解压静默，挂 CREATE_NO_WINDOW
                {
                    QProcess p;
                    stelarithHideChildConsole(p);
                    p.start(QStringLiteral("powershell"),
                            QStringList() << QStringLiteral("-Command")
                                          << QStringLiteral("Expand-Archive -Force '%1' '%2'").arg(tmp, dir));
                    p.waitForFinished(-1);
                }
                g_pluginHost->installAndLaunch(id);
                QMessageBox::information(&dlg, QStringLiteral("插件市场"), QStringLiteral("已安装并启动：%1").arg(id));
            });
        } else {
            g_pluginHost->enable(id, true);
        }
        render();
    });
    QObject::connect(btnDisable, &QPushButton::clicked, &dlg, [&]() {
        const QString id = selectedId(); if (id.isEmpty()) return;
        g_pluginHost->enable(id, false); render();
    });
    QObject::connect(btnUninstall, &QPushButton::clicked, &dlg, [&]() {
        const QString id = selectedId(); if (id.isEmpty()) return;
        if (QMessageBox::question(&dlg, QStringLiteral("插件市场"), QStringLiteral("确认卸载 %1？").arg(id)) == QMessageBox::Yes) {
            g_pluginHost->uninstall(id); render();
        }
    });
    QObject::connect(btnSample, &QPushButton::clicked, &dlg, [&]() {
        const QString src = QCoreApplication::applicationDirPath() + QStringLiteral("/plugins/samples/echo");
        const QString dst = g_pluginHost->pluginDir() + QStringLiteral("/echo");
        if (!QFile::exists(src)) { QMessageBox::warning(&dlg, QStringLiteral("插件市场"), QStringLiteral("未找到内置示例（plugins/samples/echo）")); return; }
        QDir().mkpath(dst);
        for (const QString &f : QStringList() << QStringLiteral("manifest.json") << QStringLiteral("echo.py"))
            QFile::copy(src + QLatin1Char('/') + f, dst + QLatin1Char('/') + f);
        g_pluginHost->installAndLaunch(QStringLiteral("echo"));
        QMessageBox::information(&dlg, QStringLiteral("插件市场"),
            QStringLiteral("已安装示例插件 echo（需本机 python 且 pip install websocket-client）"));
        render();
    });
    QObject::connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(list, &QListWidget::currentItemChanged, [&](QListWidgetItem *cur, QListWidgetItem *) {
        Q_UNUSED(cur);
        const QString id = selectedId();
        if (id.isEmpty()) { detail->setText(QStringLiteral("选择左侧插件查看说明")); return; }
        QJsonObject e = g_storeEntries.value(id);
        detail->setText(QStringLiteral("<b>%1</b> v%2<br>作者：%3<br>%4")
            .arg(e.value(QStringLiteral("name")).toString(id), e.value(QStringLiteral("version")).toString(),
                 e.value(QStringLiteral("author")).toString(), e.value(QStringLiteral("description")).toString()));
    });

    auto *top = new QHBoxLayout(); top->addWidget(list, 1);
    auto *right = new QVBoxLayout(); right->addWidget(detail);
    auto *row = new QHBoxLayout();
    row->addWidget(btnRefresh); row->addWidget(btnInstall); row->addWidget(btnDisable);
    row->addWidget(btnUninstall); row->addWidget(btnSample); row->addWidget(btnClose);
    right->addLayout(row);
    top->addLayout(right, 1);
    auto *mainL = new QVBoxLayout(&dlg); mainL->addLayout(top, 1);
    dlg.setLayout(mainL);

    render();
    fetchCloud();
    dlg.exec();
}

// ── P3 长期记录 / 配置对话框 ───────────────────────────────────
// 读 subdir 下全部 YYYY-MM-DD.jsonl，逐行解析成数组（跨日汇总用）。
static QJsonArray readJsonlDir(const QString &subdir)
{
    QJsonArray arr;
    const QString dir = dataRoot() + QLatin1Char('/') + subdir;
    QDir d(dir);
    if (!d.exists()) return arr;
    for (const QFileInfo &fi : d.entryInfoList(QStringList() << QStringLiteral("*.jsonl"), QDir::Files, QDir::Name)) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        while (!f.atEnd()) {
            const QByteArray line = f.readLine().trimmed();
            if (line.isEmpty()) continue;
            const QJsonObject o = QJsonDocument::fromJson(line).object();
            if (!o.isEmpty()) arr.append(o);
        }
    }
    return arr;
}

static void openFolder(const QString &subdir)
{
    const QString dir = dataRoot() + QLatin1Char('/') + subdir;
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void openNoiseLogDialog()
{
    QDialog dlg;
    dlg.setWindowTitle(QStringLiteral("噪音记录"));
    dlg.resize(520, 420);
    auto *lay = new QVBoxLayout(&dlg);
    auto *te = new QTextEdit(&dlg);
    te->setReadOnly(true);
    lay->addWidget(te);

    const QJsonArray all = readJsonlDir(QStringLiteral("noise_log"));
    if (all.isEmpty()) {
        te->setPlainText(QStringLiteral("还没有噪音记录。\n开启「噪音监测」或「自习模式」后，每次采样都会记到这里。"));
    } else {
        QMap<QString, QVector<double>> byDay;
        for (const QJsonValue &v : all) {
            const QJsonObject o = v.toObject();
            const qint64 ts = o.value(QStringLiteral("ts")).toVariant().toLongLong();
            const QString day = QDateTime::fromSecsSinceEpoch(ts).toString(QStringLiteral("yyyy-MM-dd"));
            byDay[day].append(o.value(QStringLiteral("db")).toDouble());
        }
        QString out; QTextStream ts(&out);
        ts << QStringLiteral("共 %1 天、%2 条采样\n\n").arg(byDay.size()).arg(all.size());
        for (auto it = byDay.begin(); it != byDay.end(); ++it) {
            const QVector<double> &v = it.value();
            double sum = 0, mn = 1e9, mx = -1e9;
            for (double d : v) { sum += d; if (d < mn) mn = d; if (d > mx) mx = d; }
            ts << QStringLiteral("%1：采样 %2 次，均值 %3 dB，区间 [%4, %5] dB\n")
                  .arg(it.key()).arg(v.size())
                  .arg(QString::number(sum / v.size(), 'f', 1))
                  .arg(QString::number(mn, 'f', 1)).arg(QString::number(mx, 'f', 1));
        }
        te->setPlainText(out);
    }

    auto *row = new QHBoxLayout();
    auto *btnOpen = new QPushButton(QStringLiteral("打开记录文件夹"), &dlg);
    auto *btnClose = new QPushButton(QStringLiteral("关闭"), &dlg);
    row->addWidget(btnOpen); row->addStretch(1); row->addWidget(btnClose);
    lay->addLayout(row);
    QObject::connect(btnOpen, &QPushButton::clicked, &dlg, [] { openFolder(QStringLiteral("noise_log")); });
    QObject::connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();
}

void openStudyLogDialog()
{
    QDialog dlg;
    dlg.setWindowTitle(QStringLiteral("自习记录"));
    dlg.resize(520, 420);
    auto *lay = new QVBoxLayout(&dlg);
    auto *te = new QTextEdit(&dlg);
    te->setReadOnly(true);
    lay->addWidget(te);

    const QJsonArray all = readJsonlDir(QStringLiteral("study_log"));
    if (all.isEmpty()) {
        te->setPlainText(QStringLiteral("还没有自习记录。\n开启「自习模式」后，每次会话（开始/结束/时长/异常次数）都会记到这里。"));
    } else {
        QVector<QJsonObject> sessions;
        QMap<qint64, QJsonObject> pending;
        for (const QJsonValue &v : all) {
            QJsonObject o = v.toObject();
            const QString ev = o.value(QStringLiteral("event")).toString();
            if (ev == QStringLiteral("start")) {
                pending.insert(o.value(QStringLiteral("ts")).toVariant().toLongLong(), o);
            } else if (ev == QStringLiteral("stop")) {
                const qint64 st = o.value(QStringLiteral("startTs")).toVariant().toLongLong();
                QJsonObject s = pending.take(st);
                s.insert(QStringLiteral("endTs"), o.value(QStringLiteral("ts")));
                s.insert(QStringLiteral("durationSec"), o.value(QStringLiteral("durationSec")));
                s.insert(QStringLiteral("alertCount"), o.value(QStringLiteral("alertCount")));
                sessions.append(s);
            }
        }
        QString out; QTextStream ts(&out);
        ts << QStringLiteral("共 %1 次自习会话\n\n").arg(sessions.size());
        for (const QJsonObject &s : sessions) {
            const qint64 st = s.value(QStringLiteral("ts")).toVariant().toLongLong();
            const int dur = s.value(QStringLiteral("durationSec")).toInt();
            const int al = s.value(QStringLiteral("alertCount")).toInt();
            ts << QStringLiteral("%1：时长 %2 分 %3 秒，异常录制 %4 次（阈值 %5 dB）\n")
                  .arg(QDateTime::fromSecsSinceEpoch(st).toString(QStringLiteral("yyyy-MM-dd HH:mm")))
                  .arg(dur / 60).arg(dur % 60).arg(al)
                  .arg(QString::number(s.value(QStringLiteral("thresholdDb")).toDouble(), 'f', 0));
        }
        te->setPlainText(out);
    }

    auto *row = new QHBoxLayout();
    auto *btnOpen = new QPushButton(QStringLiteral("打开记录文件夹"), &dlg);
    auto *btnClose = new QPushButton(QStringLiteral("关闭"), &dlg);
    row->addWidget(btnOpen); row->addStretch(1); row->addWidget(btnClose);
    lay->addLayout(row);
    QObject::connect(btnOpen, &QPushButton::clicked, &dlg, [] { openFolder(QStringLiteral("study_log")); });
    QObject::connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();
}

void openPlayScheduleDialog()
{
    QDialog dlg;
    dlg.setWindowTitle(QStringLiteral("播放时段"));
    dlg.setMinimumWidth(460);
    auto *lay = new QVBoxLayout(&dlg);

    auto *en = new QCheckBox(QStringLiteral("启用播放时段限制（只在这些时段内允许广播/播放）"));
    en->setChecked(AgentSettings::playScheduleEnabled());
    lay->addWidget(en);

    lay->addWidget(new QLabel(QStringLiteral("允许的时段（每行一个，格式 HH:MM-HH:MM；可跨午夜如 22:00-07:00）：")));
    auto *ed = new QTextEdit(&dlg);
    ed->setPlainText(AgentSettings::playWindows().join(QStringLiteral("\n")));
    ed->setMaximumHeight(120);
    lay->addWidget(ed);

    auto *state = new QLabel();
    {
        QString cur = (IslandOverlay::instance() && IslandOverlay::instance()->mediaOn())
            ? QStringLiteral("正在播放：%1").arg(IslandOverlay::instance()->mediaTitle()) : QStringLiteral("当前无媒体在播");
        state->setText(QStringLiteral("当前状态：%1 · 时段内允许播放：%2")
                       .arg(cur).arg(MediaSession::isPlayAllowedNow() ? QStringLiteral("是") : QStringLiteral("否")));
    }
    lay->addWidget(state);

    auto *row = new QHBoxLayout();
    auto *btnSave = new QPushButton(QStringLiteral("保存"), &dlg);
    auto *btnCancel = new QPushButton(QStringLiteral("取消"), &dlg);
    auto *btnOpen = new QPushButton(QStringLiteral("打开数据目录"), &dlg);
    row->addWidget(btnSave); row->addWidget(btnCancel); row->addStretch(1); row->addWidget(btnOpen);
    lay->addLayout(row);

    QObject::connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(btnOpen, &QPushButton::clicked, &dlg, [] { openFolder(QStringLiteral("")); });
    QObject::connect(btnSave, &QPushButton::clicked, &dlg, [&] {
        AgentSettings::setPlayScheduleEnabled(en->isChecked());
        QStringList list;
        for (const QString &line : ed->toPlainText().split(QStringLiteral("\n"), Qt::SkipEmptyParts)) {
            const QString s = line.trimmed();
            if (!s.isEmpty()) list.append(s);
        }
        AgentSettings::setPlayWindows(list);
        qInfo("[play] 🎵 播放时段已保存：enabled=%d, windows=%d", en->isChecked(), list.size());
        dlg.accept();
    });
    dlg.exec();
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
        "这台设备还没有接入学校的集控系统。\n\n"
        "最简单的方式：点下面的「用星璃账号绑定」——浏览器里登录你的星璃账号、点同意，"
        "这台设备就自动接入，不用填任何东西。\n\n"
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
        "点了之后会打开浏览器，登录你的星璃账号、点同意，这台设备就自动接入集控。"));
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
            err->setText(QStringLiteral("绑定成功，但写配置失败（目录无写权限？）。请用管理员身份重装后再试。"));
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
    auto *sep = new QLabel(QStringLiteral("── 或手动填写（管理员给了地址和令牌时用）──"));
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
        "相当于这台设备接入集控的\"门禁卡\"，每台一串，由管理员发放。"
        "默认打码显示，勾「显示」可核对是否抄错。"));
    tokenHint->setWordWrap(true);
    tokenHint->setStyleSheet(hintStyle);
    form->addRow(QString(), tokenHint);

    // —— 设备名（可选）——
    auto *uidEdit = new QLineEdit(qEnvironmentVariable("STE_QT_UID").trimmed());
    uidEdit->setPlaceholderText(QHostInfo::localHostName());
    form->addRow(QStringLiteral("设备码（可选）"), uidEdit);
    auto *uidHint = new QLabel(QStringLiteral(
        "这台设备在集控里的唯一代号（用于按班归组等）。留空就用设备本名，一般不用改。"));
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
        "管理面板里看到的这台设备的名字（如「一班讲台机」）。留空就用设备本名。"));
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
            err->setText(QStringLiteral("请先填写「云端地址」。"));
            err->show();
            return;
        }
        const QUrl u(url);
        if (!u.isValid() || u.host().isEmpty()) {
            err->setStyleSheet(QStringLiteral("color:#c03030;"));
            err->setText(QStringLiteral("地址看起来不对，应形如 ws://主机:端口/ws/agent。"));
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
            err->setText(QStringLiteral("连接超时。请核对地址、网络与服务器状态。"));
            testBtn->setEnabled(true);
        });
        QObject::connect(probe, &QWebSocket::connected, probe, [&, probe, t, testBtn] {
            t->stop();
            err->setStyleSheet(QStringLiteral("color:#1a7f37;"));
            err->setText(QStringLiteral("地址可达。（能否接入还要看令牌，保存后看托盘图标。）"));
            testBtn->setEnabled(true);
            probe->close();
        });
        QObject::connect(probe, &QWebSocket::errorOccurred, probe,
                         [&, probe, t, testBtn](QAbstractSocket::SocketError) {
                             if (!t->isActive()) return;   // 已被 connected/timeout 分支处理
                             t->stop();
                             err->setStyleSheet(QStringLiteral("color:#c03030;"));
                             err->setText(QStringLiteral("连不上这个地址，请核对地址与服务器状态。"));
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
            err->setText(QStringLiteral("请先填写「云端地址」和「设备令牌」，缺一不可。"));
            err->show();
            return;   // 原地提示，不关窗、不弹二次窗口
        }
        if (!saveAgentEnv(url, token, uid, nameEdit ? nameEdit->text() : QString())) {
            err->setStyleSheet(QStringLiteral("color:#c03030;"));
            err->setText(QStringLiteral("写入配置失败（目录无写权限？）。请用管理员身份重装后再试。"));
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

// SAPI 兜底朗读（原 speakText 本体，2026-10-11 更名保留）。
// 现在由 speakText() 在 Edge TTS 不可用/失败时回调进来 —— 离线场景（无公网、
// 无 ffmpeg、语音服务不通）只有它还能出声，所以**必须保留且行为不变**。
static void speakTextSapi(const QString &text)
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

// ── TTS 统一入口（2026-10-11 · A5 接线 §6.4）────────────────────────────
// 需求 §6.4：通知朗读**优先 Edge 神经语音**（联网时音色自然），失败自动**回退 SAPI**（离线兜底）。
//
// 为什么要回退：Edge TTS 走公网 WebSocket（speech.platform.bing.com），教室机
//   · 无公网 / 弱网超时；· 没装 ffmpeg（mp3 解码链断掉）—— 这些都不是"崩"，是"读不出来"。
//   fail-silent 红线：绝不假装读过了，而是**如实降级到 SAPI**，让通知至少能听见。
//
// ⚠️ Edge 的失败是**异步**的（连不上/超时/解码失败要等回调），所以不能"先问能不能用、
//    再决定读哪条"（那个判断当场返回不了真话）。正确做法：**先让 Edge 读**，
//    同时把它的 failed 接一根线 —— 真失败时再补一次 SAPI，且只补一次。
// ⚠️ 去重护栏：failed 可能来自**上一条**朗读（Edge 单例，新的一条会打断旧的，
//    被顶掉的那条也会 failed）。若不做判据，通知 A 的失败会把正在由通知 B 朗读的
//    内容再念一遍。判据：发起朗读时把"本条 token"记进 g_ttsCurToken；
//    failed 回调里若 g_ttsCurToken 已被更新（说明有更新的一条顶上），就**不**回退。
static quint64 g_ttsSeq = 0;            // speakText 调用计数
static quint64 g_ttsCurToken = 0;       // 当前"正在读"的那条的 token（0=没有）
static QString g_ttsCurText;            // 当前"正在读"的文本（回退 SAPI 用）
static bool g_ttsFallbackWired = false;

// ── 2026-10-11 · 语音播报音量序列（用户点单）───────────────────────────────
// 需求：开启语音播报发布通知时，**先**把音量调整到 75%，**等 TTS 播报完成后**恢复 50%。
// 为什么：教室机平时主音量 50%，直接播报在嘈杂环境听不清；播完必须收回，否则下一节
// 课件/视频会突然很响。抬/收都走 setSystemMasterVolume（Core Audio 主音量），与
// set_volume 指令同一条路径 —— 改的是**系统主音量**，不是本进程音量（否则白改）。
static const int kTtsVolSpeak   = 75;   // 播报期间抬到
static const int kTtsVolRestore = 50;   // 播报完成后恢复

// 播报结束（Edge 成功 finished / 回退 SAPI 读完）把主音量收回 50。
// tok 用来判「这条还是不是当前那条」：被更新的一条顶掉之后，旧条的回调不再收音量，
// 否则会把正在播的新一条提前掐回 50。
static void ttsRestoreVolume(quint64 tok)
{
    if (tok != 0 && tok != g_ttsSeq) return;
    QString verr;
    if (!setSystemMasterVolume(kTtsVolRestore, &verr))
        qWarning().noquote() << QStringLiteral("[agent-qt] TTS 播完恢复主音量 %1% 失败：%2")
                                    .arg(kTtsVolRestore).arg(verr);
}

static bool g_ttsFinishedWired = false;

static void speakText(const QString &text)
{
    // P0-1（2026-10-14）：本地「语音播报」开关关了就不朗读——通知/循环提醒照常显示，只是不念。
    if (!AgentSettings::voiceEnabled()) return;
    const QString say = text.trimmed();
    if (say.isEmpty()) return;

    EdgeTts &edge = EdgeTts::instance();

    // 只接一次：failed → 若仍是"当前这条"，回退 SAPI
    if (!g_ttsFallbackWired) {
        g_ttsFallbackWired = true;
        QObject::connect(&edge, &EdgeTts::failed, qApp, [](const QString &reason) {
            qWarning().noquote() << QStringLiteral("[agent-qt] Edge TTS 不可用（%1），回退 SAPI 朗读").arg(reason);
            // 被更新的一条顶掉了（token 已换）⇒ 这条 failed 是"过期"的，不再出声，
            // 否则会把正在由新一条朗读的内容重复念一遍。
            if (g_ttsCurToken == 0 || g_ttsCurToken != g_ttsSeq) return;
            const quint64 tok = g_ttsCurToken;
            const QString fallback = g_ttsCurText;
            g_ttsCurToken = 0;   // 只回退一次
            speakTextSapi(fallback);        // 同步阻塞：读完才返回
            ttsRestoreVolume(tok);          // 回退路也走「播完恢复 50」
        });
    }

    // 只接一次：finished → 播报正常结束，恢复主音量 50
    if (!g_ttsFinishedWired) {
        g_ttsFinishedWired = true;
        QObject::connect(&edge, &EdgeTts::finished, qApp, [](const QString &text) {
            // 念完的已经不是"当前这条"（中间又来了新通知）⇒ 不收，交给新那条收
            if (text != g_ttsCurText) return;
            ttsRestoreVolume(g_ttsSeq);
        });
    }

    // 记下"当前这条"：token = 本次调用号
    const quint64 tok = ++g_ttsSeq;
    g_ttsCurToken = tok;
    g_ttsCurText  = say;

    // ── 2026-10-21（审查 #4）：音量超时兜底 ──────────────────────────────
    // Edge TTS 走公网 WS，弱网/挂起时 finished/failed 两个信号都可能永远不来 ⇒
    // 系统主音量永久停在 75%（下一节课件/视频全被这个残留音量影响，没人会把它和
    // "两小时前一条通知"联系起来）。单次 QTimer：60s 后仍没收到收尾信号就强制恢复
    // 50。ttsRestoreVolume 内部有 token 校验 —— 被新一条顶掉时拒绝收，由新一条负责。
    QTimer::singleShot(60 * 1000, qApp, [tok] { ttsRestoreVolume(tok); });

    // ── 播报前先把系统主音量抬到 75%（需求：先调音量，再播）──
    {
        QString verr;
        if (!setSystemMasterVolume(kTtsVolSpeak, &verr))
            qWarning().noquote() << QStringLiteral("[agent-qt] TTS 播报前抬主音量 %1% 失败：%2")
                                        .arg(kTtsVolSpeak).arg(verr);
    }

    // 先让 Edge 读；它会异步报成功(finished)或失败(failed)
    edge.speak(say);
}

// ── 灵动岛统一入口（2026-10-07 · #98）──────────────────────────────────
// 用户 2026-10-07 定下："被控端一切通知都经过灵动岛显示，弹窗则居中，全屏则放大全屏，
// 要有过渡。"
// 被控端所有"轻量提示"统一走 IslandOverlay（灵动岛）：
//   · 顶部居中胶囊、Collapsed(56)↔Expanded(168) 弹性缓动、出现/消失都带动画；
//   · 默认鼠标穿透（WA_TransparentForMouseEvents）—— 不挡后面的课件/白板；
//   · 需要"用户点一下"的（如收到文件想看清楚 / 通知确认）走 showWithActions + setInteractive(true)。
// form（#98）：Capsule=顶部胶囊（默认）；Centered=屏幕正中卡片；Fullscreen=铺满整屏。
// 调用方按"这条内容要不要停下来看清"来选，别什么都往全屏上堆。
// 注：2026-10-14 P1 起 NotifyWindow 已删除，灵动岛是被控端唯一展示入口。
static void islandShow(const QString &title, const QString &desc = QString(),
                       const QString &icon = QString(), int ms = 5000, bool interactive = false,
                       const QString &openPath = QString(),
                       IslandOverlay::Form form = IslandOverlay::Form::Capsule,
                       bool fromAdmin = false)   // 2026-10-13：管理端下发（历史置顶用）
{
    // ── 2026-10-19 远控期崩溃规避（P0 生产事故）──────────────────────────
    // 真机三份 WER 全内存 dump 实证：远程控制开始后约 4-26s，agent 进程 0xC0000005，
    // 出错模块 Qt6Core.dll+0x38c0（QObject::d_func，rcx=0）。栈是纯 QML 路径：
    //   QObject::d_func ← QQmlDelegateModelItem::destroyObjectLater
    //   ← QQmlDelegateModelPrivate::destroyCacheItem ← QQmlDelegateModel::release
    //   ← QQuickRepeater::clear ← QQuickRepeater::setModel ← QQmlBinding 重算
    //   ← QQmlData::destroyed ← QObject::~QObject
    // 即：某对象析构 → QML 绑定重算 → 某个 Repeater 的 model 被整表替换 →
    //     Repeater 清空 delegate 时，缓存里已有"object 已销毁"的 delegate ⇒ 对 null 调
    //     d_func。岛（island）是唯一每帧都在更新 model 的 QML 视图，showIsland 每次都会
    //     upsertTask（任务表整表替换）+ 8s 后 dismiss→enterIdle→archiveToHistory，
    //     正好是这条链的入口。GDI 时代的两份 dump（10-18 22:56 / 23:29）崩溃点完全相同
    //     ⇒ 与 DXGI 抓屏无关，是岛 QML 的既有结构性缺陷，远控只是把它踩出来。
    // 处置：远控会话期间岛一律不写（改走托盘气泡，学生知情边界不破），
    //       待岛 QML 侧换成 ListModel 增量更新（Repeater → ListModel 单批改造）后再放开。
    if (g_remoteControl) {
        if (g_tray)
            g_tray->showMessage(title, desc.isEmpty() ? title : desc,
                                QSystemTrayIcon::Information, ms > 0 ? ms : 5000);
        return;
    }
    IslandOverlay *isl = IslandOverlay::instance();
    if (openPath.isEmpty()) isl->clearOpenPath();
    else                    isl->setOpenPath(openPath);
    // 交互态（可点）清掉旧的 openPath，免得下一条提示点一下去翻上一份文件。
    isl->setInteractive(interactive);
    isl->showIsland(title, desc, icon, ms, form, fromAdmin);
}

// 同上的「带发生时刻」版本（P0-0 · 2026-10-17 插件边界）：插件推的通知在它**调用那一刻**
// 就该有 ts，而不是 agent 处理那一刻（忙时可能差几百毫秒，跨整分钟就会让历史显示错 1 分钟）。
// ⚠️ 复用 IslandOverlay 的「下一条通知发生时刻」一次性注入位（m_noticeHintMs，setNoticeTimeForNext）：
//    showIsland 会读它并清零——所以必须紧贴着每次 showIsland 设置，不能提前设。
static void islandShowAt(const QString &title, const QString &desc, const QString &icon,
                         int ms, IslandOverlay::Form form, bool fromAdmin, qint64 noticeAtMs)
{
    IslandOverlay::instance()->setNoticeTimeForNext(noticeAtMs);
    islandShow(title, desc, icon, ms, false, QString(), form, fromAdmin);
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
    // 2026-10-21（审查 #7）：返回 "skipped:" 前缀 = 门控静默丢弃。调用方识别该前缀
    //   转 **skipped** 回执（云端已有该语义，重复下发保护就在用）—— 原来返回普通
    //   错误串被当成 failed，管理端会看到一条失败并可能重试/报警，和"像没收到
    //   一样"正好相反。
    const bool gated = !g_notifGate.allow(
        kind == QLatin1String("danmaku") ? NotificationGate::Danmaku
        : kind == QLatin1String("loop") ? NotificationGate::LoopRemind
        : NotificationGate::Island);
    if (gated) return QStringLiteral("skipped:exam-active-gated");
    // P0-1（2026-10-14）：本地「通知与弹窗」开关关了就整体静默（考试门控优先，已在上方处理）。
    if (!AgentSettings::notifyEnabled()) return QStringLiteral("notify-disabled-by-local-setting");
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

    // 形态分流（P1 · 2026-10-14）：kind 直接映射灵动岛形态，**不再经 NotifyWindow**
    // （岛已覆盖全屏/居中/胶囊/确认/紧急，是被控端唯一展示入口）。
    //   popup / 未知 → Centered（屏幕正中卡片，安全默认）
    //   fullscreen → Fullscreen（铺满整屏） ｜ island → Capsule（顶部胶囊）
    //   2026-10-17（#330 形态多样化）：banner → 顶部通栏（长文案单行横幅）
    //                                   mini   → 右下角角标（常驻轻状态，不挡课件）
    // ⚠️ 未知 kind 必须继续落 Centered —— 管理端老版本发的 kind 不能被新枚举带偏。
    IslandOverlay::Form f = IslandOverlay::Form::Centered;
    if (kind == QLatin1String("island"))          f = IslandOverlay::Form::Capsule;
    else if (kind == QLatin1String("fullscreen")) f = IslandOverlay::Form::Fullscreen;
    else if (kind == QLatin1String("banner"))     f = IslandOverlay::Form::Banner;
    else if (kind == QLatin1String("mini"))       f = IslandOverlay::Form::Mini;

    // ── 弹幕（2026-10-09 接线 · §1.2/§6.1）：kind=danmaku → 字幕式横向滚动 ──
    if (kind == QLatin1String("danmaku")) {
        // 2026-10-15 组件5：g_danmaku 全局指针 → 单例访问器（与学生祝福弹幕同层）
        DanmakuWidget::danmakuWidget()->push(content.isEmpty() ? title : (title + QStringLiteral("：") + content));
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
                                                       loopConfirm, 0, true);
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
    // ⚠️ 需要交互的（要确认 / 有快捷回复 / 紧急"点一下才关"）走灵动岛的 showWithActions：
    //    岛本体自带按钮行（2026-10-09 起「确认」已并入岛），不再依赖 NotifyWindow。
    // ── 时长：>0 用给定秒数；0/缺省 → 按字数自适应（约 5 秒起）────────────────────
    // ⚠️ 2026-10-09 修 bug：原来直接把 seconds*1000 传给灵动岛，而灵动岛按
    //    "durationMs>0 才自动收"判定 ⇒ seconds=0 时永远不消失、一直置顶。
    //    这里把"0=自适应"的契约在**传参前**兑现，公式复刻 NotifyWindow 那份
    //    （短 5s、长内容按字数抬升），避免两处口径漂移。
    const int durMs = seconds > 0
        ? qMin(seconds, 3600) * 1000
        : int(qMax(5000.0, 2500.0 + (title + QStringLiteral("：") + content).length() * 120.0));

    // ── 形态分流：**一律经灵动岛**（P1 · 2026-10-14 后 NotifyWindow 已删除，岛为唯一入口）──
    //   popup → Centered（正中卡片）｜island → Capsule（顶部胶囊）｜fullscreen → Fullscreen

    // ⚠️ 2026-10-09 修 bug「全屏通知没有颜色」：配色档必须**无条件先设**。
    //    原来只在"确认/紧急"那条 else 分支里调 setSeverity ⇒ **纯展示的全屏通知**
    //    （kind=fullscreen 且不需要确认）压根没设 severity，全屏永远是默认黑底，
    //    remind/inform/urgent 三档配色全被吞掉。而且不设还会残留上一条的档。
    //    （severity 只对 Fullscreen 生效，Centered/Capsule 走各自的固定配色，不受影响。）
    IslandOverlay::instance()->setSeverity(severity);

    const bool needsInteraction = needConfirm || emergency;
    if (!needsInteraction) {
        islandShow(title, content, QStringLiteral("bell"), durMs, false, QString(), f, true);
    } else {
        // 确认 / 紧急：按钮行（「确认」+ 快捷回复），点哪枚回哪句。
        // ⚠️ 胶囊那档太矮放不下按钮 ⇒ 升到 Centered（全屏仍是 Fullscreen）。
        if (f == IslandOverlay::Form::Capsule) f = IslandOverlay::Form::Centered;
        QStringList acts;
        acts << QStringLiteral("确认");
        for (const QString &rr : replies)
            if (!rr.trimmed().isEmpty() && !acts.contains(rr)) acts << rr;
        IslandOverlay::instance()->showWithActions(title, content, QStringLiteral("bell"),
                                                   f, acts, onConfirm, 0, true);
    }

    // TTS（调用方要求时）：读「标题：正文」，与岛渲染口径一致
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

// ── 周期性 OTA 复查（2026-10-17 · 修「云端发了版，设备不升级」）────────────
// 病灶：云端把最新版本**只随 registered 回执下发一次**（cloud-ws index.js 的 register 分支），
// 而 applyUpdateInfo() 也只在那里被调用。教室机是"一年不重启"的受管终端 ⇒ 云端发了新版本，
// 它永远不知道（日志里一直是"已是最新"），除非碰巧断线重连。这就是"OTA 不生效"。
//
// 补法：自己按 HTTP 轮询公开清单 GET {http基址}/api/public/ota?product=agent
//       &channel=<渠道>&current=<本机版本>，把结果**翻译成与注册回执同形**的 payload
//       交给 applyUpdateInfo —— 版本比较、托盘/岛提示、强制升级判定全部复用同一条路径。
// ⚠️ 绝不在这里另写一套版本比较：云端 ota.js 的注释已经点明，"两端各写一遍比较"
//    正是 0.6.23 之前"正式版被判成已是最新"那个静默 bug 的来源。
// 参数：STE_QT_OTA_CHECK_SEC 调间隔（默认 1800s，钳 [60, 86400]）；
//       STE_QT_OTA_DISABLE=1 与自更新共用同一把应急刹车。
static void otaSelfCheck()
{
    if (qEnvironmentVariable("STE_QT_OTA_DISABLE") == QLatin1String("1")) return;
    if (otaIsBusy()) return;                    // 正在下载/安装，别插队
    const QString wsUrl = qEnvironmentVariable("STE_QT_WS_URL").trimmed();
    if (wsUrl.isEmpty()) return;                // 没配云端地址：无从查起（注册回执那条路也走不通）

    // M3（2026-10-20）：每轮复查先做一次健康快照比对。它是"外部变更"的唯一感知途径：
    //   · 管理员放下/拿走 ota-pause（maybeAutoUpdate 不一定会被调到）；
    //   · 状态文件被手工改过（排障时常见）；
    //   · 开机以来的基线快照（本进程第一次上报，让云端知道这台机器的 OTA 子系统活着、
    //     在哪个 channel、当前有没有欠账）。
    // 全程只读本地状态文件 + 一个文件存在性判断，不发网络请求；**没变就静默**。
    otaReportHealth(QStringLiteral("periodic"));

    // ws(s)://host[:port]/ws/agent → http(s)://host[:port]（与 uploadRecordingAsync 同款推导）
    QString base = wsUrl;
    base.replace(QLatin1String("wss://"), QLatin1String("https://"))
        .replace(QLatin1String("ws://"), QLatin1String("http://"));
    const int wsIdx = base.indexOf(QLatin1String("/ws/"));
    if (wsIdx > 0) base.truncate(wsIdx);

    QUrl u(base + QStringLiteral("/api/public/ota"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("product"), QStringLiteral("agent"));
    q.addQueryItem(QStringLiteral("channel"), g_channel);
    q.addQueryItem(QStringLiteral("current"), QString::fromLatin1(kAppVersion));
    u.setQuery(q);

    auto *nam = new QNetworkAccessManager(qApp);
    QNetworkRequest req(u);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("StelarithAgentQt/%1").arg(kAppVersion));

    QNetworkReply *r = nam->get(req);
    QObject::connect(r, &QNetworkReply::finished, r, [r, nam, base] {
        const QByteArray body = r->readAll();
        const QNetworkReply::NetworkError err = r->error();
        const QString errStr = r->errorString();
        r->deleteLater();
        nam->deleteLater();
        if (err != QNetworkReply::NoError) {
            // 复查失败**不打扰师生**（网络抖动很常见），但要留痕 —— 否则"静默失败"没法诊断
            qInfo("[agent-qt] OTA 复查失败（%s）：%s", qPrintable(base), qPrintable(errStr));
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        const QJsonObject latest = o.value(QStringLiteral("latest")).toObject();
        if (latest.isEmpty()) {
            qInfo("[agent-qt] OTA 复查：云端未发布该渠道版本（channel=%s）", qPrintable(g_channel));
            return;
        }
        // 键名逐字对齐 applyUpdateInfo 的读法（latestVersion/updateUrl/updateSha256/…）
        QJsonObject pay;
        pay.insert(QStringLiteral("latestVersion"), latest.value(QStringLiteral("version")));
        pay.insert(QStringLiteral("updateUrl"), latest.value(QStringLiteral("url")));
        pay.insert(QStringLiteral("updateSha256"), latest.value(QStringLiteral("sha256")));
        pay.insert(QStringLiteral("updateNotes"), latest.value(QStringLiteral("notes")));
        pay.insert(QStringLiteral("updateMandatory"), latest.value(QStringLiteral("mandatory")));
        applyUpdateInfo(pay);
    });
}

// 起定时器：默认 30 分钟一次，首次 3 分钟后（注册回执里那次已算一次检查，不必立刻重来）。
// STE_QT_OTA_CHECK_FIRST_SEC 可把首次提前（验证用；生产默认 180s，别在启动高峰抢带宽）。
static void otaStartPeriodicCheck()
{
    int sec = qEnvironmentVariableIntValue("STE_QT_OTA_CHECK_SEC");
    if (sec <= 0) sec = 1800;
    sec = qBound(60, sec, 24 * 3600);
    int firstSec = qEnvironmentVariableIntValue("STE_QT_OTA_CHECK_FIRST_SEC");
    if (firstSec <= 0) firstSec = 180;
    firstSec = qBound(5, firstSec, 24 * 3600);
    auto *t = new QTimer(qApp);
    t->setInterval(sec * 1000);
    QObject::connect(t, &QTimer::timeout, qApp, [] { otaSelfCheck(); });
    t->start();
    QTimer::singleShot(firstSec * 1000, qApp, [] { otaSelfCheck(); });
    qInfo("[agent-qt] OTA 周期复查已启动：每 %d 秒一次、首次 %d 秒后（channel=%s；"
          "STE_QT_OTA_CHECK_SEC / STE_QT_OTA_CHECK_FIRST_SEC 可调）",
          sec, firstSec, qPrintable(g_channel));
}


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

// A6（2026-10-11）：**周期遥测**上报（噪音采样这类"没有指令 id、要持续吐数"的场景）。
// ⚠️ 为什么不走 receipt：云端的 recordResult 是**按指令 id 落表**的（registry.js:636），
//    遥测没有 id，硬塞 receipt 会在云端 results 表里塞一堆 id=undefined 的脏记录。
//    云端对 type:"event" 已有处理（index.js:1284 → pushEvent 记进事件流水），
//    所以遥测走 event，语义干净、也不污染指令结果表。
static void sendEvent(const QString &level, const QString &msg, const QJsonObject &extra = QJsonObject())
{
    if (!g_ws || g_ws->state() != QAbstractSocket::ConnectedState) return;   // 没连就不发（遥测丢一拍无所谓）
    QJsonObject p;
    p.insert(QStringLiteral("level"), level);
    p.insert(QStringLiteral("msg"), msg);
    if (!extra.isEmpty()) p.insert(QStringLiteral("extra"), extra);
    g_ws->sendTextMessage(makeEnvelope(QStringLiteral("event"), p));
}

/* ══════════════════════════════════════════════════════════════════════════
 * OTA 健康快照上报（M3，2026-10-20）
 *
 * 病灶：被控端 OTA 是"无人值守自更新"—— 失败退避、暂停刹车、回滚三件套全都只在
 *   **本机日志**里闭环。云端/管理端看不到一台机器"已经连续失败 N 次、下次几点再试、
 *   是否被管理员按下暂停"。表现：OTA 悄悄坏了对云端是静默的，只能等老师打电话。
 *   （对照：管理端批次2 刚补上"周期自动检查 + 新版本气泡"，被控端这边却没有对应回咀。）
 *
 * 补法：OTA 状态每次**变化**时经 sendEvent 上报一份健康快照。走 type:"event" 旁路
 *   （与 A6 噪音遥测同一条路）：云端 index.js:2147 收下后 pushEvent 记进事件流水，
 *   不污染按指令 id 落表的 results 表。
 *
 * ⚠️ 只在**变化**时上报（s_last 进程内比对）：
 *   · 退避 ladder 每升一级快照必然变（failCount/nextAt）⇒ 每级正好一条，不重不漏；
 *   · 周期复查 30 分钟一轮，状态不变时**一条都不发** —— 否则 events.log 会被
 *     "每分钟一条没事"的遥测淹掉（2026-10-15 逐帧事件把事件表写爆的前车之鉴）；
 *   · 进程重启后 s_last 清空 ⇒ 首轮必发一条基线（failCount=0 也算"变化"），
 *     顺便让云端看见这台机器当前的 channel 与是否带欠账上线。
 *
 * 调用点（四个）：otaNoteFailure（失败记账）/ otaNoteSuccess（升级确认成功）/
 *   maybeAutoUpdate 的暂停分支（管理员放下刹车）/ otaSelfCheck 每轮（外部变更感知 + 基线）。
 * ══════════════════════════════════════════════════════════════════════════ */
static void otaReportHealth(const QString &why)
{
    const QJsonObject st = otaLoadState();
    QJsonObject snap;
    snap.insert(QStringLiteral("kind"), QStringLiteral("ota_health"));
    snap.insert(QStringLiteral("curVer"), QString::fromLatin1(kAppVersion));
    snap.insert(QStringLiteral("latestVer"), g_latestVer);
    snap.insert(QStringLiteral("failCount"), st.value(QStringLiteral("failCount")).toInt(0));
    snap.insert(QStringLiteral("lastResult"), st.value(QStringLiteral("lastResult")).toString());
    snap.insert(QStringLiteral("lastError"), st.value(QStringLiteral("lastError")).toString());
    snap.insert(QStringLiteral("lastAt"), st.value(QStringLiteral("lastAt")).toDouble());
    snap.insert(QStringLiteral("nextAt"), st.value(QStringLiteral("nextAt")).toDouble());
    snap.insert(QStringLiteral("paused"), otaPaused());
    snap.insert(QStringLiteral("channel"), g_channel);

    static QJsonObject s_last;   // 上一份上报过的快照（进程内；重启后首次必然发一条）
    // ⚠️ lastAt **不参与比对**：otaNoteSuccess 每次注册回执都会把它刷成 now ⇒ 它一进比对，
    //    机器每重连一次就上报一条"什么都没变"的 success（实测抓到的设计缺陷，2026-10-20）——
    //    "变化才上报"会被一个时间戳字段架空。它只作为载荷里的参考信息发出去。
    QJsonObject cmp = snap;
    cmp.remove(QStringLiteral("lastAt"));
    if (cmp == s_last) {
        qInfo("[agent-qt] OTA 健康快照未变化（%s），不上报", qPrintable(why));
        return;
    }
    s_last = cmp;

    // 有失败欠账或被管理员暂停 = 需要人看一眼 ⇒ warn；其余（含成功/基线）info。
    const bool trouble = snap.value(QStringLiteral("failCount")).toInt() > 0
                        || snap.value(QStringLiteral("paused")).toBool();
    sendEvent(trouble ? QStringLiteral("warn") : QStringLiteral("info"),
              QStringLiteral("ota_health"), snap);
    qInfo("[agent-qt] 📊 OTA 健康已上报（%s）：failCount=%d lastResult=%s lastError=%s paused=%s channel=%s",
          qPrintable(why),
          snap.value(QStringLiteral("failCount")).toInt(),
          qPrintable(snap.value(QStringLiteral("lastResult")).toString()),
          qPrintable(snap.value(QStringLiteral("lastError")).toString()),
          snap.value(QStringLiteral("paused")).toBool() ? "yes" : "no",
          qPrintable(snap.value(QStringLiteral("channel")).toString()));
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
    // 护栏④b（2026-10-13 登记）：文件名白名单。QUrl::fileName() 挡得住 '/'，但挡不住 '\'
    //   —— Windows 上 QFile 把 '\' 也当路径分隔符，`a\..\..\evil.exe` 就能逃出 ota 目录
    //   （url 里塞反斜杠即可任意写盘 = 云端被攻破后升级通道变任意文件写入）。
    //   ':' 会命中 NTFS ADS（x.exe:stream 同样能写到别处），`.`/`..` 是目录自身/父目录。
    //   一律收紧成 [A-Za-z0-9._-]{1,80}，不合白名单就换默认名 —— 下载与 sha256 都不受影响。
    static const QRegularExpression otaSafeName(QStringLiteral("^[A-Za-z0-9._-]{1,80}$"));
    if (base == QStringLiteral(".") || base == QStringLiteral("..")
        || !otaSafeName.match(base).hasMatch()) {
        qWarning().noquote() << QStringLiteral("[agent-qt] OTA url 文件名不进白名单，改用默认名：%1").arg(base);
        base = QStringLiteral("stelarith-agent-update.exe");
    }
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
        // #368 需求①：静默 detached，杜绝重启助手弹出 cmd 黑窗
        stelarithStartHiddenDetached(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), helper});

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



// ── 自习模式（2026-10-13）：噪音检测 + 噪音异常自动录制 ──────────────────────
// 定位：**静默监控**，不干扰学生（区别于考试模式的全屏拦截）。
//   · 进模式：周期测教室噪音（noise_monitor 短窗口：开→录→关，不常占麦、不驻留录音）
//     → 每拍把 dB 实时回传管理端（type:noise_sample，走订阅中继，不进事件流水）。
//   · 判异常：dB **连续 N 拍**都高于阈值才算（一次瞬时爆音不算，防误报）。
//   · 触发：自动录一段摄像头（recordSec 秒）→ 停 → 上传云端 → 上报 noise_alert。
static bool    g_studyOn = false;
static double  g_studyThreshDb = -45.0;    // 阈值（dB）
static int     g_studyStreakNeed = 3;      // 连续超阈拍数
static int     g_studyRecordSec = 30;      // 触发后录多久（秒）
static int     g_studyStreak = 0;          // 当前连续超阈计数
static int     g_studyAlertCount = 0;      // 本次自习累计触发次数
static qint64   g_studyStartTs = 0;         // P3：本次自习开始时间（长期记录算时长用）
static QTimer *g_studyRecTimer = nullptr;  // 自动录制到点停

static void sendNoiseSample(const QJsonObject &p)
{
    if (!g_ws || g_ws->state() != QAbstractSocket::ConnectedState) return;
    g_ws->sendTextMessage(makeEnvelope(QStringLiteral("noise_sample"), p));
    if (g_pluginHost) g_pluginHost->forwardEvent(QStringLiteral("noise_sample"), p);   // P2：转发给订阅插件
    ModuleHost::instance()->forwardEvent(QStringLiteral("noise_sample"), p);           // 内置模块：同事件同口径
}
static void sendNoiseAlert(const QJsonObject &p)
{
    if (!g_ws || g_ws->state() != QAbstractSocket::ConnectedState) return;
    g_ws->sendTextMessage(makeEnvelope(QStringLiteral("noise_alert"), p));
}

static void studyOnSample(double db);      // 前向
static void studyTriggerRecord();          // 前向

// 懒建噪音采集器：noise_monitor_start 与 study_mode_start 共用（避免两处各 new 一份，
// 两份实例会同时抢麦且各报各的）。采集在 noise_monitor 内部异步做，不阻塞主线程。
static void ensureNoise()
{
    if (g_noise) return;
    g_noise = new NoiseMonitor(nullptr);   // 无 parent，单例式驻留（进程退出随 Qt 清理）
    QObject::connect(g_noise, &NoiseMonitor::noiseSample, g_ws, [](double db, bool active, int seq) {
        QJsonObject p;
        p.insert(QStringLiteral("db"), db);
        p.insert(QStringLiteral("active"), active);
        p.insert(QStringLiteral("seq"), seq);
        p.insert(QStringLiteral("study"), g_studyOn);
        p.insert(QStringLiteral("captureOk"), g_noise ? g_noise->captureAvailable() : false);
        if (g_studyOn) p.insert(QStringLiteral("thresholdDb"), g_studyThreshDb);
        sendNoiseSample(p);                 // 实时回传管理端（订阅中继）
        logNoiseSample(db, active, seq, g_studyOn, g_noise ? g_noise->captureAvailable() : false);  // P3：长期记录
        if (g_studyOn) studyOnSample(db);   // 自习模式：超阈判定
    });
    QObject::connect(g_noise, &NoiseMonitor::failed, g_ws, [](const QString &reason) {
        qWarning().noquote() << QStringLiteral("[agent-qt] 噪音采集失败：%1").arg(reason);
        sendEvent(QStringLiteral("warn"), QStringLiteral("noise_monitor_failed"),
                  QJsonObject{ { QStringLiteral("reason"), reason } });
    });
}

static void studyOnSample(double db)
{
    if (db > g_studyThreshDb) {
        ++g_studyStreak;
        if (g_studyStreak >= g_studyStreakNeed) {
            g_studyStreak = 0;   // 触发后复位：持续噪声不会每拍都再触发（下轮要重新攒）
            studyTriggerRecord();
        }
    } else {
        g_studyStreak = 0;       // 一安静就断链，必需"连续"才算异常
    }
}

static void studyTriggerRecord()
{
    QString dev = MediaTools::boundCamera();
    if (dev.isEmpty()) dev = QStringLiteral("video0");
    const QString err = MediaSession::beginRecording(dev, g_studyRecordSec);
    if (!err.isEmpty()) {
        // 起不来自动录制（如手动录制占着 / 设备被占）：如实上报，不静默吞
        sendEvent(QStringLiteral("warn"), QStringLiteral("noise_alert_record_failed"),
                  QJsonObject{ { QStringLiteral("reason"), err }, { QStringLiteral("device"), dev } });
        return;
    }
    ++g_studyAlertCount;
    {
        QJsonObject e;
        e.insert(QStringLiteral("event"), QStringLiteral("record_start"));
        e.insert(QStringLiteral("device"), dev);
        e.insert(QStringLiteral("seconds"), g_studyRecordSec);
        e.insert(QStringLiteral("thresholdDb"), g_studyThreshDb);
        e.insert(QStringLiteral("count"), g_studyAlertCount);
        e.insert(QStringLiteral("path"), QDir::toNativeSeparators(MediaSession::recOutPath()));
        sendNoiseAlert(e);
    }
    qInfo("[study] 噪音异常，自动录制开始（%d 秒，设备 %s）", g_studyRecordSec, qPrintable(dev));

    if (!g_studyRecTimer) {
        g_studyRecTimer = new QTimer();
        g_studyRecTimer->setSingleShot(true);
        QObject::connect(g_studyRecTimer, &QTimer::timeout, [] {
            qint64 bytes = 0; int code = 0; QString err2;
            const bool ok = MediaSession::endRecording(&bytes, &err2, &code);
            QJsonObject e;
            e.insert(QStringLiteral("event"), QStringLiteral("record_stop"));
            e.insert(QStringLiteral("ok"), ok);
            e.insert(QStringLiteral("bytes"), double(bytes));
            e.insert(QStringLiteral("count"), g_studyAlertCount);
            if (!ok) e.insert(QStringLiteral("reason"), err2);
            sendNoiseAlert(e);
            if (ok) qInfo("[study] 噪音异常录制完成：%lld 字节，已上传云端", (long long)bytes);
            else    qWarning("[study] 噪音异常录制失败：%s", qPrintable(err2));
        });
    }
    g_studyRecTimer->start(g_studyRecordSec * 1000 + 3000);   // 留 3s 余量（ffmpeg 自身 -t 也在计时）
}

// ── P3 功能补齐：噪音/自习长期记录 + 播放时段管理 的本地落盘与查询 ──────
// 落盘目录统一在 AppDataLocation/Stelarith 下（与现存 island_history 同族，独立子目录）。
static QString dataRoot()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/Stelarith");
    QDir().mkpath(dir);
    return dir;
}

// 追加一行 JSON（Compact + 换行）到 subdir/YYYY-MM-DD.jsonl。
static void appendJsonl(const QString &subdir, const QJsonObject &o)
{
    const QString dir = dataRoot() + QLatin1Char('/') + subdir;
    QDir().mkpath(dir);
    // 2026-10-21（审查 #13）：按天文件从不清理 —— 500ms 采样一天 ~13MB、一学期 2GB+，
    //   而查看端（openNoiseLogDialog/buildNoiseLogText）是把**所有文件全读进内存**再
    //   统计的，学期末点开必卡。每天**首次写入**时清掉 7 天前的旧文件（只清 jsonl，
    //   不碰当天/近几天；一天一次，不增加高频写路径的开销）。
    {
        static QDate lastPruned;
        const QDate today = QDate::currentDate();
        if (lastPruned != today) {
            lastPruned = today;
            const QFileInfoList entries =
                QDir(dir).entryInfoList({ QStringLiteral("*.jsonl") }, QDir::Files);
            const QDate cutoff = today.addDays(-7);
            for (const QFileInfo &fi : entries) {
                const QDate d = QDate::fromString(fi.completeBaseName(), QStringLiteral("yyyy-MM-dd"));
                if (d.isValid() && d < cutoff) QFile::remove(fi.absoluteFilePath());
            }
        }
    }
    const QString file = dir + QLatin1Char('/') + QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")) + QStringLiteral(".jsonl");
    QFile f(file);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        f.write("\n");
    }
}

// 噪音采样落盘（采不到不记，避免污染趋势）。
static void logNoiseSample(double db, bool active, int seq, bool study, bool ok)
{
    if (!ok) return;
    QJsonObject o;
    o.insert(QStringLiteral("ts"), QDateTime::currentSecsSinceEpoch());
    o.insert(QStringLiteral("db"), db);
    o.insert(QStringLiteral("active"), active);
    o.insert(QStringLiteral("seq"), seq);
    o.insert(QStringLiteral("study"), study);
    appendJsonl(QStringLiteral("noise_log"), o);
}

// 自习会话：开始/结束各记一行（带 startTs 便于算时长）。返回 startTs。
static qint64 logStudyStart(double th, int need, int recSec)
{
    const qint64 ts = QDateTime::currentSecsSinceEpoch();
    QJsonObject o;
    o.insert(QStringLiteral("event"), QStringLiteral("start"));
    o.insert(QStringLiteral("ts"), ts);
    o.insert(QStringLiteral("thresholdDb"), th);
    o.insert(QStringLiteral("streakNeed"), need);
    o.insert(QStringLiteral("recordSec"), recSec);
    appendJsonl(QStringLiteral("study_log"), o);
    return ts;
}
static void logStudyStop(qint64 startTs, int alertCount)
{
    QJsonObject o;
    o.insert(QStringLiteral("event"), QStringLiteral("stop"));
    o.insert(QStringLiteral("ts"), QDateTime::currentSecsSinceEpoch());
    o.insert(QStringLiteral("startTs"), startTs);
    o.insert(QStringLiteral("durationSec"),
             startTs > 0 ? (QDateTime::currentSecsSinceEpoch() - startTs) : 0);
    o.insert(QStringLiteral("alertCount"), alertCount);
    appendJsonl(QStringLiteral("study_log"), o);
}

// ── P3 播放时段管理：时段判断 ───────────────────────────────────
// 没开时段限制 → 随时可播；开了但窗口为空 → 也放开；否则落在任一窗口内才放行。
// ⚠️ 2026-10-23（§10 第四刀地基）：isPlayAllowedNow 已下沉到 src/media/MediaSession。
// 当前窗口距结束的剩余毫秒（无限制/无窗口返回 -1）。供「最大最小适配」：超长播放自动截断。
// ⚠️ 2026-10-23（§10 第四刀地基）：remainingMsInWindow 已下沉到 src/media/MediaSession。


// 修复③（2026-10-19）：定义在 6082 起，被 executeAction 的 remote_control_stop
// 分支先用到 ⇒ 此处前向声明（同 TU 静态函数，声明后可后定义）。
static void stopRemoteControlSession(const QString &reason);
static void reapRemoteControlIfStale();

ExecOut executeAction(const QString &action, const QJsonObject &params)
{
    ExecOut out;

    // ── 内置模块前置路由（2026-10-16）：官方模块**优先于**进程外插件认领 ──
    // 顺序理由：内置模块是官方能力（U盘/联动/岛界面），第三方插件只能补它没覆盖的洞；
    // 反过来会让第三方插件有机会顶掉官方实现（"装了个插件，U 盘提示就变了"）。
    {
        ModuleResult mr;
        if (ModuleHost::instance()->dispatch(action, params, mr)) {
            out.result = mr.result;
            out.error = mr.error;
            out.data = mr.data;
            return out;
        }
    }

    // ── P2 插件前置路由：插件 register 的 action 优先走插件宿主（本地 WS 同步调用）──
    if (g_pluginHost) {
        PluginResult pr;
        if (g_pluginHost->dispatch(action, params, pr)) {
            out.result = pr.result;
            out.error = pr.error;
            out.data = pr.data;
            return out;
        }
    }

    // ── P3 第三方能力友好提示 ─────────────────────────────────────
    // 插件前置路由没接住的能力名（如广播/点歌/歌词），说明对应第三方插件未安装。
    // 给管理端一条明确的"去哪装"提示，而不是默默 failed（把"能力外迁"落到实处）。
    static const QStringList kThirdPartyCaps = {
        QStringLiteral("voicehub_broadcast"), QStringLiteral("song_request"),
        QStringLiteral("lyrics"), QStringLiteral("broadcast_audio")
    };
    if (kThirdPartyCaps.contains(action)) {
        islandShow(QStringLiteral("需要安装插件"),
                    QStringLiteral("「广播 / 点歌 / 歌词」等能力由第三方插件 VoiceHub 广播提供。\n请到托盘菜单「插件市场…」安装该能力后重试。"),
                    QStringLiteral("puzzle"), 9000, false, QString(),
                    IslandOverlay::Form::Centered, false);
        out.result = QStringLiteral("needs-plugin");
        out.error = QStringLiteral("未安装 VoiceHub 广播插件（请到插件市场安装）");
        return out;
    }

    // ── dev_act：仅开发用的动作触发器（2026-10-17 T 档验证设施）──────────────
    // 为什么需要：T 档要验"多种进行中状态在岛上并存"，而岛的状态源分散在
    //   通知 / 文件传输 / 媒体会话 / 对讲频谱 / 展开收起 等模块里，**没有一条统一的入口**；
    //   用云端指令触发要连真云端、用插件口又碰不到媒体与频谱。这里给沙箱一条内建口令。
    // ⚠️ 唯一的护栏：**必须 STE_QT_UID 以 `ste-sbx-` 开头**（沙箱实例专用命名前缀），
    //   生产实例即便被下发 dev_act 也只会回 failed，不会执行。这是"默认关闭、显式开启"，
    //   而不是"有口令就能用"——口令本身可能泄到管理端脚本里。
    if (action == QStringLiteral("dev_act")) {
        if (!qEnvironmentVariable("STE_QT_UID").startsWith(QLatin1String("ste-sbx-"))) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("dev_act 仅在沙箱实例（STE_QT_UID=ste-sbx-*）可用");
            return out;
        }
        const QString kind = params.value(QStringLiteral("kind")).toString();
        IslandOverlay *isl = IslandOverlay::instance();
        if (kind == QStringLiteral("ft")) {
            isl->setFileTransfer(params.value(QStringLiteral("name")).toString(),
                                 params.value(QStringLiteral("bytes")).toVariant().toLongLong(),
                                 params.value(QStringLiteral("total")).toVariant().toLongLong());
        } else if (kind == QStringLiteral("ft_clear")) {
            isl->clearFileTransfer();
        } else if (kind == QStringLiteral("media")) {
            isl->setMediaSession(params.value(QStringLiteral("title")).toString(),
                                 params.value(QStringLiteral("totalMs")).toVariant().toLongLong(),
                                 params.value(QStringLiteral("next")).toString(), QString());
        } else if (kind == QStringLiteral("media_clear")) {
            isl->clearMediaSession();
        } else if (kind == QStringLiteral("spectrum")) {
            // 16 位 16kHz 静默帧 → 只走"置位/展开"路径，不需要真的 MIC。
            isl->feedSpectrum(QByteArray(320 * 2, '\0'), 16000, 16);
        } else if (kind == QStringLiteral("spectrum_off")) {
            isl->setSpectrumActive(false);
        } else if (kind == QStringLiteral("expand")) {
            isl->setExpanded(params.value(QStringLiteral("on")).toBool(true));
        } else {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("dev_act 未知 kind：%1").arg(kind);
            return out;
        }
        out.result = QStringLiteral("done");
        return out;
    }

    if (action == QStringLiteral("lock")) {
        // user32 真实导出名就是 LockWorkStation（无 Native 后缀，那个名字不存在）
        if (LockWorkStation()) {
            out.result = QStringLiteral("done");
        } else {
            // 2026-10-14 修（审计）：这里原来用**逗号运算符**当语句分隔符
            //（`out.result = ..., out.error = ...`）—— 靠 QString::operator= 返回引用才凑合成立。
            // 它看起来就像漏了个分号，下一个改这段的人很容易「顺手」补成分号再引入真 bug。
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("LockWorkStation 返回 FALSE，错误码 %1").arg((int)GetLastError());
        }
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
        // 2026-10-17 远控/键鼠补齐：独立按键事件（keydown/keyup）。
        //   · 参数 vk = Windows 虚拟键码（十进制），action = down | up
        //   · 组合键（Ctrl+C 之类 / Ctrl+Alt+Del）由管理端依次发多个 key down 再逐个 key up，
        //     agent 这里只负责单键注入 —— 与鼠标 down/up 语义对齐。
        //   · 不做 VkKeyScan 转换（管理端直接给 vk），保证方向键/F1-F12/修饰键都能下发。
        if (kind == QStringLiteral("key")) {
            const int vk = params.value(QStringLiteral("vk")).toInt();
            const QString ka = params.value(QStringLiteral("action")).toString();
            if (vk <= 0 || vk > 255) {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("vk 无效：%1").arg(vk);
                return out;
            }
            INPUT ki = {};
            ki.type = INPUT_KEYBOARD;
            ki.ki.wVk = (WORD)vk;
            if (ka == QStringLiteral("up")) ki.ki.dwFlags = KEYEVENTF_KEYUP;
            // down 无额外标志；extended 键（如方向键）需 KEYEVENTF_EXTENDEDKEY，见下
            if (vk >= VK_LEFT && vk <= VK_DOWN) ki.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
            if (SendInput(1, &ki, sizeof(ki)) == 1) out.result = QStringLiteral("done");
            else {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("SendInput 按键失败，错误码 %1").arg((int)GetLastError());
            }
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


    // 主音量 0-100。
    //
    // ⚠️ 2026-10-11 修「音量无法调节」：原来用 winmm 的 waveOutSetVolume，
    //    它调的是**本进程自己的波形输出音量**（Windows Vista 之后音量是每应用/每会话的），
    //    也就是说它只把"星集控被控端这个程序"的音量改了，教室机上正在放听力、放视频的
    //    那些程序一点没变 —— 老师在管理端拖滑块，教室机声音纹丝不动，报上来就是"调不了"。
    //    正确做法：走 Core Audio（WASAPI）的 IAudioEndpointVolume，改**默认播放设备的主音量**。
    //    改完回读一次实际值回给管理端（不拿请求值冒充生效值）。
    if (action == QStringLiteral("set_volume")) {
        int v = 50;
        const QJsonValue jv = params.value(QStringLiteral("value"));
        if (jv.isDouble()) v = jv.toInt();
        else if (jv.isString()) { bool okv = false; const int t = jv.toString().trimmed().toInt(&okv); if (okv) v = t; }
        const int want = v;
        v = qBound(0, v, 100);

        if (!setSystemMasterVolume(v, &out.error)) {
            // Core Audio 走不通（无音频设备 / 服务被禁 / 远程会话无端点）就如实报失败，
            // 并且**回落到老的 waveOutSetVolume** —— 能调一点是一点，但不假装调成功了：
            // 回落时回执里带 fallback 标记，管理端能看出这不是标准路径。
            const quint32 lvl = ((quint32)v * 0xFFFFu) / 100u;
            const quint32 packed = (lvl << 16) | lvl;
            waveOutSetVolume(nullptr, packed);
            qWarning("[agent-qt] set_volume 走 Core Audio 失败（%s），回落 waveOutSetVolume",
                     out.error.toUtf8().constData());
            out.result = QStringLiteral("done");
            out.data.insert(QStringLiteral("volume"), v);
            out.data.insert(QStringLiteral("fallback"), QStringLiteral("waveOut"));
            out.data.insert(QStringLiteral("note"), QStringLiteral("系统主音量接口不可用，只改了被控端进程自身音量"));
            if (want != v) out.data.insert(QStringLiteral("clampedFrom"), want);
            return out;
        }
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

        // ── 2026-10-11 · #1「截图没有反馈」＝两端都要 ──────────────────────────
        // ① 本机（被控端）知情提示：老师截的是这台机器的屏，使用者应当知道（隐私知情）。
        //    以前截完悄无声息，学生完全不知道自己被截了图。
        //    图标用 "eye"（与「屏幕正在被查看」同一枚，已在用、必然能渲染）。
        islandShow(QStringLiteral("屏幕已被截取"),
                   QStringLiteral("老师远程截取了这台设备的屏幕"),
                   QStringLiteral("eye"), 6000);

        // ② 缩略图回传：管理端以前拿到的 path 是**学生机上的绝对路径**，老师根本
        //    看不到图，等于「执行了但没反馈」。这里把原图缩到宽 640 的 JPEG 塞进
        //    回执（base64），管理端拿到就能直接预览，不必再登学生机取文件。
        {
            const QPixmap thumb = (pm.width() > 640)
                                      ? pm.scaledToWidth(640, Qt::SmoothTransformation)
                                      : pm;
            QByteArray jpg;
            QBuffer buf(&jpg);
            if (buf.open(QIODevice::WriteOnly) && thumb.save(&buf, "JPG", 70)) {
                out.data.insert(QStringLiteral("thumb"), QString::fromLatin1(jpg.toBase64()));
                out.data.insert(QStringLiteral("thumbBytes"), jpg.size());
            } else {
                qWarning("[agent-qt] 截图缩略图编码失败（回执不带 thumb，管理端仍可按 path 提示）");
            }
        }
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
        // ⚠️ 2026-10-13 三端审计修复：原来白名单用 `target.toLower().contains(item)` ——
        //    **子串**匹配。于是 `incoming\chrome-setup.exe`（file_push 能把它落到收件目录，
        //    见 g_recvDir）里"含有 chrome"就被放行 ⇒ 配合 file_push = 免确认的任意代码执行。
        //    改成**两道闸**（缺一不可）：
        //      ① 名字必须**基名精确**命中（剥路径 + 去 .exe 后缀后全等）；
        //      ② 带路径的 target 必须落在**可信安装目录**内 —— 防"把恶意二进制直接改名成
        //         chrome.exe 再放进收件目录"绕过 ①（file_push 不禁止叫 chrome.exe）。
        auto normExeName = [](const QString &s) {
            QString n = QFileInfo(s.trimmed()).fileName().toLower();
            if (n.endsWith(QLatin1String(".exe"))) n.chop(4);
            return n;
        };
        const QString tName = normExeName(target);

        // ① 名字闸：内置名单（去 .exe 后缀，与归一化对齐）+ 管理员配的放行名单
        bool allowed = false;
        const QString allow = qEnvironmentVariable("STELARITH_AGENT_LAUNCH_ALLOWLIST").toLower();
        const QStringList items = allow.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &item : items)
            if (!item.trimmed().isEmpty() && normExeName(item) == tName) { allowed = true; break; }
        static const char *const kBuiltinSafe[] = {
            "classisland", "xingjikong", "explorer", "msedge", "chrome", "firefox",
            "wechat", "wps", "word", "excel", "powerpnt", "notepad", "cmd", nullptr
        };
        if (!allowed)
            for (const char *const *p = kBuiltinSafe; *p; ++p)
                if (tName == QLatin1String(*p)) { allowed = true; break; }
        if (!allowed) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("launch_app: 「%1」不在内置安全名单（classisland/xingjikong/浏览器/Office/记事本…），"
                                       "被拒绝。可配置 STELARITH_AGENT_LAUNCH_ALLOWLIST 放行。").arg(target);
            return out;
        }

        // ② 目录闸：带路径的 target 必须落在可信安装目录（否则"改名成合法名"就能绕过 ①）。
        //    裸名字（如 "notepad"）不含分隔符 → 交给 ShellExecuteW 走 App Paths/PATH 系统级解析，安全。
        if (target.contains(QLatin1Char('\\')) || target.contains(QLatin1Char('/'))) {
            const QString abs = QDir::toNativeSeparators(QFileInfo(target).absoluteFilePath()).toLower();
            QStringList roots;
            const auto addRoot = [&roots](const QString &p) {
                if (!p.trimmed().isEmpty())
                    roots << (QDir::toNativeSeparators(QDir::cleanPath(p)).toLower() + QLatin1Char('\\'));
            };
            addRoot(qEnvironmentVariable("ProgramFiles"));
            addRoot(qEnvironmentVariable("ProgramFiles(x86)"));
            addRoot(qEnvironmentVariable("ProgramW6432"));
            addRoot(qEnvironmentVariable("SystemRoot"));                        // 含 System32 / SysWOW64
            addRoot(qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/Programs"));  // 用户级安装
            // 逃生口：非标准安装位置（如 D:\Programs）由管理员显式追加目录白名单
            for (const QString &extra : qEnvironmentVariable("STELARITH_AGENT_LAUNCH_DIRS")
                                            .split(QLatin1Char(';'), Qt::SkipEmptyParts))
                addRoot(extra);
            bool inTrusted = false;
            for (const QString &root : roots) if (abs.startsWith(root)) { inTrusted = true; break; }
            if (!inTrusted) {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("launch_app: 「%1」不在可信安装目录（Program Files / Windows / 用户程序目录），"
                                           "已拒绝。非标准安装位置可配置 STELARITH_AGENT_LAUNCH_DIRS 追加。").arg(target);
                qWarning("[agent-qt] launch_app 拒绝：%s 不在可信目录（防改名绕过）", qPrintable(target));
                return out;
            }
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
        const auto &snap = visibleWindowsCached();
        if (snap.names.isEmpty()) { out.result = QStringLiteral("failed"); out.error = QStringLiteral("无法获取进程快照"); return out; }
        const QHash<quint32, QString> &names = snap.names;
        const auto &wins = snap.wins;
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
            // 2026-10-11 需求#6：管理端进程列表要显示图标。图标只能这边取
            // （管理端没有这台机器上的 exe），取不到就留空 —— 那边画首字方块兜底。
            const QString ic = windowIconBase64(w.first);
            if (!ic.isEmpty()) o.insert(QStringLiteral("icon"), ic);
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
        // ⚠️ 2026-10-13：名字名单之外**再过一次内核闸门**。
        //    名单不可能穷举 —— MemCompression / Registry / Secure System / LogonUI / 任意未列名的
        //    session-0 服务宿主都不在 kProtectedSystem 里，而名单曾是这条指令**唯一**的保护。
        //    这与 2026-10-09 学校机房"反复重启"事故（名单漏项 → TerminateProcess 掉 critical 进程
        //    → CRITICAL_PROCESS_DIED 0xEF）是同一类机理，只是换到了 process_stop 这条路径上；
        //    本端以 Highest 权限运行，杀掉 MemCompression 就是同一个 0xEF。
        //    闸门与 exam_mode 共用 proc_guard（问内核，不猜名字；查不到一律当受保护）。
        const QString blocked = terminateBlockedReason((DWORD)pid, prot);
        if (!blocked.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("拒绝结束 %1（pid %2）：%3").arg(exe).arg(pid).arg(blocked);
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
            const auto &snap = visibleWindowsCached();
            const QHash<quint32, QString> &names = snap.names;
            const auto &wins = snap.wins;
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

    // ── 相机六件套（走 ffmpeg DirectShow 输入）──
    // ⚠️ 2026-10-23（AGENTS.md §10 第 3 刀）：分支体已下沉到 src/media/CameraActions.cpp。
    //    这里只留分支判断与调用；**函数体是逐字搬的**，行为零变化。
    if (action == QStringLiteral("camera_list")) return CameraActions::listDevices();

    // 绑定摄像头：管理端「监控中心」选定一枚后落在这里，之后抓拍/录制默认用它。
    // device 传空 = 解绑（回落默认）。绑定存本机（只有本机能枚举），不落 agent.env。
    if (action == QStringLiteral("camera_bind")) return CameraActions::bindDevice(params);

    // 摄像头自检：按绑定设备（或指定 device）真抓一帧，只回"能不能用 + 耗时 + 原因"。
    // 与 camera_snapshot 的区别：这是**诊断**用途（管理端「测试」按钮），不管画面好看不好看。
    if (action == QStringLiteral("camera_test")) return CameraActions::selfTest(params);

    // 拍一张：ffmpeg 从指定设备（默认 video0）抓一帧写 PNG。回执给路径 + 字节数 + base64 缩略图。
    if (action == QStringLiteral("camera_snapshot")) return CameraActions::snapshot(params);

    // 开始录像：后台起 ffmpeg 进程写 mp4。已经在录就如实报 failed（不静默换文件）。
    if (action == QStringLiteral("camera_record_start")) return CameraActions::recordStart(params);

    // 停止录像。回执给实际字节数——0 字节就是失败，不许报 done。
    if (action == QStringLiteral("camera_record_stop")) return CameraActions::recordStop();


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
    if (action == QStringLiteral("media_session_start")) return MediaSession::startMediaSession(params);

    if (action == QStringLiteral("media_session_stop")) return MediaSession::stopMediaSessionAction();

    // ── P3 播放时段管理：配置允许的播放时段窗口 ──
    if (action == QStringLiteral("play_schedule_set")) {
        AgentSettings::setPlayScheduleEnabled(params.value(QStringLiteral("enabled")).toBool(false));
        const QJsonArray wins = params.value(QStringLiteral("windows")).toArray();
        QStringList list;
        for (const QJsonValue &v : wins) {
            const QString s = v.toString().trimmed();
            if (!s.isEmpty()) list.append(s);
        }
        AgentSettings::setPlayWindows(list);
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("enabled"), AgentSettings::playScheduleEnabled());
        out.data.insert(QStringLiteral("windows"), QJsonArray::fromStringList(AgentSettings::playWindows()));
        qInfo("[play] 🎵 播放时段配置：enabled=%d, windows=%d", AgentSettings::playScheduleEnabled(), list.size());
        return out;
    }
    if (action == QStringLiteral("play_schedule_get")) {
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("enabled"), AgentSettings::playScheduleEnabled());
        out.data.insert(QStringLiteral("windows"), QJsonArray::fromStringList(AgentSettings::playWindows()));
        out.data.insert(QStringLiteral("allowedNow"), MediaSession::isPlayAllowedNow());
        return out;
    }

    // ── 远控模式（真实会话状态，不是假标记）──
    // start：记下会话状态 + 提高抓屏帧率（远控要看得清，2 秒一帧太慢）
    // stop：恢复默认帧率
    if (action == QStringLiteral("remote_control_start")) {
        if (g_remoteControl) {
            // ⚠️ 2026-10-18 深夜实证修复（"没有画面"根因之一）：**不能静默复用旧会话**。
            //    RFB 是有状态握手：旧客户端若半途而废（管理端崩溃/换机/上次会话没走完），
            //    server 已在旧连接上发过 ProtocolVersion、永远等不到下一步 —— 新客户端
            //    连上来只会在死握手上干等，画面全黑且两端都无报错。
            //    唯一正确姿势：幂等重启 —— 干净停掉旧 server+bridge，再全新拉起，
            //    让新客户端拿到全新握手。
#ifdef STE_USE_LIBVNCSERVER
            qInfo("[agent-qt] remote_control_start：已有会话，重启 VNC（旧握手作废，对新客户端透明）");
            VncBridge::instance().stop();
            VncServer::instance().stop();
#endif
            g_remoteControl = false;   // 落回未启动态，走下面的全新起服路径
        }
        g_remoteControl = true;
        // 修复①③（2026-10-19）：新会话起点。首帧宽限/整屏帧一次性豁免要一个
        // 干净的起点；vnc-data down 保活时间戳清零，看门狗从新会话重新起算。
        g_vncSessionStartMs = QDateTime::currentMSecsSinceEpoch();
        g_lastVncDownMs = 0;
        g_vncMegaDone = false;
        const int want = qBound(1, params.value(QStringLiteral("fps")).toInt(20), 60);
        const int oldFps = g_currentFps;
        g_currentFps = want;
        // 2026-10-19：远控期冻结灵动岛（P0 崩溃规避，见 island.h setSuppressed）。
        // 必须在下面 islandShow 之前置位——否则远控提示本身就会触发 QML Repeater 重算。
        IslandOverlay::setSuppressed(true);
#ifdef STE_USE_LIBVNCSERVER
        // 统一方案 A（2026-10-18）：起服点在这里（对齐原 spec「remote_control_start
        // 启动 VncServer」）。VncServer 自动选 loopback 端口，VncBridge 连它把 RFB
        // 字节打成 vnc-data 帧走主 WS —— 不再需要云端下发 sid/key/port。
        if (!VncServer::instance().isRunning()) {
            const quint16 p = VncServer::instance().start(0, want);
            if (p == 0) {
                qWarning("[agent-qt] FAIL remote_control_start：VncServer 起服失败，回滚会话");
                g_remoteControl = false;
                g_currentFps = oldFps;
                // 修复③顺带（同一生命周期路径）：上面已把灵动岛冻结，
                // 起服失败不回滚这个冻结 ⇒ 岛将永久冻结（与泄漏同型的静默故障）。
                IslandOverlay::setSuppressed(false);
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("VNC 服务起服失败");
                return out;
            }
            qInfo("[agent-qt] 已在 127.0.0.1:%u 起 in-agent VNC Server（远控开始）", (unsigned)p);
        }
        VncBridge::instance().start(VncServer::instance().port());
#endif
        // 学生端知情（设计文档 6.5 隐私边界）：远控=屏幕正被老师查看，学生必须知道。
        // 灵动岛形态非打扰式提示（#98：统一走灵动岛，禁原生 QMessageBox），不遮操作。
        // 时长给的比原来的 3 秒长一些：远控一整节课都在，学生错过前 3 秒就再没提示了。
        islandShow(QStringLiteral("老师正在远程控制你的屏幕"),
                   QStringLiteral("屏幕画面正实时传给老师端"), QStringLiteral("eye"), 8000);
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
        // 修复③（2026-10-19）：清理逻辑抽到 stopRemoteControlSession（与泄漏
        // 看门狗共用一段，避免两条路径各写一半、日后改漏）。
        stopRemoteControlSession(QStringLiteral("管理端指令"));
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("active"), false);
        out.data.insert(QStringLiteral("fps"), g_currentFps);
        out.data.insert(QStringLiteral("fpsWas"), was);
        qInfo("[agent-qt] 🟢 远控会话关闭，抓屏帧率恢复 5 fps");
        return out;
    }

    // ── A6（2026-10-11 起）：噪音检测（§6.5）。2026-10-13 captureRms 已真实现 ──
    // 收到指令才开采集（不常驻占麦）。采样周期由调用方给（默认 2s），每拍把
    // {db, active, seq} 经 type:"noise_sample" 实时回传（订阅该设备的管理端）。
    // 短窗口采集：每拍"开→录 150ms→关"，不驻留录音、不受语音对讲长期占用。
    if (action == QStringLiteral("noise_monitor_start")) {
        // P0-1（2026-10-14）：本地「噪音监测」开关关了，远端指令也跳过（不占麦、不回传）。
        if (!AgentSettings::noiseEnabled()) {
            out.result = QStringLiteral("skipped");
            out.data.insert(QStringLiteral("reason"), QStringLiteral("本地已关闭噪音监测"));
            return out;
        }
        ensureNoise();
        const int iv = qBound(500, params.value(QStringLiteral("intervalMs")).toInt(2000), 60000);
        if (g_noise->running()) {
            out.result = QStringLiteral("done");
            out.data.insert(QStringLiteral("alreadyActive"), true);
            out.data.insert(QStringLiteral("intervalMs"), iv);
            return out;
        }
        g_noise->start(16000, iv);
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("active"), true);
        out.data.insert(QStringLiteral("intervalMs"), iv);
        qInfo("[agent-qt] 🎙 噪音检测开启（每 %d ms 一次）", iv);
        return out;
    }

    if (action == QStringLiteral("noise_monitor_stop")) {
        if (!g_noise || !g_noise->running()) {
            out.result = QStringLiteral("done");
            out.data.insert(QStringLiteral("active"), false);
            out.data.insert(QStringLiteral("wasActive"), false);
            return out;
        }
        g_noise->stop();
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("active"), false);
        out.data.insert(QStringLiteral("wasActive"), true);
        out.data.insert(QStringLiteral("lastDb"), g_noise->lastDb());
        qInfo("[agent-qt] 🔇 噪音检测停止");
        return out;
    }

    // ── 自习模式（2026-10-13）：噪音检测 + 噪音异常自动录制 ──
    // 与 noise_monitor_start 的分工：那个是"只测不录"的底层开关；这个是上层**模式**——
    // 除周期回传 dB 外，还带"连续超阈 → 自动录一段 → 上传云端 → 上报 noise_alert"的闭环。
    if (action == QStringLiteral("study_mode_start")) {
        // P0-1（2026-10-14）：自习模式底层也走噪音采集，本地关了就不开。
        if (!AgentSettings::noiseEnabled()) {
            out.result = QStringLiteral("skipped");
            out.data.insert(QStringLiteral("reason"), QStringLiteral("本地已关闭噪音监测"));
            return out;
        }
        const double th = params.value(QStringLiteral("thresholdDb")).toDouble(-45.0);
        const int need = qBound(1, params.value(QStringLiteral("streakNeed")).toInt(3), 60);
        const int recSec = qBound(5, params.value(QStringLiteral("recordSec")).toInt(30), 300);
        const int iv = qBound(500, params.value(QStringLiteral("intervalMs")).toInt(2000), 60000);
        ensureNoise();
        g_studyOn = true;
        g_studyThreshDb = th;
        g_studyStreakNeed = need;
        g_studyRecordSec = recSec;
        g_studyStreak = 0;
        g_studyAlertCount = 0;
        g_studyStartTs = logStudyStart(th, need, recSec);   // P3：长期记录（会话开始）
        if (!g_noise->running()) g_noise->start(16000, iv);
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("study"), true);
        out.data.insert(QStringLiteral("thresholdDb"), th);
        out.data.insert(QStringLiteral("streakNeed"), need);
        out.data.insert(QStringLiteral("recordSec"), recSec);
        out.data.insert(QStringLiteral("intervalMs"), iv);
        qInfo("[study] 📖 自习模式开启：阈值 %.1f dB / 连续 %d 拍 / 触发录 %d 秒", th, need, recSec);
        return out;
    }

    if (action == QStringLiteral("study_mode_stop")) {
        g_studyOn = false;
        g_studyStreak = 0;
        if (g_studyRecTimer) g_studyRecTimer->stop();
        if (g_noise && g_noise->running()) g_noise->stop();
        logStudyStop(g_studyStartTs, g_studyAlertCount);   // P3：长期记录（会话结束 + 时长 + 触发数）
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("study"), false);
        out.data.insert(QStringLiteral("alertCount"), g_studyAlertCount);
        qInfo("[study] 📖 自习模式关闭（本次共触发 %d 次异常录制）", g_studyAlertCount);
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
        g_fileRecvLastMs = QDateTime::currentMSecsSinceEpoch();   // 会话还在动，别清理
        // #198（2026-10-11）：每收一片就推一次进度到灵动岛（分母用声明总字节）。
        IslandOverlay::instance()->setFileTransfer(g_fileRecvName, g_fileRecvBytes, g_fileRecvTotal);
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
        // 取消的判据改看**显式标志**，不再拿 total_bytes<=0 顶替：
        // 那样做等于把"0 字节文件"和"取消"焊成同一件事，一个真要推的空文件会被当成取消删掉。
        // 老管理端不会带 cancel 字段，那时仍按 total_bytes<=0 认取消 —— 兼容不变。
        const bool wantCancel = params.value(QStringLiteral("cancel")).toBool(false)
                                || (!params.contains(QStringLiteral("cancel")) && expect <= 0);
        if (wantCancel) {
            // 发端点了"取消"。半截文件**必须删掉**：留着就是这台机器上打不开的残缺文件，
            // 中间这段时间谁点开谁中招。
            out.result = QStringLiteral("done");
            out.data.insert(QStringLiteral("cancelled"), true);
            out.data.insert(QStringLiteral("bytes"), g_fileRecvBytes);
            out.data.insert(QStringLiteral("path"), QDir::toNativeSeparators(g_fileRecvTarget));
            QFile::remove(g_fileRecvTarget);
            g_fileRecvTarget.clear();
            g_fileRecvNext = 0;
            g_fileRecvBytes = 0;
            g_fileRecvLastMs = 0;
            IslandOverlay::instance()->clearFileTransfer();   // #198：取消 ⇒ 进度卡撤掉
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
            IslandOverlay::instance()->clearFileTransfer();   // #198：字节不符 ⇒ 进度卡撤掉
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("path"), QDir::toNativeSeparators(g_fileRecvTarget));
        out.data.insert(QStringLiteral("bytes"), g_fileRecvBytes);
        out.data.insert(QStringLiteral("chunks"), g_fileRecvNext);
        // ── 2026-10-11：「收到文件」的提示从这里发，不再在 file_push 里发 ──
        // 以前开会话的瞬间就报"收到文件"，可那时文件刚被 Truncate 成 0 字节、
        // 一片数据都还没到 —— 学生/老师点开就是个空文件（用户报的"推完的文件是空的"）。
        // 现在放到字节数**比对通过之后**，文件名带体积，让人一眼知道收全了没有。
        {
            const QString shown = QDir::toNativeSeparators(g_fileRecvTarget);
            const QString desc = QFileInfo(g_fileRecvTarget).fileName()
                                 + QStringLiteral("（") + humanBytes(g_fileRecvBytes)
                                 + QStringLiteral("）");
            islandShow(QStringLiteral("收到文件"), desc, QStringLiteral("download"), 6000,
                       true, shown);
            trayNotify(QStringLiteral("收到文件"), desc, QSystemTrayIcon::Information, 10000);
            IslandOverlay::instance()->clearFileTransfer();   // #198：收全 ⇒ 进度卡撤掉，改显"收到文件"
        }
        qInfo("[agent-qt] ✅ 文件收全落盘：%s（%lld 字节 / %d 片）",
              qPrintable(g_fileRecvTarget), (long long)g_fileRecvBytes, g_fileRecvNext);
        g_fileRecvTarget.clear();
        g_fileRecvNext = 0;
        g_fileRecvBytes = 0;
        g_fileRecvLastMs = 0;
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
        // #198（2026-10-11）：进度卡用。先把名字/总字节记下来，开好句柄后在成功分支点亮灵动岛。
        g_fileRecvName = clean;
        g_fileRecvTotal = total;
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
            g_fileRecvLastMs = QDateTime::currentMSecsSinceEpoch();
            out.result = QStringLiteral("done");
            out.data.insert(QStringLiteral("target"), QDir::toNativeSeparators(alt));
            out.data.insert(QStringLiteral("totalBytes"), total);
            out.data.insert(QStringLiteral("ready"), true);
            IslandOverlay::instance()->setFileTransfer(g_fileRecvName, 0, g_fileRecvTotal);
            // ⚠️ 这里**不提示**"收到文件"。此刻只是把接收句柄开好、目标刚被 Truncate 成
            // 0 字节，一片数据都还没到 —— 在这儿报"收到"就是让人去打开一个空文件
            // （2026-10-11 用户报的"推完的文件是空的"正是这条）。
            // 真正的提示挪到 file_done 字节数比对通过之后。
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("target"), QDir::toNativeSeparators(target));
        out.data.insert(QStringLiteral("totalBytes"), total);
        out.data.insert(QStringLiteral("ready"), true);
        IslandOverlay::instance()->setFileTransfer(g_fileRecvName, 0, g_fileRecvTotal);
        g_fileRecvLastMs = QDateTime::currentMSecsSinceEpoch();   // 同上：此刻不提示
        return out;
    }

    // 2026-10-04（乙阶段 2）：大屏通知（notify）——popup 居中弹窗 / island 灵动岛 / fullscreen 全屏遮罩，
    // 可配 TTS 朗读。站点广播（broadcast.ts）改走云端 WS 指令通道后，教室大屏不再依赖 CIMS。
    if (action == QStringLiteral("notify")) {
        const QString err = notifyFromParams(params);
        // 2026-10-21（审查 #7）："skipped:" 前缀 = 门控静默丢弃（考试中），不是失败
        //   —— 管理端不该看到一条 failed 去重试/报警。
        if (err.startsWith(QLatin1String("skipped:"))) {
            out.result = QStringLiteral("skipped");
            out.error = err.mid(8);
            return out;
        }
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
                          QStringLiteral("已经有终端会话开着，同一台设备同时只放一个"));
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
                        QStringLiteral("有人要从管理端连这台设备的命令行（可执行任意命令）。允许吗？"),
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

// ── 远控会话收口（2026-10-19 修复③）────────────────────────────────────────
// remote_control_stop 动作与泄漏看门狗共用同一段清理：置位、解冻灵动岛、
// 停桥、停服、恢复帧率。抽成函数是因为以前"停会话"只此一条路径（管理端
// 自觉发 stop），而管理端直接离开（关窗/切页/进程崩/网络分区）时没有任何
// 人调它 ⇒ g_remoteControl 永真、灵动岛长期冻结、VNC server 空转烧 CPU
// （2026-10-19 实证：泄漏近 30 分钟，只能重启 agent 收口）。
// 幂等：无会话时早退（stop 动作据此仍要返回 failed，见调用方）。
static void stopRemoteControlSession(const QString &reason)
{
    if (!g_remoteControl) return;
    const int was = g_currentFps;
    g_remoteControl = false;
    // 2026-10-21（审查 #5）：setSuppressed(false) 只清标志不恢复岛 —— 远控开始时
    //   hide() 掉了岛，结束不补 ensureIdle() 的话顶部空闲半胶囊就丢了（要等下一次
    //   通知才回来）。ensureIdle 内部受 m_idleEnabled 约束 + isVisible 判据（岛上
    //   正显示内容时不会把内容顶成空闲）。
    IslandOverlay::setSuppressed(false);   // 2026-10-19：解除岛冻结（远控结束）
    IslandOverlay::instance()->ensureIdle();
#ifdef STE_USE_LIBVNCSERVER
    // 统一方案 A：停整条桥再销毁 in-agent VNC Server（RFB 源）。
    VncBridge::instance().stop();
    VncServer::instance().stop();
#endif
    g_currentFps = 5;   // 恢复默认
    qInfo("[agent-qt] 🟢 远控会话关闭（%s），抓屏帧率恢复 5 fps（原 %d）",
          qPrintable(reason), was);
}

// ── 远控会话泄漏看门狗（2026-10-19 修复③）──────────────────────────────────
// 判据：管理端 VncClient 每 5s 经 RFB 发一个 1x1 增量 FramebufferUpdateRequest
//   （无害探活），字节经 vnc-data down 到达本端即刷新 g_lastVncDownMs；
//   老师纯观看不碰键鼠时也有这个流量，所以"在看"与"已离开"可区分。
// 口径：会话起步 30s 内不判（留给 DXGI 首帧 2s 黑帧预算 + RFB 握手 +
//   首个保活）；之后超过 25s（保活周期 5s 的 5 倍）没收过任何 down 字节
//   ⇒ 判管理端已离开，自动收口。
// 误杀代价可控（老师重新选中设备即重建会话）；漏杀代价是灵动岛永久冻结
//   + VNC server 空转，不可接受。搭心跳车执行，零新定时器。
static void reapRemoteControlIfStale()
{
    if (!g_remoteControl) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (g_vncSessionStartMs > 0 && now - g_vncSessionStartMs < 30000) return;   // 起步宽限
    if (g_lastVncDownMs > 0 && now - g_lastVncDownMs <= 25000) return;          // 保活正常
    qWarning("[agent-qt] [vnc] ⚠️ 远控会话管理端保活超时（down %s）—— 判定已离开，自动收口",
             g_lastVncDownMs > 0 ? "超过 25s 未到" : "从未收到（会话起步 30s 后仍无）");
    stopRemoteControlSession(QStringLiteral("泄漏看门狗"));
}

void handleControlText(const QString &text)
{
    const QByteArray raw = text.toUtf8();
    if (raw.isEmpty()) return;

    if (!raw.startsWith('{')) {
        // ⚠️ 2026-10-13：升级为 qWarning —— 非 JSON 文本多半是协议不匹配（云端一律发 v1
        //    JSON 信封，不存在"已知的纯文本消息"），属于"程序/版本出问题了"，不该当普通
        //    信息在日志里溜过去。
        qWarning("[agent-qt] 收到云端文本（非 JSON，疑似协议不匹配）：%s", qPrintable(text.left(200)));
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isObject()) {
        qWarning().noquote() << QStringLiteral("[agent-qt] FAIL: 云端消息不是合法 JSON：%1").arg(text.left(200));
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
        // 2026-10-14 修（审计 P2）：原来只要 hb > 0 就无条件采纳，异常值直接生效。
        // g_timeoutMs 只用于日志说明、不参与本端任何判定，所以不加约束。
        const int hbClamped = qBound(kHeartbeatMinMs, hb, kHeartbeatMaxMs);
        if (hb > 0 && hbClamped != g_heartbeatMs) {
            if (hbClamped != hb)
                qWarning("[agent-qt] 云端下发心跳 %d ms 超出合理范围 [%d, %d]，按 %d ms 执行",
                         hb, kHeartbeatMinMs, kHeartbeatMaxMs, hbClamped);
            g_heartbeatMs = hbClamped;
            if (g_hbTimer) g_hbTimer->setInterval(g_heartbeatMs);
        }
        if (to > 0) g_timeoutMs = to;
        g_lastOnlineAt = QDateTime::currentMSecsSinceEpoch();
        // 连上云端 = 这一版是活的（强制升级就靠这一个信号判定"新版本起没起来"）。
        // 注意要在 applyUpdateInfo **之前**执行：applyUpdateInfo 里可能立刻触发自动升级，
        // 而升级前的成功标记必须带着"我当前这一版是好的"这个事实走。
        otaMarkStartedOk();
        updateTray(TrayState::Connected, QStringLiteral("已连接"));
        // 2026-10-12「灵动岛一直处于重连状态」：断线弹「连接中断」后，恢复时若
        // 只悄悄连上，用户看到的只有反复的「连接中断」→ 像"一直在重连"。这里在
        // **重连成功**（曾断过、不是首连）时补一条「已恢复」，让断/连成对出现，
        // 用户能明确知道"刚断过、现已恢复"，而不是只被断的提示反复刷。
        if (g_lastOfflineAt > 0) {
            const qint64 dur = (QDateTime::currentMSecsSinceEpoch() - g_lastOfflineAt) / 1000;
            g_lastOfflineAt = 0;
            islandShow(QStringLiteral("已重新连接"),
                       dur >= 60 ? QStringLiteral("刚才断开约 %1 分钟，现已恢复").arg(dur / 60)
                                 : QStringLiteral("刚才断开约 %1 秒，现已恢复").arg(dur),
                       QStringLiteral("check-circle"), 2500);
        }
        qInfo("[agent-qt] ✅ 云端已确认注册 v1（uid=%s，server=%s，心跳 %d ms，超时 %d ms）",
              qPrintable(pay.value(QStringLiteral("uid")).toString()),
              qPrintable(pay.value(QStringLiteral("server")).toString()),
              g_heartbeatMs, g_timeoutMs);
        // 升级提示（2026-10-06）：云端把 ota.json 里的最新版本随回执带回，
        // 这里比一次、该提示就提示（比不出来/没发布过都如实说，不假装"已最新"）。
        applyUpdateInfo(pay);
        return;
    }

    // ── 远控（2026-10-18 · 统一方案 A · vnc-data 走主 WS）──────────────────────
    // 起服入口已移到 remote_control_start（VncServer 自动选 loopback 端口、VncBridge
    // 连它把 RFB 字节打成 vnc-data 帧走主 WS）。此分支保留只为兼容老云端：收到即幂等补起。
    // sid/key/port 不再需要（主 WS 已鉴权；端口由 VncServer 自行决定）。
    if (type == QStringLiteral("tunnel.open")) {
#ifdef STE_USE_LIBVNCSERVER
        if (!VncServer::instance().isRunning()) {
            const quint16 p = VncServer::instance().start(0, 0);   // 兼容路径无 fps 参数，默认 10fps
            if (p == 0) {
                qWarning("[agent-qt] FAIL tunnel.open：VncServer 起服失败，忽略");
                return;
            }
            // 2026-10-21（审查 #8）：下面这条日志原来**一字不差打了两遍**，排障时
            // 像起了两个 server —— 只留一条。
            qInfo("[agent-qt] 已在 127.0.0.1:%u 起 in-agent VNC Server（tunnel.open 兼容路径）", (unsigned)p);
        }
        VncBridge::instance().start(VncServer::instance().port());
        qInfo("[agent-qt] tunnel.open 兼容处理完成（统一方案 A 下起服点是 remote_control_start）");
#else
        qWarning("[agent-qt] tunnel.open 收到但未启用 STE_USE_LIBVNCSERVER，忽略");
#endif
        return;
    }
    if (type == QStringLiteral("error")) {
        // 统一错误通道：云端拒绝必须看得见（fail-silent 红线）。code 是机器可读的，message 是人话。
        // 错误码体系（2026-10-06）：ecode = 数字码（AU1001/CM4001…），code = 语义码兼容。
        const QString ecode = pay.value(QStringLiteral("ecode")).toString();
        qWarning().noquote() << QStringLiteral("[agent-qt] FAIL: 云端拒绝 → %1/%2：%3")
            .arg(ecode.isEmpty() ? QStringLiteral("-") : ecode,
                 pay.value(QStringLiteral("code")).toString(),
                 pay.value(QStringLiteral("message")).toString());
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
            // 2026-10-14 修（审计 P1）：返回值原来直接丢弃 —— mic 开不起来（无设备 /
            // 被语音对讲独占 / 驱动报错）时照样打「mic 已开启」，于是老师那头听到的是
            // 死寂，而我们这边日志写的是成功，两头都对不上。现在如实分岔。
            if (g_microphone.start(120000)) {
                qInfo("[agent-qt] 🔊 语音通话开始（duplex，学生 mic 已开启，回传老师）");
            } else {
                qWarning("[agent-qt] ⚠️ 语音通话是 duplex 但本机 mic 没开起来"
                         "（无麦克风 / 被语音对讲独占 / 设备报错），学生这端是哑的");
            }
        }
        qInfo("[agent-qt] 🔊 老师开始讲话（语音对讲）");
        return;
    }
    if (type == QStringLiteral("audio.stop")) {
        g_audioHintShown = true;   // 结束后不再提示
        g_audioPlayer.closePlayer();
        // §6.3：频谱停 + 收起"老师正在讲话"（这条 duration 给了 0，不会自己关）
        IslandOverlay::instance()->setSpectrumActive(false);
        IslandOverlay::instance()->dismiss();
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

    // ── 2026-10-18 · VoiceHub「正在播放」状态（云端集中轮询后下发）──────────
    // 链路：voicehub.245959623.xyz → 云端（30s 轮询 + 进度外推）→ 本机 → 灵动岛。
    // 为什么不由本机直连 VoiceHub：60 台教室机各连一次 = 60 条外网长连接，断网就全瞎。
    //
    // ⚠️ position 是**云端响应那一刻**的值；岛每秒重绘时由 IslandOverlay 用本地时钟继续外推。
    //    绝不为进度去问云端 —— 那是 30s 一次的事。
    // ⚠️ 本消息**只读**：不触发指令回执，也不改任何播放状态。
    // ⚠️ 消息类型名故意用 voicehub_nowplaying 而不是 voicehub_broadcast ——
    //    后者**已经是** executeAction 里的一个**动作名**（kThirdPartyCaps：未装插件时的
    //    "去插件市场装 VoiceHub"提示）。两者虽在不同函数、不直接冲突，但同名会让
    //    后来人以为"这条消息会触发那个动作"，是命名陷阱，避开。
    if (type == QStringLiteral("voicehub_nowplaying")) {
        const QString vstatus = pay.value(QStringLiteral("status")).toString();
        const QJsonObject b = pay.value(QStringLiteral("broadcast")).toObject();
        if (IslandOverlay *isl = IslandOverlay::instance()) {
            if (vstatus != QLatin1String("playing") || b.isEmpty()) {
                // idle / stale / unknown 一律撤掉广播任务（不显示"可能还在播"）
                isl->clearBroadcastTask();
            } else {
                IslandOverlay::BroadcastInfo info;
                info.title = b.value(QStringLiteral("title")).toString();
                info.artist = b.value(QStringLiteral("artist")).toString();
                info.duration = b.value(QStringLiteral("duration")).toDouble();
                info.position = b.value(QStringLiteral("position")).toDouble();
                info.isPlaying = b.value(QStringLiteral("isPlaying")).toBool();
                info.publisherName = b.value(QStringLiteral("publisherName")).toString();
                info.listenerCount = b.value(QStringLiteral("listenerCount")).toInt();
                const QJsonObject nu = b.value(QStringLiteral("nextUp")).toObject();
                info.hasNextUp = !nu.isEmpty();
                info.nextUpTitle = nu.value(QStringLiteral("title")).toString();
                isl->setBroadcastTask(info);
            }
        }
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
            // 2026-10-21（审查 #7）：门控命中 = 静默丢弃，回执 skipped 而非 failed（同上处）
            if (err.startsWith(QLatin1String("skipped:")))
                sendActionReceipt(id, action, QStringLiteral("skipped"), err.mid(8));
            else if (!err.isEmpty()) sendActionReceipt(id, action, QStringLiteral("failed"), err);
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
            const QString tsid = params.value(QStringLiteral("sid")).toString();
            const bool tAccepted = TerminalConsole::inst().input(
                tsid, params.value(QStringLiteral("keys")).toString());
            // 2026-10-13 审计修复：会话不存在/已关闭、或上条命令还在跑时 input() 是**没接受**的，
            // 以前无条件回 done ⇒ 管理端以为输入成功，其实一个字符都没进 shell。
            if (tAccepted)
                sendActionReceipt(id, action, QStringLiteral("done"));
            else
                sendActionReceipt(id, action, QStringLiteral("failed"),
                                  QStringLiteral("终端输入未被接受：会话不存在/已关闭，或上一条命令还在跑（sid=%1）").arg(tsid));
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
    // 2026-10-21（审查 #2）：远控期间 VNC（DXGI）本来就在抓屏，这里再 grabWindow +
    //   JPEG 编码纯属白烧 CPU —— 而远控恰恰是最吃 CPU 的场景。判断提到抓屏**之前**
    //   （原判据在 JPEG 编码后、发送前，白抓白编了 2 秒）。
    if (g_remoteControl) return;
    // 2026-10-12 用户命令「去掉轮询，只保留截图」：周期性抓屏 → WS 单帧推送停用
    //（当时前提是 RTC 实时流可用、看单帧用截图指令）。
    // ⚠️ 2026-10-16 生产兜底改造：RTC 链路未完全打通时，这路 WS 帧就是**唯一画面路**，
    //    停掉 = 生产完全没画面。改为**默认启用 + RTC 真连通时自动停**（智能降级）：
    //      · RTC connected → WS 帧停（实时流优先，不双路）
    //      · RTC 未连（协商中/失败/卡死）→ WS 轮询帧照推（能看，慢）。
    //    保留 STE_ENABLE_WS_FRAME_PUSH=0 显式关闭（回到 2026-10-12 行为）。
    if (qgetenv("STE_ENABLE_WS_FRAME_PUSH") == "0") {
        static int offSkipped = 0;
        if (++offSkipped % 60 == 1)
            qInfo("[agent-qt] WS 单帧轮询推送已显式停用（STE_ENABLE_WS_FRAME_PUSH=0），累计跳过 %d 次", offSkipped);
        return;
    }

    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        qWarning().noquote() << QStringLiteral("[agent-qt] FAIL: 找不到主屏");
        return;
    }

    const QPixmap pm = screen->grabWindow(0);
    if (pm.isNull()) {
        qWarning().noquote() << QStringLiteral("[agent-qt] FAIL: grabWindow 返回空图");
        return;
    }

    QByteArray jpeg;
    {
        QBuffer buffer(&jpeg);
        buffer.open(QIODevice::WriteOnly);
        if (!pm.save(&buffer, "JPEG", 60)) {
            qWarning().noquote() << QStringLiteral("[agent-qt] FAIL: JPEG 编码失败");
            return;
        }
    }

    if (!outDir.isEmpty()) {
        const QString path = outDir + QStringLiteral("/shot-%1.jpg")
                                 .arg(QDateTime::currentMSecsSinceEpoch());
        if (pm.save(path, "JPEG", 60))
            qInfo("[agent-qt] 本地存图 %s", qPrintable(path));
        else
            qWarning().noquote() << QStringLiteral("[agent-qt] FAIL: 本地存图失败 → %1").arg(path);
    }

    // 走到这里一定是 online（离线分支已在函数开头提前返回）。
    // ⚠️ 远控期间停发（2026-10-18 深夜实证 + 2026-10-21 上移）：画面已改走 VNC up 帧，
    //    JPEG 广播帧在这条劣化上行上纯属抢带宽——两路一起发，谁都会把心跳挤到迟到。
    //    viewer 侧 m_vncActive 本来就让 JPEG 帧靠行，这里源头直接不发，省下的配额全给 VNC。
    //    （判据本身已提前到 grabWindow 之前，见函数开头 —— 这里保留原注释说明语义。）
    // v1 二进制帧：[1B 版本][2B 大端 headerLen][header JSON][JPEG]，带 seq —— 旧裸帧没有序号，
    // 将来按需拉流、断帧重传、测 fps 全靠它。
    //
    // ⚠️ WS 发送积压背压（2026-10-18 实证修复）：~1.2MB/s 的 JPEG 洪流会把 QWebSocket
    //    发送队列灌满，10s 一条的小心跳文本帧排在队尾迟到 30s+ ⇒ 云端判"心跳超时"踢人，
    //    设备每 30-60s 重连一次（当天 342 次）。帧可以丢，心跳不能丢 —— 积压超 1MB 就
    //    跳过本帧（画面晚 200ms 没人察觉，掉线全班都看得见）。
    if (g_ws->bytesToWrite() > 1024 * 1024) {
        static int skipped = 0;
        if (++skipped % 20 == 1)
            qWarning("[agent-qt] WS 发送积压 %lld KB 超 1MB，跳过本帧保心跳（已跳 %d 帧）",
                     g_ws->bytesToWrite() / 1024, skipped);
        return;
    }
    // ⚠️ 带宽令牌桶（2026-10-18 实证修复）：帧洪流（~1.2MB/s）灌满上行链路后，
    //    10s 一条的小心跳在队列里被推到 28s 后才到 ⇒ 云端 30s 判超时踢人（当日 342 次）。
    //    实测当天 CF 隧道上行仅 ~250KB/s（正常时段数 MB/s，环境劣化）——
    //    帧配额定 150KB/s（留 40% 余量给心跳/信令）；链路恢复后此帽只是降帧率上限，
    //    可按需上调。画面损失：最坏 ~0.5-1fps@278KB，肉眼可感但在线稳定压倒一切。
    //    ⚠️ 2026-10-21（审查 #14）：本桶（JPEG 150KB/s）与 VNC up 桶（200KB/s）、
    //       bytesToWrite 积压闸（1MB）是**三套独立记账** —— 它们不叠加的前提是
    //       JPEG 与 VNC 互斥（远控激活时 captureAndSend 提前 return，本文件开头），
    //       非远控时 VNC 无上行（server 只在远控激活）⇒ 任意时刻最多一个桶在淌。
    {
        static qint64 winStart = 0;
        static qint64 winBytes = 0;
        static qint64 winFrames = 0;        // 2026-10-21：本秒已推帧数（单帧豁免判定用）
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - winStart >= 1000) { winStart = now; winBytes = 0; winFrames = 0; }
        // 2026-10-21（审查 #1）：原来 `winBytes + size > 150KB` 从 0 起算 —— 单帧本身
        //   超 150KB 时**每一帧**都满足条件，永远推不出去（1080p@60 复杂桌面常见
        //   150-250KB，4K 必超），大屏机器非远控时画面流全空白而日志只刷"限流"。
        //   加"单帧一次性豁免"：本秒第一帧不占配额检查直接推（对照 VNC up 路同款
        //   grace 写法，L~7676），从第二帧起恢复 150KB/s 配额 —— 最坏多一帧流量，
        //   保证画面流至少能走起来。
        const bool grace = (winFrames == 0);
        if (!grace && winBytes + jpeg.size() > 150 * 1024) {
            static int bwSkipped = 0;
            if (++bwSkipped % 50 == 1)
                qWarning("[agent-qt] 本秒帧流量已到 150KB 配额，跳帧保心跳（累计跳 %d）", bwSkipped);
            return;
        }
        winBytes += jpeg.size();
        winFrames++;
    }
    const QByteArray frame = makeFrameBytes(g_uid, g_frameSeq + 1, jpeg);
    const qint64 n = g_ws->sendBinaryMessage(frame);
    g_frameSeq++;
    g_frameBytes += jpeg.size();   // 只累计 JPEG 净荷，跟云端 bytesIn 对账才对得上
    qInfo("[agent-qt] 帧#%d 推送 %lld B 净荷（含帧头 %lld B，累计 %.2f MB，耗时 %lldms）",
          g_frameSeq, (qint64)jpeg.size(), n, g_frameBytes / 1048576.0, cost.elapsed());
}

} // namespace

// ── 录像上传（2026-10-22 移出匿名 namespace）─────────────────────────────
// 必须排在 `} // namespace` 之后：它引用 g_uid，而 g_uid 在匿名域内（同 TU 可见，
// 但要排在其后）。MediaSession.cpp 用 extern 跨 TU 调它，故需外部链接。
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

// ── U5-A3：QML 控制面板（2026-10-16）──────────────────────────────────
// header-only（AgentUi + AgentShell 两个 Q_OBJECT 类），内联实现直接调用上面
// 匿名域里的 static 自由函数与 AgentSettings —— 匿名域符号在本 TU 内全程可见，
// 但本 include **绝不能放进匿名域**（Qt 头会被拽进域内 ⇒ QVariant/QtPrivate 与
// 全局的同名声明冲突，C2872 ambiguous，实测首编即炸）。只被本文件 include，
// 见 agent_ui.h 顶部说明。
#include "agent_ui.h"

/* ── U5-A3：QML 控制面板的数据接线（定义必须在本文件 —— 要访问匿名域符号）────
 * agentUiFill 是 agent_ui.h 的 friend（直接改快照成员，不触发 switchChanged）。
 * ⚠️ 不能加 static：friend 声明是 external linkage，加 static 就成另一个符号。
 */
/* ── A-3 第二批：四页的数据 builder（static，只产出数据不碰 AgentUi 成员）──── */

// 自检四项（判据照 runSelfCheck 一字不差）
static QVariantList buildSelfCheckItems()
{
    QVariantList items;
    auto add = [&items](const QString &name, bool ok, const QString &fix) {
        QVariantMap m;
        m.insert(QStringLiteral("name"), name);
        m.insert(QStringLiteral("ok"), ok);
        m.insert(QStringLiteral("fix"), ok ? QString() : fix);
        items.append(m);
    };
    add(QStringLiteral("配置完整（云端地址 + 设备令牌）"), g_configured, QStringLiteral("去配置 / 绑定"));
    const bool connected = g_ws && g_ws->state() == QAbstractSocket::ConnectedState;
    add(QStringLiteral("已连接云端"), connected, QStringLiteral("重试连接"));
    add(QStringLiteral("已注册（云端已确认）"), g_registered, QStringLiteral("重试连接"));
    add(QStringLiteral("班级已绑定（管理端按班可见）"), !g_classCode.isEmpty(), QStringLiteral("绑定班级"));
    return items;
}

// 插件列表（本地快照 + 云端条目缓存；口径照 openPluginStore::render）
static QHash<QString, QJsonObject> s_storeEntries;   // id -> 云端条目（pluginsRefresh 刷新）
static QString pluginRegistryUrl()
{
    QString u = qEnvironmentVariable("STE_PLUGIN_REGISTRY");
    return u.isEmpty() ? QStringLiteral("https://control.245959623.xyz/api/plugins") : u;
}
static QVariantList buildPluginList()
{
    QVariantList out;
    QHash<QString, QJsonObject> localById;
    if (g_pluginHost) {
        const QJsonArray local = g_pluginHost->snapshot();
        for (const QJsonValue &v : local) {
            const QJsonObject o = v.toObject();
            localById.insert(o.value(QStringLiteral("id")).toString(), o);
        }
    }
    QStringList ids;
    for (auto it = s_storeEntries.begin(); it != s_storeEntries.end(); ++it) ids << it.key();
    for (const QString &id : localById.keys()) if (!ids.contains(id)) ids << id;
    for (const QString &id : ids) {
        const QJsonObject cloud = s_storeEntries.value(id);
        const QJsonObject loc = localById.value(id);
        const bool inst = localById.contains(id);
        const bool en = loc.value(QStringLiteral("enabled")).toBool();
        const bool run = loc.value(QStringLiteral("running")).toBool();
        const QString status = inst ? (en ? (run ? QStringLiteral("● 运行中")
                                                 : QStringLiteral("○ 已装未运行"))
                                         : QStringLiteral("× 已禁用"))
                                    : QStringLiteral("— 未安装");
        QVariantMap m;
        m.insert(QStringLiteral("id"), id);
        m.insert(QStringLiteral("name"), cloud.value(QStringLiteral("name")).toString(loc.value(QStringLiteral("name")).toString(id)));
        m.insert(QStringLiteral("version"), cloud.value(QStringLiteral("version")).toString(loc.value(QStringLiteral("version")).toString()));
        m.insert(QStringLiteral("author"), cloud.value(QStringLiteral("author")).toString());
        m.insert(QStringLiteral("description"), cloud.value(QStringLiteral("description")).toString());
        m.insert(QStringLiteral("installed"), inst);
        m.insert(QStringLiteral("enabled"), en);
        m.insert(QStringLiteral("status"), status);
        out.append(m);
    }
    return out;
}

// 记录文本（照 openNoiseLogDialog / openStudyLogDialog 的生成口径）
static QString buildNoiseLogText()
{
    const QJsonArray all = readJsonlDir(QStringLiteral("noise_log"));
    if (all.isEmpty())
        return QStringLiteral("还没有噪音记录。\n开启「噪音监测」或「自习模式」后，每次采样都会记到这里。");
    QMap<QString, QVector<double>> byDay;
    for (const QJsonValue &v : all) {
        const QJsonObject o = v.toObject();
        const qint64 ts = o.value(QStringLiteral("ts")).toVariant().toLongLong();
        byDay[QDateTime::fromSecsSinceEpoch(ts).toString(QStringLiteral("yyyy-MM-dd"))]
            .append(o.value(QStringLiteral("db")).toDouble());
    }
    QString out; QTextStream ts(&out);
    ts << QStringLiteral("共 %1 天、%2 条采样\n\n").arg(byDay.size()).arg(all.size());
    for (auto it = byDay.begin(); it != byDay.end(); ++it) {
        const QVector<double> &v = it.value();
        double sum = 0, mn = 1e9, mx = -1e9;
        for (double d : v) { sum += d; if (d < mn) mn = d; if (d > mx) mx = d; }
        ts << QStringLiteral("%1：采样 %2 次，均值 %3 dB，区间 [%4, %5] dB\n")
              .arg(it.key()).arg(v.size())
              .arg(QString::number(sum / v.size(), 'f', 1))
              .arg(QString::number(mn, 'f', 1)).arg(QString::number(mx, 'f', 1));
    }
    return out;
}
static QString buildStudyLogText()
{
    const QJsonArray all = readJsonlDir(QStringLiteral("study_log"));
    if (all.isEmpty())
        return QStringLiteral("还没有自习记录。\n开启「自习模式」后，每次会话（开始/结束/时长/异常次数）都会记到这里。");
    QVector<QJsonObject> sessions;
    QMap<qint64, QJsonObject> pending;
    for (const QJsonValue &v : all) {
        QJsonObject o = v.toObject();
        const QString ev = o.value(QStringLiteral("event")).toString();
        if (ev == QStringLiteral("start")) {
            pending.insert(o.value(QStringLiteral("ts")).toVariant().toLongLong(), o);
        } else if (ev == QStringLiteral("stop")) {
            const qint64 st = o.value(QStringLiteral("startTs")).toVariant().toLongLong();
            QJsonObject s = pending.take(st);
            s.insert(QStringLiteral("endTs"), o.value(QStringLiteral("ts")));
            s.insert(QStringLiteral("durationSec"), o.value(QStringLiteral("durationSec")));
            s.insert(QStringLiteral("alertCount"), o.value(QStringLiteral("alertCount")));
            sessions.append(s);
        }
    }
    QString out; QTextStream ts(&out);
    ts << QStringLiteral("共 %1 次自习会话\n\n").arg(sessions.size());
    for (const QJsonObject &s : sessions) {
        const qint64 st = s.value(QStringLiteral("ts")).toVariant().toLongLong();
        const int dur = s.value(QStringLiteral("durationSec")).toInt();
        ts << QStringLiteral("%1：时长 %2 分 %3 秒，异常录制 %4 次（阈值 %5 dB）\n")
              .arg(QDateTime::fromSecsSinceEpoch(st).toString(QStringLiteral("yyyy-MM-dd HH:mm")))
              .arg(dur / 60).arg(dur % 60)
              .arg(s.value(QStringLiteral("alertCount")).toInt())
              .arg(QString::number(s.value(QStringLiteral("thresholdDb")).toDouble(), 'f', 0));
    }
    return out;
}

void agentUiFill(AgentUi *ui)
{
    ui->m_appVersion = QString::fromLatin1(kAppVersion);
    ui->m_uid = g_uid;
    ui->m_cloudUrl = qEnvironmentVariable("STE_QT_WS_URL");
    ui->m_connState = g_registered ? QStringLiteral("已连接（云端已确认）")
                                   : QStringLiteral("未连接 / 正在重连");
    ui->m_dataDir = qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong");
    ui->m_logPath = g_logPath;

    // 升级状态四态 + canUpgrade（照 openInfoDialog 的口径，一字不差）
    const QString cur = QString::fromLatin1(kAppVersion);
    ui->m_hasNewer = g_updateChecked && !g_latestVer.isEmpty() && compareVersion(g_latestVer, cur) > 0;
    ui->m_canUpgrade = ui->m_hasNewer && !g_updateUrl.isEmpty() && g_updateSha.length() == 64;
    if (!g_updateChecked)            ui->m_latestText = QStringLiteral("还没查到（等云端回执）");
    else if (g_latestVer.isEmpty())  ui->m_latestText = QStringLiteral("云端还没发布过版本");
    else if (ui->m_hasNewer)         ui->m_latestText = QStringLiteral("有新版本 v%1 可升级").arg(g_latestVer);
    else                             ui->m_latestText = QStringLiteral("已是最新（云端 v%1）").arg(g_latestVer);
    ui->m_updateNotes = g_updateNotes;

    ui->m_otaModeText = g_updateMandatory
        ? QStringLiteral("强制：到点自动装（装完重启一次）")
        : QStringLiteral("可选：只有点「立即升级」才装");
    ui->m_otaPaused = otaPaused();
    ui->m_otaAutoText = ui->m_otaPaused
        ? QStringLiteral("已暂停（点「恢复自动升级」解除）")
        : QStringLiteral("到点自动装");
    {
        const QJsonObject st = otaLoadState();
        const QString lastRes = st.value(QStringLiteral("lastResult")).toString();
        if (lastRes.isEmpty())
            ui->m_otaLastText = QStringLiteral("（这台设备还没自动升级过）");
        else if (lastRes == QLatin1String("ok"))
            ui->m_otaLastText = QStringLiteral("成功（已升到 v%1）")
                                    .arg(st.value(QStringLiteral("lastVer")).toString());
        else
            ui->m_otaLastText = QStringLiteral("失败 ×%1：%2")
                .arg(st.value(QStringLiteral("failCount")).toInt(0))
                .arg(st.value(QStringLiteral("lastError")).toString());
        const qint64 nextAt = st.value(QStringLiteral("nextAt")).toVariant().toLongLong();
        ui->m_otaNextText = nextAt > QDateTime::currentMSecsSinceEpoch()
            ? QDateTime::fromMSecsSinceEpoch(nextAt).toString(QStringLiteral("MM-dd HH:mm"))
            : QStringLiteral("立刻（有网就试）");
    }

    ui->m_notifyEnabled   = AgentSettings::notifyEnabled();
    ui->m_voiceEnabled    = AgentSettings::voiceEnabled();
    ui->m_noiseEnabled    = AgentSettings::noiseEnabled();
    ui->m_islandIdle      = AgentSettings::islandIdle();
    ui->m_lightMode       = AgentSettings::lightMode();
    ui->m_pluginIslandPush = AgentSettings::pluginIslandPush();   // P0-0：第三方推岛开关
    ui->m_autostart       = agentAutostartEnabled();
    ui->m_desktopShortcut = agentDesktopShortcutExists();

    // ── A-3 第二批：自检 / 插件 / 记录 / 播放时段 ──
    ui->m_selfCheckItems = buildSelfCheckItems();
    ui->m_plugins        = buildPluginList();
    ui->m_modules        = ModuleHost::instance()->snapshot().toVariantList();
    ui->m_noiseLogText   = buildNoiseLogText();
    ui->m_studyLogText   = buildStudyLogText();
    ui->m_playScheduleEnabled = AgentSettings::playScheduleEnabled();
    ui->m_playWindowsText     = AgentSettings::playWindows().join(QStringLiteral("\n"));
    {
        const QString cur = (IslandOverlay::instance() && IslandOverlay::instance()->mediaOn())
            ? QStringLiteral("正在播放：%1").arg(IslandOverlay::instance()->mediaTitle())
            : QStringLiteral("当前无媒体在播");
        ui->m_playStateText = QStringLiteral("当前状态：%1 · 时段内允许播放：%2")
                                  .arg(cur).arg(MediaSession::isPlayAllowedNow() ? QStringLiteral("是") : QStringLiteral("否"));
    }
}

void agentUiWire(AgentUi *ui)
{
    agentUiFill(ui);

    // 打开面板 / QML 主动刷新：重读快照（连接/升级状态是"点开那一刻"的）。
    QObject::connect(ui, &AgentUi::refreshRequested, ui, [ui] {
        agentUiFill(ui);
        ui->publish();
    });

    // 开关：真实写（QSettings / 岛联动 / 提权），写完**重读快照再 publish**
    // —— 提权被拒时界面自动回弹，不会"勾上了、其实没改"。
    QObject::connect(ui, &AgentUi::switchChanged, ui,
                     [ui](const QString &key, bool v) {
        if (key == QLatin1String("notifyEnabled"))       AgentSettings::setNotifyEnabled(v);
        else if (key == QLatin1String("voiceEnabled"))   AgentSettings::setVoiceEnabled(v);
        else if (key == QLatin1String("noiseEnabled"))   AgentSettings::setNoiseEnabled(v);
        else if (key == QLatin1String("islandIdle")) {
            AgentSettings::setIslandIdle(v);
            if (IslandOverlay::instance()) IslandOverlay::instance()->setIdleEnabled(v);
        } else if (key == QLatin1String("lightMode")) {
            AgentSettings::setLightMode(v);
            if (IslandOverlay::instance()) IslandOverlay::instance()->setLightMode(v);
        } else if (key == QLatin1String("pluginIslandPush")) {
            // P0-0：只落 QSettings —— pluginhost 每次推岛都现问 AgentSettings（没有缓存），
            // 所以下一次插件推岛即生效，不需要在这里通知宿主。
            AgentSettings::setPluginIslandPush(v);
            qInfo("[agent-shell] 第三方推岛开关 → %s", v ? "开" : "关");
        } else if (key == QLatin1String("autostart")) {
            if (!agentSetAutostart(v))
                qWarning("[agent-shell] 开机自启没改成（多半 UAC 被拒）—— 界面将回弹真实状态");
        } else if (key == QLatin1String("desktopShortcut")) {
            if (!agentSetDesktopShortcut(v))
                qWarning("[agent-shell] 桌面快捷方式没改成（多半 UAC 被拒）—— 界面将回弹真实状态");
        } else if (key == QLatin1String("playScheduleEnabled")) {
            AgentSettings::setPlayScheduleEnabled(v);
        }
        agentUiFill(ui);
        ui->publish();
    });

    QObject::connect(ui, &AgentUi::openLogDirRequested, ui, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(g_logPath).absolutePath()));
    });
    QObject::connect(ui, &AgentUi::openDataDirRequested, ui, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(
            qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong")));
    });
    // 「立即升级」：走的就是云端下发 self_update 用的同一条链路（含四条护栏）。
    QObject::connect(ui, &AgentUi::upgradeNowRequested, ui, [ui] {
        if (!ui->canUpgrade()) return;
        QJsonObject p;
        p.insert(QStringLiteral("url"),     g_updateUrl);
        p.insert(QStringLiteral("sha256"),  g_updateSha);
        p.insert(QStringLiteral("version"), g_latestVer);
        qInfo("[agent-qt] 用户在 QML 控制面板点了「立即升级」→ v%s", qPrintable(g_latestVer));
        startSelfUpdate(QStringLiteral("manual"), p);
        agentUiFill(ui);
        ui->publish();
    });
    // 紧急刹车：往数据目录扔一个空文件就停手（删掉即恢复）。
    QObject::connect(ui, &AgentUi::toggleOtaPauseRequested, ui, [ui] {
        if (otaPaused()) {
            QFile::remove(otaPausePath());
            qInfo("[agent-qt] 操作员恢复了本机自动升级");
        } else {
            QDir().mkpath(otaBaseDir());
            QFile f(otaPausePath());
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write(QStringLiteral("paused by operator %1\n")
                            .arg(QDateTime::currentDateTime().toString(Qt::ISODate)).toUtf8());
                f.flush();
            }
            qWarning("[agent-qt] ⛔ 操作员暂停了本机自动升级（%s）", qPrintable(otaPausePath()));
        }
        agentUiFill(ui);
        ui->publish();
    });

    // ── A-3 第二批：自检 / 插件市场 / 记录 / 播放时段 ──────────────────
    QObject::connect(ui, &AgentUi::selfCheckRequested, ui, [ui] {
        agentUiFill(ui);
        ui->publish();
    });
    // 一键修复（判据与 doFix 一字不差）：配置缺/班级缺 → 配置向导；否则立即重连。
    QObject::connect(ui, &AgentUi::selfCheckFixRequested, ui, [ui] {
        if (!g_configured || g_classCode.isEmpty()) {
            openConfigDialog();
        } else {
            g_backoffMs = 1000;
            if (g_ws && g_ws->state() != QAbstractSocket::UnconnectedState) {
                g_reconnectNow = true; g_ws->close();
            } else {
                updateTray(TrayState::Disconnected, QStringLiteral("正在重连…"));
                connectNow();
            }
        }
        agentUiFill(ui);
        ui->publish();
    });
    QObject::connect(ui, &AgentUi::pluginsRefreshRequested, ui, [ui] {
        QNetworkAccessManager *nam = new QNetworkAccessManager(ui);
        QNetworkRequest req((QUrl(pluginRegistryUrl())));
        QNetworkReply *rep = nam->get(req);
        QObject::connect(rep, &QNetworkReply::finished, ui, [ui, rep, nam] {
            const QByteArray data = rep->readAll();
            rep->deleteLater(); nam->deleteLater();
            if (rep->error() == QNetworkReply::NoError) {
                const QJsonArray arr = QJsonDocument::fromJson(data).array();
                for (const QJsonValue &v : arr)
                    s_storeEntries.insert(v.toObject().value(QStringLiteral("id")).toString(), v.toObject());
            }   // 离线也能看本地已装（照 openPluginStore 口径）
            agentUiFill(ui);
            ui->publish();
        });
        agentUiFill(ui);   // 先给本地快照
        ui->publish();
    });
    // ── 内置模块层：刷新 / 启停（不可卸载——它编译在 agent 里）──
    QObject::connect(ui, &AgentUi::modulesRefreshRequested, ui, [ui] {
        agentUiFill(ui);
        ui->publish();
    });
    QObject::connect(ui, &AgentUi::moduleEnableRequested, ui, [ui](const QString &id, bool enable) {
        ModuleHost::instance()->setEnabled(id, enable);
        agentUiFill(ui);
        ui->publish();
    });
    // 安装：已装 ⇒ 启用；未装 ⇒ 下载 zip → 解压 → installAndLaunch（照 openPluginStore）。
    QObject::connect(ui, &AgentUi::pluginInstallRequested, ui, [ui](const QString &id) {
        if (!g_pluginHost) return;
        bool installed = false;
        for (const QJsonValue &v : g_pluginHost->snapshot())
            if (v.toObject().value(QStringLiteral("id")).toString() == id) { installed = true; break; }
        if (installed) { g_pluginHost->enable(id, true); agentUiFill(ui); ui->publish(); return; }
        const QString url = s_storeEntries.value(id).value(QStringLiteral("downloadUrl")).toString();
        if (url.isEmpty()) {
            qWarning("[agent-shell] 插件 %s 暂无可下载包（需先在商店后台上传）", qPrintable(id));
            return;
        }
        QNetworkAccessManager *nam = new QNetworkAccessManager(ui);
        QNetworkReply *rep = nam->get(QNetworkRequest(QUrl(url)));
        QObject::connect(rep, &QNetworkReply::finished, ui, [ui, rep, nam, id] {
            const QByteArray data = rep->readAll();
            rep->deleteLater(); nam->deleteLater();
            if (rep->error() != QNetworkReply::NoError) {
                qWarning("[agent-shell] 插件 %s 下载失败", qPrintable(id));
                return;
            }
            const QString tmp = QDir::tempPath() + QStringLiteral("/stelarith-plugin-%1.zip").arg(id);
            { QFile f(tmp); if (f.open(QIODevice::WriteOnly)) { f.write(data); f.close(); } }
            const QString dir = g_pluginHost->pluginDir() + QLatin1Char('/') + id;
            QDir().mkpath(dir);
            // #368 需求①：进程内 powershell 解压静默，挂 CREATE_NO_WINDOW
            {
                QProcess p;
                stelarithHideChildConsole(p);
                p.start(QStringLiteral("powershell"),
                        QStringList() << QStringLiteral("-Command")
                                      << QStringLiteral("Expand-Archive -Force '%1' '%2'").arg(tmp, dir));
                p.waitForFinished(-1);
            }
            g_pluginHost->installAndLaunch(id);
            qInfo("[agent-shell] 已安装并启动插件：%s", qPrintable(id));
            agentUiFill(ui);
            ui->publish();
        });
    });
    QObject::connect(ui, &AgentUi::pluginEnableRequested, ui, [ui](const QString &id, bool enable) {
        if (g_pluginHost) g_pluginHost->enable(id, enable);
        agentUiFill(ui);
        ui->publish();
    });
    QObject::connect(ui, &AgentUi::pluginUninstallRequested, ui, [ui](const QString &id) {
        if (g_pluginHost) g_pluginHost->uninstall(id);
        agentUiFill(ui);
        ui->publish();
    });
    QObject::connect(ui, &AgentUi::pluginSampleInstallRequested, ui, [ui] {
        if (!g_pluginHost) return;
        const QString src = QCoreApplication::applicationDirPath() + QStringLiteral("/plugins/samples/echo");
        const QString dst = g_pluginHost->pluginDir() + QStringLiteral("/echo");
        if (!QFile::exists(src)) {
            qWarning("[agent-shell] 未找到内置示例（plugins/samples/echo）");
            return;
        }
        QDir().mkpath(dst);
        for (const QString &f : QStringList() << QStringLiteral("manifest.json") << QStringLiteral("echo.py"))
            QFile::copy(src + QLatin1Char('/') + f, dst + QLatin1Char('/') + f);
        g_pluginHost->installAndLaunch(QStringLiteral("echo"));
        qInfo("[agent-shell] 已安装示例插件 echo");
        agentUiFill(ui);
        ui->publish();
    });
    QObject::connect(ui, &AgentUi::noiseFolderRequested, ui, [] { openFolder(QStringLiteral("noise_log")); });
    QObject::connect(ui, &AgentUi::studyFolderRequested, ui, [] { openFolder(QStringLiteral("study_log")); });
    QObject::connect(ui, &AgentUi::playScheduleSaveRequested, ui, [ui] {
        AgentSettings::setPlayScheduleEnabled(ui->playScheduleEnabled());
        QStringList list;
        for (const QString &line : ui->playWindowsText().split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QString s = line.trimmed();
            if (!s.isEmpty()) list.append(s);
        }
        AgentSettings::setPlayWindows(list);
        qInfo("[play] 🎵 播放时段已保存（QML 面板）：enabled=%d, windows=%d",
              ui->playScheduleEnabled() ? 1 : 0, list.size());
        agentUiFill(ui);
        ui->publish();
    });
}

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

    // 2026-10-12 修（跨端对齐报告 #2）：以前**没设**应用名/组织名 → QSettings 和
    // QStandardPaths::writableLocation(AppLocalDataLocation) 定位不到稳定落点，
    // IslandOverlay::historyPath() 里那种"返回空→手拼兜底 xingjikong/"的怪癖就是它造成的。
    // 与管理端 main.cpp 同款三行（见管理端为什么这么写的注释）。QLockFile 路径也受益。
    QCoreApplication::setApplicationName(QStringLiteral("Stelarith Agent"));
    // ⚠️ 2026-10-13 修：此前**全仓从未调用 setApplicationVersion()** ⇒ applicationVersion()
    //    恒为空串，whale_audio.cpp 里"临时文件名带版本"那条修复（stelarith_whale_v<ver>_*.wav）
    //    实际拼出来是 `_v_`，跨版本不变，升级后仍复用旧音效 —— 那条修复等于没生效。这里补上。
    QCoreApplication::setApplicationVersion(QString::fromLatin1(kAppVersion));
    QCoreApplication::setOrganizationName(QStringLiteral("Stelarith"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("stelarith.local"));

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
    // 2026-10-21（审查 #11）：原来用 qEnvironmentVariable(key, default) —— 变量存在但
    // 为空字符串时返回空（不是 default），部署脚本写了 `set STE_QT_SHOT_DIR=` 就会把
    // 截图路径顶成空、图写到盘根。与本文件其他目录统一走 envOr（空串回落默认值）。
    g_shotDir = envOr("STE_QT_SHOT_DIR", dataDir + QStringLiteral("/shots"));
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
            QStringLiteral("被控端已在运行。\n本次启动自动退出，请勿重复启动。\n（%1）")
                .arg(g_singleWhy));
        return 0;
    }

    // 关键：常驻托盘程序**不能**在"最后一个窗口关闭时退出"。
    // 默认 quitOnLastWindowClosed=true —— 配置窗是本进程唯一的窗口，老师一点「以后再说」/
    // 保存关窗，整个被控端就跟着退了（托盘也一起没），比不配置还糟。必须关掉这个默认行为。
    app.setQuitOnLastWindowClosed(false);

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

    // （dataDir / g_shotDir / g_logPath 的初始化**已移到 main() 最前面** —— 必须早于单例守卫，
    //   否则守卫拦下第二次启动时那行"为什么被拦"的日志会被 appendLogFile 静默丢弃。见上面那段。）

    // 自启时计划任务**直接拉 exe**（不再经 bat）→ 配置得由自己从 agentEnvPath()（数据目录）读。
    // 2026-10-03 实测：计划任务里套一层 `cmd /c start-agent.bat` 会**卡住不退出**，
    // 任务永远 Running、被控端根本没被拉起。
    migrateAgentEnvFromInstallDir();   // 老机器：把安装目录里的旧配置搬进数据目录（幂等，无则不动）

    // ── 系统媒体识别（2026-10-22）──────────────────────────────────────
    // 起一条 MTA 工作线程轮询 SMTC，回答「这台机器现在真在放什么」。
    // 与 VoiceHub 那套（bridge.media*，语义是"我们的排期播放器播到哪"）互补：
    // 学生自己开个网易云，VoiceHub 一无所知，SMTC 能看见。
    // ⚠️ 必须在这里（日志 handler 已装之后）：失败原因要落 agent-qt.log，
    //    否则装到没起 WMP/SMTC 服务的老机器上，全是"看着像没实现"的黑洞。
    SmtcMonitor::instance()->start();
    qInfo("[agent-qt] SMTC 系统媒体识别已启动（800ms 轮询，独立 MTA 线程）");    const int envN = loadEnvFile(agentEnvPath());
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
        // U5-A3（2026-10-16）：QML 控制面板（MD3 界面，两端同 shell 的被控端入口）。
        // 逐页迁移 QDialog 的过渡期：旧菜单项并存，迁完一批撤一批。
        auto *actPanel = menu->addAction(QStringLiteral("控制面板…"));
        QObject::connect(actPanel, &QAction::triggered, &app, [] { AgentShell::openPanel(); });
        // 首次运行配置窗的入口：老师在界面里填云端地址/令牌/设备名，写回数据目录的 agent.env。
        auto *actConfig = menu->addAction(QStringLiteral("配置…"));
        QObject::connect(actConfig, &QAction::triggered, &app, [] { openConfigDialog(); });
        // 首次启动引导（2026-10-20）：欢迎 → 选绑定方式 → 扫码/输码 → 确认班级。
        // 首运行会自动弹一次；这里留手动入口（跳过的机器想补绑、或换班级时用）。
        auto *actOobe = menu->addAction(QStringLiteral("绑定引导…"));
        QObject::connect(actOobe, &QAction::triggered, &app, [] { openOobeWizard(); });
        // 设置与信息（2026-10-06）：一眼看清 版本 / uid / 云端地址 / 连接状态 / 能不能升级
        auto *actInfo = menu->addAction(QStringLiteral("设置与信息…"));
        QObject::connect(actInfo, &QAction::triggered, &app, [] { openInfoDialog(); });
        // 配置自检（P0-2 · 2026-10-14）：env 自动判断 + 一键修复
        auto *actSelf = menu->addAction(QStringLiteral("配置自检…"));
        QObject::connect(actSelf, &QAction::triggered, &app, [] { runSelfCheck(); });
        // 插件市场（P2 · 2026-10-14）：浏览/安装/启停第三方能力（你 P0 反思里的「第三方能力窗口」）
        auto *actPlugin = menu->addAction(QStringLiteral("插件市场…"));
        QObject::connect(actPlugin, &QAction::triggered, &app, [] { openPluginStore(); });
        // P3：三张长期记录 / 配置入口
        auto *actNoise = menu->addAction(QStringLiteral("噪音记录…"));
        QObject::connect(actNoise, &QAction::triggered, &app, [] { openNoiseLogDialog(); });
        auto *actStudy = menu->addAction(QStringLiteral("自习记录…"));
        QObject::connect(actStudy, &QAction::triggered, &app, [] { openStudyLogDialog(); });
        auto *actPlay  = menu->addAction(QStringLiteral("播放时段…"));
        QObject::connect(actPlay, &QAction::triggered, &app, [] { openPlayScheduleDialog(); });
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
        // P0-1（2026-10-14）：空闲半胶囊默认开；环境变量 STE_QT_ISLAND_IDLE=0 可硬关，否则读本地设置。
        {
            const QString envIdle = qEnvironmentVariable("STE_QT_ISLAND_IDLE");
            const bool idle = envIdle.isEmpty() ? AgentSettings::islandIdle()
                                                : (envIdle != QStringLiteral("0"));
            isl->setIdleEnabled(idle);
        }
        isl->setLightMode(AgentSettings::lightMode());   // 浅色主题开关（P0-1）
        isl->ensureIdle();

        // ── U5-A3 验证开关（2026-10-16）：STE_QT_SHELL_AUTOOPEN=1 → 启动 1.5s 后
        //    自动开一次 QML 控制面板。面板是懒建的（不点托盘不加载 QML），
        //    无人值守验证 QML 真实加载全靠它；日常不设即零影响。
        if (qEnvironmentVariable("STE_QT_SHELL_AUTOOPEN") == QStringLiteral("1")) {
            QTimer::singleShot(1500, &app, [] {
                AgentShell::openPanel();
                qInfo("[agent-shell] 自动打开控制面板（STE_QT_SHELL_AUTOOPEN=1）");
            });
        }

        // ── #330 形态验证开关（2026-10-17）：STE_QT_ISLAND_DEMO_FORM=banner|mini|mini-nodesc|centered|fullscreen
        //    → 启动 2.5s 后弹一次该形态，12s 自动收回，并把**落定后**的几何打进日志。
        //    为什么需要它：离屏探针的屏幕是 800×800 且**没有任务栏**，Banner 的
        //    "宽 = 可用宽 − 48" 与 Mini 的"躲开底部任务栏"这两件事只有真机能验。
        //    日常不设即零影响（与 STE_QT_SHELL_AUTOOPEN 同款约定）。
        {
            const QString demoForm = qEnvironmentVariable("STE_QT_ISLAND_DEMO_FORM").trimmed();
            if (!demoForm.isEmpty()) {
                QTimer::singleShot(2500, &app, [demoForm] {
                    IslandOverlay *demoIsl = IslandOverlay::instance();
                    IslandOverlay::Form f = IslandOverlay::Form::Mini;
                    if (demoForm == QStringLiteral("banner"))          f = IslandOverlay::Form::Banner;
                    else if (demoForm == QStringLiteral("centered"))    f = IslandOverlay::Form::Centered;
                    else if (demoForm == QStringLiteral("fullscreen"))  f = IslandOverlay::Form::Fullscreen;

                    // 2026-10-17：centered / fullscreen 两档此前在 QML 里没有版式
                    //（落进胶囊分支 ⇒ 圆角/排版全错），补了面之后要能在真机上单独拉起来看。
                    // centered 用 inform（琥珀顶条 + 琥珀图标），fullscreen 用 urgent（红底）——
                    // 正好覆盖两条 severity 路径。
                    if (demoForm == QStringLiteral("centered")) {
                        demoIsl->showIsland(QStringLiteral("课件已下发"),
                                            QStringLiteral("数学·第三章 二次函数.pdf · 3.2 MB"),
                                            QStringLiteral("download"), 12000, f);
                        demoIsl->setSeverity(QStringLiteral("inform"));
                    } else if (demoForm == QStringLiteral("fullscreen")) {
                        demoIsl->showIsland(QStringLiteral("考试即将开始"),
                                            QStringLiteral("请把与考试无关的物品放到讲台前，桌面只留笔和草稿纸"),
                                            QStringLiteral("bell"), 12000, f);
                        demoIsl->setSeverity(QStringLiteral("urgent"));
                    } else if (demoForm == QStringLiteral("banner")) {
                        demoIsl->showIsland(
                            QStringLiteral("全校广播：今天下午 3 点开始消防演练，请各班按指定路线撤离到操场集合"),
                            QStringLiteral("学生处 · 全程保持安静，到操场后按班级列队"),
                            QStringLiteral("speaker"), 12000, f);
                    } else if (demoForm == QStringLiteral("mini-nodesc")) {
                        demoIsl->showIsland(QStringLiteral("在播"), QString(),
                                            QStringLiteral("music"), 12000, f);
                    } else {
                        demoIsl->showIsland(QStringLiteral("正在接收 课件.pdf"),
                                            QStringLiteral("42%"), QStringLiteral("download"), 12000, f);
                    }
                    // ⚠️ 几何要等**动画落定**再读：showIsland 走 animateGeometry，
                    //    立刻读 geometry() 拿到的是上一条的旧几何（探针也踩过同款）。
                    // ⚠️ 接收者用 qApp 而不是 &app：`app` 是 main 的局部变量，
                    //    外层 lambda 没捕获它 ⇒ 内层再捕获也拿不到（C2326 实测）。
                    QTimer::singleShot(1200, qApp, [demoForm] {
                        const QRect g = IslandOverlay::instance()->geometry();
                        const QScreen *sc = QGuiApplication::primaryScreen();
                        const QRect ag = sc ? sc->availableGeometry() : QRect();
                        // 顺手把**可用区**也打出来：Banner 的宽度与 Mini 的右下角都
                        // 按 availableGeometry 算（要躲任务栏），离屏探针没有任务栏，
                        // 真机核对时没有这个基准就只能靠反推（本轮就因此对不上号）。
                        qInfo("[island-demo] 形态=%s 落定几何=%dx%d@(%d,%d) 可用区=%dx%d@(%d,%d) dpr=%.2f",
                              demoForm.toUtf8().constData(), g.width(), g.height(), g.x(), g.y(),
                              ag.width(), ag.height(), ag.x(), ag.y(),
                              sc ? sc->devicePixelRatio() : 0.0);
                    });
                });
            }
        }
    } else {
        qWarning().noquote() << QStringLiteral("[agent-qt] WARN 系统托盘不可用（Session0？）—— 进程照跑，但没有可见入口");
    }

    // 2026-10-17：OTA 周期复查（不再只依赖 registered 回执那一次）。
    // ⚠️ 必须放在托盘分支**之外**：无托盘的 Session0/服务化环境照样要能查更新
    //    （放在里面时离屏实测根本没启动 —— 托盘不可用就整段跳过）。
    otaStartPeriodicCheck();

    // ── 通知体系接线（2026-10-09 · DeepSeek Harness）─────────────────────
    // 循环提醒：展示由 notifyFromParams 的 loop 分支走灵动岛（含确认按钮，
    // 点确认 → loopConfirm 回调 → g_loopReminder.confirm 终止）。这里只收终止日志。
    QObject::connect(&g_loopReminder, &LoopReminder::expired, &app,
        [](const QString &id, const QString &reason) {
            qInfo("[agent-qt] 循环提醒已终止 %s（%s）", id.toUtf8().constData(),
                  reason.toUtf8().constData());
        });
    // 语音通话：mic 采集帧 → v1 音频帧发回云端（云端 duplex 单播回发起老师）
    // 2026-10-21（P1）：先编码成 Opus（640B PCM → ~40B 包，出口带宽降一个数量级）。
    // 编码失败/未就绪 → **回落裸 PCM**（mime=audio/pcm），绝不能因为编码器问题把语音弄哑。
    QObject::connect(&g_microphone, &Microphone::pcmReady, &app,
        [](const QByteArray &pcm) {
            if (!g_ws || g_ws->state() != QAbstractSocket::ConnectedState) return;
            if (!g_opusEnc.encoderReady())
                g_opusEnc.initEncoder(16000, 1, 16000);
            bool opus = false;
            QByteArray payload = pcm;
            if (g_opusEnc.encoderReady() && pcm.size() >= 2) {
                const auto pkt = g_opusEnc.encode(
                    reinterpret_cast<const int16_t *>(pcm.constData()), pcm.size() / 2);
                if (!pkt.empty()) {
                    payload = QByteArray(reinterpret_cast<const char *>(pkt.data()),
                                         static_cast<int>(pkt.size()));
                    opus = true;
                }
            }
            g_ws->sendBinaryMessage(makeAudioFrameBytes(payload, opus));
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

    // ── 组件6 云端上报旁路注入（2026-10-15）────────────────────────
    // 灵动岛的互动上报（祝福/点赞/点歌）经这个 lambda 出海：岛不认识 g_ws，
    // 只认 IslandOverlay::CloudReporter；依赖方向保持 main → island。
    // payload 的 type 与云端 interact.js 的入向消息一一对应：
    //   blessing → {text} / feedback → {kind,value} / song_request → {title,artist,source}
    // 班级码由云端按 uid 自己查（knownDevices 表，自报值不可信），这里不带。
    // 发送失败（ws 未连/已断）由 QWebSocket 自己记状态，这里不重试不提示——
    // 端侧先行：本地体验（弹幕已滚/赞已点）不因云端欠收而回退。
    IslandOverlay::instance()->setCloudReporter(
        [](const QString &type, const QJsonObject &payload) {
            if (!g_ws) return;
            g_ws->sendTextMessage(makeEnvelope(type, payload));
        });

    // ── 班级域缩略图上报（#459 · 与云端 class-events.js #458 配对）─────
    // 班级工作台设备网格的画面源：定时抓小图 → JPEG(base64) → class-event
    // {kind:'thumbnail'} → 云端按班 fanout 给订阅者。纪律：
    //   · payload 不带 classCode（伪冒防护：班级归属云端只认 knownDevices 表）；
    //   · 未绑班 / WS 未连时 tick 直接跳过（模块内不抓屏，零空转）；
    //   · 信封仍走 makeEnvelope（cloud_proto.h 的唯一封装修辞），模块不认识协议。
    ThumbnailReporter::setSender([](const QJsonObject &payload) {
        if (!g_ws || g_ws->state() != QAbstractSocket::ConnectedState) return;
        g_ws->sendTextMessage(makeEnvelope(QStringLiteral("class-event"), payload));
    });
    ThumbnailReporter::setReadyCheck([] {
        return !g_classCode.isEmpty()
            && g_ws && g_ws->state() == QAbstractSocket::ConnectedState;
    });
    // 定时器归 app（主线程单线程跑 tick：DXGI timeout=0 非阻塞 + 首帧一次性
    // ~200ms 可接受）。间隔 env 现读 ⇒ 改 STE_THUMB_INTERVAL_MS 下拍生效。
    auto *thumbTimer = new QTimer(&app);
    QObject::connect(thumbTimer, &QTimer::timeout, &app, [thumbTimer] {
        thumbTimer->setInterval(ThumbnailReporter::intervalMs());
        ThumbnailReporter::tick();
    });
    thumbTimer->start(ThumbnailReporter::intervalMs());

    QObject::connect(g_ws, &QWebSocket::connected, &app, [] {
        qInfo("[agent-qt] 已连上云端（等待 register 回执）");
        g_backoffMs = 1000;      // 连上了就重置退避
        sendRegister();
    });

    // 云端踢人时给不给原因，被控端这边都得说清楚，否则运维只能猜。
    // （Qt 6.8 的 aboutToClose() 不带参数，拿不到 close code，所以退到 socket 错误串 + 上下文判断。）
    QObject::connect(g_ws, &QWebSocket::disconnected, &app, [] {
        g_registered = false;
        // 采集页回收兜底的起点只认**第一次**断连：每退避重连失败一次就把起点刷新一次的话，
        // 倒计时会被无限往后推，看门狗永远走不到头（实测断线 26 秒、重连失败 3 次就没触发过）。
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
        g_lastOfflineAt = nowOffline;   // 2026-10-12：记断线时刻，恢复时报「已重新连接」
        if (!g_islandOfflineShown && nowOffline - g_lastOfflineToastMs > 20000) {
            g_islandOfflineShown = true;
            g_lastOfflineToastMs = nowOffline;
            islandShow(QStringLiteral("连接中断"), QStringLiteral("正在自动重连…"), QStringLiteral("triangle-alert"), 3000);
        }
        qWarning().noquote() << QStringLiteral("[agent-qt] FAIL: 云端断开（%1 帧已发）").arg(g_frameBytes);
        if (!g_registered) {   // 连上了却从没拿到 registered 就被断开 = 握手/令牌没过
            qWarning().noquote() << QStringLiteral("[agent-qt]   └ 本轮 register 未通过云端校验 —— 先查令牌（STE_QT_WS_TOKEN）与云端 CLOUD_WS_TOKEN 是否一致，再看云端 events.log");
        } else {
            qWarning().noquote() << QStringLiteral("[agent-qt]   └ 网络/服务侧断开，正在退避重连");
        }
        scheduleReconnect();
    });

    QObject::connect(g_ws, &QWebSocket::errorOccurred, &app,
                     [](QAbstractSocket::SocketError) {
                         // disconnected 里统一退避重连，这里只报真实原因
                         qWarning().noquote() << QStringLiteral("[agent-qt] FAIL: 连接出错 —— %1").arg(g_ws->errorString());
                     });

    // 2026-10-14 加（审计 P2）：sslErrors 以前根本没接。Qt 默认 VerifyPeer + 未连接
    // 此信号 = fail-closed，坏证书**本来就不会被放行**（这点一直是安全的）；但
    // sslErrors 之后只走 errorOccurred 报一句笼统的 "SSL handshake failed"，日志
    // 分不清是证书过期、host mismatch 还是自签 —— 教室排查 TLS 问题时很痛。
    // 这里接上**只打日志、绝不调 ignoreSslErrors**，fail-closed 语义不变，只是把原因打细。
    QObject::connect(g_ws, &QWebSocket::sslErrors, &app, [](const QList<QSslError> &errs) {
        // Qt 6 已移除 QSslSocket::SslErrorString / QSslError::sslError()，
        // errorString() 本身就是人类可读的具体原因，够用。
        for (const QSslError &e : errs)
            qWarning().noquote() << QStringLiteral("[agent-qt] TLS 校验失败（已中止，不会放行）：%1")
                .arg(e.errorString());
    });

    QObject::connect(g_ws, &QWebSocket::textMessageReceived, &app, [](const QString &msg) {
        handleControlText(msg);
    });
    // 远控统一 A（2026-10-18）：VncBridge 出向字节 → 打包 vnc-data 帧（direction=up）发主 WS。
    QObject::connect(&VncBridge::instance(), &VncBridge::outBytes, &app, [](const QByteArray &data) {
        if (data.isEmpty()) return;
        if (!g_ws || g_ws->state() != QAbstractSocket::ConnectedState) {
            qInfo("[vnc-probe] up 丢弃 %d 字节（WS 未连）", data.size());
            return;
        }
        // ⚠️ 背压治理（2026-10-18 深夜实证修复）：Raw 增量帧（2560x1440 一批 ~40-400KB）
        //    此前**不过闸直发**，在劣化隧道（~250KB/s）上瞬间灌满上行 ⇒ 心跳迟到 ⇒
        //    云端 30s 踢线 ⇒ 设备掉线画面定格（真机：4 帧后就断）。与 JPEG 帧同一套规矩：
        //    积压超 1MB 丢帧 + 每秒带宽配额，帧可以丢，心跳不能丢。
        if (g_ws->bytesToWrite() > 1024 * 1024) {
            static int vncSkipped = 0;
            if (++vncSkipped % 20 == 1)
                qWarning("[agent-qt] [vnc] WS 积压 %lld KB 超 1MB，丢弃本批 up 字节保心跳（已丢 %d 批）",
                         g_ws->bytesToWrite() / 1024, vncSkipped);
            return;
        }
        // 每秒 200KB 配额窗口（整条链路共用一组记账）
        static qint64 vWinStart = 0;
        static qint64 vWinBytes = 0;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - vWinStart >= 1000) { vWinStart = now; vWinBytes = 0; }
        // 小帧（<1KB，握手/键鼠回执类）不占配额直放，避免被大帧挤死协议字节
        if (data.size() < 1024) {
            g_ws->sendBinaryMessage(makeVncFrameBytes(data));
            qInfo("[vnc-probe] up 已发 %d 字节（小帧免配额）", data.size());
            return;
        }
        // ── 修复①（2026-10-19）：整屏基础图免配额 ────────────────────────────
        // 根因：720p 档全屏首帧 hextile 编码后 ≈275KB，**单批即超** 200KB/s 配额
        //   （vWinBytes=0 时 0+275KB>200KB 也成立）⇒ 被下方配额检查整批丢弃；
        //   而丢帧发生在 bridge 已读走字节之后 ⇒ libvncserver 认为已发送、
        //   **不重发** ⇒ 管理端永远拿不到基础图 ⇒ 全屏空白；静态桌面再无第二帧，
        //   空白是**永久性**的（真机实证：日志有"桥出向 275433 字节"、无"up 已发"，
        //   只剩配额丢弃告警；#358「没有画面」的真根因）。
        // 口径（两条都只为"基础图必须到"，不放松稳态背压）：
        //   · 会话起步 3s 宽限：覆盖 DXGI 首帧 2s 黑帧预算 + RFB 握手，首帧
        //     不管拆成几批都放得过去（仍过上方 1MB 积压闸）；
        //   · 之后仍放行**一个** ≥200KB 的整屏帧（DXGI 首帧踩点到达等边角
        //     情形）——720p hextile 全屏 ≈275KB，增量脏区帧远小于此，≥200KB
        //     基本只可能是整屏帧；一次性（g_vncMegaDone 本会话只豁免一个），
        //     之后的整屏量级帧回到配额（帧可丢的规矩不破）。
        const bool grace = (g_vncSessionStartMs > 0 && now - g_vncSessionStartMs < 3000);
        if (grace || (data.size() >= 200 * 1024 && !g_vncMegaDone)) {
            if (!grace) g_vncMegaDone = true;   // 宽限期内不消耗一次性豁免
            vWinBytes += data.size();           // 这一秒的配额确实被它占了，照常记账
            g_ws->sendBinaryMessage(makeVncFrameBytes(data));
            qInfo("[vnc-probe] up 已发 %d 字节（%s）", data.size(),
                  grace ? "会话起步宽限" : "整屏基础图一次性豁免");
            return;
        }
        if (vWinBytes + data.size() > 200 * 1024) {
            static int vBwSkipped = 0;
            if (++vBwSkipped % 50 == 1)
                qWarning("[agent-qt] [vnc] 本秒 up 流量已到 200KB 配额，丢弃本批保心跳（累计丢 %d 批）", vBwSkipped);
            return;
        }
        vWinBytes += data.size();
        g_ws->sendBinaryMessage(makeVncFrameBytes(data));
        qInfo("[vnc-probe] up 已发 %d 字节", data.size());
    });
    // 语音对讲（2026-10-07）：云端转发来的音频帧（v1 二进制帧，mime=audio/pcm）。
    // 与画面帧区分：画面帧是 agent→云端→viewer 的单向（被控端不接收）；这里只收 audio。
    // 远控统一 A（2026-10-18）：主 WS 还会收到 vnc-data（管理端键鼠/更新请求，direction=down）。
    QObject::connect(g_ws, &QWebSocket::binaryMessageReceived, &app, [](const QByteArray &msg) {
        // v1 帧：[1B 版本][2B headerLen][header JSON][payload]；header.mime 标 audio/pcm
        if (msg.size() < 4 || (uchar)msg[0] != 1) return; // 非 v1 帧忽略
        const int hLen = ((uchar)msg[1] << 8) | (uchar)msg[2];
        if (hLen <= 0 || hLen > 4096 || msg.size() < 3 + hLen) return;
        const QByteArray head = msg.mid(3, hLen);
        const QJsonDocument doc = QJsonDocument::fromJson(head);
        if (!doc.isObject()) return;
        const QString type = doc.object().value(QStringLiteral("type")).toString();
        const QString mime = doc.object().value(QStringLiteral("mime")).toString();
        const QByteArray payload = msg.mid(3 + hLen);
        if (type == QLatin1String("vnc-data")) {
            // 修复③（2026-10-19）：管理端任何 down 字节（键鼠/更新请求/5s 保活）
            // 都刷新"管理端还在"的时刻，泄漏看门狗（reapRemoteControlIfStale）据此
            // 区分"老师在看"与"老师已离开"。保活由管理端 VncClient 每 5s 发一个
            // 1x1 增量 FramebufferUpdateRequest 产生，纯观看不碰键鼠也有流量。
            g_lastVncDownMs = QDateTime::currentMSecsSinceEpoch();
            // 远控统一 A：管理端键鼠/更新请求（direction=down）→ 喂本地 VNC 桥。
            qInfo("[vnc-probe] down 收到 %d 字节", payload.size());
            VncBridge::instance().feedDown(payload);
            return;
        }
        if (mime == QLatin1String("audio/opus") || mime == QLatin1String("audio/pcm")) {
            // P1（2026-10-21）：Opus 包先解成 PCM（16k/mono/20ms）再播。
            // audio/pcm 分支是**滚动升级兜底**（旧管理端仍发裸 PCM）——两种都必须能播，
            // 不能因为对端没升级就变静音。
            QByteArray pcm = payload;
            if (mime == QLatin1String("audio/opus")) {
                if (!g_opusDec.decoderReady()) g_opusDec.initDecoder(16000, 1);
                if (!g_opusDec.decoderReady()) return;
                // 5760 = 120ms@16k 上限：对端万一发来非 20ms 包也不会越界
                const auto out = g_opusDec.decode(
                    reinterpret_cast<const uint8_t *>(payload.constData()),
                    payload.size(), 5760);
                if (out.empty()) return;   // 丢包/坏帧：丢掉这一帧，不阻塞后续
                pcm = QByteArray(reinterpret_cast<const char *>(out.data()),
                                 static_cast<int>(out.size() * sizeof(int16_t)));
            }
            // 语音对讲（设计文档 3.3.6）：老师讲话学生可见（每会话一次，不刷屏）
            if (!g_audioHintShown) {
                g_audioHintShown = true;
                // #98：走灵动岛（顶部，不摊在桌面正中挡课件）。
                // 2026-10-13：时长给 0（不自动关）—— 对讲期间岛要一直开着放频谱（§6.3），
                // 结束由 audio.stop 收（见下）。原来 4000ms 会在讲话中途自己收起。
                islandShow(QStringLiteral("老师正在讲话"),
                           QStringLiteral("老师正在通过语音对讲广播"), QStringLiteral("mic"), 0);
                // 频谱画在展开态 ⇒ 显式展开（已展开时该调用早退，不打断用户）
                IslandOverlay::instance()->setExpanded(true);
            }
            if (!g_audioPlayer.isOpen()) g_audioPlayer.open();
            g_audioPlayer.write(pcm);
            // §6.3 语音频谱：喂给灵动岛在**展开态**画 16 条。
            // 2026-10-13 修 bug「频谱显示到窗口去了」：原实现这里 `new SpectrumWidget()`
            // 无 parent 直接 show() ⇒ 桌面上弹出一个独立顶层窗口（挡课件）。
            // 规格 §6.3 是「展开态显示」⇒ 现由岛自己画，不再有独立窗口。
            IslandOverlay::instance()->feedSpectrum(pcm);
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
    // ⚠️ 2026-10-13：原来无条件 start —— 而 captureAndSend 第一件事就是判
    //    STE_ENABLE_WS_FRAME_PUSH，默认关 ⇒ 每 2 秒空转一次事件循环、什么都不做，
    //    且那份判定落在每一次 tick 里。改为**启动时判定一次**：只有逃生口打开才建 timer。
    // ⚠️ 2026-10-16 生产兜底改造：WS 单帧轮询默认启用（条件反转：只有显式=0 才关）。
    //    captureAndSend 内部已有「RTC connected 就停」的智能降级，frameTimer 常驻即可。
    QTimer *frameTimer = nullptr;   // 供下面"立即触发首帧"用；关停时为 null
    if (qgetenv("STE_ENABLE_WS_FRAME_PUSH") != "0") {
        frameTimer = new QTimer(&app);
        QObject::connect(frameTimer, &QTimer::timeout, &app, [storeFrames] {
            captureAndSend(storeFrames);
        });
        frameTimer->start(kFrameIntervalMs);
        qInfo("[agent-qt] WS 单帧轮询推送已启用（RTC 未连通时兜底；STE_ENABLE_WS_FRAME_PUSH=0 可关）");
    } else {
        qInfo("[agent-qt] WS 单帧轮询推送已停用（STE_ENABLE_WS_FRAME_PUSH=0）");
    }

    // 心跳定时器：独立于抓屏。抓屏失败/按需拉流不推帧时，云端仍要靠它判在线。
    // 间隔先取环境变量，等云端 registered 下发 heartbeatMs 后再改（协议 v1：参数由云端定，两端不各猜）。
    int hbMs = qEnvironmentVariableIntValue("STE_QT_HEARTBEAT_MS");
    if (hbMs <= 0) hbMs = kHeartbeatIntervalMs;
    g_heartbeatMs = hbMs;
    g_hbTimer = new QTimer(&app);
    // 2026-10-11：顺带捡一下"卡死的文件接收会话"——发端中途断线就再不会有 file_done，
    // 目标文件会以一个 0 字节/半截的形态永久留在收件目录里。
    QObject::connect(g_hbTimer, &QTimer::timeout, &app, [] {
        sendHeartbeat();
        reapFileRecvIfStale();
        reapRemoteControlIfStale();   // 修复③：远控会话泄漏看门狗（搭心跳车，零新定时器）
    });
    g_hbTimer->start(g_heartbeatMs);
    qInfo("[agent-qt] 心跳间隔 %d ms（等云端 registered 下发 heartbeatMs 后以其为准；本机当前按 %d ms 无心跳即判离线）",
          g_heartbeatMs, g_timeoutMs);

    // ── TLS 后端自检（2026-10-13 生产事故修复）──────────────────────────────
    // 事故：学生机 DESKTOP-I15OM55 上 Qt 的 availableBackends() 只剩 "cert-only"
    //   ⇒ TLS 完全不可用 ⇒ ① wss:// 连不上（表现为"被控端连不上地址"：日志里
    //   `连接 wss://…` 之后**永远没有** `已连上云端`）② 所有 HTTPS 请求报 "HTTP 0"。
    //   这类故障以前**完全静默** —— 只有一个"连不上"的表象，直到翻出 agent-qt.log
    //   里 oauthbind 那行自检才定位。故启动时大声报一次。
    // Qt 的 TLS 后端是**插件**（安装目录 tls\），windeployqt 默认只部署
    // certonly + schannel、**不拷 openssl 后端**；任一后端加载失败即退化成 cert-only。
    {
        const QStringList be = QSslSocket::availableBackends();
        if (!QSslSocket::supportsSsl()) {
            qWarning("[agent-qt] ⛔ TLS 不可用！可用后端=[%s]。后果：连不上云端 wss、"
                     "所有 HTTPS 请求报 HTTP 0。请检查安装目录 tls\\ 下三个后端是否齐全"
                     "（qcertonlybackend / qschannelbackend / qopensslbackend），"
                     "以及 libssl-3-x64.dll / libcrypto-3-x64.dll 是否存在。",
                     qPrintable(be.join(QLatin1Char(','))));
        } else {
            qInfo("[agent-qt] TLS 就绪：后端=[%s] 运行时=%s",
                  qPrintable(be.join(QLatin1Char(','))),
                  qPrintable(QSslSocket::sslLibraryVersionString()));
        }
    }

    connectNow();
    if (frameTimer) QMetaObject::invokeMethod(frameTimer, "timeout", Qt::QueuedConnection);

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
                const QString txt = QStringLiteral("%1 · %2").arg(name, at.toString("HH:mm"));
                // P1 · 2026-10-14：统一走灵动岛（原 NotifyWindow 已删），本地铃记 fromAdmin=false
                islandShow(QStringLiteral("上课啦"), txt, QStringLiteral("bell"),
                           6000, false, QString(), IslandOverlay::Form::Centered, false);
                speakText(QStringLiteral("上课啦") + QStringLiteral("：") + txt);
                qInfo().noquote() << "[agent-qt] 上课提醒已弹: " << name;
            });
        QObject::connect(clock, &ScheduleClock::periodEnded, &app,
            [](const QString& name, const QTime& at) {
                const QString txt = QStringLiteral("%1 · %2").arg(name, at.toString("HH:mm"));
                islandShow(QStringLiteral("下课啦"), txt, QStringLiteral("bell"),
                           6000, false, QString(), IslandOverlay::Form::Centered, false);
                speakText(QStringLiteral("下课啦") + QStringLiteral("：") + txt);
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
        QTimer::singleShot(700, &app, [] { runSelfCheck(); });   // P0-2：首运行先自检（含一键修复入口）
    }

    // ── 首次启动引导（OOBE Wizard）（2026-10-20 · 用户点单）──────────────────
    // ⚠️ 必须放在上面那个 if 的**外面**：向导不依赖"未配置"，也不依赖托盘
    //   （离屏/无托盘场景照样要能跑 —— 探针就是靠这一点验证的）。
    // 触发条件：**从没走过向导** 且 **还没绑班级**。
    //   · 跳过一次就写 oobe_done.txt ⇒ 之后开机不再弹（"稍后配置"是活路，不是骚扰）；
    //   · 已绑班的机器不弹（升级上来的老机器不该被拦在向导里）。
    // STE_OOBE_FORCE=1：演示/探针强制拉起（与 STE_QT_SHELL_AUTOOPEN 同款约定）。
    // 延迟 3s：等岛/托盘先就位，避免两个窗口同时抢焦点。
    {
        const bool force = qEnvironmentVariable("STE_OOBE_FORCE") == QStringLiteral("1");
        if (force || (!oobeSettled() && g_classCode.isEmpty())) {
            QTimer::singleShot(3000, &app, [force] {
                if (force || (!oobeSettled() && g_classCode.isEmpty())) openOobeWizard();
            });
        }
    }

    // ── P2：插件宿主启动（进程外插件 ABI）──
    // 本地 WS 仅绑 127.0.0.1:26789；agent 暴露的能力经 PluginAgentApi 注入，
    // 插件无法直接调内部符号，只能走 islandShow / notify / settings / 事件订阅。
    g_pluginHost = new PluginHost(qApp);
    g_pluginHost->setAgentVersion(kAppVersion);
    PluginAgentApi api;
    api.islandShow = [](const QString &title, const QString &desc, const QString &icon,
                        int form, bool fromAdmin, int durationMs) {
        islandShow(title, desc, icon, durationMs, false, QString(),
                   (IslandOverlay::Form)form, fromAdmin);
    };
    api.notify = [](const QJsonObject &params) { notifyFromParams(params); };
    // P0-0（2026-10-17）：插件推的通知用「插件调用那一刻」当发生时刻（见 islandShowAt）。
    // 传进来的 title 已带「[插件名] 」前缀、长度与频率已由 PluginHost::handleIslandShow 治理过；
    // fromAdmin 恒 false —— 插件不是管理端，历史排序不能被它污染。
    api.islandShowAt = [](const QString &title, const QString &desc, const QString &icon,
                          int form, bool fromAdmin, int durationMs, qint64 noticeAtMs) {
        // form 越界（插件给 7、-1 之类）一律回落胶囊，别让 std::function 里出现非法枚举值。
        const auto f = (form >= int(IslandOverlay::Form::Capsule) && form <= int(IslandOverlay::Form::Mini))
            ? static_cast<IslandOverlay::Form>(form) : IslandOverlay::Form::Capsule;
        islandShowAt(title, desc, icon, durationMs, f, fromAdmin, noticeAtMs);
    };
    // 第三方推岛开关（设置页）：pluginhost 每次推岛前问一次，关掉即静默丢弃并计数。
    api.senderPriorityLimit = []() { return AgentSettings::pluginIslandPush(); };
    // 沙箱验证：插件回包里带岛的状态快照（只读）。同机并存生产实例时，截图分不清是哪个进程的岛。
    api.islandSnapshot = []() { return IslandOverlay::instance()->historySnapshotJson(); };
    api.settingsGet = [](const QString &key) -> QJsonValue {
        if (key == QStringLiteral("noiseEnabled"))   return AgentSettings::noiseEnabled();
        if (key == QStringLiteral("voiceEnabled"))   return AgentSettings::voiceEnabled();
        if (key == QStringLiteral("notifyEnabled"))  return AgentSettings::notifyEnabled();
        if (key == QStringLiteral("islandIdle"))     return AgentSettings::islandIdle();
        if (key == QStringLiteral("lightMode"))      return AgentSettings::lightMode();
        return QJsonValue();
    };
    api.settingsSet = [](const QString &key, const QJsonValue &v) {
        const bool on = v.toBool();
        if (key == QStringLiteral("noiseEnabled"))       AgentSettings::setNoiseEnabled(on);
        else if (key == QStringLiteral("voiceEnabled"))  AgentSettings::setVoiceEnabled(on);
        else if (key == QStringLiteral("notifyEnabled")) AgentSettings::setNotifyEnabled(on);
        else if (key == QStringLiteral("islandIdle"))    AgentSettings::setIslandIdle(on);
        else if (key == QStringLiteral("lightMode"))     AgentSettings::setLightMode(on);
    };
    api.subscribeEvents = [](const QString &, const QStringList &) { /* 订阅登记在宿主侧，事件经 forwardEvent 推送 */ };
    g_pluginHost->setAgentApi(api);
    if (g_pluginHost->start()) {
        g_pluginHost->loadEnabled();   // 启动 QSettings 中已启用的插件
    }

    // ── 内置模块层启动（2026-10-16「模块化插件」）──
    // 与进程外插件的区别：模块编译在本进程内，够得到原生窗口消息与岛本体；
    // 能力面**形状相同**（岛/通知/设置/上报），权限词表同一份，只是不需要跨进程。
    {
        ModuleHost *mh = ModuleHost::instance();
        ModuleAgentApi mapi;
        mapi.islandShow = [](const QString &title, const QString &desc, const QString &icon,
                             int form, int durationMs) {
            islandShow(title, desc, icon, durationMs, false, QString(),
                       static_cast<IslandOverlay::Form>(form), false);
        };
        mapi.islandAsk = [](const QString &title, const QString &desc, const QString &icon, int form,
                            const QStringList &actions,
                            const std::function<void(const QString &)> &onAction, int durationMs) {
            IslandOverlay::instance()->showWithActions(title, desc, icon,
                                                       static_cast<IslandOverlay::Form>(form),
                                                       actions, onAction, durationMs);
        };
        mapi.notify = [](const QJsonObject &params) { notifyFromParams(params); };
        mapi.settingsGet = [](const QString &key) -> QJsonValue {
            if (key == QStringLiteral("noiseEnabled"))  return AgentSettings::noiseEnabled();
            if (key == QStringLiteral("voiceEnabled"))  return AgentSettings::voiceEnabled();
            if (key == QStringLiteral("notifyEnabled")) return AgentSettings::notifyEnabled();
            if (key == QStringLiteral("islandIdle"))    return AgentSettings::islandIdle();
            if (key == QStringLiteral("lightMode"))     return AgentSettings::lightMode();
            return QJsonValue();
        };
        mapi.settingsSet = [](const QString &key, const QJsonValue &v) {
            const bool on = v.toBool();
            if (key == QStringLiteral("noiseEnabled"))       AgentSettings::setNoiseEnabled(on);
            else if (key == QStringLiteral("voiceEnabled"))  AgentSettings::setVoiceEnabled(on);
            else if (key == QStringLiteral("notifyEnabled")) AgentSettings::setNotifyEnabled(on);
            else if (key == QStringLiteral("islandIdle"))    AgentSettings::setIslandIdle(on);
            else if (key == QStringLiteral("lightMode"))     AgentSettings::setLightMode(on);
        };
        // 模块私有设置：QSettings 的 modules/<moduleId>/<key>，模块之间互不干扰。
        // ⚠️ QSettings::value 返回 QVariant（要显式转成 QJsonValue——两者没有隐式转换，
        //    直接 return 是 C2440）；QVariant::toJsonValue 是 Qt6 的转换口。
        mapi.moduleGet = [](const QString &moduleId, const QString &key, const QJsonValue &dflt) -> QJsonValue {
            const QVariant v = QSettings().value(QStringLiteral("modules/") + moduleId
                                                 + QLatin1Char('/') + key, dflt.toVariant());
            return v.isValid() ? QJsonValue::fromVariant(v) : dflt;
        };
        mapi.moduleSet = [](const QString &moduleId, const QString &key, const QJsonValue &v) {
            QSettings().setValue(QStringLiteral("modules/") + moduleId + QLatin1Char('/') + key, v);
        };
        mapi.reportCloud = [](const QString &type, const QJsonObject &payload) {
            if (!g_ws) return;
            g_ws->sendTextMessage(makeEnvelope(type, payload));
        };
        // ⚠️ 只开**文件夹**：与岛的 openInShell 同口径（explorer 打开目录，绝不运行文件）。
        mapi.openFolder = [](const QString &path) {
            ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(path.utf16()),
                          nullptr, nullptr, SW_SHOWNORMAL);
        };
        mapi.examActive = []() { return ExamMode::inst().active(); };
        mh->setAgentApi(mapi);

        mh->registerModule(new UsbModule());
        // VoiceHub 联动（2026-10-17）：模块自己持有 QNAM/SSE/轮询全套客户端，
        // QML 侧只读它的 Q_PROPERTY 快照。两个展示面各挂各的：
        //   · 灵动岛（IslandOverlay 的 QQuickWidget）—— setQmlObjectProperty 走
        //     rootObject()->setProperty，模块后注册也能被既有绑定拾取（vhRef null 守卫）；
        //   · 控制面板（AgentShell 的 QQuickWidget）—— 构造早于任何 ensure() 调用，
        //     走 context property（设置页 AgentSettingsPage 直接引用 voicehub）。
        auto *vh = new VoiceHubModule();
        mh->registerModule(vh);
        mh->startEnabled();
        IslandOverlay::instance()->setQmlObjectProperty(QStringLiteral("voicehubObj"), vh);
        AgentShell::setVoicehub(vh);

        // 广播社成员模块（2026-10-23 #广播站移交）：独立身份登录通道，账号在云端自管。
        // 不注册为内置模块（不是可启停插件），仅作为 QObject 注入控制面板上下文属性
        // `broadcastClub`，供新页 BroadcastClubPage 调用。base URL 复用 agent 自身云端地址。
        {
            auto *bc = new BroadcastClubModule();
            bc->setBaseUrl(siteBaseFromWsUrl(qEnvironmentVariable("STE_QT_WS_URL")));
            AgentShell::setBroadcastClub(bc);
        }
    }

    const int rc = app.exec();
    // 释放单例锁（2026-10-05）：正常退出才放，进程结束前交给系统/OS 清理兜底。
    // 老实现这行 CloseHandle(g_singletonMutex) 引用的变量早删了，改成守卫自己的 release()。
    g_single.release();
    return rc;
}
