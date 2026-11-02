#pragma once
#include <QAbstractListModel>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QTimer>

// 今日课表模型（2026-11-02 · 替换 QWidget 手绘大屏窗口）
//
// ── 为什么新建它 ──────────────────────────────────────────────────────────
// 原 `src/schedule_view.{h,cpp}` 是 `class ScheduleViewWindow : public QWidget`，
// 在 `paintEvent()` 里用 QPainter::fillRect/drawText 手绘整屏（标题 + 列表 + 当前节高亮），
// 色值硬编码 #0A0A0A/#FAFAFA/#1E1E1E、字号靠 `qMax(22, h/28)` 拍。
// 那是 CPU 光栅化整屏，且完全绕开设计令牌 —— 用户 2026-11-01 裁决明令禁止
// （「qtshadcn 作为唯一图形库，不允许使用 qqwiget 等 cpu 渲染」）。
//
// 同时旧的 `ScheduleTodayProvider` 用 `QJsonArray rows` 直喂 QML，
// 违反 AGENTS.md §10.7（列表数据必须 QAbstractListModel + roleNames）。
// 两者合流成这一个类：**数据这条路只有一条**。
//
// ── 定位（分层，照 §10 口径）──
// 本类是**数据层**：只算数、只存行，不认识 QML，不碰任何绘制。
// 绘制全在 `qml/panels/TodayScheduleWindow.qml`（QtShadcn 组件）。
// 所以它可以离屏单测（`scripts/_today_schedule_probe.py` 用 QTest 式驱动）。
//
// ── 为什么要 computed 这些字段 ───────────────────────────────────────────
// 教室大屏上学生真正关心的是「现在上什么 / 还有几分钟下课 / 下一节是什么」，
// 而不是一排静态时间。所以模型直接算好：
//   · progress    当前节的进行比例 0..1（给进度条用，QML 不自己做减法）
//   · remainMin   当前节还剩几分钟；不在上课时为 -1
//   · statusText  一句话状态（给大屏顶部），文案在 C++ 拼好，QML 不做字符串判断
// 「计算在 C++、QML 只画」是 §10.9 的原文要求。
//
// ── 刷新节奏 ────────────────────────────────────────────────────────────
// 分两个定时器，别合并：
//   · 课表数据 30s 一次（档案文件可能被编辑器改动）
//   · 时钟     1s  一次（大屏上的秒针和倒计时要走字，30s 一跳是坏的）
// 时钟跳只改 `nowText/statusText` 这两个标量 + 当期行的 progress，
// 不重建整个列表 —— 否则 ListView 每秒重排一次，滚动位置和动画全乱。

class TodayScheduleModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY rowsChanged)
    Q_PROPERTY(QString nowText READ nowText NOTIFY tickChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY tickChanged)
    Q_PROPERTY(QString dateText READ dateText NOTIFY rowsChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY rowsChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY tickChanged)
    Q_PROPERTY(int remainMin READ remainMin NOTIFY tickChanged)
    Q_PROPERTY(QString profilePath READ profilePath WRITE setProfilePath NOTIFY rowsChanged)

public:
    enum Role {
        SubjectRole = Qt::UserRole + 1,
        TimeTextRole,
        StartMinRole,
        EndMinRole,
        IsNowRole,
        IsPastRole,
        ProgressRole,
        SlotNameRole,   // 只含节次名（"第一节"），不带科目 —— 大屏要分两行排版
        SubjectOnlyRole // 只含科目名（"语文"）
    };
    Q_ENUM(Role)

    explicit TodayScheduleModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_rows.size(); }
    bool empty() const { return m_rows.isEmpty(); }
    QString nowText() const { return m_nowText; }
    QString statusText() const;
    QString dateText() const { return m_dateText; }
    int currentIndex() const { return m_currentIndex; }
    int remainMin() const { return m_remainMin; }
    QString profilePath() const { return m_profilePath; }

    void setProfilePath(const QString &path);

    /** QML 可调用：立刻重读档案（编辑器保存后主动刷新用）。 */
    Q_INVOKABLE void reload();

signals:
    void rowsChanged();
    /** 每秒一次：只影响 nowText / statusText / 当前行的 progress。 */
    void tickChanged();
    /** 当前节变了（上一节下课、下一节上课）——大屏可以放一次强调动效。 */
    void currentChanged();

private:
    struct Row {
        QString subject;      // "语文"
        QString slotName;     // "第一节"
        QString timeText;     // "08:00 - 08:45"
        int startMin = 0;
        int endMin = 0;
        qreal progress = -1.0; // -1 = 不在上课
        bool isPast = false;
    };

    void load();
    void tick();
    void recomputeLive();
    void rebuildRows();

    QString m_profilePath;
    QList<Row> m_rows;
    QString m_nowText;
    QString m_dateText;
    int m_currentIndex = -1;
    int m_remainMin = -1;
    QTimer m_dataTimer;
    QTimer m_clockTimer;
};
