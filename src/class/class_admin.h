#ifndef CLASS_ADMIN_H
#define CLASS_ADMIN_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QString>
#include <QVariantList>

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
    // ── 班级成员（人可以被添加，2026-10-24）──
    // members() 是同步取一次成员清单（拉完发 membersList 信号，QML 填表）；
    // addMember/removeMember 是写操作，反馈走 opFinished（成功文案带成员账号）。
    Q_INVOKABLE void members(const QString &code);
    Q_INVOKABLE void addMember(const QString &code, const QString &uid, const QString &role);
    Q_INVOKABLE void removeMember(const QString &code, const QString &uid);

    // ── 发布中心（P1 第一刀：通知三件套 + 作业）──
    // publish 走 POST /api/class/publish，body { classCode, card:{type,title,body} }。
    // type ∈ {student-notice, class-notice, fullscreen-notice, homework}（云端白名单，
    // 非法类型/空内容/无权限 都回 opFinished(false, 人话)）。成功文案带送达/在线台数。
    Q_INVOKABLE void publish(const QString &classCode, const QString &type,
                             const QString &title, const QString &body);

signals:
    /** 操作完成。ok=false 时 message 是云端给的人话错误（网络层失败也有兜底文案）。 */
    void opFinished(const QString &op, bool ok, const QString &message);
    /** 一个班的成员清单取回（members() 的结果；空数组 = 班里没人）。 */
    void membersList(const QString &classCode, const QVariantList &members);
    /** 2026-10-24 重认证门：写操作被云端 REAUTH_REQUIRED 拦（403）。
     *  此时连接/会话都还在，只是"该再验一次密码了"。UI 收到后弹提示 →
     *  走一次重新登录（backend.loginWithSite()）→ 新票到位后重试即可。 */
    void reauthRequired(const QString &message);

private:
    void post(const QString &action, const QJsonObject &body);

    ViewerBackend *m_backend = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
};

#endif // CLASS_ADMIN_H
