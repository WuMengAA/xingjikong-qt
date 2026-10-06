#pragma once
#include <QWidget>
#include <QList>
#include <QTimer>
#include "schedule/profile.h"

// 大屏课表窗口：显示今日全天课程（节次/科目/时间），当前上课节高亮。
// 独立 QWidget，窗口可全屏（教室大屏）或普通大小；每 30 秒刷新当前节高亮。
class ScheduleViewWindow : public QWidget {
    Q_OBJECT
public:
    explicit ScheduleViewWindow(QWidget* parent = nullptr);

    // 设置档案路径并刷新（空路径/无课表时显示占位）
    void setProfilePath(const QString& path);
    // 切换全屏/窗口模式
    void toggleFullscreen();

protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent*) override;   // Esc 退出全屏

private:
    struct Row {
        QString timeText;    // "08:00 - 08:45"
        QString subject;     // "第一节 · 语文"
        bool isNow = false;  // 当前正在上
        bool isPast = false; // 已结束
    };

    void refresh();
    QList<Row> buildRows(const Profile& profile, const QDate& today, int currentWeek);

    QString m_profilePath;
    QList<Row> m_rows;
    QTimer m_timer;
    bool m_fullscreen = false;
};
