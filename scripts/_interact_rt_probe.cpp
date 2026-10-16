// 管理端「互动审批面板」· 真桥运行时探针（2026-10-16）。
//
// 与 scripts/_interact_probe.cpp 的区别（为什么另写）：
//   · 那个用**假桥 + 离屏 QQuickView**，验的是「面板长什么样」（渲染 / 主题 / 布局），
//     不需要云端 —— 但也就**没有验证 InteractBridge 这个类本身**。
//   · 这个用**真的 ViewerBackend + 真的 InteractBridge**，连真实云端 HTTP
//     （/api/interact/*），验的是「面板点下去，云端到底收没收到、回没回来」。
//
// 怎么做到不必链接整个 viewer：沿用 scripts/_linknew.py 的思路 —— 复用 viewer-qt 目标
// 已编好的**全部 obj**，只把 main.cpp.obj 换成本文件编出的 obj，链成一个"真 viewer 壳"。
// InteractBridge / ViewerBackend 都是真实现，未做任何替换、打桩或条件编译。
//
// 入参（env，由 scripts/_interact_rt_probe.py 设置）：
//   STE_VIEWER_URL    云端地址（viewerbackend.start() 读它）
//   STE_VIEWER_TOKEN  管理端令牌
// 退出码：0 全过；非 0 有失败。

#include <cstdarg>
#include <cstdio>

#include <QApplication>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QDebug>
#include <QString>

#include "viewerbackend.h"
#include "interactbridge.h"

static int g_pass = 0, g_fail = 0;

static void ck(const char *name, bool ok, const QString &detail = QString())
{
    if (ok) ++g_pass; else ++g_fail;
    QString line = ok ? QStringLiteral("  [PASS] ") : QStringLiteral("  [FAIL] ");
    line += QString::fromUtf8(name);
    if (!detail.isEmpty()) line += QStringLiteral(" — ") + detail;
    qInfo().noquote() << line;
}

