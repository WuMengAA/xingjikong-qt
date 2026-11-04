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

#include "system/singleinstance.h"
#include "domain/alarm_domain.h"   // 2026-10-27 第五刀：闹钟域
#include "domain/notify_domain.h"  // 2026-10-27 第六刀：定时通知域（F 收尾）
#include "annotate/annotate_domain.h" // 2026-10-27 H 批：批注/白板内核（替代 ICC CE）
#include "domain/timesync_domain.h" // 2026-10-27 G3：时间同步域（NTP + 人工偏移，独立文件）
#include "domain/settings_domain.h" // 2026-10-28 模块三：安全模式/存储统计/备份/重置（独立域）
#include "domain/panel_domain.h"     // 2026-10-27 G4/G5/G6：岛组件面板（日期/课表/倒计时）
#include "domain/class_identity.h"   // 2026-11-02：班级身份（码+名）的唯一读写口
#include "automation/automation_engine.h" // 2026-10-27 Phase 3：定时自动化规则引擎（纯模型 + 1s 调度，点火转 executeAction）
#include "ppt/ppt_domain.h"       // 2026-10-27 I：PPT 放映控制（COM/IDispatch，PowerPoint/WPS）
#include "schedule/schedule_clock.h"   // 2026-10-06：课表时钟（上下课提醒）
#include "schedule/schedule_event_bus.h"  // 2026-11-02：课表事件总线（schedule_change 唯一出口）
#include "schedule/timetable_apply_wire.h"  // 2026-11-02：云端课表下发流水线（域文件）
#include "schedule/profile_repository.h"
#include "schedule/schedule_view.h"    // 2026-10-06：大屏今日课表窗口   // 单实例守卫（与管理端 viewer 同一份实现）
#include "oobe/oauthbind.h"        // 2026-10-06：用星璃账号绑定（OAuth + 设备票）
#include "island/island.h"           // 2026-10-07：被控端灵动岛（顶部居中胶囊 + 缓动 + 交互穿透）
#include "island/island_panel_source.h"  // 2026-11-04：岛收起条组件槽（barSlots/openFace/openEditor）
#include "system/smtc_monitor.h"
#include "media/MediaTools.h"
#include "media/MediaSession.h"  // 2026-10-22 §10 第二刀：录像/播放会话控制
#include "media/CameraActions.h" // 2026-10-23 §10 第 3 刀：相机六件套分支体
#include "media/ThumbnailReporter.h" // 2026-10-23 #459：班级域缩略图上报（class-event{kind:thumbnail}）
#include "core/ProcessUtil.h"    // 2026-10-22 §10 第二刀：通用进程等待（跨域复用）   // 2026-10-22 §10 第 1 刀：媒体工具层     // 2026-10-22：系统媒体识别（SMTC），起工作线程 + 元数据
#include "core/ExecOut.h"     // 2026-10-23 §10 第 3 刀：指令执行结果（跨 TU 共享）
#include "core/AgentSettings.h" // 2026-10-23 §10 第 4 刀：本地功能开关（原嵌在匿名域内，跨不了 TU）
#include "core/BlackboardCard.h" // 2026-10-24 P1：发布中心卡片 → 通知参数（纯映射，可单测）
#include "ota/channel_persistence.h" // 2026-10-30 main 拆分：OTA 灰度渠道持久化迁出
#include "ota/ota_state.h"  // 2026-10-30 OTA 状态机/健康快照迁出
#include "ota/ota_update.h" // 2026-10-31 main 拆分 #15：OTA 自更新动作层（OtaDeps 注入 startSelfUpdate/otaIsBusy）
#include "ota/ota_download.h" // 2026-10-31 main 拆分 #16：OTA 下载簇（startSelfUpdate/otaSelfCheck/otaStartPeriodicCheck/otaIsBusy）
#include "remote/remote_control.h" // 2026-10-30 远控会话收口 + 泄漏看门狗迁出
#include "ui/agent_panel.h" // 2026-10-30 main 拆分 #9：QML 控制面板数据接线（AgentPanelDeps/TrayState/g_panelDeps）
#include "action/execute_action.h" // 2026-10-30 main 拆分 #10：播放时段管理/调度/软控/文件接收（ActionDeps/g_actionDeps/executeAction）
#include "action/action_caps.h" // 2026-11-03：action 名册唯一真源（从 main 抽出，main 只做装配）
#include "wire/frame_builder.h" // 2026-10-31 main 拆分 #12：协议 v1 信封三打包函数（makeFrameBytes/makeVncFrameBytes/makeAudioFrameBytes）
#include "media/H264Streamer.h"  // A档（2026-10-31）：H264 推流装配（信号接线，业务逻辑在 src/media/）
#include "core/proc_inspect.h" // 2026-10-31 main 拆分 #13：进程/窗口只读检查（snapshotProcessNames/enumWindowsCb/visibleWindowsCached/windowTitleOf/memMbOf）
#include "policy/policy_engine.h" // 2026-10-31 main 拆分 #14：调度/软控/进程策略（ActionDeps 共享类型 + PolicyDeps 注入）
#include "policy/quit_guard.h"    // 2026-11-03：退出口令守卫（托盘「退出被控端」前校验）
#include "system/boot_trace.h"    // 2026-11-03：启动耗时埋点（用户笔记序号 3「启动慢」）
#include "localapi/localapi_server.h" // 2026-10-31：本地 API 鉴权网关（127.0.0.1:8080，三条已知缺口之一）

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
#include <QTime>          // 2026-10-28：存储统计的"统计于 HH:mm:ss"（别指望 QDateTime 间接带进来）
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
#include "study_mode/study_mode.h"  // 2026-10-30 自习模式（噪音异常自动录制）迁出
#include "notify/notify_entry.h" // 2026-10-30 灵动岛统一入口迁出
#include "p3log/p3_log.h" // 2026-10-30 P3 长期记录落盘内核迁出
#include "p3log/p3_reader.h" // 2026-10-30 P3 长期记录查询/展示迁出
#include "remote/vnc_bridge.h"       // 2026-10-18：远控桥（主 WS vnc-data ↔ 本地 VNC，纯字节）
#ifdef STE_USE_LIBVNCSERVER
#include "remote/vnc_server.h"         // 2026-10-18：in-agent VNC Server（libvncserver 封装，方案 A）
#endif
#include "terminal/terminal_console.h"  // 2026-10-06：远程终端（REPL，见该文件注释）
#include "system/exam_mode.h"         // 2026-10-06：考试模式（全屏拦截 + 黑名单轮询 + 计时）
#include "system/proc_guard.h"        // 2026-10-13：进程终止安全闸门（与 exam_mode 共用同一道门）
#include "media/audio_player.h"      // 2026-10-07：语音对讲播放（waveOut）
#include "opus_codec.h"        // 2026-10-21（P1）：语音 Opus 编解码（shared/audio，两端同源）
#include "ui/broadcast_view.h"    // 2026-10-07：屏幕广播全屏显示
// ── 通知体系新组件（2026-10-09 接线 · DeepSeek Harness 造）──
#include "system/danmaku.h"           // §1.2 弹幕：字幕式横向滚动
#include "automation/loop_reminder.h"     // §1.2 loopRemind：未确认循环提醒
#include "automation/notification_gate.h" // §6.2 考试模式禁一切通知与监视
#include "system/edge_tts.h"          // §6.4 Edge 语音 TTS（管理端可选）
#include "tts/tts_speaker.h"   // 2026-10-30 main 拆分第一刀：TTS 统一播报（含 SAPI 兜底/音量序列）
#include "noise/noise_monitor.h"     // §6.5 噪音检测
#include "noise/noise_assembly.h"   // P0-2 分层装配工厂（main 只调 createNoisePipeline）
#include "cloud_event_bridge.h"     // P0-2：噪音存证事件经 WS 桥发出（实体在本文件）
#include "media/microphone.h"        // 语音通话：mic 会话内采集
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
#include "plugin/pluginhost.h"                 // P2：插件宿主（进程外插件 ABI）
#include "plugin/plugin_api_bridge.h"     // 2026-11-02：插件能力注入（从 main 搬出）
// 2026-10-20：首次启动引导（OOBE Wizard）——QML 向导 + 绑定数据面（详见 oobe_wizard.h）。
#include "oobe/oobe_wizard.h"
#include <QQuickWidget>          // 向导宿主（与 AgentShell 同款：QQuickWidget 顶格承载 QML）
#include <QQuickWindow>          // 2026-10-25：渲染诊断埋点（sceneGraphBackend 决议值）
#include <QSysInfo>              // 2026-10-25：渲染诊断埋点（VxKex 谎报后的 OS 版本）
#include <QPointer>              // 向导窗口单例（窗口被关掉后指针自动置空）
#include <QQmlContext>
#include <QQmlEngine>
#include "modules/modulehost.h"  // 2026-10-16：内置模块层（编译进 agent 的官方模块，与进程外插件共用门控）
#include "modules/usbmodule.h"
#include "modules/voicehubmodule.h"  // 2026-10-17：VoiceHub 联动（只读展示权威广播状态）
#include "system/broadcastclub.h"  // 2026-10-23：广播社成员独立身份登录通道（非插件模块，2026-10-26 移出 src/modules/）
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
#include "system/winlauncher.h"   // 2026-10-19 需求①：子进程静默启动（杜绝任何场景弹出 cmd 窗）
#include <tlhelp32.h>   // 进程快照（process_list / process_stop）
#include <psapi.h>      // 进程工作集内存
#include <mmsystem.h>   // waveOutSetVolume（set_volume，Rust 版实测零依赖可用）
#include <shellapi.h>   // ShellExecuteW（launch_app）+ SHFileOperationW（media_delete 进回收站）
// <sapi.h> 已于 2026-10-30 随 TTS 模块（src/tts/tts_speaker.cpp）迁出 —— SAPI 5 朗读只在 TTS 用，main 不再直接引用。
#include "media/system_volume.h"  // 2026-10-11：系统主音量（Core Audio/WASAPI，独立编译单元）
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
// sapi.lib 已于 2026-10-30 随 TTS 模块迁出（tts_speaker.cpp 自带 #pragma comment(lib,"sapi.lib")）
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

// 2026-10-30：g_ws 与噪音存证桥 sendNoiseIncident 需要**外部链接**（被 noise_cloud_transport.cpp
// 跨 TU 调用），故移出匿名 namespace。g_ws 原先藏在匿名域内→内部链接，跨 TU 取不到；
// sendNoiseIncident 同理，且本体必须留在 main.cpp（只有这里持有 g_ws 与信封格式）。
QWebSocket *g_ws = nullptr;        // 全局 WS（管理端长连）；sendNoiseSample/sendNoiseAlert/sendNoiseIncident 共用

// P0-2（2026-10-30）：噪音存证事件（含两张图 base64）。类型 "noise_incident"，
// 由云端 noise-evidence.js 接收并存储（图片保留 5 分钟）。未连则静默丢弃（与遥测同口径）。
// ⚠️ 本函数**仅**被 noise_cloud_transport.cpp（另一 TU）跨 TU 调用，本 TU 不调用；
// 故必须文件作用域 + 外部链接。Release /Gy+/OPT:REF 会把未引用 COMDAT 丢弃 → LNK2019，
// 用 pragma + 外部链接锚双重兜底（符号名与 LNK 报错一致 ?sendNoiseIncident@@YAXAEBVQJsonObject@@@Z）。
#pragma comment(linker, "/include:?sendNoiseIncident@@YAXAEBVQJsonObject@@@Z")
void sendNoiseIncident(const QJsonObject &p)
{
    if (!g_ws || g_ws->state() != QAbstractSocket::ConnectedState) return;
    g_ws->sendTextMessage(makeEnvelope(QStringLiteral("noise_incident"), p));
}

