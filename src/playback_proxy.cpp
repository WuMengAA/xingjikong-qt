#include "playback_proxy.h"

#include <QAudioOutput>
#include <QHostAddress>
#include <QMediaPlayer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpSocket>
#include <QUuid>
#include <QUrl>
#include <QUrlQuery>

PlaybackProxy::PlaybackProxy(QObject *parent) : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, &PlaybackProxy::onNewConnection);
}

PlaybackProxy::~PlaybackProxy()
{
    stop();
}

QString PlaybackProxy::start(QNetworkAccessManager *nam, const QString &cloudHttpBase, const QString &token)
{
    if (m_server.isListening())
        return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    if (!nam || cloudHttpBase.isEmpty())
        return {};
    m_nam = nam;
    m_base = cloudHttpBase;
    m_token = token;
    m_secret = QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-')).left(24);
    if (!m_server.listen(QHostAddress::LocalHost, 0)) {
        m_secret.clear();
        return {};
    }
    return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
}

void PlaybackProxy::stop()
{
    if (m_server.isListening())
        m_server.close();
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
        it.value()->abort();
        it.value()->deleteLater();
    }
    m_pending.clear();
    m_headBuf.clear();
    m_secret.clear();
}

QString PlaybackProxy::urlFor(const QString &uid, const QString &name) const
{
    if (!m_server.isListening() || m_secret.isEmpty())
        return {};
    return QStringLiteral("http://127.0.0.1:%1/%2/p?uid=%3&name=%4")
        .arg(m_server.serverPort())
        .arg(m_secret,
             QString::fromUtf8(QUrl::toPercentEncoding(uid)),
             QString::fromUtf8(QUrl::toPercentEncoding(name)));
}

bool PlaybackProxy::mediaBackendAvailable()
{
    // 只探测，不真播：缺 Qt6Multimedia.dll 时进程起不来（压根走不到这里）；
    // 缺 plugins/multimedia 后端插件时，QMediaPlayer 构造后立刻置 error。
    QMediaPlayer player;
    QAudioOutput out;
    player.setAudioOutput(&out);
    return player.error() == QMediaPlayer::NoError;
}

void PlaybackProxy::onNewConnection()
{
    while (QTcpSocket *sock = m_server.nextPendingConnection()) {
        connect(sock, &QTcpSocket::readyRead, this, [this, sock]() { serve(sock); });
        connect(sock, &QTcpSocket::disconnected, this, [this, sock]() {
            if (QNetworkReply *r = m_pending.take(sock)) {
                r->abort();
                r->deleteLater();
            }
            m_headBuf.remove(sock);
            sock->deleteLater();
        });
    }
}

void PlaybackProxy::serve(QTcpSocket *sock)
{
    // 已在转发中：忽略后续上行（播放器不会发请求体；
    // 而 Range 续传在 Qt 侧是一次新连接，不会复用旧 socket）
    if (m_pending.contains(sock))
        return;

    QByteArray &buf = m_headBuf[sock];
    buf += sock->readAll();
    const int hdrEnd = buf.indexOf("\r\n\r\n");
    if (hdrEnd < 0)
        return;                       // 头没收全，等下一次 readyRead
    const QByteArray head = buf.left(hdrEnd);
    buf.clear();

    const QList<QByteArray> lines = head.split('\n');
    if (lines.isEmpty()) {
        sock->disconnectFromHost();
        return;
    }
    const QList<QByteArray> reqLine = QByteArray(lines.first()).trimmed().split(' ');
    if (reqLine.size() < 2 || reqLine.first() != "GET") {
        sock->write("HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n");
        sock->disconnectFromHost();
        return;
    }

    const QByteArray target = reqLine.at(1);
    const int qpos = target.indexOf('?');
    const QString path = QString::fromLatin1(qpos >= 0 ? target.left(qpos) : target);
    // 路径必须带本机的随机段 —— 挡掉同机其它进程顺手用这个端口拖录像
    if (path != QStringLiteral("/%1/p").arg(m_secret)) {
        sock->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
        sock->disconnectFromHost();
        return;
    }

    // ⚠️ 三元两侧必须同型：QByteArray vs const char[1] 会 C2445（编译不过）
    const QUrlQuery q(QString::fromUtf8(qpos >= 0 ? target.mid(qpos + 1) : QByteArray()));
    const QString uid = q.queryItemValue(QStringLiteral("uid"));
    const QString name = q.queryItemValue(QStringLiteral("name"));
    if (uid.isEmpty() || name.isEmpty()) {
        sock->write("HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
        sock->disconnectFromHost();
        return;
    }

    // Range 头：拖进度条不重下整段的关键，必须原样带到云端
    QByteArray rangeHdr;
    for (const QByteArray &ln : lines) {
        const QByteArray t = ln.trimmed();
        if (t.startsWith("Range:") || t.startsWith("range:"))
            rangeHdr = t.mid(6).trimmed();
    }

    QUrl up(m_base + QStringLiteral("/recordings/") + uid + QLatin1Char('/') + name);
    QNetworkRequest req(up);
    if (!m_token.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    if (!rangeHdr.isEmpty())
        req.setRawHeader("Range", rangeHdr);

    QNetworkReply *reply = m_nam->get(req);
    // 256KB 在途上限：转发大文件时不会被 QNAM 把内存顶满
    reply->setReadBufferSize(1 << 18);
    m_pending.insert(sock, reply);

    auto *sentFlag = new bool(false);      // metaDataChanged 可能多次来（重定向），只回一次状态行

    connect(reply, &QNetworkReply::metaDataChanged, this, [sock, reply, sentFlag]() {
        if (*sentFlag)
            return;
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (code == 0)
            return;                        // 还没拿到状态行
        *sentFlag = true;
        const char *reason = code == 206 ? "Partial Content"
                           : code == 200 ? "OK"
                           : code == 416 ? "Range Not Satisfiable"
                           : code == 401 ? "Unauthorized"
                           : code == 403 ? "Forbidden"
                           : code == 404 ? "Not Found"
                                         : "Error";
        QByteArray out = "HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\n";
        // 只挑播放器真正需要的头。刻意**不**转发 Transfer-Encoding / Content-Encoding：
        // QNAM 已把分块/压缩处理干净，再转一次反而让播放器算错长度。
        for (const char *h : {"Content-Type", "Content-Length", "Content-Range", "Accept-Ranges"}) {
            const QByteArray v = reply->rawHeader(h);
            if (!v.isEmpty())
                out += QByteArray(h) + ": " + v + "\r\n";
        }
        out += "Connection: close\r\n\r\n";
        sock->write(out);
    });

    connect(reply, &QNetworkReply::readyRead, this, [sock, reply]() {
        const QByteArray chunk = reply->readAll();
        if (!chunk.isEmpty())
            sock->write(chunk);
    });

    connect(reply, &QNetworkReply::finished, this, [this, sock, reply, sentFlag]() {
        delete sentFlag;
        m_pending.remove(sock);
        reply->deleteLater();
        sock->disconnectFromHost();
    });
}
