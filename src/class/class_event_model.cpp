#include "class_event_model.h"

ClassEventModel::ClassEventModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ClassEventModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_events.size();
}

QVariant ClassEventModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_events.size())
        return QVariant();
    const Event &e = m_events.at(index.row());
    switch (role) {
    case IdRole:       return e.id;
    case AtRole:       return e.at;
    case KindRole:     return e.kind;
    case UidRole:      return e.uid;
    case DataTextRole: return e.dataText;
    case IsMemberRole: return e.isMember;
    case DetailRole:   return e.detail;
    }
    return QVariant();
}

QHash<int, QByteArray> ClassEventModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[IdRole]       = "eventId";
    roles[AtRole]       = "at";
    roles[KindRole]     = "kind";
    roles[UidRole]      = "uid";
    roles[DataTextRole] = "dataText";
    roles[IsMemberRole] = "isMember";
    roles[DetailRole]   = "detail";
    return roles;
}

void ClassEventModel::resetFromArray(const QJsonArray &events)
{
    beginResetModel();
    m_events.clear();
    m_maxId = 0;
    m_events.reserve(qMin(events.size(), (int)kMaxEvents));
    // 云端给的是旧→新；倒序装，保持"新在头"。
    for (int i = events.size() - 1; i >= 0; --i) {
        const QJsonObject o = events.at(i).toObject();
        Event e;
        e.id   = o.value(QStringLiteral("id")).toInteger();
        e.at   = o.value(QStringLiteral("at")).toString();
        e.kind = o.value(QStringLiteral("kind")).toString();
        e.uid  = o.value(QStringLiteral("uid")).toString();
        const QJsonObject data = o.value(QStringLiteral("data")).toObject();
        e.dataText = QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact));
        e.isMember = e.kind.startsWith(QLatin1String("member."));
        e.detail = data.value(QStringLiteral("reason")).toString();
        if (e.detail.isEmpty()) e.detail = data.value(QStringLiteral("message")).toString();
        if (e.id > m_maxId) m_maxId = e.id;
        if (m_events.size() < kMaxEvents) m_events.append(e);
    }
    endResetModel();
}

bool ClassEventModel::prepend(const QJsonObject &ev)
{
    const qint64 id = ev.value(QStringLiteral("id")).toInteger();
    if (id <= 0 || id <= m_maxId) return false;   // 无 id / 重复/旧事件：水位去重
    Event e;
    e.id   = id;
    e.at   = ev.value(QStringLiteral("at")).toString();
    e.kind = ev.value(QStringLiteral("kind")).toString();
    e.uid  = ev.value(QStringLiteral("uid")).toString();
    const QJsonObject data = ev.value(QStringLiteral("data")).toObject();
    e.dataText = QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact));
    e.isMember = e.kind.startsWith(QLatin1String("member."));
    e.detail = data.value(QStringLiteral("reason")).toString();
    if (e.detail.isEmpty()) e.detail = data.value(QStringLiteral("message")).toString();

    beginInsertRows(QModelIndex(), 0, 0);
    m_events.prepend(e);
    m_maxId = id;
    endInsertRows();
    if (m_events.size() > kMaxEvents) {
        beginRemoveRows(QModelIndex(), m_events.size() - 1, m_events.size() - 1);
        m_events.removeLast();
        endRemoveRows();
    }
    return true;
}