// 编译期引用锚（默认外部链接的非 const 全局）：迫使编译器在本 TU 产出 sendNoiseIncident 的定义。
// 仅对函数取地址不够——static const 锚会被 /Gy 整段丢弃（本 TU 不调用该函数，锚本身又无读取者），
// 连带 sendNoiseIncident 的定义也被一起丢掉 → 跨 TU 调用方 LNK2019。改为外部链接的全局，
// 编译器必须为它产出定义，从而连带产出 sendNoiseIncident。链接侧 sendNoiseIncident 由
// noise_cloud_transport.cpp 的真实调用保活，锚本身是否存活无关紧要。
void (*kSendNoiseIncidentAnchor)(const QJsonObject &) = &sendNoiseIncident;
QString g_uid;                 // 本机设备码（register 时定下来，心跳复用，避免每次重算）；文件作用域外部链接，供 upload 模块跨 TU 引用

// ── 调度/软控策略全局：定义已迁至 src/policy/policy_engine.cpp（main 拆分 #14）──
// main.cpp 只 extern 引用（供下方 ActionDeps 填充取地址 &g_schedFile 等；execute_action.cpp 经指针访问）。
extern QHash<QString, QTimer *> g_schedTimerById;
extern QString g_schedFile;
extern SoftPolicy g_soft;
extern QString g_softPolicyFile;
extern QSet<quint32> g_frozenPids;
QString agentEnvPath();                               // 前向声明（外部链接；定义见本文件末尾 namespace 之后，供 OTA 渠道持久化模块跨 TU 引用）
// ── 自习状态全局：文件作用域（namespace 外），供 study_mode 模块 extern 复用（同 g_uid / agentEnvPath 模式）──
static bool    g_studyOn = false;
double  g_studyThreshDb = -45.0;    // 阈值（dB）—— 外部链接，供 study_mode 模块复用（同 g_uid 模式）
int     g_studyStreakNeed = 3;      // 连续超阈拍数（外部链接）
int     g_studyRecordSec = 30;      // 触发后录多久（秒）（外部链接）
int     g_studyStreak = 0;          // 当前连续超阈计数（外部链接）
int     g_studyAlertCount = 0;      // 本次自习累计触发次数（外部链接）
static qint64   g_studyStartTs = 0;         // P3：本次自习开始时间（长期记录算时长用）
// g_studyRecTimer 已迁至 src/study_mode/study_mode.cpp（模块内私有）

// sendEvent：文件作用域前向声明（定义见本文件 namespace 下方），供 study_mode 模块 extern 复用。
void sendEvent(const QString &level, const QString &msg, const QJsonObject &extra = QJsonObject());

// ── 通知体系全局：文件作用域（namespace 外），供 src/notify/notify_entry 模块 extern 复用 ──
LoopReminder g_loopReminder;                 // 循环提醒（无 UI，纯逻辑）
NotificationGate g_notifGate;                // 通知门控（考试模式禁通知）
bool g_remoteControl = false;      // remote_control_start/stop 的真实会话状态
QSystemTrayIcon *g_tray = nullptr;
// ── 远控会话状态：文件作用域（namespace 外），供 src/remote/remote_control 模块 extern 复用 ──
int g_currentFps = 5;              // 动态帧率：当前抓屏频率（次/分钟）—— 远控开会时提高
qint64 g_vncSessionStartMs = 0;    // 本次远控会话起服时刻（首帧宽限 + 看门狗起步宽限都用它）
qint64 g_lastVncDownMs = 0;        // 最近一次收到管理端 vnc-data down 字节（键鼠/请求/5s 保活）
bool g_vncMegaDone = false;        // 本会话是否已豁免过一个 ≥200KB 整屏帧（一次性）
// sendActionReceipt：文件作用域前向声明（定义见本文件 namespace 之后），供 notify_entry 模块 extern 复用。
void sendActionReceipt(const QString &id, const QString &action, const QString &result,
                       const QString &error = QString(), const QJsonObject &data = QJsonObject());


// ── OTA 全局：文件作用域（namespace 外），供 src/ota/ota_state 模块 extern 复用 ──
extern const char *kAppVersion = "0.6.68";
QString g_channel = QStringLiteral("stable");   // 供 register 上报；main 启动即解析覆盖
QString g_latestVer, g_updateUrl, g_updateSha, g_updateNotes;bool g_updateChecked = false;       // 有没有从云端拿到过版本信息
bool g_updateMandatory = false;     // 云端 ota.json 的 mandatory（true=强制自动升级）
// 2026-10-17：已主动提示过的版本（周期性复查会反复读到同一版本，只提示一次）
// 2026-10-31 main 拆分 #15：去掉 static 提升为**外部链接**（内部链接跨不了 TU），
//   供 src/ota/ota_update 模块 extern 读写（全仓仅 applyUpdateInfo 使用，行为不变）。
QString g_otaPromptedVer;
qint64 g_lastOnlineAt = 0;          // 最近一次连上云端（强制升级回滚判定用）
QAction *g_actUpdate = nullptr;     // 托盘里"发现新版本…"那一条（无更新时隐藏）

// ── QML 控制面板（agent_panel）依赖全局：文件作用域（namespace 外），供 src/ui/agent_panel 模块 extern 复用（main 拆分 #9）──
bool g_registered = false;           // 云端回了 registered
QString g_logPath;                   // 被控端日志落盘（装机后没有 stdout，没日志就只能靠猜）
bool g_configured = true;            // URL+TOKEN 是否齐全（loadEnvFile 后判定）
bool g_unconfigured = false;         // 已进入"未配置"态（不连接/不重试）
bool g_unconfiguredNotified = false; // 未配置气泡只弹一次
bool g_reconnectNow = false;         // 保存新配置后：断线回调里立刻重连（别等退避）
bool g_configDialogOpen = false;     // 配置窗防重入（首运行自动弹 + 手动点可能撞车）
PluginHost *g_pluginHost = nullptr;  // P2：插件宿主（进程外插件 ABI）
constexpr int kMaxBackoffMs = 8000;      // 退避上限（2026-10-08：从 15s 降到 8s，断网恢复更快）
constexpr int kFirstBackoffMs = 500;     // 首连/未注册时的快速重试间隔（2026-10-08）
int g_backoffMs = kFirstBackoffMs;   // 指数退避（毫秒）
QString g_classCode;                 // 本机归属的班级码（OOBE 绑定后持久化，register 时带给云端 → 管理端按班分组）
QString g_deviceRole = QStringLiteral("classroom");   // 设备用途（classroom 教室机 / broadcast 广播机）
// QML 控制面板依赖注入（main 拆分 #9，方案 B）：main() 在同一 TU 才能取匿名域函数地址，
// 故此处仅声明全局指针；实际填充在 main() 开头（见 g_panelDeps 填充点）。
AgentPanelDeps *g_panelDeps = nullptr;

