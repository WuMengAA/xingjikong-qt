// 星集控 · ViewerBackend 语音对讲 + 屏幕广播子域（2026-10-13 架构拆分，审计 2.1）
//
// 为什么拆出来：viewerbackend.cpp 曾 3573 行（C1060）。语音对讲（startSpeaking/
// stopSpeaking + 时长上限）和屏幕广播（startBroadcast/stopBroadcast + 抓屏）是
// **弱状态、共享一个 m_leftTick 剩余秒计数器 + m_autoNote** 的两个子域，合在一起
// 拆到独立翻译单元，行为不变（同 header 的 ViewerBackend 成员，QObject 跨编译
// 单元实现合法）。
//
// ⚠️ 两条硬约束：
//   · Android 麦克风权限走 Qt 官方 QPermission 三态（Granted 直接用 / Denied 引导
//     去设置 / Undetermined 才弹框）——**别改回每次 requestPermission**（拒过一次
//     后不再弹框，那会变成"请求→立刻 Denied→报失败"死循环）。
//   · 语音和广播共用一个 m_leftTick（剩余秒 1s 跳动），只有当两者都不在跑才停它
//     ——见 stopSpeaking/stopBroadcast 里那句 `!m_speaking && !m_broadcasting`。

#include "viewerbackend.h"

#include "audio_capture.h"   // 语音对讲采集（完整类型：m_audioCapture->stopCapture()）
#include "audio_playback.h"  // B6 播放（完整类型：m_audioPlayback->stop()）

#include <QBuffer>           // 屏幕广播抓屏 → JPEG
#include <QDateTime>         // makeBinaryFrame 时间戳
#include <QGuiApplication>
#include <QJsonDocument>     // 上行二进制帧头（与云端 protocol.js makeFrame 同格式）
#include <QPixmap>
#include <QScreen>
#include <QTimer>
#include <QWebSocket>        // startBroadcast 里读 m_ws->state()

// ── 上行二进制帧构造（2026-10-21 · 协议对称化）──────────────────────────────
// 与云端 protocol.js makeFrame 同格式：[1B ver=1][2B 大端 headerLen][header JSON][payload]
// header: {v,type:"frame",uid,seq,ts,mime,bytes}。语音/屏幕广播统一走这条，
// 云端按 header.mime 路由（audio/pcm → 学生端；image/jpeg → 屏幕广播），
// 不再靠"裸帧 + 会话态推断"——与端侧协议范式对称、向后兼容旧管理端裸帧兜底。
static QByteArray makeBinaryFrame(const QString &uid, qint64 seq,
                                  const QString &mime, const QByteArray &payload)
{
    QJsonObject h;
    h[QStringLiteral("v")] = 1;
    h[QStringLiteral("type")] = QStringLiteral("frame");
    h[QStringLiteral("uid")] = uid;
    h[QStringLiteral("seq")] = seq;
    h[QStringLiteral("ts")] = QDateTime::currentMSecsSinceEpoch();
    h[QStringLiteral("mime")] = mime;
    h[QStringLiteral("bytes")] = payload.size();
    const QByteArray header = QJsonDocument(h).toJson(QJsonDocument::Compact);
    QByteArray frame;
    frame.reserve(3 + header.size() + payload.size());
    frame.append(char(1));
    frame.append(char((header.size() >> 8) & 0xFF));
    frame.append(char(header.size() & 0xFF));
    frame.append(header);
    frame.append(payload);
    return frame;
}

#if defined(Q_OS_ANDROID)
#include <QJniObject>
#include <QPermissions>                        // QMicrophonePermission（QtCore 自带）
#include <QtCore/private/qandroidextras_p.h>   // startActivity（应用详情页）
#include <QtCore/qcoreapplication_platform.h>   // QAndroidApplication::context()
#endif

