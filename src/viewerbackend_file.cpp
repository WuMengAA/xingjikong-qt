// 星集控 · ViewerBackend 文件推送子域（2026-10-13 架构拆分，审计 2.1）
//
// 为什么拆出来：viewerbackend.cpp 曾 3573 行（单编译单元内存峰值 C1060）。
// 文件推送状态机（pushFile / sendNextChunk / cancelPush / 看门狗 / 收口）是
// **弱状态、边界清晰**的一块：只碰 m_pushFile/m_pushNext/m_pushWatchdog 和
// 一组 file* 读数，不与连接/鉴权/帧纠缠。拆到独立翻译单元，行为不变
// （同 header 的 ViewerBackend 成员函数，Qt QObject 跨编译单元实现完全合法）。
//
// ⚠️ 两条红线（跨文件搬动时保持，别以为"搬走就能改"）：
//   · 一片一等回执（sendNextChunk 只在 file_chunk 回执里再发下一片）——绝不
//     改成定时器盲发，被控端拒收一片就乱序。
//   · 取消必须发 file_done(total_bytes=0) 让被控端关会话删半截——不然它那边
//     g_fileRecvTarget 一直挂着，后续推任何文件都落到同一个坏目标上。

#include "viewerbackend.h"

#include <QFileInfo>
#include <QTimer>   // m_pushWatchdog（QTimer*）——viewerbackend.h 只前向声明
#include <QUrl>

