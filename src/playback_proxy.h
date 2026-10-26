// 2026-10-26 · 产品笔记 P4（监控回放小窗）—— 回放用的本机回环代理
//
// 【为什么必须有它】云端 /recordings/* 是 Bearer 令牌鉴权（2026-10-08 补的那道：
//   早期"URL 不可知即可"已被证伪 —— 枚举能拖到全校录像）。而 QML MediaPlayer 的
//   source 只吃 QUrl，**带不了请求头**，直接给它云端地址只会拿到 401。
//   于是只有两条路：
//     · 放宽云端鉴权（例如允许 ?token= 写在 URL 里）→ 令牌进日志/Referer，坚决不做；
//     · 本机起回环代理，转发时补 Authorization → 令牌不出进程。
//   选后者。只监听 127.0.0.1，且路径带一段随机串，同机其它进程撞不进来。
//
// 【它同时是"传得更快"的另一半】代理逐字转发 Range 请求头与 206 响应
//   （云端本轮同步补了 206），所以拖动进度条 = 一次小范围请求，
//   而不是把几十 MB 的录像整段重新拉一遍。
#pragma once

#include <QObject>
#include <QTcpServer>
#include <QHash>

class QNetworkAccessManager;
class QNetworkReply;
class QTcpSocket;

class PlaybackProxy : public QObject
{
    Q_OBJECT
public:
    explicit PlaybackProxy(QObject *parent = nullptr);
    ~PlaybackProxy() override;

    // 起回环监听。返回基址（http://127.0.0.1:<port>）；失败返回空串。
    // cloudHttpBase 形如 http://host:8788（不带尾斜杠）
    QString start(QNetworkAccessManager *nam, const QString &cloudHttpBase, const QString &token);
    void stop();
    bool isRunning() const { return m_server.isListening(); }

    // 给 QML MediaPlayer 用的播放地址（含随机路径段 + uid + 文件名）
    QString urlFor(const QString &uid, const QString &name) const;

    // 诊断：多媒体运行时是否可用（QML 拿它决定要不要亮"回放"入口，
    // 而不是点了没反应）。缺失 Qt6Multimedia.dll / plugins/multimedia 时会是 false。
    static bool mediaBackendAvailable();

private slots:
    void onNewConnection();

private:
    void serve(QTcpSocket *sock);

    QTcpServer m_server;
    QNetworkAccessManager *m_nam = nullptr;
    QString m_base;      // 云端 http 基址
    QString m_token;     // 云端令牌（只在本进程内存里，不进 URL）
    QString m_secret;    // 路径里的随机段，挡掉同机误用
    QHash<QTcpSocket *, QByteArray> m_headBuf;   // 尚未收完的请求头
    QHash<QTcpSocket *, QNetworkReply *> m_pending;
};