// ── 语音对讲（设计文档 3.3，2026-10-07）──────────────────────────────────
// Android 麦克风权限：Qt Multimedia 未装（QMicrophonePermission 是 minimal 无 status），
// 改用 Qt 官方 QPermission 三态模型（见文件头硬约束）。桌面（winmm）直接采。
bool ViewerBackend::startSpeaking()
{
    if (m_speaking) return true;
#if defined(Q_OS_ANDROID)
    // 2026-10-13 修（用户 #111 · Android 运行时权限模型）：
    // 旧写法直接调 QtAndroidPrivate::requestPermission —— **每次都请求**。而 Android
    // 在用户拒过一次后就**不再弹框**，于是变成"请求 → 立刻 Denied → 报失败"的死循环，
    // 永远卡在"没权限"（dsh 10-11 记的"弹框只出现一次"就是这个症状，不是根因）。
    // 正确模型是先 checkPermission 分三态：
    //   Granted      → 直接开麦；
    //   Denied       → 已拒/勾了"不再询问"，**再请求也不会弹** ⇒ 不浪费请求，直接引导去系统设置；
    //   Undetermined → 才轮到系统请求框（第一次、也是唯一该弹的一次）。
    QMicrophonePermission mic;
    switch (qApp->checkPermission(mic)) {
    case Qt::PermissionStatus::Granted:
        return startSpeakingCapture();
    case Qt::PermissionStatus::Denied:
        m_speakError = QStringLiteral("麦克风权限被拒绝——请到「设置 → 应用 → 星集控 → 权限」里开启麦克风");
        emit speakErrorChanged();
        setStatus(m_speakError, true);
        return false;
    case Qt::PermissionStatus::Undetermined:
    default:
        break;
    }
    // 首次请求：弹系统框，回调里开麦。返回 **true**（"已受理"）——
    // 别让 QML 立刻报"开麦失败"，框还开着等用户点呢。
    qApp->requestPermission(mic, this, [this](const QPermission &p) {
        emit audioPermChanged();
        if (p.status() == Qt::PermissionStatus::Granted) {
            startSpeakingCapture();
        } else {
            m_speakError = QStringLiteral("麦克风权限被拒绝——请到「设置 → 应用 → 星集控 → 权限」里开启麦克风");
            emit speakErrorChanged();
            setStatus(m_speakError, true);
        }
    });
    setStatus(QStringLiteral("请允许录音权限；授权后自动开麦"), false);
    return true;
#else
    return startSpeakingCapture();
#endif
}

bool ViewerBackend::audioPermGranted() const
{
#if defined(Q_OS_ANDROID)
    // 2026-10-13：改用 Qt 官方 QPermission（跨平台、不再手写 JNI checkSelfPermission）。
    // 注意语义：**只有 Granted 才算"有权限"** —— Undetermined（没问过）对界面来说
    // 同样是"还没有权限"，红条该显示（提示用户点一下会弹框）。
    return qApp->checkPermission(QMicrophonePermission()) == Qt::PermissionStatus::Granted;
#else
    return true;
#endif
}

void ViewerBackend::requestAudioPerm()
{
#if defined(Q_OS_ANDROID)
    // 2026-10-13：权限请求逻辑已并入 startSpeaking（三态检查 + 首次才弹框），
    // 这里保留 Q_INVOKABLE 入口只为不破接口，语义等同"走一次开麦请求"。
    startSpeaking();
#endif
}

void ViewerBackend::openAudioSettings()
{
#if defined(Q_OS_ANDROID)
    // ACTION_APPLICATION_DETAILS_SETTINGS：跳应用详情页，用户可手动开关「麦克风」
    QJniObject intent("android/content/Intent",
                      "(Ljava/lang/String;)V",
                      QJniObject::fromString(QStringLiteral("android.settings.APPLICATION_DETAILS_SETTINGS"))
                          .object<jstring>());
    const QJniObject ctx(QNativeInterface::QAndroidApplication::context().object());
    const QString pkg = ctx.callMethod<jstring>(
                            "getPackageName", "()Ljava/lang/String;").toString();
    // Uri.parse("package:<包名>")
    QJniObject uri = QJniObject::callStaticObjectMethod(
        "android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
        QJniObject::fromString(QStringLiteral("package:") + pkg).object<jstring>());
    if (uri.isValid() && intent.isValid()) {
        intent.callObjectMethod("setData", "(Landroid/net/Uri;)Landroid/content/Intent;",
                                uri.object());
        QJniObject chooser = intent.callObjectMethod(
            "createChooser",
            "(Landroid/content/Intent;)Landroid/content/Intent;",
            intent.object());
        if (chooser.isValid())
            QtAndroidPrivate::startActivity(chooser, 1002, [](int, jint, const QJniObject &) {});
    }
#endif
}

