#include "class_event_model.h"

/** 把云端统一信封里的一个 event 拆进模型行。
 *  两条来源（快照 / 实时流）共用这一份，避免"快照有 title、实时流没有"的漂移。 */
void ClassEventModel::fillFromJson(Event &e, const QJsonObject &o)
{
    e.id   = o.value(QStringLiteral("id")).toInteger();
    e.at   = o.value(QStringLiteral("at")).toString();
    e.kind = o.value(QStringLiteral("kind")).toString();
    e.uid  = o.value(QStringLiteral("uid")).toString();
    const QJsonObject data = o.value(QStringLiteral("data")).toObject();
    e.dataText = QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact));
    e.isMember = e.kind.startsWith(QLatin1String("member."));
    e.title = data.value(QStringLiteral("title")).toString();
    e.body  = data.value(QStringLiteral("body")).toString();
    e.type  = data.value(QStringLiteral("type")).toString();
    e.from  = data.value(QStringLiteral("from")).toString();
    // detail = 一句话摘要（离线原因 / 消息 / 发布标题）。Timeline 第二行只用它，
    // 所以 publish 事件必须回落到 title，否则"发了什么"在界面上是空白。
    e.detail = data.value(QStringLiteral("reason")).toString();
    if (e.detail.isEmpty()) e.detail = data.value(QStringLiteral("message")).toString();
    if (e.detail.isEmpty()) e.detail = e.title;
}

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
    case TitleRole:    return e.title;
    case BodyRole:     return e.body;
    case TypeRole:     return e.type;
    case FromRole:     return e.from;
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
    roles[TitleRole]    = "title";
    roles[BodyRole]     = "body";
    roles[TypeRole]     = "publishType";
    roles[FromRole]     = "from";
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
        Event e;
        fillFromJson(e, events.at(i).toObject());
        if (e.id > m_maxId) m_maxId = e.id;
        if (m_events.size() < kMaxEvents) m_events.append(e);
    }
    ++m_revision;
    endResetModel();
    emit countChanged();
    emit revisionChanged();
}

bool ClassEventModel::prepend(const QJsonObject &ev)
{
    const qint64 id = ev.value(QStringLiteral("id")).toInteger();
    if (id <= 0 || id <= m_maxId) return false;   // 无 id / 重复/旧事件：水位去重
    Event e;
    fillFromJson(e, ev);

    beginInsertRows(QModelIndex(), 0, 0);
    m_events.prepend(e);
    m_maxId = id;
    ++m_revision;
    endInsertRows();
    if (m_events.size() > kMaxEvents) {
        beginRemoveRows(QModelIndex(), m_events.size() - 1, m_events.size() - 1);
        m_events.removeLast();
        endRemoveRows();
    }
    emit countChanged();
    emit revisionChanged();
    return true;
}

QVariantList ClassEventModel::recentOfKind(const QString &kind, int limit) const
{
    QVariantList out;
    if (kind.isEmpty() || limit <= 0) return out;
    out.reserve(qMin(limit, (int)m_events.size()));
    for (const Event &e : m_events) {          // m_events 新在头 ⇒ 天然"最新在前"
        if (e.kind != kind) continue;
        QVariantMap m;
        m.insert(QStringLiteral("id"), e.id);
        m.insert(QStringLiteral("at"), e.at);
        m.insert(QStringLiteral("kind"), e.kind);
        m.insert(QStringLiteral("uid"), e.uid);
        m.insert(QStringLiteral("title"), e.title);
        m.insert(QStringLiteral("body"), e.body);
        m.insert(QStringLiteral("type"), e.type);
        m.insert(QStringLiteral("from"), e.from);
        out.append(m);
        if (out.size() >= limit) break;
    }
    return out;
}
