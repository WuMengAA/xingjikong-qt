#pragma once
// 星集控被控端 · Local API 网关（2026-10-31 · 三条已知地基缺口之一）
//
// 服务本机 127.0.0.1:8080 的 HTTP/JSON 服务，供 ClassIsland / 插件 / 局域网手机联动。
// 鉴权网关（本轮最小可验证版）：
//   · 仅绑定回环 127.0.0.1（边界写死，不靠配置，对应 vnc_bridge 同款安全口径）
//   · 公开端点白名单：GET /v1/ping（存活探测）
//   · 其余一律要求请求头 X-Local-Api-Token == 本机签发的 token（见 localapi_auth）
//   · 鉴权失败 / 限流 → 401/429 + 追加失败审计日志
//   · 设备身份（classcode/devrole）从 main.cpp 同源文件读取，不靠请求体自报
//
// 设计取舍：用 QTcpServer + 手写最小 HTTP/1.1 解析（仅 GET，Connection: close），
// 不引入 QHttpServer 模块 —— 避免新增 Qt 模块依赖与 CMake 改动，且与 vnc_bridge /
// oauthbind 的"纯字节 + QTcpServer"既有风格一致。鉴权是重点，HTTP 层保持最小。

#include <QObject>
#include <QByteArray>
#include <QHash>

class QTcpServer;
class QTcpSocket;
class LocalApiAuth;

class LocalApiServer : public QObject
{
    Q_OBJECT
public:
    explicit LocalApiServer(QObject *parent = nullptr);
    ~LocalApiServer() override;

    // 绑定 127.0.0.1:8080 + 读身份 + 加载 token。失败只记日志、不崩（fail-closed 于端点层）。
    void start();
    QString lastError() const { return m_lastError; }
    bool isListening() const { return m_listening; }

private slots:
    void onNewConnection();
    void onReadyRead(QTcpSocket *sock);

private:
    void handleRequest(QTcpSocket *sock, const QByteArray &req);
    void respond(QTcpSocket *sock, int status, const QByteArray &body);
    void finishClient(QTcpSocket *sock);
    QString readFileTrimmed(const QString &path) const;
    // 路由（请求已过鉴权/限流）
    void route(QTcpSocket *sock, const QByteArray &method, const QByteArray &target);

    QTcpServer *m_server = nullptr;
    LocalApiAuth *m_auth = nullptr;
    QHash<QTcpSocket *, QByteArray> m_buf;
    QString m_lastError;
    QString m_classCode;   // 本机班级码（classcode.txt，可能空=未归属）
    QString m_role;        // classroom / broadcast（devrole.txt，默认 classroom）
    bool m_listening = false;
};

// ── 装配入口（main 拆分纪律：main.cpp 只加这一行调用，业务全在本域）──────
// 构造并启动本地鉴权网关；失败（端口占用等）只记日志，不阻塞被控端启动。
// 返回的实例由 Qt 父子机制管理（挂到 main 的 application 上），无需手动释放。
void localApiInit();