namespace {

constexpr int kFrameIntervalMs = 2000;   // 兜底默认（现由 previewIntervalMs() 读取，可 env 覆盖）

// ── 2026-10-24：预览流（WS 单帧 JPEG）参数改为可配 ──────────────────────────
// 背景：实测生产帧率恒为 2.01s/帧，且**与帧大小无关**（240KB 与 320KB 同为 2.01s）
//   ⇒ 不是链路限速，是本文件里两处自设闸门（2s 定时器 + 150KB/s 令牌桶）在卡。
//   而 CF 隧道才是真正的限流大头，把自设帽放开来才能让隧道吃饱。
// 取值：STE_PREVIEW_INTERVAL_MS（默认 1000，钳 200..10000）
//       STE_PREVIEW_KBPS（默认 400 KB/s，0=不限；钳 0 或 50..20000）
//       STE_PREVIEW_MAX_EDGE（默认 854 = 480p 档长边；0=不缩放）
static int previewIntervalMs()
{
    int v = qEnvironmentVariableIntValue("STE_PREVIEW_INTERVAL_MS");
    if (v <= 0) v = 1000;
    return qBound(200, v, 10000);
}

static qint64 previewQuotaBytes()
{
    int kbps = qEnvironmentVariableIntValue("STE_PREVIEW_KBPS");
    if (kbps == 0 && qEnvironmentVariableIsSet("STE_PREVIEW_KBPS")) return 0;   // 显式 0 = 不限
    if (kbps <= 0) kbps = 400;
    kbps = qBound(50, kbps, 20000);
    return static_cast<qint64>(kbps) * 1024;
}

static int previewMaxEdge()
{
    int v = qEnvironmentVariableIntValue("STE_PREVIEW_MAX_EDGE");
    if (v == 0 && qEnvironmentVariableIsSet("STE_PREVIEW_MAX_EDGE")) return 0;  // 显式 0 = 不缩放
    if (v <= 0) v = 854;
    return qBound(320, v, 3840);
}
constexpr int kHeartbeatIntervalMs = 10000; // 心跳缺省值（云端 registered 里给了就以云端为准）
// 2026-10-14 修（审计 P2）：云端下发的心跳间隔以前只要 >0 就无条件采纳，
// 异常值（笔误写成 3600000 = 1 小时之类）会让心跳变得极稀 → 云端 30s 判离线 →
// 重连 → 又收到同一份配置 → 循环「连上又断」。现在钳制在这个区间内。
constexpr int kHeartbeatMinMs = 1000;    // 下界：再密就白耗流量
constexpr int kHeartbeatMaxMs = 60000;   // 上界：再稀就会被云端当离线
// kMaxBackoffMs / kFirstBackoffMs 已提升文件作用域（namespace 上方，供 g_backoffMs 与 agent_panel 复用）。
// kProtocolVersion 已迁至 src/wire/frame_builder.cpp（main 拆分 #12，帧构建模块）。
// kProcessListLimit 已迁至 action/execute_action.h（main 拆分 #10，executeAction 迁移）。

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
// kAppVersion 已迁至文件作用域（namespace 上方），供 src/ota/ota_state 模块 extern 复用。

// 2026-10-10：灰度渠道持久化（发现①修复）—— 当前生效渠道在 main 启动时解析并落盘 channel.txt，
// 后续任何重启/升级都自动沿用，不再依赖启动器注入 STE_QT_CHANNEL。见 resolvedChannel/persistChannel。
// g_channel 已迁至文件作用域（namespace 上方），供 src/ota/ota_state 模块 extern 复用。

// g_backoffMs 已提升文件作用域（namespace 上方），供 agent_panel 模块 extern 复用（main 拆分 #9）。
// 2026-10-08：本机归属的班级码（OOBE 绑定后持久化，register 时带给云端 → 管理端按班分组）。
// g_classCode 已提升文件作用域（namespace 上方），供 agent_panel 模块 extern 复用（main 拆分 #9）。
// 2026-10-25：设备用途（classroom 教室机 / broadcast 广播机）。与班级码**并列**的第二维身份：
//   班级码回答「这台机器属于谁」，用途回答「这台机器是什么」。
// 为什么放全局而不是塞进 OobeWizard：register、缩略图门、外壳选择都要读它，
// 而 OobeWizard 有明文纪律「不引用 main.cpp 任何符号」（见其头注释）。
// g_deviceRole 已提升文件作用域（namespace 上方），供 agent_panel 模块 extern 复用（main 拆分 #9）。
// 2026-10-09：班级**显示名**（站点 /api/device/activate 回喂的 className，如「1班」）。
// 云端班级索引的 name 默认等于 code；带上它，管理端面板才显示真名而不是 class_01。
QString g_className;

// 2026-11-02（插件大升级 #2）：课表时钟提到文件作用域。
// 原来它是 main() 里的 `static`，插件 schedule_get 在 4300 行附近要用它、
// 而课表时钟在 4100 行才 new —— 局部 static 在 main 之外根本不可见。
// 它没有多实例语义（全机一份课表），提上来只是把"作用域说了算"改成"属于进程"。
ScheduleClock *g_schedClock = nullptr;
// 2026-10-08：是否曾经注册成功过（决定首连用快速重试还是正常退避）。
bool g_everRegistered = false;
// 语音对讲（2026-10-07）：学生端播放器（waveOut，懒打开——只在收到音频帧时开）
AudioPlayer g_audioPlayer;
bool g_audioHintShown = false;   // "老师正在讲话"提示是否已显示（每会话一次）
// g_audioUpSeq 已迁至 src/wire/frame_builder.cpp（main 拆分 #12，帧构建模块）。
// P1（2026-10-21）：语音编解码实例。**惰性初始化**（首次用到才开）——没开过语音的机器
// 不该白占两个 Opus 上下文；编码器 16 kbps 起步，与方案 §5 P1 的档位一致。
static OpusCodec g_opusEnc;      // 学生 mic 上行编码
static OpusCodec g_opusDec;      // 老师下行解码
// ── 通知体系组件实例（2026-10-09 接线 · DeepSeek Harness）──
// 弹幕（2026-10-15 组件5）：全局指针 g_danmaku 已删，改 danmaku.h 的单例访问器
// danmakuWidget()/danmakuWidgetPeek() —— 通知路径（下方 kind=danmaku）与学生祝福
// 路径（IslandOverlay::sendBlessing）共享同一个弹幕层，全局指针会指岔。
// g_loopReminder / g_notifGate 已迁至文件作用域（namespace 上方），供 src/notify/notify_entry 模块 extern 复用。
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
// g_registered 已提升文件作用域（namespace 上方），供 agent_panel 模块 extern 复用（main 拆分 #9）。
int g_heartbeatSeq = 0;
int g_heartbeatMs = kHeartbeatIntervalMs;   // 实际心跳间隔：云端下发就听云端的
int g_timeoutMs = 30000;                    // 云端判离线阈值（只用于日志说明，不自己判）
QTimer *g_hbTimer = nullptr;   // 心跳定时器（registered 拿到参数后要能改间隔，所以放外面）

// g_logPath 已提升文件作用域（namespace 上方），供 agent_panel 模块 extern 复用（main 拆分 #9）。

// 前向声明：调度器回调在文件后面用到它，避免 C++ 的"用到才定义"顺序问题。
bool enableShutdownPriv();

// ══ 批次 2（全量补齐）新增的全局状态 ══
// g_schedTimers / g_schedTimerById / g_schedFile / g_schedDir 已迁至 src/policy/policy_engine.cpp（main 拆分 #14）。
// g_schedTimerById / g_schedFile 为外部链接：main.cpp 文件作用域 extern 声明供 ActionDeps 填充取地址。
QString g_recvDir;                 // file_push 落盘目录
// g_remoteControl 已迁至文件作用域（namespace 上方），供 src/notify/notify_entry 模块 extern 复用。
// g_currentFps / g_vncSessionStartMs / g_lastVncDownMs / g_vncMegaDone 已迁至文件作用域
//（namespace 上方），供 src/remote/remote_control 模块 extern 复用。
int g_frameDiffMs = -1;            // 上一帧耗时（毫秒），动态帧率控制器用它判断画面是否稳定

// A6（2026-10-11）：噪音检测采集器。懒加载单例 —— 没收到 noise_monitor_start 前不 new，
// 不占麦克风（教室机麦克风可能同时被语音对讲用，见 noise_monitor.cpp 顶部注释）。
NoiseMonitor *g_noise = nullptr;

// P0-2 分层（2026-10-30）：噪音存证链路协调器。懒加载单例，由课表块构造。
NoiseIncidentPipeline *g_noisePipeline = nullptr;

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
// TrayState 已提升文件作用域（src/ui/agent_panel.h，main 拆分 #9）——本文件与
// agent_panel 模块共用**同一类型**（否则两个 enum 各是一个类型，跨 TU 传参不匹配）。
// g_configured 已提升文件作用域（namespace 上方），供 agent_panel 模块 extern 复用（main 拆分 #9）。
// g_unconfigured 已提升文件作用域（namespace 上方），供 agent_panel 模块 extern 复用（main 拆分 #9）。
// g_unconfiguredNotified 已提升文件作用域（namespace 上方），供 agent_panel 模块 extern 复用（main 拆分 #9）。

// ── P0-1（2026-10-14）：本地功能开关 ───────────────────────────────────────
// 把被控端已存在的各项能力（通知/语音播报/噪音监测/灵动岛常驻/浅色主题）做成可勾选的开关，
// 落 QSettings（组织 Stelarith / 应用 Stelarith Agent，注册表 HKCU）。默认全开，
// 各子系统的入口点（speakText / notifyFromParams / noise_monitor_start / 灵动岛初始化）统一读这里。
// 这样"设置项"才真的"调用设置项"，而不是只摆几个开机自启开关（用户 2026-10-14 反思）。
// ⚠️ 2026-10-23（AGENTS.md §10 第四刀地基）：AgentSettings 已下沉到 src/core/AgentSettings.h。
//    原因：它原先**嵌套在本文件匿名 namespace（251 行起）内**、函数又全带 static ⇒ 内部链接，
//    跨不了 TU；media_session_start 分支下沉到 src/media/ 之后，那边必须能读 playScheduleEnabled。
//    唯一变换 static → inline（头文件定义的标准要求），函数体与键名逐字未动。
// g_reconnectNow 已提升文件作用域（namespace 上方），供 agent_panel 模块 extern 复用（main 拆分 #9）。
// g_configDialogOpen 已提升文件作用域（namespace 上方），供 agent_panel 模块 extern 复用（main 拆分 #9）。

QString envOr(const char *key, const QString &fallback)
{
    const QString v = qEnvironmentVariable(key);
    return v.isEmpty() ? fallback : v;
}

/* ---------- 协议 v1 封装 ----------
 * 文本：{"v":1,"type":...,"id":...,"ts":...,"payload":{...}}
 * 帧  ：[1B 版本=1][2B 大端 headerLen][header JSON][JPEG]
 * 三个打包函数（makeFrameBytes/makeVncFrameBytes/makeAudioFrameBytes）与 kProtocolVersion、
 * 上行序号计数器已迁至 src/wire/frame_builder.{h,cpp}（main 拆分 #12）。
 */

// ⚠️ makeEnvelope 的**定义**已经在文件末尾（匿名 namespace 外面）。
// 原先它就写在下面那段匿名 namespace（103–3846 行）里，匿名域符号跨不了 TU ——
// terminal_console.cpp 一调用就 LNK2019。调用点仍在 namespace 内，靠 cloud_proto.h 的声明可见。

void scheduleReconnect();

// 定义在后面（托盘区），这里前向声明供 connectNow/sendRegister 提前调用
void updateTray(TrayState st, const QString &detail);
void goUnconfigured();
// 定义在后面（OTA 段）：设置面板里的「立即升级」复用它，所以提前声明。
// 2026-10-31 main 拆分 #16：startSelfUpdate 已迁 src/ota/ota_download.cpp 并提升为外部链接，
// 声明由 ota_download.h 提供（此处不再写 static —— 否则声明成内部链接会 LNK2019）。
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

/** 本端 action 名册（能力上报用）已抽到 src/action/action_caps.{h,cpp}：
 *  ActionCaps::kActionNames / kActionCount 是唯一真源，与 executeAction 分支逐条对齐。
 *  这里不再内联——main.cpp 只做装配，名册属动作域知识。 */

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
    for (int i = 0; i < ActionCaps::kActionCount; ++i)
        acts.append(QString::fromLatin1(ActionCaps::kActionNames[i]));
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
    // 2026-10-25：用途随注册上报。云端只在新登记时采纳，之后以云端为准（回执覆盖），
    // 与 classCode 同一套「云端权威」模型 —— 后台改用途能推下来，端侧改不回去。
    p.insert(QStringLiteral("role"), g_deviceRole);

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

// SchedTask 已迁至 action/execute_action.h（main 拆分 #10，executeAction 迁移）。

// 持久化：写失败必须报出来（否则崩溃恢复等于没做）。
/* ---------- 调度 / 软控 / 进程策略 ----------
 * 调度（定时关机/重启）：saveSchedules/loadSchedules/schedFireAllowed/fireScheduledTask/
 *   armScheduleTimer/reschedulePersisted + g_schedFile/g_schedDir/g_schedTimerById/g_schedTimers；
 * 软控（黑白名单/冻结/去网/守护）：softNorm/softHit/saveSoftPolicy/loadSoftPolicy/
 *   setProcessFrozen/setNetBlocked/softGuardTick + g_softPolicyFile/g_soft/g_softGuard/g_frozenPids；
 * 保护名单：kProtectedSelf/System/Security + protectedCategory。
 * 已迁至 src/policy/policy_engine.{h,cpp}（main 拆分 #14）。enableShutdownPriv 经 PolicyDeps 注入；
 * snapshotProcessNames 见 src/core/proc_inspect.h（#13）。
 */

/** 枚举所有进程 pid → exe 名。只读快照，不打开任何进程句柄。 */
/* ---------- 进程/窗口只读检查 ----------
 * snapshotProcessNames / enumWindowsCb / visibleWindowsCached / windowTitleOf / memMbOf
 * 与 WinCache 缓存（g_winCache/kWinCacheTtlMs）已迁至 src/core/proc_inspect.{h,cpp}
 * （main 拆分 #13）。纯 Win32 只读快照、零 main 全局依赖；execute_action.cpp
 * 经 g_actionDeps 函数指针访问（无需改动），main.cpp 的 ActionDeps 填充取地址即用。
 */

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
// g_tray 已迁至文件作用域（namespace 上方），供 src/notify/notify_entry 模块 extern 复用。
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
} // namespace ── 临时关闭：cleanLine 提升文件作用域（供 agent_panel 模块 extern 复用，main 拆分 #9）

QString cleanLine(const QString &s)
{
    QString t = s;
    t.remove(QLatin1Char('\r'));
    t.remove(QLatin1Char('\n'));
    return t.trimmed();
}

