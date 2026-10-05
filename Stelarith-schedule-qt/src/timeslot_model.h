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
    // QML role 名映射
    QHash<int, QByteArray> roleNames() const override {
        QHash<int, QByteArray> roles;
        roles[IdRole] = "id";
        roles[NameRole] = "name";
        roles[StartTimeRole] = "startTime";
        roles[EndTimeRole] = "endTime";
        roles[DurationMinutesRole] = "durationMinutes";
        roles[TimeTypeRole] = "timeType";
        roles[IsActiveRole] = "isActive";
        roles[StartSecsRole] = "startSecs";
        roles[EndSecsRole] = "endSecs";
        return roles;
    }

    // 获取/设置时间点
    TimeSlot getSlot(int row) const;
    bool setSlotTime(int row, QTime start, QTime end);
    bool setSlotName(int row, const QString& name);
    bool setSlotType(int row, int timeType);

    // 更新整个时间点列表
    void setSlots(const QList<TimeSlot>& newSlots);
    const QList<TimeSlot>& timeSlots() const { return m_slots; }

    // ---- QML 可调用 ----
    Q_INVOKABLE QString startTimeAt(int row) const {
        return (row >= 0 && row < m_slots.size()) ? m_slots[row].startTime.toString("HH:mm") : QString();
    }
    Q_INVOKABLE int timeTypeAt(int row) const {
        return (row >= 0 && row < m_slots.size()) ? m_slots[row].timeType : 0;
    }
    Q_INVOKABLE QString endTimeAt(int row) const {
        return (row >= 0 && row < m_slots.size()) ? m_slots[row].endTime.toString("HH:mm") : QString();
    }
    Q_INVOKABLE QString nameAt(int row) const {
        return (row >= 0 && row < m_slots.size()) ? m_slots[row].name : QString();
    }
    Q_INVOKABLE int durationMinAt(int row) const {
        return (row >= 0 && row < m_slots.size()) ? m_slots[row].durationMinutes() : 0;
    }
    Q_INVOKABLE int startMinAt(int row) const {
        if (row < 0 || row >= m_slots.size()) return 0;
        const QTime& t = m_slots[row].startTime;
        return t.hour() * 60 + t.minute();
    }
    Q_INVOKABLE int endMinAt(int row) const {
        if (row < 0 || row >= m_slots.size()) return 0;
        const QTime& t = m_slots[row].endTime;
        return t.hour() * 60 + t.minute();
    }
    // 设置时间段（分钟自午夜；吸附到 5 分钟；返回是否成功）
    Q_INVOKABLE bool setSlotByMinutes(int row, int startMin, int endMin) {
        if (row < 0 || row >= m_slots.size()) return false;
        QTime start = QTime(0, 0).addSecs(startMin * 60);
        QTime end = QTime(0, 0).addSecs(endMin * 60);
        return setSlotTime(row, start, end);
    }

    // 吸附到 5 分钟
    static QTime snapTo5(QTime t);
    // 验证时间点不与相邻重叠
    static bool isValidRange(QTime start, QTime end);

private:
    QList<TimeSlot> m_slots;
    bool m_isUpdating = false;  // 防重入
};
