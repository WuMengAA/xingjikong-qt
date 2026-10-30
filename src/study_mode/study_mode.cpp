// 自习模式（2026-10-13）：噪音检测 + 噪音异常自动录制
// 2026-10-30 从 src/main.cpp 迁出（原 4261–4370）。纯搬迁，逻辑/注释逐字保留。
//
// 定位：静默监控，不干扰学生（区别于考试模式的全屏拦截）。
//   · 进模式：周期测教室噪音（noise_monitor 短窗口：开→录→关，不常占麦、不驻留录音）
//     → 每拍把 dB 实时回传管理端。
//   · 判异常：dB **连续 N 拍**都高于阈值才算（一次瞬时爆音不算，防误报）。
//   · 触发：自动录一段摄像头（recordSec 秒）→ 停 → 上传云端 → 上报 noise_alert。
//
// 状态全局（g_study*）仍驻 main，本模块按既有 extern 模式（同 g_uid / agentEnvPath，
// 提升外部链接）复用，保证纯搬迁、零行为变化。

#include "study_mode.h"

#include <QJsonObject>                // 必须在 cloud_proto.h 之前（makeEnvelope 用到 QJsonObject）
#include "../cloud_proto.h"            // makeEnvelope（v1 信封，跨 TU 共用）
#include "../media/MediaSession.h"
#include "../media/MediaTools.h"
#include <QWebSocket>
#include <QAbstractSocket>
#include <QTimer>
#include <QDir>

// ── 复用 main 的状态全局（提升外部链接，同 g_uid / agentEnvPath 模式）──
extern double  g_studyThreshDb;
extern int     g_studyStreakNeed;
extern int     g_studyRecordSec;
extern int     g_studyStreak;
extern int     g_studyAlertCount;
extern QWebSocket *g_ws;
extern void sendEvent(const QString &level, const QString &msg, const QJsonObject &extra);

// 自动录制到点停的计时器（模块私有）
static QTimer *g_studyRecTimer = nullptr;

static void sendNoiseAlert(const QJsonObject &p)
{
    if (!g_ws || g_ws->state() != QAbstractSocket::ConnectedState) return;
    g_ws->sendTextMessage(makeEnvelope(QStringLiteral("noise_alert"), p));
}

static void studyTriggerRecord();          // 前向声明（onSample 先用到）

void StudyMode::onSample(double db)
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

void StudyMode::stopRecordingTimer()
{
    if (g_studyRecTimer) g_studyRecTimer->stop();
}
