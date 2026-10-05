#pragma once
#include <QAbstractListModel>
#include <QList>
#include <QTime>
#include "profile.h"

// 时间点列表模型：供 QML ListView 时间轴使用
class TimeSlotModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        StartTimeRole,
        EndTimeRole,
        DurationMinutesRole,
        TimeTypeRole,
        IsActiveRole,
        StartSecsRole,   // 从 00:00 起的秒数，供坐标计算
        EndSecsRole,
    };

    explicit TimeSlotModel(QObject* parent = nullptr);
    explicit TimeSlotModel(const QList<TimeSlot>& slots, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;

    // 获取/设置时间点
    TimeSlot getSlot(int row) const;
    bool setSlotTime(int row, QTime start, QTime end);
    bool setSlotName(int row, const QString& name);
    bool setSlotType(int row, int timeType);

    // 更新整个时间点列表
    void setSlots(const QList<TimeSlot>& newSlots);
    const QList<TimeSlot>& timeSlots() const { return m_slots; }

    // 吸附到 5 分钟
    static QTime snapTo5(QTime t);
    // 验证时间点不与相邻重叠
    static bool isValidRange(QTime start, QTime end);

private:
    QList<TimeSlot> m_slots;
    bool m_isUpdating = false;  // 防重入
};
