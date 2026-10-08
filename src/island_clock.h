#pragma once
// 灵动岛时间/状态显示（2026-10-09 · 用户最初需求"灵动岛显示时间、状态"独立组件）
//
// 被控端岛（IslandOverlay）目前只显示标题+描述，没有时间。本组件提供
// HH:mm 时钟 + 一行小字状态（如"空闲"/"考试中"/"录屏中"），供岛嵌入。
//
// 设计（延续 marquee_text 的约定）：
//   · 不持有自己窗口（无 FLAGS、无大小策略强约束）——由岛按排布安排，
//     减少与单例岛的生命周期纠缠
//   · QTimer 每 500ms 刷新一次（分钟级足够；不逐秒重绘）
//   · setStatus() 显示状态行；空状态只画时间
//   · 浅/深色由调用方 setLight() 决定（跟随岛主题）
//   · 时间字体放大（"现在几点"是岛的信息密度优势），状态小字
//
// 接入（等 island.cpp 工作区解禁）：IslandOverlay 布局里 new IslandClock，
// 展开态大字号、收起态缩小或隐藏。

#include <QWidget>
#include <QString>
#include <QTimer>
#include <QColor>

class IslandClock : public QWidget
{
    Q_OBJECT
public:
    explicit IslandClock(QWidget *parent = nullptr);

    void setStatus(const QString &status);   // 状态小字（空 = 不显示状态行）
    QString status() const { return m_status; }

    void setLight(bool light);
    bool light() const { return m_light; }

    // 字号：展开态可调大（默认 22pt 时间 / 10pt 状态）
    void setTimeFontSize(int px) { m_timePx = qMax(8, px); update(); }

    QString currentTimeText() const;   // 供测试/外部读（HH:mm）

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void refresh();                     // 定时刷新时间

    QString m_status;
    bool m_light = false;
    int m_timePx = 22;
    QTimer m_tick;
};