// ──────────────────────────────────────────────────────────────────────
// 7 个读数（fileState/fileName/fileBytes/fileTotal/filePercent/fileError/fileTarget）
// 在头文件里内联定义了 —— 那里是唯一一处，别在这儿再写一遍（C2084 重定义）。
QString ViewerBackend::fileBytesText() const { return humanBytes(m_fileBytes); }
QString ViewerBackend::fileTotalText() const { return humanBytes(m_fileTotal); }
void ViewerBackend::clearFilePush()
{
    if (m_pushWatchdog) m_pushWatchdog->stop();
    if (m_pushFile.isOpen()) m_pushFile.close();
    m_pushPath.clear();
    m_fileState = QStringLiteral("idle");
    m_fileName.clear();
    m_fileBytes = 0;
    m_fileTotal = 0;
    m_filePercent = 0;
    m_fileError.clear();
    m_fileTarget.clear();
    m_pushNext = 0;
    emit fileProgressChanged();
}
void ViewerBackend::setFileFail(const QString &why)
{
    // 任何失败路径都先停看门狗：否则它到时还会再咬一次，把真实原因覆盖成"超时"。
    if (m_pushWatchdog) m_pushWatchdog->stop();
    m_fileState = QStringLiteral("failed");
    m_fileError = why;
    if (m_pushFile.isOpen()) m_pushFile.close();
    logf("[viewer] FAIL 文件推送中断：%s", why.toUtf8().constData());
    emit fileProgressChanged();
}
void ViewerBackend::pushFile(const QString &urlText)
{
    // 没选设备就直接判失败：sendAction 会自己拦下来发不出去，但状态已经被设成 pushing 了，
    // 那条回执永远不来 → 按钮锁死在"等待"，界面看着像卡住
    if (m_currentUid.isEmpty()) {
        setFileFail(QStringLiteral("还没选设备，文件发不出去"));
        return;
    }
    clearFilePush();
    // 界面拿到的是 file:// URL，这里统一解一次，顺便把"到底在推哪个文件"印进日志 ——
    // 出错时日志里能直接对上，不用再去问人"你选的是哪个文件"
    const QString path = QUrl::fromUserInput(urlText).toLocalFile();
    if (path.isEmpty()) {
        setFileFail(QStringLiteral("这个地址不是本机文件：%1").arg(urlText));
        return;
    }
    const QFileInfo fi(path);
    if (!fi.isFile()) {
        setFileFail(QStringLiteral("读不到这个文件：%1").arg(path));
        return;
    }
    if (fi.size() <= 0) {
        // 空文件：被控端的 file_done 会拿 total_bytes 和实收字节比对，空文件那边还没法收口
        setFileFail(QStringLiteral("这是个空文件（0 字节），推过去对不上账"));
        return;
    }
    m_fileName = fi.fileName();
    m_fileTotal = fi.size();
    m_pushPath = path;
    // QFile 必须先 setFileName 才能 open —— 2026-10-04 踩过：漏了这行，open() 直接 false，
    // 而且 errorString() 只会给你一句无用的 "Unknown error"（没文件名时 Qt 连错误类型都设不出来）。
    // 那时候会在"开本机文件"这一步卡死，一片 file_chunk 都发不出去，日志上看不出根因。
    m_pushFile.setFileName(path);
    if (!m_pushFile.open(QIODevice::ReadOnly)) {
        // 本机都读不了就不开被控端会话：少一条无谓指令，也少一台机器挂着一个空会话。
        // 打开失败一律先把 fileName/exists/size/error 摊开 —— errorString() 在这类失败上
        // 只会给一句无用的 "Unknown error"，不摊开根本不知道是文件没了还是句柄问题
        logf("[viewer] 诊断 pushFile 打开失败：fileName=[%s] exists=%d size=%lld err=[%s]",
             m_pushFile.fileName().toUtf8().constData(),
             (int)m_pushFile.exists(),
             (long long)m_pushFile.size(),
             m_pushFile.errorString().toUtf8().constData());
        setFileFail(QStringLiteral("本机文件读不开：%1（%2）")
                        .arg(path).arg(m_pushFile.errorString()));
        return;
    }
    logf("[viewer] 本机源文件已打开（%lld 字节），开始推给教室机", (long long)m_pushFile.size());
    m_fileState = QStringLiteral("pushing");
    emit fileProgressChanged();
    logf("[viewer] 开会话 → file_push %s（%lld 字节）→ %s",
         m_fileName.toUtf8().constData(), (long long)m_fileTotal,
         m_currentUid.toUtf8().constData());
    QJsonObject p;
    p.insert(QStringLiteral("name"), m_fileName);
    p.insert(QStringLiteral("total_bytes"), (double)m_fileTotal);
    sendAction(QStringLiteral("file_push"), p);
    // 2026-10-14 修（审计 P1）：原来发出 file_push 后**不启动看门狗**。
    // m_pushWatchdog 只在「收到回执之后」才 start（viewerbackend.cpp:201），
    // 于是「教室机离线 / agent 崩了 / 云端把这条 file_push 丢了」时第一条回执永远
    // 不来 → 看门狗从未启动 → m_fileState 永远停在 "pushing"，界面上没有任何超时
    // 提示，用户只能盯着转圈猜是不是卡死了。
    // 分片那一段本来就靠 :201 那条续命，缺口恰好只有第一条 —— 这里补上就闭环了。
    if (m_pushWatchdog) m_pushWatchdog->start(kPushAckTimeoutMs);
}
void ViewerBackend::sendNextChunk()
{
    if (m_fileState != QStringLiteral("sending")) return;
    const qint64 left = m_fileTotal - m_fileBytes;
    if (left <= 0) {
        // 收口：把声明的总字节数交给被控端，它自己会比对、对不上就把半截文件删掉
        QJsonObject p;
        p.insert(QStringLiteral("total_bytes"), (double)m_fileTotal);
        sendAction(QStringLiteral("file_done"), p);
        return;
    }
    const QByteArray blob = m_pushFile.read(qMin(left, kFileChunkBytes));
    if (blob.isEmpty()) {
        // 注意：格式化参数只能给 QString::arg，不能塞进 QStringLiteral —— 那是宏，
        // 多一个整数字面量就连参数个数都算不对（C4002）
        setFileFail(QStringLiteral("文件读不出来了（本机文件被改小了？已推 %1 / 原 %2）")
                        .arg((long long)m_fileBytes).arg((long long)m_fileTotal));
        return;
    }
    QJsonObject p;
    p.insert(QStringLiteral("seq"), (double)m_pushNext);
    p.insert(QStringLiteral("data"), QString::fromLatin1(blob.toBase64()));
    m_fileBytes += blob.size();
    m_pushNext++;
    m_filePercent = (int)(m_fileBytes * 100 / m_fileTotal);
    emit fileProgressChanged();
    sendAction(QStringLiteral("file_chunk"), p);
}
void ViewerBackend::cancelPush()
{
    if (m_fileState == QStringLiteral("idle")) return;
    if (m_fileState == QStringLiteral("done") || m_fileState == QStringLiteral("failed")) {
        clearFilePush();
        return;
    }
    // 真推到一半：仍要发一次 file_done 让被控端**关掉接收会话 + 删掉半截文件**
    // （否则它那边的 g_fileRecvTarget 一直挂着，后面再推任何文件都会先落到同一个目标上，
    // 而且残缺文件留在教室机上是打不开的）。
    // total_bytes 传 0 —— 被控端认这是"取消"：走删半截那条分支、不做字节数比对。
    QJsonObject p;
    p.insert(QStringLiteral("total_bytes"), QJsonValue(0));
    sendAction(QStringLiteral("file_done"), p);
    if (m_pushFile.isOpen()) m_pushFile.close();
    m_fileState = QStringLiteral("idle");
    m_fileError = QStringLiteral("已取消");
    logf("[viewer] 文件推送已取消（被控端已关会话并删掉半截文件，收件目录不留残骸）");
    emit fileProgressChanged();
}