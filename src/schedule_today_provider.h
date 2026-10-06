#pragma once
#include <QObject>
#include <QJsonArray>
#include <QTimer>
#include "schedule/profile.h"

// 今日课表数据提供者：读 ClassIsland 档案，暴露「今日课程列表」给 QML。
// QML 侧用 scheduleToday.rows（JSON 数组）渲染概览页的今日课表卡。
// 30 秒刷新；当前节/已过标记由 QML 按时间渲染（rows 里带 startTime/endTime 字符串）。
class ScheduleTodayProvider : public QObject {
    Q_OBJECT
    Q_PROPERTY(QJsonArray rows READ rows NOTIFY rowsChanged)
public:
    explicit ScheduleTodayProvider(QObject* parent = nullptr);

    void setProfilePath(const QString& path);

    QJsonArray rows() const { return m_rows; }

    // QML 可调用：刷新
    Q_INVOKABLE void refresh();

signals:
    void rowsChanged();

private:
    void load();

    QString m_profilePath;
    QJsonArray m_rows;
    QTimer m_timer;
};
