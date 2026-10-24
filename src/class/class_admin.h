#ifndef CLASS_ADMIN_H
#define CLASS_ADMIN_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QString>

class ViewerBackend;

// 班级管理调用层（2026-10-24 · 管理端「班级管理」面板 · 方案 B）
//
// 四个写操作全部走 HTTP POST /api/class/*（云端 src/class-admin.js）：
//   create 只给班名即建班（不绑设备）/ rename 只改显示名（canonical 码不动）/
//   delete 带策略（refuse 有设备即拒 / release 先移出再删）/ unbind 移出一台设备。
//
// 与 ClassService 的分工（刻意不混）：ClassService 是 WS 班级域**数据面**
// （订阅/快照/事件/缩略图，零 HTTP）；本类只发**管理写操作**，不持有任何
// 班级状态 —— 班级名单唯一真源是云端 /api/classes（写成功后 QML 立即重拉，
// 不等 15s 轮询）。凭据不外泄：每次 POST 现取 backend->httpBaseUrl() /
// viewerToken()（与 InteractBridge 同一接口，抄两份必然漂）。
class ClassAdmin : public QObject
{
    Q_OBJECT
public:
    explicit ClassAdmin(ViewerBackend *backend, QObject *parent = nullptr);

    Q_INVOKABLE void createClass(const QString &className);
    Q_INVOKABLE void renameClass(const QString &code, const QString &name);
    Q_INVOKABLE void deleteClass(const QString &code, const QString &strategy);
    Q_INVOKABLE void unbindDevice(const QString &code, const QString &uid);
    Q_INVOKABLE void bindDevice(const QString &code, const QString &uid);

signals:
    /** 操作完成。ok=false 时 message 是云端给的人话错误（网络层失败也有兜底文案）。 */
    void opFinished(const QString &op, bool ok, const QString &message);

private:
    void post(const QString &action, const QJsonObject &body);

    ViewerBackend *m_backend = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
};

#endif // CLASS_ADMIN_H
