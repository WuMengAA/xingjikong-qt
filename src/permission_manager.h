#pragma once
// 管理端（前端）权限镜像层 —— 教育行业 B 端「千人千面 / 数据隔离」的前端部分。
//
// 设计铁律（与云端 perms.js 契约《星集控-多校多身份-现状勘误与scope契约草案》一致）：
//   ① 云端是唯一权限真源；本类只在客户端做「体验层裁剪」，绝不替代后端校验。
//   ② 语义必须与云端 perms.js 逐字一致：scope 类型 {class,grade,school}，
//      id 为班级码形态（class_<校>_<届>_<班>）；父节点前缀覆盖子节点
//      （school 覆盖 class_<校>_*，grade 覆盖 class_<校>_<届>_*）。
//   ③ 向后兼容：scopes === null（静态全权令牌 / 旧票）⇒ 全权；roles 含 platform_admin ⇒ 跳过过滤。
//   ④ 脱敏是前端镜像：最终以云端「字段脱敏」为准；本层只在 UI 数据落地前兜底。
//
// 数据来源：管理端以站点签发的每用户 viewer 票鉴权后，云端在 auth-ok 回执里带 identity
//          （{uid, schoolId, roles, scopes}）；ViewerBackend 收到后调 setIdentity() 注入。
// 当前生产管理端仍用静态全权令牌（CLOUD_VIEWER_TOKEN），此时走 setFullAccess()，行为不变。
//
// 线程模型：本对象只在主线程（ViewerBackend / QML）访问，不另加锁；
//          跨线程使用前需经 queued 调用或外部同步。

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

class PermissionManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList roles READ roles NOTIFY identityChanged)
    Q_PROPERTY(bool isPlatformAdmin READ isPlatformAdmin NOTIFY identityChanged)
    Q_PROPERTY(bool fullAccess READ fullAccess NOTIFY identityChanged)

public:
    explicit PermissionManager(QObject *parent = nullptr);

    // ---- 身份注入 ----
    // 与云端 perms.js identityFromAuth 同构。scopes 为 [{type,id}] 数组；
    // 调用方需在「票里无 identity（旧票/静态令牌）」时改调 setFullAccess()。
    void setIdentity(const QString &uid, const QString &schoolId,
                     const QStringList &roles, const QJsonArray &scopes);
    void setFullAccess();   // scopes == null：静态令牌 / 旧票（契约 §4.4，全权）
    void reset();           // 未鉴权：一切拒绝

    // ---- 角色 ----
    QStringList roles() const { return m_roles; }
    Q_INVOKABLE bool hasRole(const QString &role) const { return m_roles.contains(role); }
    bool isPlatformAdmin() const;   // schoolId == null && roles 含 platform_admin
    bool fullAccess() const { return m_full; }

    // ---- 行级（按班级 scope）----
    // 镜像 perms.js canAccess：null 身份拒；fullAccess/platform_admin 过；
    // 目标设备无班级归属 ⇒ 拒；class 精确 / grade|school 前缀覆盖。
    Q_INVOKABLE bool canAccess(const QString &classCode) const;
    Q_INVOKABLE bool canAccessDevice(const QJsonObject &device) const;

    // ---- 字段脱敏（前端镜像；最终以云端为准）----
    // 敏感设备字段（老师/助教不应看到）。platform_admin 与 fullAccess 不脱敏。
    static const QStringList &sensitiveDeviceFields();
    Q_INVOKABLE QJsonObject maskedDevice(const QJsonObject &device) const;
    QJsonArray maskDevices(const QJsonArray &devices) const;

    // ---- 行级裁剪（集合形态）----
    // 2026-10-25：把 canAccessDevice 的判定**批量**应用一次 —— 设备表在 C++ 单一入口
    // 裁剪，QML 十几处 `backend.devices` 消费点不必各写一份过滤（写一份就漂一份）。
    // 全权 / 平台超管原样返回（契约 §4.4）；受限身份按 scope 保留可见设备。
    QJsonArray scopedDevices(const QJsonArray &devices) const;

    // ---- 组件级鉴权（QML 用）----
    // cap 形如 "domain:action"，例："device:ip-view" / "class:manage" / "system:settings"。
    // 前端镜像：platform_admin / fullAccess 放行一切；其余按内置 role→caps 策略
    //          （须与云端 allowedActions 对齐；后端校验才是唯一边界）。
    Q_INVOKABLE bool can(const QString &cap) const;

signals:
    void identityChanged();

private:
    struct Scope { QString type; QString id; };

    bool m_full = false;       // scopes == null ⇒ 全权（向后兼容）
    QString m_uid;
    QString m_schoolId;
    QStringList m_roles;
    QList<Scope> m_scopes;
    /** scopedDevices 上次隐藏了几台（-1 = 没算过）。设备表 15s 轮询一次，
     *  只在数量**变化**时打日志，避免把"被裁剪"刷成日志噪音、也避免完全不吭声。 */
    mutable int m_lastScopedOut = -1;

    bool scopeCovers(const QString &classCode) const;
    bool shouldMask() const { return !m_full && !isPlatformAdmin(); }
    static const QHash<QString, QStringList> &roleCaps();  // 内置策略表
};
