// 星集控 · 管理端后端实现（从 main.cpp 原样搬过来，行为保持一致）

#include "viewerbackend.h"

#include <QAbstractSocket>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonValue>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>

namespace {

/**
 * 一片多大（**二进制**字节）。base64 之后约再膨胀 1/3，也就是一条 ~87KB。
 *
 * 为什么是 64KB 而不是"整文件一次发完"：① 被控端 file_chunk 的 data 是 base64，
 * 大文件整发＝一条几 MB 的 JSON，WebSocket 和云端都要各自缓存；② 一片一回报，
 * 卡在第几片、卡在哪一条能直接看出来，整发的话失败了你不知道是哪一步断的。
 */
constexpr qint64 kFileChunkBytes = 64 * 1024;

/* 协议 v1 信封：{"v":1,"type":...,"id":...,"ts":...,"payload":{...}}
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

} // namespace

ViewerBackend::ViewerBackend(QObject *parent)
    : QObject(parent)
{
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
                             m_fileState = QStringLiteral("done");
                             m_fileTarget = data.value(QStringLiteral("path")).toString();
                             m_fileError.clear();
                             if (m_pushFile.isOpen()) m_pushFile.close();
                             logf("[viewer] ✅ 文件已推到教室机：%s（%lld 字节）",
                                  m_fileTarget.toUtf8().constData(), (long long)m_fileBytes);
                             emit fileProgressChanged();
                         }
                     });
}

void ViewerBackend::start()
{
    m_url = qEnvironmentVariable("STE_VIEWER_URL", QStringLiteral("ws://127.0.0.1:8788/ws/viewer")).trimmed();
    if (m_url.isEmpty()) m_url = QStringLiteral("ws://127.0.0.1:8788/ws/viewer");
    m_token = qEnvironmentVariable("STE_VIEWER_TOKEN", QString()).trimmed();
    if (m_token.isEmpty()) {
        logf("[viewer] WARN 未设环境变量 STE_VIEWER_TOKEN：云端 /ws/viewer 已加鉴权，"
             "空令牌会被拒绝（不是网络问题，是没带令牌）");
    }
    logf("[viewer] 云端地址 %s", m_url.toUtf8().constData());
    emit cloudUrlChanged();

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
    connectToCloud();
}

void ViewerBackend::setStatus(const QString &s, bool warn)
{
    if (m_statusText == s && m_statusWarn == warn) return;
    m_statusText = s;
    m_statusWarn = warn;
    emit statusTextChanged();
}

void ViewerBackend::connectToCloud()
{
    if (!m_ws) return;
    m_ws->close();
    m_ws->open(QUrl(m_url));
    logf("[viewer] 正在连云端 %s", m_url.toUtf8().constData());
}

void ViewerBackend::sendEnvelope(const QString &type, const QJsonObject &payload)
{
    if (!m_ws) return;
    m_ws->sendTextMessage(makeEnvelope(type, payload));
}

void ViewerBackend::onConnected()
{
    m_connected = true;
    emit connectedChanged();
    logf("[viewer] 已连上云端 %s，正在鉴权…", m_url.toUtf8().constData());
    setStatus(QStringLiteral("已连上云端，正在鉴权…"), false);
    m_authed = false;
    emit authedChanged();
    // 云端要求第一条消息必须是 auth（v1 信封），否则直接拒收（鉴权前不推任何设备表/画面）
    QJsonObject p;
    p.insert(QStringLiteral("token"), m_token);
    sendEnvelope(QStringLiteral("auth"), p);
}

void ViewerBackend::onDisconnected()
{
    m_connected = false;
    emit connectedChanged();
    if (m_authFailed) {
        // 鉴权失败再重连只会一遍遍失败：把原因留在屏幕上，别循环
        logf("[viewer] FAIL 鉴权没通过，已停止重连：%s", m_authFailReason.toUtf8().constData());
        setStatus(QStringLiteral("云端鉴权失败，已停止重连：") + m_authFailReason, true);
        emit authFailed(m_authFailReason);
        return;
    }
    logf("[viewer] FAIL 与云端断开，5 秒后重连");
    setStatus(QStringLiteral("与云端断开，5 秒后重连…"), true);
    QTimer::singleShot(kReconnectMs, this, [this] { connectToCloud(); });
}

void ViewerBackend::requestDevices()
{
    sendEnvelope(QStringLiteral("devices"), QJsonObject());
}

void ViewerBackend::setCurrentUid(const QString &uid)
{
    if (uid.isEmpty() || uid == m_currentUid) return;
    m_currentUid = uid;
    emit currentUidChanged();
    logf("[viewer] 切到 %s，向云端订阅它的画面", uid.toUtf8().constData());
    QJsonObject sp;
    sp.insert(QStringLiteral("uid"), uid);
    sendEnvelope(QStringLiteral("subscribe"), sp);
}

void ViewerBackend::onTextMessage(const QString &text)
{
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
        emit authedChanged();
        logf("[viewer] ✅ 云端鉴权通过（mode=%s）",
             pay.value(QStringLiteral("mode")).toString().toUtf8().constData());
        setStatus(QStringLiteral("已连上云端，等教室机推画面…"), false);
        requestDevices();   // 鉴权过后再拉设备表（之前是连上就拉，现在会被云端拒）
    } else if (type == QStringLiteral("auth-fail")) {
        m_authFailed = true;
        m_authFailReason = pay.value(QStringLiteral("reason")).toString();
        logf("[viewer] FAIL 云端拒绝鉴权：%s", m_authFailReason.toUtf8().constData());
        setStatus(QStringLiteral("云端拒绝鉴权：") + m_authFailReason, true);
        emit authFailed(m_authFailReason);
        if (m_ws) m_ws->close();
    } else if (type == QStringLiteral("error")) {
        // 统一错误通道（v1）：云端拒绝必须看得见，不许静默
        const QString code = pay.value(QStringLiteral("code")).toString();
        const QString message = pay.value(QStringLiteral("message")).toString();
        logf("[viewer] FAIL 云端拒绝 → %s：%s", code.toUtf8().constData(), message.toUtf8().constData());
        setStatus(QStringLiteral("云端拒绝：") + message, true);
    } else if (type == QStringLiteral("viewer-ready")) {
        logf("[viewer] 云端回话：管理端在线（云端在线管理端 %d 个）",
             pay.value(QStringLiteral("viewers")).toInt(-1));
    } else if (type == QStringLiteral("devices")) {
        refreshDevices(pay.value(QStringLiteral("devices")).toArray());
    } else if (type == QStringLiteral("subscribed")) {
        logf("[viewer] 云端确认订阅 %s", pay.value(QStringLiteral("uid")).toString().toUtf8().constData());
    } else if (type == QStringLiteral("frame")) {
        // 兼容旧文本 frame（base64）；v1 客户端走的是二进制帧分支
        const QByteArray b64 = QByteArray::fromBase64(pay.value(QStringLiteral("data")).toString().toUtf8());
        if (b64.isEmpty()) logf("[viewer] FAIL 收到一帧但内容是空的（云端推了空 base64）");
        else applyFrameBytes(b64, QJsonObject());
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
        // 探活的往返时间只有 backend 知道（发出时刻记在这儿），算好塞进 data 给界面用
        if (action == QStringLiteral("ping") && m_pingSentMs > 0) {
            data.insert(QStringLiteral("rttMs"),
                        (double)(QDateTime::currentMSecsSinceEpoch() - m_pingSentMs));
        }
        emit resultReceived(uid, action, state, result, err, detail, data);
    } else {
        logf("[viewer] 收到未处理的云端消息 type=%s", type.toUtf8().constData());
    }
}

void ViewerBackend::onBinaryMessage(const QByteArray &buf)
{
    // v1 帧：[1B 版本=1][2B 大端 headerLen][header JSON][JPEG]
    if (buf.size() >= 4 && (quint8)buf.at(0) == 1) {
        const int hLen = ((unsigned char)buf.at(1) << 8) | (unsigned char)buf.at(2);
        if (hLen > 0 && hLen <= kFrameHeaderMax && buf.size() >= 3 + hLen) {
            const QJsonDocument hd = QJsonDocument::fromJson(buf.mid(3, hLen));
            if (!hd.isObject()) { logf("[viewer] FAIL 帧头解析失败（非 JSON）"); return; }
            const QJsonObject h = hd.object();
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

void ViewerBackend::refreshDevices(const QJsonArray &arr)
{
    const bool countChanged = (arr.size() != m_devices.size());
    m_devices = arr;
    if (countChanged) logf("[viewer] 设备表更新：在线 %d 台", (int)arr.size());
    emit devicesChanged();

    if (arr.isEmpty()) {
        if (!m_currentUid.isEmpty()) {
            m_currentUid.clear();
            emit currentUidChanged();
        }
        return;
    }
    // 第一台上线就默认看它（和云端 autoSubscribe 一个口径）
    if (m_currentUid.isEmpty()) {
        setCurrentUid(arr.first().toObject().value(QStringLiteral("uid")).toString());
    }
}

void ViewerBackend::applyFrameBytes(const QByteArray &jpeg, const QJsonObject &header)
{
    if (jpeg.isEmpty()) {
        logf("[viewer] FAIL 收到一帧但内容是空的");
        return;
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

    // ── 静态区：画面连续 kStaticStreak 帧一模一样 → 不再 emit frameChanged，
    //    界面就不换 tick，等于"这张已经画出过了，别再画一遍"。
    //    （画面内容本身没变，所以停刷显示的仍是最新内容，不是旧图。）
    const quint64 fp = frameFingerprint(img);
    m_sameFrameStreak = (fp == m_frameHash) ? (m_sameFrameStreak + 1) : 0;
    m_frameHash = fp;
    setStatic(m_sameFrameStreak >= kStaticStreak);
    if (!m_screenStatic) emit frameChanged();

    if (m_frameCount % 30 == 1) {   // 每 30 帧报一次，别把日志刷爆
        const QString seq = header.isEmpty()
                                ? QStringLiteral("(旧帧)")
                                : QString::number(header.value(QStringLiteral("seq")).toInt());
        logf("[viewer] 画面在动：第 %d 帧（seq=%s，%d 字节，%.1f fps）",
             m_frameCount, seq.toUtf8().constData(), m_lastFrameBytes, m_fps);
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
    if (secs >= 1.0 && m_framesSinceCheck > 0) {
        m_fps = m_framesSinceCheck / secs;
        m_framesSinceCheck = 0;
        m_lastFpsCheckMs = now;
        emit statsChanged();
    }
}

void ViewerBackend::sendAction(const QString &action, const QJsonObject &params)
{
    if (m_currentUid.isEmpty()) {
        setStatus(QStringLiteral("还没选设备，指令没发（不报错就是骗人）"), true);
        logf("[viewer] FAIL 没选设备就点 %s，不发出去", action.toUtf8().constData());
        return;
    }
    setStatus(QStringLiteral("已把「%1」发给 %2，等云端回话…").arg(action).arg(m_currentUid), false);
    QJsonObject p;
    p.insert(QStringLiteral("uid"), m_currentUid);
    p.insert(QStringLiteral("action"), action);
    if (!params.isEmpty()) p.insert(QStringLiteral("params"), params);
    sendEnvelope(QStringLiteral("instruction"), p);
}

void ViewerBackend::sendPing()
{
    if (m_currentUid.isEmpty()) {
        setStatus(QStringLiteral("还没选设备，探活没发"), true);
        return;
    }
    m_pingSentMs = QDateTime::currentMSecsSinceEpoch();
    QJsonObject p;
    p.insert(QStringLiteral("uid"), m_currentUid);
    p.insert(QStringLiteral("action"), QStringLiteral("ping"));
    sendEnvelope(QStringLiteral("instruction"), p);
    setStatus(QStringLiteral("已向 %1 发探活，等它回话…").arg(m_currentUid), false);
}

void ViewerBackend::sendPointer(const QString &kind, double nx, double ny)
{
    if (m_currentUid.isEmpty()) { logf("[viewer] FAIL 没选设备不发操控"); return; }
    QJsonObject params;
    params.insert(QStringLiteral("kind"), kind);
    params.insert(QStringLiteral("x"), nx);
    params.insert(QStringLiteral("y"), ny);
    QJsonObject p;
    p.insert(QStringLiteral("uid"), m_currentUid);
    p.insert(QStringLiteral("action"), QStringLiteral("input"));
    p.insert(QStringLiteral("params"), params);
    sendEnvelope(QStringLiteral("instruction"), p);
    logf("[viewer] 操控→ %s 在 (%d%%, %d%%)", kind.toUtf8().constData(),
         (int)(nx * 100), (int)(ny * 100));
}

// ──────────────────────────────────────────────────────────────────────
// 文件推送
// ──────────────────────────────────────────────────────────────────────

// 7 个读数（fileState/fileName/fileBytes/fileTotal/filePercent/fileError/fileTarget）
// 在头文件里内联定义了 —— 那里是唯一一处，别在这儿再写一遍（C2084 重定义）。

void ViewerBackend::clearFilePush()
{
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
    m_fileState = QStringLiteral("failed");
    m_fileError = why;
    if (m_pushFile.isOpen()) m_pushFile.close();
    logf("[viewer] FAIL 文件推送中断：%s", why.toUtf8().constData());
    emit fileProgressChanged();
}

QString ViewerBackend::pickFile()
{
    const QString path = QFileDialog::getOpenFileName(
                nullptr, QStringLiteral("选要推给这台教室机的文件"), QString(),
                QStringLiteral("所有文件 (*.*)"));
    if (path.isEmpty()) return QString();
    // 统一给 file:// URL，让 pushFile 那边的 QUrl::fromUserInput 有统一入口可解
    const QString url = QUrl::fromLocalFile(path).toString();
    logf("[viewer] 选文件 → %s", url.toUtf8().constData());
    return url;
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

void ViewerBackend::sendType(const QString &text)
{
    if (m_currentUid.isEmpty()) { logf("[viewer] FAIL 没选设备不发打字"); return; }
    QJsonObject params;
    params.insert(QStringLiteral("kind"), QStringLiteral("type"));
    params.insert(QStringLiteral("text"), text);
    QJsonObject p;
    p.insert(QStringLiteral("uid"), m_currentUid);
    p.insert(QStringLiteral("action"), QStringLiteral("input"));
    p.insert(QStringLiteral("params"), params);
    sendEnvelope(QStringLiteral("instruction"), p);
    logf("[viewer] 操控→ 打字 %d 字符", text.size());
}
