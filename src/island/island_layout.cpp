#include "island_layout.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

// ── 内置默认序 ─────────────────────────────────────────────────────
// 改造前写死在 IslandRoot.qml:308-324；现在搬到 C++，QML 只读。
// label 是面签单字（空间只有 6px 间距，两个字就挤），title 是设置页里的完整名。
// 「媒」「自」标 requiresLive：条件不满足时不该出现 —— 这是原有行为，不能因为
// 引入了"用户开关"就退化成"一直显示一个永远空着的面"。
QVector<IslandLayoutModel::Row> IslandLayoutModel::builtinRows()
{
    QVector<Row> rows;
    const auto add = [&rows](int id, const char *label, const char *title,
                             const char *note, bool live, bool enabled) {
        Row r;
        r.id = id;
        r.label = QString::fromUtf8(label);
        r.title = QString::fromUtf8(title);
        r.note = QString::fromUtf8(note);
        r.requiresLive = live;
        r.enabled = enabled;
        r.scale = 100;
        rows.push_back(r);
    };
    // id 与 IslandRoot.qml 既有编号逐一对应（0/1/4/5/6/7）。
    // ⚠️ id 3（「态」）与 id 2（「史」）已被用户裁决移除，**不要复用这两个号**：
    //    老机器磁盘上可能还留着它们，复用会让旧配置指向错的面。
    add(0, "概", "概览", "当前通知 / 状态一句话", false, true);
    add(7, "今", "今日课表", "今天全部课程、当前节与倒计时", false, true);
    add(10, "钟", "时钟", "当前时刻（时:分，可选秒）", false, true);
    add(8, "期", "日期周次", "今天日期、星期与第几教学周", false, true);
    add(9, "倒", "倒计时", "距指定时刻的倒计时（管理端可设目标）", false, true);
    add(1, "媒", "媒体", "正在播放的曲目与进度（没在播就不出现）", true, true);
    add(4, "笔", "批注", "批注工具与画笔（默认收起）", false, true);
    add(5, "设", "音量设置", "系统音量 / 通知音量 / 通知黑名单", false, true);
    add(6, "自", "自动化", "本机自动化规则（没有规则就不出现）", true, true);
    return rows;
}

IslandLayoutModel::IslandLayoutModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_rows = builtinRows();
    reload();
}

void IslandLayoutModel::reload()
{
    QSettings s;
    // 2026-11-03（m14970 ①）：显示方向（全局）。与面序同一 JSON 但分开存键——
    // 面序键 `island/layout` 是门禁契约（见头注释），内容格式也不能动。
    m_direction = s.value(QStringLiteral("island/direction"),
                          QStringLiteral("carousel")).toString();
    if (m_direction != QStringLiteral("carousel") && m_direction != QStringLiteral("tiled"))
        m_direction = QStringLiteral("carousel");   // 被改坏 ⇒ 认默认，绝不弹错

    const QString raw = s.value(QStringLiteral("island/layout")).toString();
    if (raw.isEmpty()) return;          // 从没配过 ⇒ 内置默认，不写盘

    QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    if (!doc.isArray()) return;         // 被改坏 ⇒ 认默认，绝不弹错

    const QJsonArray arr = doc.array();
    QVector<Row> loaded;
    loaded.reserve(arr.size());
    for (const auto &v : arr) {
        const QJsonObject o = v.toObject();
        const int id = o.value(QStringLiteral("id")).toInt(-1);
        const int idx = [&]() {
            for (int i = 0; i < m_rows.size(); ++i) if (m_rows[i].id == id) return i;
            return -1;
        }();
        // 设计决定 ②：未知 id 直接丢（不转成"概"）。
        if (idx < 0) continue;
        Row r = m_rows[idx];            // 文案永远取内置的，磁盘只存用户可改的三项
        r.enabled = o.value(QStringLiteral("enabled")).toBool(r.enabled);
        r.scale = qBound(80, o.value(QStringLiteral("scale")).toInt(100), 140);
        loaded.push_back(r);
    }
    // 磁盘里少记了几个面（旧版本写的 / 手工删过）⇒ 补齐到末尾，保持"没配过就是全有"。
    for (const Row &b : m_rows) {
        bool seen = false;
        for (const Row &l : loaded) if (l.id == b.id) { seen = true; break; }
        if (!seen) loaded.push_back(b);
    }
    if (loaded.isEmpty()) return;       // 一个都不认 ⇒ 保留内置默认
    m_rows = loaded;
    emit layoutChanged();
}

void IslandLayoutModel::setDirection(const QString &direction)
{
    // 2026-11-03（m14970 ①）：只认两个合法值，其它静默忽略（与 setData 的幂等口径一致）。
    if (direction != QStringLiteral("carousel") && direction != QStringLiteral("tiled"))
        return;
    if (m_direction == direction) return;
    m_direction = direction;
    QSettings s;
    s.setValue(QStringLiteral("island/direction"), m_direction);
    emit layoutChanged();
}

void IslandLayoutModel::persist() const
{
    QSettings s;
    s.setValue(QStringLiteral("island/layout"),
               QString::fromUtf8(QJsonDocument(toJson()).toJson(QJsonDocument::Compact)));
}

QJsonArray IslandLayoutModel::toJson() const
{
    QJsonArray arr;
    for (const Row &r : m_rows) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), r.id);
        o.insert(QStringLiteral("enabled"), r.enabled);
        o.insert(QStringLiteral("scale"), r.scale);
        arr.append(o);
    }
    return arr;
}

