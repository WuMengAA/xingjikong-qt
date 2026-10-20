#include "permission_manager.h"

#include <QLatin1Char>

PermissionManager::PermissionManager(QObject *parent) : QObject(parent) {}

void PermissionManager::setIdentity(const QString &uid, const QString &schoolId,
                                    const QStringList &roles, const QJsonArray &scopes) {
    m_full = false;
    m_uid = uid;
    m_schoolId = schoolId;
    m_roles = roles;
    m_scopes.clear();
    for (const QJsonValue &v : scopes) {
        const QJsonObject o = v.toObject();
        const QString t = o.value(QStringLiteral("type")).toString();
        const QString id = o.value(QStringLiteral("id")).toString();
        if (!t.isEmpty() && !id.isEmpty()) m_scopes.append({t, id});
    }
    emit identityChanged();
}

void PermissionManager::setFullAccess() {
    m_full = true;
    m_uid.clear();
    m_schoolId.clear();
    m_roles.clear();
    m_scopes.clear();
    emit identityChanged();
}

void PermissionManager::reset() {
    m_full = false;
    m_uid.clear();
    m_schoolId.clear();
    m_roles.clear();
    m_scopes.clear();
    emit identityChanged();
}

bool PermissionManager::isPlatformAdmin() const {
    // schoolId == null（QJsonValue::isNull ⇒ toVariant().isNull ⇒ QString().isNull()）且 roles 含 platform_admin
    return m_schoolId.isNull() && m_roles.contains(QStringLiteral("platform_admin"));
}

bool PermissionManager::scopeCovers(const QString &classCode) const {
    if (classCode.isEmpty()) return false;
    for (const Scope &s : m_scopes) {
        if (s.type == QStringLiteral("class")) {
            if (s.id == classCode) return true;
        } else if (s.type == QStringLiteral("grade") || s.type == QStringLiteral("school")) {
            // 父节点前缀覆盖：class_xlzx_2028_08 以 class_xlzx_2028_ 或 class_xlzx_ 开头
            if (classCode.startsWith(s.id + QLatin1Char('_'))) return true;
        }
    }
    return false;
}

bool PermissionManager::canAccess(const QString &classCode) const {
    if (m_full) return true;                 // 全权：静态令牌 / 旧票
    if (isPlatformAdmin()) return true;      // 平台超管
    if (classCode.isEmpty()) return false;   // 无班级归属 ⇒ 谁都不该碰
    return scopeCovers(classCode);
}

bool PermissionManager::canAccessDevice(const QJsonObject &device) const {
    const QString cc = device.value(QStringLiteral("classCode")).toString();
    return canAccess(cc);
}

const QStringList &PermissionManager::sensitiveDeviceFields() {
    static const QStringList fields = {
        QStringLiteral("ip"), QStringLiteral("mac"),
        QStringLiteral("lanIp"), QStringLiteral("wanIp"), QStringLiteral("localIp"),
        QStringLiteral("macAddr"), QStringLiteral("ipAddr"),
        QStringLiteral("ipv4"), QStringLiteral("hardwareAddr"),
    };
    return fields;
}

QJsonObject PermissionManager::maskedDevice(const QJsonObject &device) const {
    QJsonObject out = device;
    if (!shouldMask()) return out;          // 平台超管 / 全权 ⇒ 不脱敏
    for (const QString &key : sensitiveDeviceFields()) {
        if (out.contains(key)) out.remove(key);
    }
    return out;
}

QJsonArray PermissionManager::maskDevices(const QJsonArray &devices) const {
    QJsonArray out;
    for (const QJsonValue &v : devices) {
        if (v.isObject()) out.append(maskedDevice(v.toObject()));
        else out.append(v);
    }
    return out;
}

const QHash<QString, QStringList> &PermissionManager::roleCaps() {
    // 前端镜像策略（须与云端 allowedActions 对齐；后端校验才是唯一边界）。
    // 默认拒绝（deny-by-default）：表里没列的 cap 一律 false。
    static const QHash<QString, QStringList> table = {
        { QStringLiteral("teacher"), {
            QStringLiteral("device:monitor"), QStringLiteral("device:control"),
            QStringLiteral("device:intercom"), QStringLiteral("device:file-push"),
            QStringLiteral("device:notify"), QStringLiteral("device:remote"),
            QStringLiteral("class:manage"),
            // RBAC 首切（2026-10-20）：操作日志属阅读类，老师可看；assistant 不列（不可看）。
            QStringLiteral("system:logs"),
        }},
        { QStringLiteral("assistant"), {
            QStringLiteral("device:monitor"), QStringLiteral("device:notify"),
            QStringLiteral("device:intercom"),
        }},
    };
    return table;
}

bool PermissionManager::can(const QString &cap) const {
    if (m_full || isPlatformAdmin()) return true;   // 全权 / 平台超管 ⇒ 一切放行
    for (const QString &role : m_roles) {
        const auto it = roleCaps().find(role);
        if (it != roleCaps().end() && it.value().contains(cap)) return true;
    }
    return false;   // 默认拒绝（deny-by-default）
}