bool ViewerBackend::startSpeakingCapture()
{
    if (m_speaking) return true;
    if (!m_audioCapture) m_audioCapture = new AudioCapture(this);
    // 采集回调：每 20ms 一帧 PCM → 发云端（二进制帧，云端按 audio 会话转发）
    // ⚠️ 2026-10-13 修：**必须跨线程投递**。这个 onFrame 是在 winmm/AAudio 的**音频驱动线程**里
    //    被调用的（见 audio_capture.cpp 的 waveInProc 回调 / audio_capture_android.cpp 的 AAudio 回调，
    //    后者注释自己都写了"跑在音频线程"），而 m_ws（QWebSocket）属**主线程** —— Qt 要求 QObject
    //    及其子类只能在所属线程使用，每 20ms 跨线程直调 sendBinaryMessage()/state() 是数据竞争 + UB：
    //    轻则内部发送缓冲与状态机被并发破坏、帧错乱，重则断言或访问冲突崩溃。
    //    改成投递回主线程再发（帧只有 640B，队列开销可忽略）。
    // 2026-10-21：裸 PCM 改为带 mime 的 v1 二进制帧（上行协议对称化），云端按 header.mime 路由。
    m_audioCapture->onFrame = [this](const QByteArray &pcm) {
        QMetaObject::invokeMethod(this, [this, pcm] {
            if (!m_ws || m_ws->state() != QAbstractSocket::ConnectedState) return;
            m_ws->sendBinaryMessage(
                makeBinaryFrame(QString(), ++m_voiceSeq, QStringLiteral("audio/pcm"), pcm));
        }, Qt::QueuedConnection);
    };
    // 采集中途出错也要把状态收干净：只把 m_speaking 置 false 而不发 audio.stop，
    // 云端那边还挂着"老师在讲话"，学生端会一直提示。（2026-10-07 补）
    m_audioCapture->onError = [this](const QString &err) {
        m_speakError = err;
        emit speakErrorChanged();
        setStatus(QStringLiteral("语音：") + err, true);
        if (m_speaking) stopSpeaking(QStringLiteral("语音出错，已自动停：") + err);
    };
    if (!m_audioCapture->startCapture()) {
        m_speakError = m_audioCapture->lastError();
        emit speakErrorChanged();
        setStatus(QStringLiteral("开麦失败：") + m_speakError, true);
        return false;
    }
    // 通知云端开始语音会话（之后二进制帧才被接受并转发）
    sendEnvelope(QStringLiteral("audio.start"), QJsonObject());
    m_speaking = true;
    m_speakError.clear();
    emit speakErrorChanged();
    // 起时长上限：到点自动停 + 界面上的"还剩几秒"
    m_speakLeftMs = m_speakMaxMs;
    m_speakLeftSec = m_speakMaxMs / 1000;
    m_speakLimit->start(m_speakMaxMs);
    m_leftTick->start();
    emit speakingChanged();
    emit limitsChanged();
    setStatus(QStringLiteral("🎤 正在讲话（全班可听）；最长 %1，到点自动停").arg(fmtLimit(m_speakMaxMs)), false);
    qInfo("[viewer] 🎤 语音对讲开始（上限 %lld 秒）", (long long)m_speakMaxMs / 1000);
    return true;
}
void ViewerBackend::stopSpeaking(QString why)
{
    if (!m_speaking) return;
    if (m_audioCapture) m_audioCapture->stopCapture();
    // B6（2026-10-11）：本人停止讲话 ⇒ 对端回传的音频也不该再放（会话结束）。
    // 不停的话，waveOut 缓冲里剩的半帧、以及断线前在途的几帧会继续出声。
    if (m_audioPlayback) m_audioPlayback->stop();
    m_speakLimit->stop();
    m_speaking = false;
    m_speakLeftMs = 0;
    m_speakLeftSec = 0;
    sendEnvelope(QStringLiteral("audio.stop"), QJsonObject());
    // 2026-10-12 修：原 `if (!m_leftTick->isActive()) m_leftTick->stop()` 条件写反
    // （inactive 时 stop 是空操作、active 时进不来）——恒不执行。正确判据：
    // 语音和广播都没在跑了才停倒计时 tick（与 m_leftTick 的 timeout lambda 同款）。
    if (!m_speaking && !m_broadcasting) m_leftTick->stop();
    if (why.isEmpty()) {
        setStatus(QStringLiteral("语音已停止（静音）"), false);
        qInfo("[viewer] 🎤 语音对讲结束");
    } else {
        // 自动停必须说清楚：界面上按钮会自己变回"开始讲话"，
        // 但不说一句，老师只觉得"刚才那半句话怎么断了，也不提示"。
        m_autoNote = why;
        emit autoNoteChanged();
        setStatus(why, false);
        qInfo("[viewer] %s", qPrintable(why));
    }
    emit speakingChanged();
    emit limitsChanged();
}
bool ViewerBackend::isSpeaking() const
{
    return m_speaking;
}
int ViewerBackend::speakLeftSec() const
{
    return m_speakLeftSec;
}
QString ViewerBackend::speakError() const
{
    return m_speakError;
}