bool IslandLayoutModel::fromJson(const QJsonArray &arr)
{
    beginResetModel();
    m_rows = builtinRows();
    // 复用 reload 的合并策略：先把磁盘值搬进 m_rows，再交给 reload 走同一套校验。
    QSettings s;
    s.setValue(QStringLiteral("island/layout"),
               QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    const QVector<Row> keep = m_rows;
    m_rows = keep;
    endResetModel();
    reload();
    return true;
}

int IslandLayoutModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int IslandLayoutModel::indexOfId(int id) const
{
    for (int i = 0; i < m_rows.size(); ++i) if (m_rows[i].id == id) return i;
    return -1;
}

QVariant IslandLayoutModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows[index.row()];
    switch (role) {
    case IdRole:          return r.id;
    case LabelRole:       return r.label;
    case TitleRole:       return r.title;
    case NoteRole:        return r.note;
    case EnabledRole:     return r.enabled;
    case OrderRole:       return index.row() + 1;
    case RequiresLiveRole: return r.requiresLive;
    case ScaleRole:       return r.scale;
    default:              return {};
    }
}

bool IslandLayoutModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return false;
    Row &r = m_rows[index.row()];
    switch (role) {
    case EnabledRole: {
        const bool on = value.toBool();
        if (r.enabled == on) return true;   // 幂等：QML 的 checked 绑回写会重复进来
        r.enabled = on;
        break;
    }
    case ScaleRole: {
        const int sc = qBound(80, value.toInt(), 140);
        if (r.scale == sc) return true;
        r.scale = sc;
        break;
    }
    default:
        // 其余角色只读：顺序靠 moveRow、条件是运行态。静默拒绝而不是 return false 之外的动作。
        return false;
    }
    emit dataChanged(index, index, {role});
    emit layoutChanged();
    persist();
    return true;
}

QHash<int, QByteArray> IslandLayoutModel::roleNames() const
{
    return {
        {IdRole, "faceId"}, {LabelRole, "label"}, {TitleRole, "title"},
        {NoteRole, "note"}, {EnabledRole, "enabled"}, {OrderRole, "orderNo"},
        {RequiresLiveRole, "requiresLive"}, {ScaleRole, "scale"},
    };
}

QString IslandLayoutModel::summary() const
{
    int on = 0;
    for (const Row &r : m_rows) if (r.enabled) ++on;
    return QStringLiteral("%1 个组件 · 已启用 %2").arg(m_rows.size()).arg(on);
}

bool IslandLayoutModel::anyEnabled() const
{
    for (const Row &r : m_rows) if (r.enabled) return true;
    return false;
}

QVariantList IslandLayoutModel::faceList() const
{
    QVariantList out;
    for (const Row &r : m_rows) {
        if (!r.enabled) continue;
        QVariantMap m;
        m.insert(QStringLiteral("id"), r.id);
        m.insert(QStringLiteral("label"), r.label);
        m.insert(QStringLiteral("scale"), r.scale);
        // 条件面（媒/自）的"此刻该不该出现"仍由 QML 侧判（它本来就有 mediaFaceOn /
        // automationFaceOn 两个现成判据）。C++ 只如实告知"这面是条件的"，
        // 不在这里读运行态 —— 否则岛每帧都要把状态推回模型，多一条必然漂移的链路。
        m.insert(QStringLiteral("requiresLive"), r.requiresLive);
        out.append(m);
    }
    // 一个都不剩时**不留空壳**：「概」是岛的语义底座（通知就画在它上面），
    // 全关也强制留一个概览 —— 否则老师关完最后一页会看到一块什么都不渲染的空白展开区。
    if (out.isEmpty()) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), 0);
        m.insert(QStringLiteral("label"), QStringLiteral("概"));
        m.insert(QStringLiteral("scale"), 100);
        m.insert(QStringLiteral("requiresLive"), false);
        out.append(m);
    }
    return out;
}

void IslandLayoutModel::moveRow(int from, int to)
{
    if (from < 0 || from >= m_rows.size()) return;
    if (to < 0 || to >= m_rows.size()) return;
    if (from == to) return;
    beginMoveRows(QModelIndex(), from, from, QModelIndex(),
                  to > from ? to + 1 : to);
    m_rows.move(from, to);
    endMoveRows();
    emit layoutChanged();
    persist();
}

void IslandLayoutModel::setEnabledById(int id, bool on)
{
    const int i = indexOfId(id);
    if (i < 0) return;
    setData(index(i, 0), on, EnabledRole);
}

void IslandLayoutModel::setScaleById(int id, qint64 scale)
{
    const int i = indexOfId(id);
    if (i < 0) return;
    setData(index(i, 0), int(scale), ScaleRole);
}

void IslandLayoutModel::resetToDefault()
{
    beginResetModel();
    m_rows = builtinRows();
    endResetModel();
    // 2026-11-03（m14970 ①）：恢复默认 = 显示方向也回到轮播（builtinRows 语义的一部分）。
    m_direction = QStringLiteral("carousel");
    QSettings s;
    s.remove(QStringLiteral("island/direction"));
    emit layoutChanged();
    QSettings().remove(QStringLiteral("island/layout"));   // 删键而不是写默认值：
    // 写默认值会把"从没配过"和"配成默认"混成一种状态，以后改默认序老机器跟不上。
}

IslandLayoutModel *islandLayoutInstance()
{
    // 函数内静态：首次调用即构造，进程内唯一。构造只读 QSettings，无依赖顺序问题
    //（岛壳可能在 main 之前/之后被创建，两个 engine 拿到的是同一个）。
    static IslandLayoutModel s_model;
    return &s_model;
}

void IslandLayoutModel::touch() { emit layoutChanged(); }
