// 星集控被控端 · 灵动岛桥（历史通知域）
// 2026-11-02 从 island_bridge.cpp 按职能切出（600 行红线拆分，搬家不改逻辑）。
// 排序/筛选/已读/删除都在这里；★history() 与 showHistoryItem() 必须共用
// historyOrder()，否则「看到的第 N 条」≠「点开的第 N 条」（2026-10-13/10-17 两次踩坑）。
#include "island_bridge.h"
#include "island.h"

#include <algorithm>
#include <QDateTime>

// ── 历史通知（2026-10-11 · 持久化；2026-10-17 记录与逻辑升级）────────
// 从 IslandOverlay 的 m_history 取，桥不缓存。
//
// 2026-10-17：加了分类/关键词两个筛子。**筛在排序之前**，且 history() 与
// showHistoryItem() 都走这一个函数 —— 这是关键：上一轮（10-13）已经踩过
// 「管理端置顶后 m_history 下标 ≠ 显示序」，如果这次再在 history() 里单独过滤、
// 而 showHistoryItem 用未过滤的 order，就会变成"点第 3 条打开第 5 条"。
QVector<int> IslandBridge::historyOrder() const
{
    QVector<int> order;
    if (!m_host) return order;
    const int n = m_host->m_history.size();
    order.reserve(n);

    // 「全部」/空 = 不限分类；关键词为空 = 不搜
    const QString cat = (m_histCategory == QStringLiteral("全部")) ? QString() : m_histCategory;
    const QString kw  = m_histKeyword.trimmed();

    auto pass = [&](const IslandOverlay::HistoryItem &h) {
        if (!cat.isEmpty() && h.category != cat) return false;
        if (!kw.isEmpty()) {
            // 标题 + 描述 都搜（老师通常记得一句内容而不是标题）
            if (!h.title.contains(kw, Qt::CaseInsensitive)
                && !h.desc.contains(kw, Qt::CaseInsensitive)) return false;
        }
        return true;
    };

    for (int i = 0; i < n; ++i)
        if (pass(m_host->m_history.at(i)))
            order.append(i);

    // 2026-10-31：分组顺序修正。原实现先放管理端再放本机（追加序=旧→新），
    //   ListView section 按**模型序**展示分组（本 Qt 版本不按属性值重排）⇒ 旧日期组顶到上面
    //   （实测：历史同时有 10-27 与今天条目时，面板顶部先见 "10月27日"，用户以为日期错）。
    //   改为按「日期降序（今天在前）→ 同日管理端先 → 同日时间降序（新在前）」排序。
    //   管理端"置顶"语义保留在**同一天内**（跨天仍按日期走，否则昨天管理端会压过今天本机）。
    auto dayStart = [](qint64 ts) -> qint64 {
        if (ts <= 0) return 0;   // "更早" 归 0，排在所有真实日期之后
        return QDateTime::fromSecsSinceEpoch(ts).date().startOfDay().toSecsSinceEpoch();
    };
    std::sort(order.begin(), order.end(), [this, &dayStart](int a, int b) {
        const auto &ha = m_host->m_history.at(a);
        const auto &hb = m_host->m_history.at(b);
        const qint64 da = dayStart(ha.ts), db = dayStart(hb.ts);
        if (da != db) return da > db;                          // 日期降序：今天在前
        if (ha.fromAdmin != hb.fromAdmin) return ha.fromAdmin; // 同日管理端先
        return ha.ts > hb.ts;                                  // 同日新在前
    });
    return order;
}
QVariantList IslandBridge::history() const
{
    QVariantList out;
    if (!m_host) return out;
    const QVector<int> order = historyOrder();
    for (int idx : order) {
        const auto &h = m_host->m_history.at(idx);
        QVariantMap m;
        m.insert(QStringLiteral("id"), h.id);
        m.insert(QStringLiteral("title"), h.title);
        m.insert(QStringLiteral("desc"), h.desc);
        m.insert(QStringLiteral("icon"), h.icon);
        m.insert(QStringLiteral("ts"), h.ts);
        // ⚠️ 2026-10-17：时间文本**在这里现算**，不用 JSON 里存的值。
        //    存死值隔天就错（昨天归档时算的「今天」今天是错的、"23:50" 也永远
        //    补不上日期前缀），而 list 只有 50 条、每次 changed 重算一次开销可忽略。
        m.insert(QStringLiteral("timeStr"), IslandOverlay::historyTimeOf(h.ts));
        m.insert(QStringLiteral("timeFull"), IslandOverlay::historyTimeFullOf(h.ts));
        m.insert(QStringLiteral("dateGroup"), IslandOverlay::historyDateGroupOf(h.ts));   // ListView section 用
        m.insert(QStringLiteral("category"), h.category);     // 指令/告警/消息/系统
        m.insert(QStringLiteral("level"), h.level);           // info/success/warning/error
        m.insert(QStringLiteral("read"), h.read);             // 未读点
        m.insert(QStringLiteral("fromAdmin"), h.fromAdmin);   // QML 可据此加"管理端"标记
        out.append(m);
    }
    return out;
}
int IslandBridge::historyUnreadCount() const
{
    return m_host ? m_host->historyUnreadCount() : 0;
}
void IslandBridge::setHistoryCategoryFilter(const QString &c)
{
    if (m_histCategory == c) return;
    m_histCategory = c;
    emit changed();
}
void IslandBridge::setHistoryKeyword(const QString &k)
{
    if (m_histKeyword == k) return;
    m_histKeyword = k;
    emit changed();
}
void IslandBridge::showHistoryItem(int index)
{
    if (!m_host) return;
    // ⚠️ index 是**显示序**（historyOrder）里的位置，**不是** m_history 的下标 ——
    //    两列表在"管理端置顶"后不再相同，直接拿 index 索引 m_history 会点开错的条。
    //    2026-10-17：加了分类/关键词过滤后，显示序还会被筛子缩短，所以必须走同一个
    //    historyOrder()（它已经内含过滤），这里一行都不用改 —— 这也是当初把
    //    排序收口成一个函数的好处。
    const QVector<int> order = historyOrder();
    if (index < 0 || index >= order.size()) return;
    const auto &h = m_host->m_history.at(order.at(index));
    // 打开即已读（设计稿 §5.4「跳转后自动标记已读」）
    if (!h.read) {
        m_host->markHistoryRead(h.id);
        emit changed();
    }
    // 把历史项恢复为岛的通知（纯展示，不自动关）；来源标记一并带回去（再归档时不丢标）
    m_host->showIsland(h.title, h.desc, h.icon, 0, IslandOverlay::Form::Capsule, h.fromAdmin);
}
void IslandBridge::showHistoryById(const QString &id)
{
    if (!m_host || id.isEmpty()) return;
    // 用 id 定位：不受过滤/重排影响（QML 列表滚动或筛选变化后 index 会漂）
    for (const auto &h : m_host->m_history) {
        if (h.id != id) continue;
        if (!h.read) {
            m_host->markHistoryRead(h.id);
            emit changed();
        }
        m_host->showIsland(h.title, h.desc, h.icon, 0, IslandOverlay::Form::Capsule, h.fromAdmin);
        return;
    }
    qWarning("[island] showHistoryById：找不到 id=%s（可能已被 7 天清理）", qPrintable(id));
}
void IslandBridge::markHistoryRead(const QString &id)
{
    if (!m_host) return;
    if (m_host->markHistoryRead(id)) emit changed();
}
void IslandBridge::markAllHistoryRead()
{
    if (!m_host) return;
    if (m_host->markAllHistoryRead() > 0) emit changed();
}
void IslandBridge::removeHistory(const QString &id)
{
    if (!m_host) return;
    if (m_host->removeHistory(id)) emit changed();
}
void IslandBridge::clearHistory()
{
    if (!m_host) return;
    m_host->m_history.clear();
    m_host->saveHistory();
    sync();
}