namespace { // ── 重新打开：cleanLine 已提升文件作用域，其余符号仍在匿名域

// agent.env 的路径 —— **唯一真源**：读、写、备份、提示文案全从这里出，别处不许自己拼字符串。
//
// ⚠️ 2026-10-07 从「exe 同目录」改到「数据目录」。原先返回 applicationDirPath()，也就是




// ── OTA 灰度渠道持久化（2026-10-30 迁出 → src/ota/channel_persistence.{h,cpp}）──
//   定义已不在本文件；main.cpp 经 #include "ota/channel_persistence.h" 调用 channelFilePath/
//   resolvedChannel/persistChannel，agentEnvPath 跨 TU 引用在 OTA 模块内处理。

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

// 班级码/名的持久化（2026-10-08/10-09）整块已迁至 src/domain/class_identity.{h,cpp}
// （2026-11-02 · 班级绑定整合）。原因：码与名是一门"身份组"，原来散在这里由两份
// static 各写各的，结果"移出班级"只清了码没清名 —— 界面挂着旧班名、缩略图已停报。
// 现在四个入口收敛为 ClassIdentity::saveCode/saveName/clearAll/applyBind，
// 两个全局变量仍留在此处（register 报文的数据源），域内通过 init() 拿引用。
static void saveClassCode(const QString &code) { ClassIdentity::saveCode(code); }
static void saveClassName(const QString &name) { ClassIdentity::saveName(name); }

// ── 设备用途持久化（2026-10-25）──────────────────────────────────
// 与 classcode.txt 同目录同风格（单独小文件，不动 agent.env 的 4 行格式）。
// 为什么必须落盘：用途决定外壳与云端判定，重启后靠 register 自报 —— 丢了就退回教室机。
static QString deviceRolePath()
{
    return qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong/devrole.txt");
}
/** 写用途（白名单外一律 classroom）。同值直接返回，不做无谓落盘。 */
static void saveDeviceRole(const QString &role)
{
    const QString r = role.trimmed().toLower();
    const QString n = (r == QStringLiteral("broadcast")) ? r : QStringLiteral("classroom");
    if (n == g_deviceRole) return;
    g_deviceRole = n;
    QFile f(deviceRolePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(g_deviceRole.toUtf8());
        f.close();
        qInfo().noquote() << QStringLiteral("[agent-qt] 设备用途 = %1").arg(g_deviceRole);
    } else {
        qWarning().noquote() << QStringLiteral("[agent-qt] ⚠️ 无法保存设备用途：%1").arg(f.errorString());
    }
}
static void loadDeviceRole()
{
    QFile f(deviceRolePath());
    if (!f.open(QIODevice::ReadOnly)) return;          // 没存过 = 教室机（向后兼容）
    const QString r = QString::fromUtf8(f.readAll()).trimmed().toLower();
    f.close();
    if (r != QStringLiteral("broadcast")) return;       // 只有广播机才需要标；其余按教室机
    g_deviceRole = QStringLiteral("broadcast");
    qInfo("[agent-qt] 已载入设备用途：广播机");
}

// saveVncAccessLocally / vncAccessPath 已迁至 src/remote/remote_control（main 拆分 #8）。
/* ══════════ 班级绑定（2026-10-04：OOBE 里消费绑定码，把本机绑到班级） ══════════
 * 背景：管理员在管理面板生成一次性、绑班级的接入码（云端 6 位票）；教室端在配置向导里
 * 填进去，保存时调云端 `POST /api/device/activate?code=…&uid=…` 消费，
 * 云端把它落进班级目录（uid→class）。这就是"被控端绑定界面"。
 * 绑定接口地址从云端 ws 地址推导（与 WS 同源，见 bindBaseFromWsUrl）；
 * 自建场景可用 STE_QT_BIND_URL 覆盖。
 * 消费失败不阻断保存——绑定是可选项，配错了以后还能在面板改。
 * （2026-10-25：原站点 XJK-XXXX-XXX 码路径已整体废止，云端不再做代兑换。）
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
            // 2026-10-09：把**云端权威班级码**回存，替掉先前存进去的一次性接入码 ——
            //   否则云端班级索引的键会用得不稳定的码。
            //   云端回 classCode（真源 class_id，如 class_01）；云端不回则回退 classId。
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

// 班级绑定的**唯一入口**（2026-10-09 抽出来，2026-10-25 收紧）。
// 为什么抽：这段逻辑原来只长在「保存并连接」按钮里，于是**「用星璃账号绑定」这条路压根不走班级绑定**
// —— 用户填了班级码、点了账号绑定，班级就是不绑（本轮补的缺口）。两条路各写一遍必然漂移，
// 这就是第一次漂移的实例，所以只留一份实现。
//
// 一个输入框吃两种取值，不让用户去分辨自己手里是哪一种：
//   · **班级码**（云端班级索引的键，6-16 位字母数字或 -_）→ 直接持久化，
//     下次 register 带上 classCode → 云端按班归组（云端 registry.js 的班级索引）。
//   · **接入码**（云端按班开的 6 位一次性票，见 Stelarith-cloud/src/bind-qr.js）→
//     打**云端**的 `/api/device/activate` 消费，云端把它绑到对应班级
//     （2026-10-20 班级真源统一，见 Stelarith-cloud/src/class-dir.js）。
//   旧的站点 XJK-XXXX-XXX 码已于 2026-10-25 废止（云端不再做站点代兑换），
//   设备端也不再识别 —— 这类码现在会被当成无效码由云端拒掉。
//   ⚠️ 自建场景（云端没升级）时把 `STE_QT_BIND_URL` 显式指到绑定服务，仍走这条唯一路径。
static void applyClassBinding(const QString &rawCode, const QString &wsUrl, const QString &uid)
{
    const QString code = rawCode.trimmed();
    if (code.isEmpty()) return;

    saveClassCode(code);   // 持久化 → 下一次 register 带上 classCode

    // 只有 6 位一次性票需要去云端兑换；班级码没有可消费的东西。
    static const QRegularExpression ticket(QStringLiteral("^[A-Z0-9]{6}$"));
    if (!ticket.match(code).hasMatch()) {
        qInfo().noquote() << QStringLiteral("[agent-qt] 已记录班级码 %1（register 时带给云端按班归组）").arg(code);
        return;
    }

    // 唯一入口：云端（绑定接口与 WS 同源，见 bindBaseFromWsUrl）
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
    wiz->setInitialRole(g_deviceRole);   // 重开向导时默认选中上次的用途
    // 用途在向导第一步就定，**不等绑定完成**：用户选了广播机后点「稍后配置」，
    // 这台机器的身份也必须已经生效（否则云端按教室机对待、外壳还是教室面）。
    QObject::connect(wiz, &OobeWizard::roleChosen, dlg, [](const QString &role) {
        saveDeviceRole(role);
        if (g_ws && g_ws->state() == QAbstractSocket::ConnectedState) {
            sendRegister();   // 与绑班补发同一套（旧 register 重复发是既有用法）
            qInfo("[oobe] 用途已补发云端（%s）", qPrintable(role));
        }
    });
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

// 更新信息全局（g_latestVer/g_update*/g_otaPromptedVer/g_lastOnlineAt/g_actUpdate）
// 已整体迁至文件作用域（namespace 上方），供 src/ota/ota_state 模块 extern 复用。

// ── OTA 无人值守自更新「动作层」已迁至 src/ota/ota_update.{h,cpp}（2026-10-31 main 拆分 #15）──
//   compareVersion / applyUpdateInfo / otaBackupExe / otaWritePending / otaMarkStartedOk /
//   otaRollback + 内部件 maybeAutoUpdate / desktopIdleMs / kOtaRollbackMs / kOtaMinFreeBytes。
//   startSelfUpdate（定义见本文件下文）与 otaIsBusy 经 OtaDeps 注入给该模块；
//   状态全局（kAppVersion / g_latestVer / g_update* / g_otaPromptedVer / g_actUpdate /
//   g_tray / g_unconfigured）为文件作用域外部链接，模块侧 extern 复用。

} // namespace ── 临时关闭：开机自启/桌面快捷方式函数提升文件作用域（供 agent_panel 模块 extern 复用，main 拆分 #9）

/* ── 开机自启 / 桌面快捷方式（2026-10-09 · 用户清单第②条）────────────────────
 * 被控端是**受管终端**：自启 = 安装器建的「登录触发器计划任务」（RunLevel Highest，
 * 才拿得到锁屏/关机/抓屏所需权限）；快捷方式在**公共桌面**。两者改动都要管理员，
 * 所以点开关时用 runas 提权（弹 UAC；用户拒绝就如实报错、绝不假装成功）。
 * ⚠️ 管理端走的是另一套（HKCU Run + 当前用户桌面，免提权）—— 两端机制不同是用户选定的。
 */
const char *kAgentTaskName = "StelarithAgentQt";

bool runElevatedWait(const QString &file, const QString &params, int timeoutMs = 60000)
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

