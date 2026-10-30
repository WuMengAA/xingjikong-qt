#include "class_device_model.h"

ClassDeviceModel::ClassDeviceModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ClassDeviceModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_devices.size();
}

QVariant ClassDeviceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_devices.size())
        return QVariant();
    const Device &d = m_devices.at(index.row());
    switch (role) {
    case UidRole:       return d.uid;
    case NameRole:      return d.name;
    case OnlineRole:    return d.online;
    case VersionRole:   return d.version;
    case LastSeenRole:  return d.lastSeenAgoSec;
    case HasThumbRole:  return d.hasThumb;
    case ThumbRevRole:  return d.thumbRev;
    case DevRoleRole:   return d.devRole;
    }
    return QVariant();
}

QHash<int, QByteArray> ClassDeviceModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[UidRole]      = "uid";
    roles[NameRole]     = "name";
    roles[OnlineRole]   = "online";
    roles[VersionRole]  = "version";
    roles[LastSeenRole] = "lastSeen";
    roles[HasThumbRole] = "hasThumb";
    roles[ThumbRevRole] = "thumbRev";
    roles[DevRoleRole]  = "devRole";
    return roles;
}

void ClassDeviceModel::resetFromArray(const QJsonArray &devices)
{
    beginResetModel();
    m_devices.clear();
    m_devices.reserve(devices.size());
    for (const QJsonValue &v : devices) {
        const QJsonObject o = v.toObject();
        Device d;
        d.uid = o.value(QStringLiteral("uid")).toString();
        if (d.uid.isEmpty()) continue;   // 无 uid 的设备行没有意义（QML 按 uid 取图/发指令）
        d.name    = o.value(QStringLiteral("name")).toString();
        d.online  = o.value(QStringLiteral("online")).toBool(true);
        d.version = o.value(QStringLiteral("version")).toString();
        d.lastSeenAgoSec = o.value(QStringLiteral("lastSeenAgoSec")).toInt(-1);
        // 设备用途（系统总图挂账①）：云端 knownDevices.role 已随 devices 下发（maskDevice 只删
        // ip/mac，保留 role），此前 viewer 未消费 ⇒ 管理端看不出这台是教室端还是广播端。
        d.devRole = o.value(QStringLiteral("role")).toString();
        m_devices.append(d);
    }
    endResetModel();
}

int ClassDeviceModel::upsert(const QJsonObject &dev)
{
    const QString uid = dev.value(QStringLiteral("uid")).toString();
    if (uid.isEmpty()) return -1;
    const int row = rowOf(uid);
    if (row < 0) {
        // 新设备：只认 member.online 给的字段（uid + online），其余留默认，
        // 等下一次快照补全 name/version（快照是唯一整表权威）。
        Device d;
        d.uid = uid;
        d.online = dev.value(QStringLiteral("online")).toBool(true);
        const QString name = dev.value(QStringLiteral("name")).toString();
        if (!name.isEmpty()) d.name = name;
        d.devRole = dev.value(QStringLiteral("role")).toString();
        const int at = m_devices.size();
        beginInsertRows(QModelIndex(), at, at);
        m_devices.append(d);
        endInsertRows();
        return at;
    }
    Device &d = m_devices[row];
    if (dev.contains(QStringLiteral("online")))
        d.online = dev.value(QStringLiteral("online")).toBool();
    if (dev.contains(QStringLiteral("name"))) {
        const QString n = dev.value(QStringLiteral("name")).toString();
        if (!n.isEmpty()) d.name = n;
    }
    if (dev.contains(QStringLiteral("version")))
        d.version = dev.value(QStringLiteral("version")).toString();
    if (dev.contains(QStringLiteral("lastSeenAgoSec")))
        d.lastSeenAgoSec = dev.value(QStringLiteral("lastSeenAgoSec")).toInt(-1);
    if (dev.contains(QStringLiteral("role")))
        d.devRole = dev.value(QStringLiteral("role")).toString();
    const QModelIndex mi = index(row, 0);
    emit dataChanged(mi, mi);
    return row;
}

bool ClassDeviceModel::setOnline(const QString &uid, bool online)
{
    const int row = rowOf(uid);
    if (row < 0) return false;
    Device &d = m_devices[row];
    if (d.online == online) return true;
    d.online = online;
    const QModelIndex mi = index(row, 0);
    emit dataChanged(mi, mi);
    return true;
}

bool ClassDeviceModel::markThumbnail(const QString &uid, int rev)
{
    const int row = rowOf(uid);
    if (row < 0) return false;
    Device &d = m_devices[row];
    d.hasThumb = true;
    d.thumbRev = rev;
    const QModelIndex mi = index(row, 0);
    emit dataChanged(mi, mi);
    return true;
}

int ClassDeviceModel::rowOf(const QString &uid) const
{
    // 班级规模 ≤ 百台，线性扫足够；不为 O(1) 引入第二张表（同步成本 > 收益）。
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices.at(i).uid == uid) return i;
    }
    return -1;
}

QString ClassDeviceModel::uidAt(int row) const
{
    return (row >= 0 && row < m_devices.size()) ? m_devices.at(row).uid : QString();
}

int ClassDeviceModel::onlineCount() const
{
    int n = 0;
    for (const Device &d : m_devices) {
        if (d.online) ++n;
    }
    return n;
}
