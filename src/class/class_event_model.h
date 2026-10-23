#pragma once
#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

// 班级 Timeline 事件模型（#460 · 2026-10-23 · 班级工作台右侧时间线）
//
// 云端班级域事件（class-events.js）统一信封：{classCode, id, at, kind, uid, data}。
// class.event / class.member.online / class.member.offline / 快照里的历史 events
// 全是这个形状 —— 一个模型全接，不按 kind 分模型（Timeline 本来就混排）。
//
// 两条纪律：
//   · id 水位去重：云端 id 是全局单调序列（evSeq）。重连后快照（最近 20 条）与
//     实时流可能重叠，id ≤ 已见最大值的一律丢，否则 Timeline 出现重复条目。
//   · 容量与云端环形缓冲同阶（200）：本地再大也没有更多历史可显示。
class ClassEventModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        AtRole,       ///< ISO 8601 原文（QML 侧自行格式化）
        KindRole,
        UidRole,
        DataTextRole, ///< data 的紧凑 JSON 原文（详情展开用）
        IsMemberRole, ///< kind 以 "member." 开头（上下线事件，UI 可专用图标）
        DetailRole,   ///< data.reason / data.message 提炼（离线原因等；无则空）
    };

    explicit ClassEventModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    /** 快照历史（云端 recentEvents：旧→新）。内部翻成新→头，并抬 id 水位。 */
    void resetFromArray(const QJsonArray &events);
    /** 实时事件头插；id ≤ 水位（重复/乱序旧事件）返回 false 丢弃。 */
    bool prepend(const QJsonObject &ev);

    Q_INVOKABLE int count() const { return m_events.size(); }

private:
    struct Event {
        qint64 id = 0;
        QString at;
        QString kind;
        QString uid;
        QString dataText;
        bool isMember = false;
        QString detail;
    };
    QList<Event> m_events;   // 新在头
    qint64 m_maxId = 0;
    static constexpr int kMaxEvents = 200;   // 与云端 CLASS_EVENT_LOG_KEEP 同容
};