bool agentAutostartEnabled()
{
    QProcess p;
    stelarithHideChildConsole(p);   // #368 需求①：schtasks 查询静默，不弹控制台窗
    p.start(QStringLiteral("schtasks"),
            {QStringLiteral("/Query"), QStringLiteral("/TN"), QString::fromLatin1(kAgentTaskName)});
    if (!ProcessUtil::waitFinished(p, 4000)) { p.kill(); return false; }
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

bool agentSetAutostart(bool on)
{
    return runElevatedWait(QStringLiteral("schtasks.exe"),
        QStringLiteral("/Change /TN %1 /%2")
            .arg(QString::fromLatin1(kAgentTaskName),
                 on ? QStringLiteral("ENABLE") : QStringLiteral("DISABLE")));
}

QString agentPublicDesktopLnk()
{
    const QString pub = qEnvironmentVariable("PUBLIC");
    return pub.isEmpty() ? QString() : (pub + QStringLiteral("/Desktop/星集控被控端.lnk"));
}

bool agentDesktopShortcutExists()
{
    const QString p = agentPublicDesktopLnk();
    return !p.isEmpty() && QFile::exists(p);
}

bool agentSetDesktopShortcut(bool on)
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

namespace { // ── 重新打开：开机自启/桌面快捷方式簇已提升（文件作用域），其余符号仍在匿名域

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
void openPlayScheduleDialog(); // 前向声明：P3 播放时段配置

// g_pluginHost 已提升文件作用域（namespace 上方），供 agent_panel 模块 extern 复用（main 拆分 #9）。
static AutomationEngine *g_automationEngine = nullptr; // 2026-10-27 Phase 3：规则引擎单例（executeAction 经此下发/拉取规则）
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
// readJsonlDir / openFolder / openNoiseLogDialog / openStudyLogDialog
// 已迁至 src/p3log/p3_reader.{h,cpp}（2026-10-30 main 拆分 #6b）。

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
    codeEdit->setPlaceholderText(QStringLiteral("班级码，或 6 位接入码"));
    form->addRow(QStringLiteral("班级（可选）"), codeEdit);
    auto *codeHint = new QLabel(QStringLiteral(
        "「保存并连接」与「用星璃账号绑定」两个按钮都会带上它："
        "接入码（6 位）会去云端换成班级，班级码直接上报云端按班归组。"
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
// TTS 统一播报已抽至 src/tts/tts_speaker.{h,cpp}（2026-10-30 main 拆分第一刀）。
// 本文件只留调用点（speakText() 经 tts/tts_speaker.h 声明）；SAPI 兜底、音量序列、
// token 去重、超时兜底全部封进新模块，main 只做装配。

// ── 灵动岛统一入口已迁至 src/notify/notify_entry.{h,cpp}（2026-10-30 main 拆分 #7）──
//   islandShow / islandShowAt / trayNotify / notifyFromParams + 设计注释整块迁出；
//   通知体系全局（g_tray/g_remoteControl/g_notifGate/g_loopReminder）与 sendActionReceipt
//   提升为文件作用域外部链接（namespace 上方/下方），本文件只留调用点。

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
 *   （原注释块剩余 ③④ 与结尾的分隔线随代码一并迁至 ota_download.cpp，此处补上闭合）
 */

// ── OTA 下载簇已迁至 src/ota/ota_download.{h,cpp}（2026-10-31 main 拆分 #16）──
//   startSelfUpdate（护栏：http(s)/sha256/文件名白名单/300MB 上限 → 异步下载 + Range 续传）、
//   otaOnFinished（关文件 → 成品流式重算 sha256 → 回滚三件套 → 派重启助手 → 启安装包）、
//   otaSelfCheck（周期 HTTP 复查 /api/public/ota → 交 applyUpdateInfo）、otaStartPeriodicCheck、
//   otaIsBusy 及内部状态 g_ota* / kOtaMaxBytes。均为外部链接，main.cpp 调用点与
//   ActionDeps / OtaDeps 填充行一字未改。



// ── 自习模式（2026-10-13）：噪音检测 + 噪音异常自动录制 ──────────────────────
// 定位：**静默监控**，不干扰学生（区别于考试模式的全屏拦截）。
//   · 进模式：周期测教室噪音（noise_monitor 短窗口：开→录→关，不常占麦、不驻留录音）
//     → 每拍把 dB 实时回传管理端（type:noise_sample，走订阅中继，不进事件流水）。
//   · 判异常：dB **连续 N 拍**都高于阈值才算（一次瞬时爆音不算，防误报）。
//   · 触发：自动录一段摄像头（recordSec 秒）→ 停 → 上传云端 → 上报 noise_alert。
// 自习状态全局（g_study*）整块已迁至本文件 namespace 上方（文件作用域），供 study_mode 模块 extern 复用。

static void sendNoiseSample(const QJsonObject &p)
{
    if (!g_ws || g_ws->state() != QAbstractSocket::ConnectedState) return;
    g_ws->sendTextMessage(makeEnvelope(QStringLiteral("noise_sample"), p));
    if (g_pluginHost) g_pluginHost->forwardEvent(QStringLiteral("noise_sample"), p);   // P2：转发给订阅插件
    ModuleHost::instance()->forwardEvent(QStringLiteral("noise_sample"), p);           // 内置模块：同事件同口径
}

// sendNoiseAlert / studyOnSample / studyTriggerRecord 已迁至 src/study_mode/study_mode.cpp（2026-10-30）

// 懒建噪音采集器（noise_monitor_start 与 study_mode_start 共用，避免两实例抢麦各报各的）
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
        if (g_studyOn) StudyMode::onSample(db);   // 自习模式：超阈判定（已迁 study_mode 模块）
    });
    QObject::connect(g_noise, &NoiseMonitor::failed, g_ws, [](const QString &reason) {
        qWarning().noquote() << QStringLiteral("[agent-qt] 噪音采集失败：%1").arg(reason);
        sendEvent(QStringLiteral("warn"), QStringLiteral("noise_monitor_failed"),
                  QJsonObject{ { QStringLiteral("reason"), reason } });
    });
    // 提醒环（2026-10-31）：3 次超阈 → 语音+事件；announcer 由噪音域装配创建，这里只接信号
    QObject::connect(g_noise, &NoiseMonitor::thresholdReached, getNoiseAnnouncer(), &NoiseAnnouncer::onNoiseThreshold);

    // ── P0-1 / P0-2（2026-10-30）：笔记 P0 噪音检测 ───────────────────────────
    // ① 启用自适应基线：笔记要「80dB」，但 dBFS→dB SPL 的偏置随麦克风/驱动变化，
    //    未校准时 80 是个**永远够不到的数**（探针已实锤触发 0 次）。
    //    自适应改为「学本教室安静底噪 + 35dB」，换设备/开 AGC 都不影响判定。
    //    这是**有意改变 active 的判定口径**：旧口径是固定 -45 dBFS（任意值，无物理意义），
    //    新口径＝本教室基线 + 35dB。下游若依赖旧语义需知悉。
    g_noise->setAdaptive(true);
    g_noise->setAdaptiveDelta(35.0);

    // P0-2 分层（2026-10-30）：噪音样本 → 存证链路协调器。判定/计数/抓帧/上报全在
    // src/noise/ 里，这里只做「把样本喂给协调器」的接线（纯数据搬运，无业务判断）。
    // 旧版把「累计 3 次→发 warn 事件」直接写进 main 的 lambda 已删除 —— 计数改由
    // NoiseIncidentService 负责、上报存证由 NoiseReporter 负责，main 不再写业务。
    QObject::connect(g_noise, &NoiseMonitor::noiseSample, g_noisePipeline,
                     [pipe = g_noisePipeline, mon = g_noise](double db, bool active, int seq) {
        Q_UNUSED(seq);
        if (pipe)
            pipe->onSample(db, active, mon->baselineDb(), mon->effectiveThresholdDb());
    });
}

// studyOnSample / studyTriggerRecord 已迁至 src/study_mode/study_mode.cpp（2026-10-30）

// ── P3 长期记录落盘内核已迁至 src/p3log/p3_log.{h,cpp}（2026-10-30 main 拆分 #6a）──
//   dataRoot / appendJsonl / logNoiseSample / logStudyStart / logStudyStop 迁出；
//   零 main 全局依赖，main 侧查询对话框与自习/噪音接线经 #include 同名调用。

// ── P3 播放时段管理：时段判断 ───────────────────────────────────
// 没开时段限制 → 随时可播；开了但窗口为空 → 也放开；否则落在任一窗口内才放行。
// ⚠️ 2026-10-23（§10 第四刀地基）：isPlayAllowedNow 已下沉到 src/media/MediaSession。
// 当前窗口距结束的剩余毫秒（无限制/无窗口返回 -1）。供「最大最小适配」：超长播放自动截断。
// ⚠️ 2026-10-23（§10 第四刀地基）：remainingMsInWindow 已下沉到 src/media/MediaSession。


// stopRemoteControlSession / reapRemoteControlIfStale 已迁至 src/remote/remote_control
//（main 拆分 #8），经 remote_control.h 声明可见。

// ── P3 播放时段管理：executeAction（1938 行）已迁至 src/action/execute_action.{h,cpp} ──
//（main 拆分 #10，2026-10-30）。匿名域符号经 g_actionDeps 注入访问（方案 B），
// 函数名/参数不变、语义等价；main() 填充 g_actionDeps（见 main 内填充点）。

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

// stopRemoteControlSession / reapRemoteControlIfStale 已迁至 src/remote/remote_control
//（main 拆分 #8），经 remote_control.h 声明可见。

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
        // 2026-10-24（闭环断点修复）：云端 registered 回执补带 classCode/className
        // （knownDevices 表真源，含**离线期被绑班**的设备——它 register 自报空码，
        // 本地 classcode.txt 也还没有，只能靠回执下发）。收到即幂等落盘：
        //   · saveClassCode 内部已判空 + 落盘 + 更新 g_classCode
        //   · 缩略图上报 readyCheck 门（!g_classCode.isEmpty()）靠它开启
        const QString rcClassCode = pay.value(QStringLiteral("classCode")).toString().trimmed();
        if (!rcClassCode.isEmpty())
            saveClassCode(rcClassCode);
        const QString rcClassName = pay.value(QStringLiteral("className")).toString().trimmed();
        if (!rcClassName.isEmpty())
            saveClassName(rcClassName);
        // 2026-10-25：用途回执（云端权威）。只在新版云端下发时才有值；
        // 老云端不带 ⇒ 保留本地值（不能当"改回教室机"，否则升级回退会把广播机打回原形）。
        const QString rcRole = pay.value(QStringLiteral("role")).toString().trimmed();
        if (!rcRole.isEmpty() && rcRole.toLower() != g_deviceRole) {
            saveDeviceRole(rcRole);
            qInfo("[agent-qt] 云端下发用途 = %s（下次打开面板生效）", qPrintable(g_deviceRole));
        }
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
    // 此分支保留只为兼容老云端：收到即幂等补起（起服真入口在 remote_control_start，见 execute_action）。
    if (type == QStringLiteral("tunnel.open")) {
#ifdef STE_USE_LIBVNCSERVER
        if (!VncServer::instance().isRunning()) {
            const quint16 p = VncServer::instance().start(0, 0);   // 兼容路径无 fps 参数，默认 10fps
            if (p == 0) {
                qWarning("[agent-qt] FAIL tunnel.open：VncServer 起服失败，忽略");
                return;
            }
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

    // 班级绑定下发（2026-10-24 · G4）：云端在绑班/改绑/移出后向**在线**设备推 class-bound
    // （registry.js notifyClassBoundIfOnline）。没有它，在线设备要等重连才知新码 ——
    // 缩略图 readyCheck 门（!g_classCode.isEmpty()）不开 ⇒ 班级工作台「暂无画面」。
    // code 为空 = 被移出班级：整组清（持久化文件 + 全局），班级域数据停报。
    if (type == QStringLiteral("class-bound")) {
        const QString code = pay.value(QStringLiteral("code")).toString().trimmed();
        const QString name = pay.value(QStringLiteral("name")).toString().trimmed();
        // 2026-11-02（班级绑定整合）：整组语义收进 ClassIdentity，不再在此手写
        // 「清码 → 清文件 → 顺手记得也清名」这种分步操作 —— 上次漏的正是最后一步。
        if (code.isEmpty()) {
            if (ClassIdentity::clearAll())
                qInfo("[agent-qt] 云端已将本机移出班级（班级码与班级名均已清，班级域上报停止）");
            return;
        }
        if (ClassIdentity::applyBind(code, name)) {
            qInfo().noquote() << QStringLiteral("[agent-qt] 云端下发班级绑定：%1（%2），已落盘")
                                     .arg(code, name.isEmpty() ? QStringLiteral("（未带班名）") : name);
        }
        return;
    }

    // 2026-11-02（课表同步）：管理端下发课表 → 落盘 → 时钟热重载 → 回执。
    // 流水线整段在 schedule/timetable_apply_wire.cpp（main 只留"认领"这一行）。
    if (type == QStringLiteral("timetable-apply")) {
        TimetableApplyWire::handle(pay);
        return;
    }

    // 发布中心（P1 · 2026-10-24）：管理端发布的卡片 → 按类型分流到既有展示通道。
    // 类型→形态的映射是纯逻辑（core/BlackboardCard），这里只负责把结果交给
    // notifyFromParams —— 考试门控 / 本机通知总开关 / 托盘留存 / TTS 朗读 全在它内部，
    // 复用即自动生效（另写一套展示必然与它漂移，两份真源铁律）。
    if (type == QStringLiteral("blackboard-card")) {
        QJsonObject np;
        QString why;
        if (!BlackboardCard::toNotifyParams(pay, np, &why)) {
            qWarning().noquote() << QStringLiteral("[agent-qt] 忽略黑板卡片：") << why;
            return;
        }
        const QString err = notifyFromParams(np);
        if (err.startsWith(QLatin1String("skipped:"))) {
            // 考试中门控静默丢弃 —— 与 notify 指令同口径：转 skipped，不回 failed
            // （管理端看到 failed 会重试/报警，和「像没收到一样」正好相反）。
            qInfo().noquote() << QStringLiteral("[agent-qt] 黑板卡片被门控丢弃：") << err.mid(8);
        } else if (!err.isEmpty()) {
            qWarning().noquote() << QStringLiteral("[agent-qt] 黑板卡片展示失败：") << err;
        }
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

    // 2026-10-26：被控端访问密码下发（云端统一设置/修改，明文经已鉴权设备 WS 到本机）。
    // 云端是鉴权权威；本机落盘供记录与未来本地鉴权用。真正的访问闸门在云端中继处。
    if (type == QStringLiteral("vnc-access")) {
        const bool enabled = pay.value(QStringLiteral("enabled")).toBool(false);
        const QString password = pay.value(QStringLiteral("password")).toString();
        saveVncAccessLocally(enabled, password);
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
        // 2026-10-24：预览帧按 480p 档送出（原生 1080p/2K 直接压 JPEG，单帧 200-400KB，
        //   在 CF 隧道上是纯浪费）。长边限到 previewMaxEdge()（默认 854），保比例不放大。
        //   只影响**网络帧**；outDir 落盘仍用原始 pm，取证不受影响。
        QPixmap net = pm;
        const int maxEdge = previewMaxEdge();
        if (maxEdge > 0) {
            const int longEdge = qMax(pm.width(), pm.height());
            if (longEdge > maxEdge) {
                net = pm.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
        }
        QBuffer buffer(&jpeg);
        buffer.open(QIODevice::WriteOnly);
        if (!net.save(&buffer, "JPEG", 60)) {
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
        // 2026-10-24：配额由 150KB/s 放开到可配（默认 400KB/s，0=不限）。
        //   原 150KB/s 是"CF 隧道劣化到 250KB/s 时保心跳"的应急值，长期挂着等于
        //   把 4Mbps 的线当 1.2Mbps 用。真限流大头在 CF 隧道，自设帽只会让它吃不满。
        //   心跳保护仍由上面的 bytesToWrite(1MB) 积压闸兜底 —— 那条才是真正防踢人的。
        const qint64 quota = previewQuotaBytes();
        if (quota > 0 && !grace && winBytes + jpeg.size() > quota) {
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

// ── sendActionReceipt：文件作用域定义（从匿名 namespace 提出，供 notify_entry 模块 extern 复用）──
void sendActionReceipt(const QString &id, const QString &action, const QString &result,
                       const QString &error, const QJsonObject &data)
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
// ── sendEvent：文件作用域定义（从匿名 namespace 提出，供 study_mode 模块 extern 复用）──
void sendEvent(const QString &level, const QString &msg, const QJsonObject &extra)
{
    if (!g_ws || g_ws->state() != QAbstractSocket::ConnectedState) return;   // 没连就不发（遥测丢一拍无所谓）
    QJsonObject p;
    p.insert(QStringLiteral("level"), level);
    p.insert(QStringLiteral("msg"), msg);
    if (!extra.isEmpty()) p.insert(QStringLiteral("extra"), extra);
    g_ws->sendTextMessage(makeEnvelope(QStringLiteral("event"), p));
}


// agentEnvPath()（2026-10-30 提升为文件作用域外部链接：原在匿名 namespace 内，因 OTA 渠道
// 持久化模块跨 TU 引用而迁出；逻辑与原地逐字一致。）
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

// ── 录像/班级监控上传（2026-10-30 迁出 → src/upload/recording_upload.{h,cpp}）──
//   定义已不在本文件；MediaSession.cpp 经 #include "upload/recording_upload.h" 调用，
//   g_uid 的跨 TU 引用在上传模块内处理。

// ── U5-A3：QML 控制面板（2026-10-16）──────────────────────────────────
// header-only（AgentUi + AgentShell 两个 Q_OBJECT 类），内联实现直接调用上面
// 匿名域里的 static 自由函数与 AgentSettings —— 匿名域符号在本 TU 内全程可见，
// 但本 include **绝不能放进匿名域**（Qt 头会被拽进域内 ⇒ QVariant/QtPrivate 与
// 全局的同名声明冲突，C2872 ambiguous，实测首编即炸）。只被本文件 include，
// 见 agent_ui.h 顶部说明。
#include "agent_ui.h"

// ── U5-A3：QML 控制面板的数据接线（2026-10-30 main 拆分 #9 迁出）──
//   buildSelfCheckItems / buildPluginList / pluginRegistryUrl / s_storeEntries /
//   agentUiFill（AgentUi 的 friend）/ agentUiWire 已整体迁至 src/ui/agent_panel.cpp。
//   agent_ui.h 的 AgentShell 构造仍调 agentUiWire(&m_ui)；main() 启动时填充
//   g_panelDeps（匿名域依赖函数地址），agent_panel.cpp 经它访问（方案 B）。

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
    // ── 控件风格＝Basic（2026-10-29 新增）──────────────────────────────
    // 用户裁决：UI 全量替换为 **QtShadcn**，被控端原「零 QtQuick.Controls 依赖、
    //   手写 ash 组件库」铁律同步废止（详见 CMakeLists 里的铁律变更记录）。
    // QtShadcn 组件是靠覆写 QQC 的 background/contentItem 实现的 ⇒ 宿主风格
    // **必须**是 Basic，否则覆写被宿主风格吃掉，表现为「组件渲染出来了但样式
    // 完全不对」的静默故障。
    // 写进程环境而非 QQuickStyle::setStyle：风格插件加载时读，必须早于 app 构造
    // （与管理端同款写法；2026-10-21 的教训：风格插件可能在 app 构造期间就被加载）。
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");

    QApplication app(argc, argv);   // 要托盘 → QApplication（托盘在 QtWidgets 里）

    // 启动耗时埋点（2026-11-03 · 用户笔记序号 3「启动慢」）：实现见 src/system/boot_trace.h
    BootTrace::mark("qapp");

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

    // 2026-10-28 模块三：设置域初始化（**必须在此处、越早越好**）。
    //   它做两件事：① 读上次的运行标记 → 判定上次是否异常退出；
    //   ② 写本次标记（有序退出时 aboutToQuit 删掉）。
    //   放在日志/单例锁之前的原因：这是"上一次为什么没起来/为什么崩"的第一手证据，
    //   后续任何一步 fail-silent 都不该把这条判据吃掉。
    SettingsDomain::init(dataDir);
    BootTrace::mark("settings-domain");
    // 2026-10-21（审查 #11）：原来用 qEnvironmentVariable(key, default) —— 变量存在但
    // 为空字符串时返回空（不是 default），部署脚本写了 `set STE_QT_SHOT_DIR=` 就会把
    // 截图路径顶成空、图写到盘根。与本文件其他目录统一走 envOr（空串回落默认值）。
    g_shotDir = envOr("STE_QT_SHOT_DIR", dataDir + QStringLiteral("/shots"));
    // ── QML 控制面板依赖注入（main 拆分 #9，方案 B）─────────────────────
    // main() 与匿名域函数同一 TU ⇒ 可取其地址（内部链接不跨 TU，只能在这填）。
    // AgentShell 懒建（托盘点开才 ensure）⇒ 本行必先于任何 agentUiWire 调用。
    static AgentPanelDeps s_panelDeps = {
        &compareVersion, &otaSelfCheck, &saveAgentEnv,
        &openConfigDialog, &updateTray, &connectNow,
        &startSelfUpdate,
    };
    g_panelDeps = &s_panelDeps;

    // ── 指令执行依赖注入（main 拆分 #10，方案 B）──────────────────────────
    // 与 #9 同理：main() 与匿名域符号同一 TU ⇒ 可取其地址。executeAction 经
    // g_actionDeps 访问全部匿名域符号（19 全局量 + 20 函数），函数名/参数不变。
    static ActionDeps s_actionDeps = {
        // 19 个匿名域全局量（指向真实变量的指针）
        &g_fileRecvBytes, &g_fileRecvFile, &g_fileRecvLastMs, &g_fileRecvName,
        &g_fileRecvNext, &g_fileRecvTarget, &g_fileRecvTotal, &g_frozenPids,
        &g_lastInstrId, &g_noise, &g_recvDir, &g_schedFile, &g_schedTimerById,
        &g_soft, &g_softPolicyFile, &g_terminalDone, &g_automationEngine,
        &g_studyOn, &g_studyStartTs,
        // 20 个匿名域函数
        &armScheduleTimer, &enableShutdownPriv, &humanBytes, &loadSchedules,
        &markTerminalDone, &memMbOf, &pathInside, &processPathOf, &protectedCategory,
        &saveSchedules, &saveSoftPolicy, &schedFireAllowed, &setNetBlocked,
        &setProcessFrozen, &softHit, &windowTitleOf, &ensureNoise,
        &visibleWindowsCached, &snapshotProcessNames, &windowIconBase64,
        &enumWindowsCb,
    };
    g_actionDeps = &s_actionDeps;
    // 调度/软控策略域注入（main 拆分 #14）：enableShutdownPriv 定义仍在匿名域（内部链接），
    // main() 同 TU 取地址注入 PolicyDeps（execute_action.cpp 经 g_actionDeps 函数指针访问，零改动）。
    policyInit(PolicyDeps{ &enableShutdownPriv });
    // OTA 自更新动作层注入（main 拆分 #15）：startSelfUpdate / otaIsBusy 定义仍在匿名域
    // （内部链接），main() 同 TU 取地址注入 OtaDeps（模块侧经 g_otaDeps-> 访问）。
    otaUpdateInit(OtaDeps{ &startSelfUpdate, &otaIsBusy });
    // 本地 API 鉴权网关（2026-10-31 · 三条已知缺口之一）：127.0.0.1:8080，仅回环。
    // 失败只记日志不阻塞；业务全在 src/localapi/（main 拆分纪律，只加这一行）。
    localApiInit();
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
    BootTrace::mark("single-instance");

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
    qInfo("[agent-qt] SMTC 系统媒体识别已启动（800ms 轮询，独立 MTA 线程）");
    const int envN = loadEnvFile(agentEnvPath());
    BootTrace::mark("env+class-identity");
    // 班级身份域初始化（2026-11-02）：把两个全局变量**按引用**交给域，域内不再存副本。
    // 灵动岛菜单栏的同步做成回调而不是域内直调 IslandOverlay —— 域不反向依赖 UI 单例，
    // 否则探针（无岛实例）与设置页预览（要建岛）会走出两条不同的路径。
    ClassIdentity::init(qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong"),
                        g_classCode, g_className,
                        [](const QString &n) {
                            if (IslandOverlay *isl = IslandOverlay::instance()) isl->setClassName(n);
                        });
    if (envN > 0) qInfo("[agent-qt] 已从 agent.env 读入 %d 项配置（%s）", envN, qPrintable(agentEnvPath()));
    ClassIdentity::loadPersisted();   // 2026-11-02：班级码 + 班级名一起载入（域内两文件各自独立）
    loadDeviceRole();  // 2026-10-25：载入用途（决定 register 自报与外壳），默认教室机

    // ── Win7 渲染兜底（2026-10-25 · 现场实证）────────────────────────────
    // 现象：Win7+VxKex 上，灵动岛等**无边框透明窗口**整块窗口矩形被填成不透明黑底
    //   （rc.4 现场，用户报「黑色不透明背景显示出来」，属渲染通病不是单个界面）。
    //
    // 实证（同一台 Win7 实机，非推演）：
    //   · 0.6.48 + agent.env 写 QT_QUICK_BACKEND=software ⇒ `set qt` 回读确认生效；
    //     灵动岛压在**纯黑 cmd 窗口**上做像素采样：岛本体黑占 0.8%（主色 rgb(240,240,240)），
    //     岛四角包围盒黑占 0.0%（100% 透出背景）—— 透明合成完全正常，黑底消失。
    //   · rc.4 → 0.6.48 之间**没有任何渲染相关改动**（只加了日志埋点/退出埋点/QML 守卫），
    //     所以唯一变量就是那个 env ⇒ Win7 的 GPU 后端（d3d11/opengl）在 alpha 合成上有缺陷，
    //     software 后端（光栅）正常。
    //
    // 所以这里**兜底**，让每台 Win7 自动拿到正确后端，不必逐台手配 agent.env：
    //   · 只认真实内核 6.1 —— 走 ntdll!RtlGetVersion。**不能用 QOperatingSystemVersion /
    //     QSysInfo**：VxKex 补了版本查询 API，Win7 在 Qt 眼里会谎报成 Win10，用它判断
    //     会恰好漏掉这批最需要兜底的机器。（RtlGetVersion 读的是内核真值，VxKex 只补用户态 API）
    //   · 绝不覆盖部署侧的显式选择：agent.env / 真环境里只要写了 QT_QUICK_BACKEND、QT_OPENGL
    //     或 QSG_RHI_BACKEND 任一项，就原样尊重（灰度机要做 A/B 时不至于被代码抢走控制权）。
    //   · Win8+ 一概不动：那边 GPU 后端没问题，套 software 只是白丢性能（教室机几十台的量级）。
    {
        struct OsVersionInfoW_ {
            ULONG dwOSVersionInfoSize;
            ULONG dwMajorVersion;
            ULONG dwMinorVersion;
            ULONG dwBuildNumber;
            ULONG dwPlatformId;
            WCHAR szCSDVersion[128];
        };
        bool isRealWin7 = false;
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll) {
            using FnRtlGetVersion = LONG(WINAPI *)(OsVersionInfoW_ *);
            FnRtlGetVersion fn = (FnRtlGetVersion)GetProcAddress(ntdll, "RtlGetVersion");
            if (fn) {
                OsVersionInfoW_ vi = {};
                vi.dwOSVersionInfoSize = sizeof(vi);
                if (fn(&vi) == 0)
                    isRealWin7 = (vi.dwMajorVersion == 6 && vi.dwMinorVersion == 1);
            }
        }
        const bool backendPinned = !qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND")
                                || !qEnvironmentVariableIsEmpty("QT_OPENGL")
                                || !qEnvironmentVariableIsEmpty("QSG_RHI_BACKEND");
        if (isRealWin7 && !backendPinned) {
            qputenv("QT_QUICK_BACKEND", "software");
            qInfo("[render] Win7 内核 6.1 且未显式指定渲染后端 → 兜底 QT_QUICK_BACKEND=software"
                  "（Win7 GPU 后端透明合成有缺陷，software 实测正常）");
        } else if (isRealWin7) {
            qInfo("[render] Win7 内核 6.1，但部署侧已显式指定后端 → 不覆盖（尊重配置）");
        }
    }

    // ── 渲染诊断（2026-10-25 · Win7 黑底排查）──────────────────────────
    // 被控端透明窗口（灵动岛/弹幕）在 Win7+VxKex 上出现「黑色不透明背景」。
    // 日志打三行，下轮现场日志一锤定音：
    //   ① sceneGraphBackend() 实际决议值 —— Qt 会读 env；本程序从不 setSceneGraphBackend，
    //      这里的读取不会提前烧死任何后端配置，只是「问」Qt 已怎么定。
    //   ② 相关 env 现值 —— agent.env 可在部署侧预设 QT_QUICK_BACKEND=software 做 A/B。
    //   ③ OS 版本（VxKex 谎报后 Qt 看到的）+ DWM 合成状态（Win7 Aero 关闭=透明链另一大嫌疑）。
    // 必须排在此处：loadEnvFile（上方 6908）已把 agent.env 读入；任何 QQuickWidget/岛窗口
    // 尚未创建 —— 早于这里调用 sceneGraphBackend() 会看到空的 env；晚于岛窗口则已成定局。
    qInfo("[render] sceneGraphBackend=%s | env QT_QUICK_BACKEND=%s QT_OPENGL=%s QSG_RHI_BACKEND=%s",
          qPrintable(QQuickWindow::sceneGraphBackend()),
          qEnvironmentVariable("QT_QUICK_BACKEND").constData(),
          qEnvironmentVariable("QT_OPENGL").constData(),
          qEnvironmentVariable("QSG_RHI_BACKEND").constData());
    qInfo("[render] OS=%s Qt=%s", qPrintable(QSysInfo::prettyProductName()), qVersion());
    {
        BOOL dwmOn = FALSE;
        using FnDwm = BOOL(WINAPI *)(BOOL *);
        HMODULE dwm = LoadLibraryA("dwmapi.dll");   // 动态加载，不新增链接依赖
        if (dwm) {
            FnDwm f = (FnDwm)GetProcAddress(dwm, "DwmIsCompositionEnabled");
            if (f) f(&dwmOn);
            FreeLibrary(dwm);
        }
        qInfo("[render] DWM composition=%s", dwmOn ? "ON" : "OFF");
    }

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
    const QString schedDir = envOr("STE_QT_SCHED_DIR", dataDir + QStringLiteral("/sched"));
    QDir().mkpath(schedDir);
    policySetSchedDir(schedDir);
    AlarmDomain::init(schedDir + QStringLiteral("/alarms.json"), speakText);   // 闹钟与开关机同目录（2026-10-26 P1）
    AlarmDomain::rescheduleAlarms();                                          // 崩溃/重启后重挂闹钟
    // 2026-10-27 第六刀：定时通知域 —— 注入触发回调（转 islandShow，带渠道标签）。
    // 不做成域内直接调通知渲染：底层域不反向依赖主文件（与 speakText 注入同款）。
    NotifyDomain::init(schedDir + QStringLiteral("/notifies.json"),
                       [](const NotifyDomain::NotifyTask &t) {
        if (!t.channel.isEmpty())
            IslandOverlay::instance()->setChannelForNext(t.channel);
        islandShow(t.title, t.content, QStringLiteral("bell"), 6000, false,
                   QString(), IslandOverlay::Form::Capsule, false);
    });
    NotifyDomain::reschedule();

    // ── 时间同步（2026-10-27 · G3 灵动岛拓展第二项）：NTP 校时 + 人工偏移 ──────
    // 为什么放在这里：日历/课表/提醒三条链都读"现在几点"，校时必须在它们之前就位。
    // ⚠️ 课表时钟（本函数后半段）才创建 —— 用 static 指针把"偏移变了"转给它：
    //    未建时回调是空操作（不崩），建时立刻补一次当前校正量。
    // ⚠️ **不改系统钟**：skew 只作用于判定（schedule_clock 的 tick）。
    TimesyncDomain::init(dataDir + QStringLiteral("/timesync.json"));
    TimesyncDomain::setOnChanged([] {
        if (g_schedClock) g_schedClock->setClockSkewSec(TimesyncDomain::skewMs() / 1000);
    });
    TimesyncDomain::start();

    // ── PPT 放映控制（2026-10-27 · I 批）：只连已运行的 PowerPoint / WPS ────────
    // 偏好（auto/powerpoint/wps）落 ppt.json；**不做启动**，启动走 launch_app。
    PptDomain::init(dataDir + QStringLiteral("/ppt.json"));

    // ── I3：批注跟 PPT 翻页（2026-10-27）────────────────────────────────
    // 注入"当前放映页码"取数函数（0 = 没在放映）。批注域不认识 PPT，只认页键；
    // 这样 PPT 换成别的放映器时只需改这一行。取数用 query()（只 attach 不启动，
    // 见 ppt_domain.h 的红线），由批注层在自己的定时器里按需调用（1.5s，仅可见时）。
    AnnotateDomain::init([]() -> int {
        const PptDomain::Status st = PptDomain::query();
        return (st.ok && st.presenting) ? st.slide : 0;
    });
    // 用户 2026-10-27 点单「PPT自动打开」：批注一起来，就让**已在运行**的演示软件进放映。
    // 不拉起进程（ppt_domain 红线）：老师没开 PPT 时这里只会拿到一条失败原因，
    // 静默记日志即可 —— 他也许只是想批注桌面。
    AnnotateDomain::setAutoShowFn([]() -> bool {
        PptDomain::Status st;
        QString  why;
        const bool on = PptDomain::autoStartShow(&st, &why);
        if (!on && !why.isEmpty())
            qInfo("[agent-qt] 批注自动放映未触发：%s", qPrintable(why));
        return on;
    });

    // ── 软件管理策略（2026-10-26 P3）：黑白名单 + 黑名单守护 ──
    policyStart(dataDir + QStringLiteral("/soft-manage.json"), &app);
    g_mediaDir = envOr("STE_QT_MEDIA_DIR", dataDir + QStringLiteral("/media"));
    QDir().mkpath(g_mediaDir);

    // ── file_push 的收件目录（2026-10-04 补上）──
    // 和上面 g_schedDir / g_mediaDir 是同一类缺口：这行只声明过、从来没赋过值，
    // 于是 file_push 算出来的落盘路径是 "/文件名" —— 直接写进当前盘根目录（C:\），
    // 同时 g_shotDir/g_mediaDir 那套目录约定在这里也是断的。收件文件统一进 ~\incoming。
    g_recvDir = envOr("STE_QT_RECV_DIR", dataDir + QStringLiteral("/incoming"));
    QDir().mkpath(g_recvDir);
    BootTrace::mark("domains-sched-notify-timesync");

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
        // 2026-11-03（用户笔记序号2）：退出前询口令。没设口令时 confirmQuit() 直接 true，
        // 行为与之前完全一致；设了才弹框。只挡这一条路（关机/任务管理器/OTA 自重启不挡）。
        QObject::connect(actQuit, &QAction::triggered, &app, [] {
            if (QuitGuard::confirmQuit()) QCoreApplication::quit();
        });
        g_tray->setContextMenu(menu);
        if (g_unconfigured) {
            // 配置校验时已落过盘；这里把图标刷成"未配置"，气泡留到 show() 之后再弹
            updateTray(TrayState::Unconfigured,
                       QStringLiteral("未配置：%1（右键托盘 →「配置…」）").arg(unconfiguredReason()));
        } else {
            updateTray(TrayState::Disconnected, QStringLiteral("正在连接…"));
        }
        g_tray->show();             // 先设图标再 show，避免 "No Icon set" 警告
        BootTrace::mark("tray-show");
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
        BootTrace::mark("island-idle");
        AgentShell::wireIslandEditorHook();   // 岛收起条长按 → 编辑（m16757，实现在 .cpp）
        // ── U5-A3 验证开关（2026-10-16）：STE_QT_SHELL_AUTOOPEN=1 → 启动 1.5s 后
        //    自动开一次 QML 控制面板。面板是懒建的（不点托盘不加载 QML），
        //    无人值守验证 QML 真实加载全靠它；日常不设即零影响。
        if (qEnvironmentVariable("STE_QT_SHELL_AUTOOPEN") == QStringLiteral("1")) {
            // 2026-11-03：延时可用 STE_QT_SHELL_AUTOOPEN_MS 覆盖（默认 1500）。
            // 为什么需要：要验「面板预热是否真让首开变快」，自动开必须排在
            // prewarmPanel（默认 3s）之后，否则量到的还是未预热的首开 —— 已踩过。
            const int openMs = qEnvironmentVariableIntValue("STE_QT_SHELL_AUTOOPEN_MS") > 0
                                   ? qEnvironmentVariableIntValue("STE_QT_SHELL_AUTOOPEN_MS")
                                   : 1500;
            QTimer::singleShot(openMs, &app, [] {
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

    // ── 面板预热（2026-11-03 · 用户笔记序号 3「启动慢」）─────────────────
    // 实测：托盘 137ms / 岛 777ms / **面板首开 1732ms**（构造+解析 1340 + 显示 392）——
    // 用户感知的"慢"就是点托盘后那 1.3 秒。启动空档期（3s 后，岛已稳定）提前建好，
    // 首次点击就只剩 show。放托盘分支之外；show→hide 不留任务栏痕迹，env 可关。
    AgentShell::prewarmPanel();

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
        // ⚠️ 广播机不参与班级域（2026-10-25）：它的 classCode 是**站码**，
        // 上报缩略图会让班级工作台的设备网格里多出一台"广播机"，
        // 而它根本没有教室画面语义。用途是第二维身份，门禁必须读它。
        return !g_classCode.isEmpty()
            && g_deviceRole == QStringLiteral("classroom")
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
        // 每秒配额窗口（整条链路共用一组记账）
        // 2026-10-24：由写死 200KB/s 改为可配 STE_VNC_KBPS（默认 800 KB/s ≈ 6.4Mbps）。
        //   原 200KB/s 是"CF 隧道劣化到 250KB/s 时保心跳"的应急值，长期挂着等于
        //   把链路当 1.6Mbps 用；真限流大头在 CF 隧道，自设帽只会让它吃不满。
        //   心跳保护仍由上面的 1MB 积压闸兜底（它才是真正防踢人的那道）。
        static const qint64 vQuota = [] {
            int kbps = qEnvironmentVariableIntValue("STE_VNC_KBPS");
            if (kbps == 0 && qEnvironmentVariableIsSet("STE_VNC_KBPS")) return qint64(0);  // 显式 0 = 不限
            if (kbps <= 0) kbps = 800;
            return qint64(qBound(100, kbps, 20000)) * 1024;
        }();
        const qint64 vMegaBytes = qMax<qint64>(64 * 1024, vQuota / 2);   // "整屏基础图"判定阈值随配额走
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
        if (grace || (data.size() >= vMegaBytes && !g_vncMegaDone)) {
            if (!grace) g_vncMegaDone = true;   // 宽限期内不消耗一次性豁免
            vWinBytes += data.size();           // 这一秒的配额确实被它占了，照常记账
            g_ws->sendBinaryMessage(makeVncFrameBytes(data));
            qInfo("[vnc-probe] up 已发 %d 字节（%s）", data.size(),
                  grace ? "会话起步宽限" : "整屏基础图一次性豁免");
            return;
        }
        if (vQuota > 0 && vWinBytes + data.size() > vQuota) {
            static int vBwSkipped = 0;
            if (++vBwSkipped % 50 == 1)
                qWarning("[agent-qt] [vnc] 本秒 up 流量已到 %lld KB 配额，丢弃本批保心跳（累计丢 %d 批）",
                         vQuota / 1024, vBwSkipped);
            return;
        }
        vWinBytes += data.size();
        g_ws->sendBinaryMessage(makeVncFrameBytes(data));
        qInfo("[vnc-probe] up 已发 %d 字节", data.size());
    });
    QObject::connect(&H264Streamer::instance(), &H264Streamer::h264FrameReady, &app, [](const QByteArray &nalu) { if (g_ws && g_ws->state() == QAbstractSocket::ConnectedState) g_ws->sendBinaryMessage(makeH264FrameBytes(nalu)); });  // A档（2026-10-31）H264 推流：NALU→h264-data 帧发主 WS（与 VncBridge 对称；MF 0 产出时静默无帧）
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
        frameTimer->start(previewIntervalMs());
        qInfo("[agent-qt] 预览流：间隔 %d ms，配额 %lld KB/s，长边 %d px（均可 env 覆盖）",
              previewIntervalMs(), previewQuotaBytes() / 1024, previewMaxEdge());
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
        // 2026-10-27（G3）：接上时间同步的校正量 —— 之后 NTP 成功/人工偏移变更
        // 都会经 TimesyncDomain 的回调重设（见上方 init 块）。
        g_schedClock = clock;
        clock->setClockSkewSec(TimesyncDomain::skewMs() / 1000);
        // 2026-11-02（课表同步）：注入课表下发流水线的三个依赖（时钟/班级码/回执）。
        // 用取值函数而非裸指针：班级码与 g_ws 之后还会被换掉（重连/绑班）。
        TimetableApplyWire::init({
            []() -> ScheduleClock * { return g_schedClock; },
            []() -> QString { return g_classCode; },
            [](const QString &ty, const QJsonObject &rc) {
                if (g_ws && g_ws->state() == QAbstractSocket::ConnectedState)
                    g_ws->sendTextMessage(makeEnvelope(ty, rc).toUtf8());
            }});

        // ── P0-2 分层装配（2026-10-30）：噪音存证链路 + 提醒环 ──────────────
        // 具体类型 new/接线收在 createNoisePipeline；这里持有协调器，并翻译课表节次为周期 key。
        g_noisePipeline = createNoisePipeline(&app);
        QObject::connect(clock, &ScheduleClock::periodStarted, g_noisePipeline,
            [pipe = g_noisePipeline](const QString& name, const QTime&) {
                // 周期 key = 日期#节次名（如 2026-10-30#第一节 · 语文）；换节次即换统计周期。
                pipe->setPeriodKey(
                    QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")) + QStringLiteral("#") + name);
            });
        QObject::connect(clock, &ScheduleClock::periodEnded, g_noisePipeline,
            [pipe = g_noisePipeline]() { pipe->setPeriodKey(QString()); });
        // 2026-10-27（G5）：把课表源交给面板域 —— 日期/课表/倒计时三面里，
        // 课表与"默认倒计时"都要读"今天上什么"。注入而不是让面板域自己 new 一个：
        // 课表必须与上下课提醒**同一份判定数据**，两份会分叉（档案换了只刷新一边）。
        PanelDomain::init(clock);
        QObject::connect(clock, &ScheduleClock::periodStarted, &app,
            [](const QString& name, const QTime& at) {
                const QString txt = QStringLiteral("%1 · %2").arg(name, at.toString("HH:mm"));
                // 课程提醒：渠道「课程提醒」+ 岛上胶囊 + 不进历史（一天十几次会淹掉真通知）；
                // provider=class ⇒ 设置页关掉「课程提醒」后整条不弹（门控见 provider_registry.h）。
                // 三条 hint 都是一次性，必须在 islandShow 之前设。
                IslandOverlay::instance()->setProviderForNext(QStringLiteral("class"));
                IslandOverlay::instance()->setChannelForNext(QStringLiteral("课程提醒"));
                IslandOverlay::instance()->setSkipHistoryForNext(true);
                islandShow(QStringLiteral("上课啦"), txt, QStringLiteral("bell"),
                           6000, false, QString(), IslandOverlay::Form::Capsule, false);
                speakText(QStringLiteral("上课啦") + QStringLiteral("：") + txt);
                qInfo().noquote() << "[agent-qt] 上课提醒已弹: " << name;

                // ── P0-1（2026-10-30）：笔记「噪音检测只在上课时段、强制开启」──
                // 上课即起采集：本来要等云端 noise_monitor_start 指令，现在由课表驱动。
                // ⚠️ 仍然**只测不录**（noise_monitor 从不落音频样本），也别在这里加录音——
                //    那是 P0-2 存证（1 帧截屏 + 1 帧摄像头）的事，且有 5 分钟保留约束。
                ensureNoise();
                if (g_noise) {
                    g_noise->resetTriggerCounter();   // 新的一节课，重新计数
                    g_noise->resetBaseline();         // 底噪可能变了（开窗/空调/换教室）
                    if (!g_noise->running()) g_noise->start(16000, 2000);
                    qInfo().noquote() << QStringLiteral("[agent-qt] 上课：噪音采集已开启（自适应基线）");
                }
            });
        QObject::connect(clock, &ScheduleClock::periodEnded, &app,
            [](const QString& name, const QTime& at) {
                const QString txt = QStringLiteral("%1 · %2").arg(name, at.toString("HH:mm"));
                // 同上课提醒：provider=class + 渠道「课程提醒」+ 岛上胶囊 + 不进历史（见上方）
                IslandOverlay::instance()->setProviderForNext(QStringLiteral("class"));
                IslandOverlay::instance()->setChannelForNext(QStringLiteral("课程提醒"));
                IslandOverlay::instance()->setSkipHistoryForNext(true);
                islandShow(QStringLiteral("下课啦"), txt, QStringLiteral("bell"),
                           6000, false, QString(), IslandOverlay::Form::Capsule, false);
                speakText(QStringLiteral("下课啦") + QStringLiteral("：") + txt);
                qInfo().noquote() << "[agent-qt] 下课提醒已弹: " << name;

                // ── P0-1（2026-10-30）：下课即停并**释放麦克风**（笔记：只在上课时段）──
                // 是 stop 不是静音：stop() 会停定时器、清 busy 标记，下一轮不再开设备。
                // 这样课间麦克风完全不被占用（语音对讲/别的程序可正常用）。
                if (g_noise && g_noise->running()) {
                    g_noise->stop();
                    qInfo().noquote() << QStringLiteral("[agent-qt] 下课：噪音采集已停止，麦克风释放");
                }
            });
        // 2026-11-02（插件大升级 #1）：课表事件总线。
        // `schedule_change` 在 pluginhost.cpp:246 早就被列为需 events:schedule 权限的
        // 可订阅事件，但全库**从未 emit 过** —— 插件订阅了永远收不到，且不报错、
        // 不打日志，属最难查的一类。这里把三条边界信号一次性接到总线上。
        // 两个 sink 都惰性解引用全局指针：本节执行时 g_pluginHost 还是 nullptr（在后面才 new）。
        ScheduleEventBus::init({ [](const QString &n, const QJsonObject &p) {
                                     if (g_pluginHost) g_pluginHost->forwardEvent(n, p);
                                 },
                                 [](const QString &n, const QJsonObject &p) {
                                     ModuleHost::instance()->forwardEvent(n, p);
                                 },
                                 nullptr });
        ScheduleEventBus::attachClock(clock);
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
    // 2026-11-02（插件大升级 #4）：13 个能力指针的形状转换全部搬进 plugin_api_bridge，
    // 这里只留"给谁、什么时候给"。扩插件能力从此不用动 main.cpp。
    PluginAgentApi api = pluginApiBridgeBuild(g_schedClock);
    g_pluginHost->setAgentApi(api);
    if (g_pluginHost->start()) {
        // 2026-10-28 模块三「安全模式（意外崩溃防护）」：
        // 宿主照常起来（本地 WS / 能力注入都在，插件市场对话框不会空指针），
        // 但**不加载**已启用插件 —— 一台被插件拖崩过的机器，重启后能直接上课，
        // 而不是"又崩一次"。老师改完设置页里的开关再重启即恢复。
        if (SettingsDomain::safeModeActive()) {
            qInfo("[agent-qt] 安全模式：插件宿主已就绪，**跳过** loadEnabled（本次不加载任何第三方插件）");
        } else {
            g_pluginHost->loadEnabled();   // 启动 QSettings 中已启用的插件
        }
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

        // 定时自动化规则引擎（2026-10-27 Phase 3）：纯模型 + 1s 调度，本机自运行不依赖管理端在场。
        // 点火时 emit actionRequested(action)，这里接到既有 executeAction —— 复用整套设备指令词表
        // 及其安全白名单（launch_app 白名单 / notify 门控等），零新攻击面。
        // 注入岛 QML 为 `automationObj`（与 voicehubObj 同模式，IslandRoot 读 automationRef 带 null 守卫）。
        {
            auto *engine = new AutomationEngine(qApp);
            g_automationEngine = engine;
            QObject::connect(engine, &AutomationEngine::actionRequested,
                             [](const QJsonObject &a) {
                                 executeAction(a.value(QStringLiteral("action")).toString(), a);
                             });
            IslandOverlay::instance()->setQmlObjectProperty(QStringLiteral("automationObj"), engine);
            // 事件触发器：开机即评估一次 "boot" 型规则（其余事件由对应子系统在发生时调用 engine->onEvent）。
            engine->onEvent(QStringLiteral("boot"));
        }

        // 广播社成员模块（2026-10-23 #广播站移交）：独立身份登录通道，账号在云端自管。
        // 不注册为内置模块（不是可启停插件），仅作为 QObject 注入控制面板上下文属性
        // `broadcastClub`，供新页 BroadcastClubPage 调用。base URL 复用 agent 自身云端地址。
        {
            auto *bc = new BroadcastClubModule();
            bc->setBaseUrl(siteBaseFromWsUrl(qEnvironmentVariable("STE_QT_WS_URL")));
            AgentShell::setBroadcastClub(bc);
        }
    }

    // 2026-10-25（Win7 现场排障埋点）：Win7 rc.4 首启后进程在 OOBE 打开不久即结束、
    // 日志里没有任何退出线索（无法区分「用户/设计路径有序退出」与「静默崩溃」）。
    // 有序退出（app.quit/托盘退出/OOBE 收尾）必定经过 aboutToQuit；硬崩溃则不会
    // ⇒ 下轮现场看到这行=有序退出，看不到=崩溃，扳机范围立减一半。
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [] {
        qInfo("[agent-qt] 退出事件循环（aboutToQuit——有序退出路径）");
    });

    const int rc = app.exec();
    // 释放单例锁（2026-10-05）：正常退出才放，进程结束前交给系统/OS 清理兜底。
    // 老实现这行 CloseHandle(g_singletonMutex) 引用的变量早删了，改成守卫自己的 release()。
    g_single.release();
    return rc;
}