// ── 屏幕广播（设计文档《屏幕广播-第一版设计》，2026-10-07 改写）──
// 管理端抓屏（~10fps JPEG）→ 二进制帧发云端 → 云端 fan-out → 被控端全屏显示。
// 同样带硬时长上限：抓屏比开麦更费，忘了关能把一台机器和上行一起熬干。
bool ViewerBackend::startBroadcast()
{
    if (m_broadcasting) return true;
    if (!m_bcastTimer) {
        m_bcastTimer = new QTimer(this);
        // 2026-10-08 修「屏幕广播延迟太大」：原 330ms（~3fps）是主要延迟源（单帧最多等 333ms 才刷新）。
        // 提到 100ms（~10fps）：屏幕共享标准档，帧新鲜度提升 3 倍、体感明显跟手；
        // 上行只是老师单机一条流，10fps 完全扛得住（带宽瓶颈在云端 fan-out，教室场景可接受）。
        m_bcastTimer->setInterval(100); // ~10fps
        connect(m_bcastTimer, &QTimer::timeout, this, [this] {
            if (!m_ws || m_ws->state() != QAbstractSocket::ConnectedState) return;
            QScreen *screen = QGuiApplication::primaryScreen();
            if (!screen) return;
            // 抓屏 → JPEG（质量 70，1280 内缩放 —— 带宽 60 台 × 300kbps 可承受）
            QPixmap pm = screen->grabWindow(0);
            if (pm.width() > 1280) pm = pm.scaledToWidth(1280, Qt::SmoothTransformation);
            QByteArray jpg;
            QBuffer buf(&jpg);
            buf.open(QIODevice::WriteOnly);
            // 2026-10-21：裸 JPEG 改为带 mime 的 v1 二进制帧（上行协议对称化），
            // 云端按 header.mime=image/jpeg 路由到屏幕广播。
            if (pm.save(&buf, "JPG", 70))
                m_ws->sendBinaryMessage(
                    makeBinaryFrame(QString(), ++m_voiceSeq, QStringLiteral("image/jpeg"), jpg));
        });
    }
    m_bcastTimer->start();
    m_broadcasting = true;
    sendEnvelope(QStringLiteral("broadcast.start"), QJsonObject());
    // 起时长上限：到点自动停 + 界面上的"还剩几秒"
    m_bcastLeftMs = m_bcastMaxMs;
    m_bcastLeftSec = m_bcastMaxMs / 1000;
    m_bcastLimit->start(m_bcastMaxMs);
    m_leftTick->start();
    emit broadcastingChanged();
    emit limitsChanged();
    setStatus(QStringLiteral("📺 正在屏幕广播（老师屏幕 → 全部在线设备）；最长 %1，到点自动停").arg(fmtLimit(m_bcastMaxMs)), false);
    qInfo("[viewer] 📺 屏幕广播开始（上限 %lld 秒）", (long long)m_bcastMaxMs / 1000);
    return true;
}
void ViewerBackend::stopBroadcast(QString why)
{
    if (!m_broadcasting) return;
    m_bcastTimer->stop();
    m_broadcasting = false;
    m_bcastLeftMs = 0;
    m_bcastLeftSec = 0;
    sendEnvelope(QStringLiteral("broadcast.stop"), QJsonObject());
    // 2026-10-12 修：同 stopSpeaking —— 条件写反恒不执行，改为"语音广播都停了才停 tick"
    if (!m_speaking && !m_broadcasting) m_leftTick->stop();
    if (why.isEmpty()) {
        setStatus(QStringLiteral("屏幕广播已停止"), false);
        qInfo("[viewer] 📺 屏幕广播结束");
    } else {
        m_autoNote = why;
        emit autoNoteChanged();
        setStatus(why, false);
        qInfo("[viewer] %s", qPrintable(why));
    }
    emit broadcastingChanged();
    emit limitsChanged();
}
bool ViewerBackend::isBroadcasting() const
{
    return m_broadcasting;
}
int ViewerBackend::bcastLeftSec() const
{
    return m_bcastLeftSec;
}

QString ViewerBackend::autoNote() const
{
    return m_autoNote;
}
void ViewerBackend::stopAllLive(const QString &why)
{
    if (m_speaking) stopSpeaking(why);
    if (m_broadcasting) stopBroadcast(why);
}