// ── 补上 main.cpp 原本提供的 logf ───────────────────────────────────
// 本探针顶替了 main.cpp.obj，而 logf 定义在 main.cpp:135（viewerbackend.h:86 声明，
// oauthlogin / updater / ldc_rtc_bridge / viewerbackend*.cpp 都在引用它）——
// 不补就是 LNK2001。探针不需要"落盘日志"，打到控制台即可。
void logf(const char *fmt, ...)
{
    char buf[2048];
    va_list ap; va_start(ap, fmt);
    const int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n >= (int)sizeof(buf))
        fputs("[truncated]\n", stdout);
    const QByteArray local = QString::fromUtf8(buf).toLocal8Bit();
    fwrite(local.constData(), 1, local.size(), stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // ⚠️ Windows 上 Qt 默认把 qInfo/qDebug 交给 OutputDebugString（stderr 不是控制台时），
    //    而探针的输出由驱动脚本用管道捕获 ⇒ 不自己装 handler 就一条都看不到
    //    （实测：只有 logf 的输出可见，qInfo 全丢）。这里统一写到 stdout。
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &msg) {
        const QByteArray b = msg.toLocal8Bit();
        fwrite(b.constData(), 1, b.size(), stdout);
        fputc('\n', stdout);
        fflush(stdout);
    });

    qInfo().noquote() << QStringLiteral("[rt-probe] 互动审批面板 · 真桥运行时探针");

    // ── 真 ViewerBackend：地址与令牌来自 STE_VIEWER_URL / STE_VIEWER_TOKEN ──
    ViewerBackend backend;
    backend.start();

    const QString url = backend.cloudUrl();
    const QString tok = backend.viewerToken();
    qInfo().noquote() << QStringLiteral("[rt-probe] cloudUrl    = ") + url;
    qInfo().noquote() << QStringLiteral("[rt-probe] viewerToken= ")
                         + (tok.isEmpty() ? QStringLiteral("(空)") : QStringLiteral("(非空，长度 %1)").arg(tok.size()));
    ck("ViewerBackend 从 env 读到云端地址（start() 生效）", !url.isEmpty(), url);
    ck("ViewerBackend 从 env 读到管理端令牌", !tok.isEmpty());

    // ── 对照（反向证据，零产品改动）──
    // 修复前 InteractBridge 是直接拿 cloudUrl() 拼路径的。这里把那个拼法**原样重现**，
    // 看它产出什么 scheme —— QNetworkAccessManager 只认 http/https，ws/wss 根本发不出去。
    // 所以这条断言与"是否已修复"无关，它锁的是"旧拼法不可能工作"这个事实。
    const QString legacy = url + QStringLiteral("/api/interact/song-requests");
    const QString legacyScheme = QUrl(legacy).scheme();
    qInfo().noquote() << QStringLiteral("[rt-probe] 对照·修复前拼法 = ") + legacy
                             + QStringLiteral("   (scheme=%1)").arg(legacyScheme);
    ck("对照：修复前拼法 scheme 非 http(s) ⇒ QNAM 发不出去 ⇒ 面板在真机拉不到数据",
       legacyScheme != QLatin1String("http") && legacyScheme != QLatin1String("https"),
       QStringLiteral("scheme=%1").arg(legacyScheme));
    qInfo().noquote() << QStringLiteral("[rt-probe] 修复后拼法 = ") + backend.httpBaseUrl()
                             + QStringLiteral("/api/interact/song-requests")
                             + QStringLiteral("   (scheme=%1)").arg(QUrl(backend.httpBaseUrl()).scheme());

    // ── 真 InteractBridge ──
    InteractBridge bridge(&backend);
    int changes = 0;
    QObject::connect(&bridge, &InteractBridge::dataChanged, [&changes, &bridge] {
        ++changes;
        qInfo().noquote() << QStringLiteral("[rt-probe] dataChanged #%1  state=%2  songs=%3  blessings=%4  err=%5")
                                 .arg(changes).arg(bridge.state())
                                 .arg(bridge.songRequests().size())
                                 .arg(bridge.blessings().size())
                                 .arg(bridge.error().isEmpty() ? QStringLiteral("-") : bridge.error());
    });

    qInfo().noquote() << QStringLiteral("[rt-probe] 调 reload() → 三轮 HTTP 请求");
    bridge.reload();

    QTimer::singleShot(3000, [&] {
        // ── 阶段 1：三轮 GET 全部收口到 state=ok ──
        ck("reload 收口到 state=ok（m_pendingReplies 三轮全 200）",
           bridge.state() == QLatin1String("ok"),
           bridge.state() + QLatin1Char(' ') + bridge.error());
        ck("点歌待审队列非空（GET /api/interact/song-requests）",
           bridge.songRequests().size() >= 1, QString::number(bridge.songRequests().size()));
        ck("祝福待审队列非空（GET /api/interact/blessings）",
           bridge.blessings().size() >= 1, QString::number(bridge.blessings().size()));

        const QVariantMap fb = bridge.feedbackSummary();
        ck("反馈汇总非空（GET /api/interact/feedback/summary） likeTotal>=1",
           fb.value(QStringLiteral("likeTotal")).toInt() >= 1,
           QStringLiteral("likeTotal=") + QString::number(fb.value(QStringLiteral("likeTotal")).toInt()));

        if (!bridge.blessings().isEmpty()) {
            const QVariantMap b = bridge.blessings().first().toMap();
            ck("条目带 classCode（按班过滤依据，QML 面板据此显示班级）",
               b.contains(QStringLiteral("classCode")),
               QStringLiteral("classCode=") + b.value(QStringLiteral("classCode")).toString());
        } else {
            ck("条目带 classCode（按班过滤依据，QML 面板据此显示班级）", false,
               QStringLiteral("队列空，无法判定"));
        }

        // ── 阶段 2：真审批（handleBlessing → HTTP POST /api/interact/handle）──
        if (bridge.blessings().isEmpty()) {
            qInfo().noquote() << QStringLiteral("\n══ 结果: %1 通过 / %2 失败 ══").arg(g_pass).arg(g_fail);
            app.exit(g_fail == 0 ? 0 : 1);
            return;
        }
        const int id = bridge.blessings().first().toMap().value(QStringLiteral("id")).toInt();
        qInfo().noquote() << QStringLiteral("[rt-probe] 调 handleBlessing(id=%1, approve) → POST /api/interact/handle").arg(id);
        bridge.handleBlessing(id, QStringLiteral("approve"));

        QTimer::singleShot(2500, [&, id] {
            bool found = false;
            QString st;
            for (const QVariant &v : bridge.blessings()) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("id")).toInt() == id) {
                    found = true;
                    st = m.value(QStringLiteral("state")).toString();
                }
            }
            // applyHandleResult 成功后**就地**改本地条目；而默认 filterState=pending
            // ⇒ 该条会被移出当前列表（两种结果都算通过）。
            ck("HTTP 审批 handle 生效（本地条目转 approved 或已移出 pending 列表）",
               !found || st == QLatin1String("approved"),
               found ? QStringLiteral("state=") + st : QStringLiteral("已移出 pending 列表"));

            qInfo().noquote() << QStringLiteral("\n══ 结果: %1 通过 / %2 失败 ══").arg(g_pass).arg(g_fail);
            app.exit(g_fail == 0 ? 0 : 1);
        });
    });

    return app.exec();
}
