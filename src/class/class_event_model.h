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
    // QML 绑定用：revision 任何增删都 +1。为什么不能只靠 count —— 快照重置
    // （resetFromArray）在满 200 时条数不变，只有 count 的话展板通知列表不会重算。
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        AtRole,       ///< ISO 8601 原文（QML 侧自行格式化）
        KindRole,
        UidRole,
        DataTextRole, ///< data 的紧凑 JSON 原文（详情展开用）
        IsMemberRole, ///< kind 以 "member." 开头（上下线事件，UI 可专用图标）
        DetailRole,   ///< data.reason / data.message / data.title 提炼（无则空）
        // ── publish 专用（2026-10-25）──
        // 云端 adminPublish 落的 class-event(kind=publish) 载荷是
        // {type,title,body,from}；此前只提炼 reason/message ⇒ 发布出去的通知在
        // Timeline 里是一行空条目（"发了什么"查不出来）。这四个字段补上原文。
        TitleRole,    ///< data.title（通知标题）
        BodyRole,     ///< data.body（通知正文）
        TypeRole,     ///< data.type（student-notice / class-notice / fullscreen-notice / homework）
        FromRole,     ///< data.from（发布人标识，publish 事件的 uid 与之相同）
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
    int revision() const { return m_revision; }

    /** 取某类事件的前 limit 条（新→旧），字段摊平成 map 供 QML 直接吃。
     *  用途：班级展板的「已发布内容」—— 从混排事件流里筛 kind=publish。 */
    Q_INVOKABLE QVariantList recentOfKind(const QString &kind, int limit) const;

signals:
    void countChanged();
    void revisionChanged();

private:
    struct Event {
        qint64 id = 0;
        QString at;
        QString kind;
        QString uid;
        QString dataText;
        bool isMember = false;
        QString detail;
        QString title;
        QString body;
        QString type;
        QString from;
    };
    QList<Event> m_events;   // 新在头
    qint64 m_maxId = 0;
    int m_revision = 0;
    static constexpr int kMaxEvents = 200;   // 与云端 CLASS_EVENT_LOG_KEEP 同容

    /** 云端信封 → 模型行。快照与实时流共用，防两处字段解析漂移。 */
    static void fillFromJson(Event &e, const QJsonObject &o);
};
