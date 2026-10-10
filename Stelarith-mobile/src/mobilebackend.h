#pragma once
// 手机端 backend（2026-10-09 · 设计文档 §2/§5）
//
// 与 viewerbackend 同协议（v1 信封 + auth 首消息），独立轻量实现：
//   · 站点 OAuth 会话 → 云端接入票（第一版简化：静态令牌 STE_MOBILE_TOKEN）
//   · WS 连接 → auth 信封 → 收 devices/events/instruction-result
//   · 发轻指令（ping / 锁屏 / 考试开关）
//
// 协议已在隔离云端验证 9/9（_probe/mobile_protocol_probe.cpp）。
// Android 打包后续；本文件纯 Qt Core/WebSockets，桌面壳也可编译。

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QElapsedTimer>

class QWebSocket;
class QNetworkAccessManager;
class QNetworkReply;

class MobileBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool authed READ authed NOTIFY authedChanged)
    Q_PROPERTY(QJsonArray devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(int onlineCount READ onlineCount NOTIFY devicesChanged)

public:
    explicit MobileBackend(QObject *parent = nullptr);

    bool connected() const { return m_connected; }
    bool authed() const { return m_authed; }
    QJsonArray devices() const { return m_devices; }
    QString status() const { return m_status; }

    // 连接（URL 默认取 STE_MOBILE_URL，令牌 STE_MOBILE_TOKEN）
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

    // 轻指令（手机端边界：只做确认式小动作）——QML 按钮触发
    Q_INVOKABLE void sendLightInstruction(const QString &uid, const QString &action,
                                          const QJsonObject &params = QJsonObject());
    // 便捷封装：ping（在线探测）/ shutdown（关机，需 confirm）
    Q_INVOKABLE void pingDevice(const QString &uid);
    Q_INVOKABLE void shutdownDevice(const QString &uid);
    // P3：考试模式开关（破坏性，需确认；exam_mode / exam_mode_stop）
    Q_INVOKABLE void examMode(const QString &uid, bool on);

    // ── P2（2026-10-10 · 手机端设计 §5）：摘要 + 回放列表（HTTP GET）──
    Q_INVOKABLE void fetchEvents();       // GET /api/events → eventsChanged
    Q_INVOKABLE void fetchRecordings();   // GET /api/recordings → recordingsChanged

    // 在线设备数（Q_PROPERTY 暴露给 QML）
    int onlineCount() const;

signals:
    void connectedChanged();
    void authedChanged();
    void devicesChanged();
    void statusChanged();
    void instructionResult(const QString &uid, const QString &action,
                           const QString &result, const QJsonObject &data);
    // P2：事件摘要 / 回放列表（QML 监控页显示）
    void eventsChanged(const QJsonArray &events);
    void recordingsChanged(const QJsonArray &recordings);

private:
    void connectCloud();
    void onConnected();
    void onDisconnected();
    void onTextMessage(const QString &msg);
    void onError(int err, const QString &msg);
    void setStatus(const QString &s);

    QWebSocket *m_ws = nullptr;
    QNetworkAccessManager *m_net = nullptr;   // P2：HTTP GET（events/recordings）
    QString m_url;
    QString m_token;
    bool m_connected = false;
    bool m_authed = false;
    QJsonArray m_devices;
    QString m_status;
    QTimer m_reconnect;
    int m_reconnectMs = 5000;
};