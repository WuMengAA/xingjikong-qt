#pragma once
// 灵动岛滚动文本条（2026-10-09 · 通知体系 §6.1 独立组件）
//
// 需求（用户 2026-10-08）：灵动岛**展开后标题+正文滚动式展示，持续直到人为点击**。
// 现有 IslandOverlay 的展开态是静态两行（elide 截断），长文本看不全。
// 本组件是独立 QWidget：一段文本横向滚动（marquee），供灵动岛展开态嵌入。
//
// 设计：
//   · 纯展示件：不持有自己的窗口（无 FLAGS），由父级（IslandOverlay）按排布安排
//     —— 减少与单例岛的生命周期纠缠
//   · reserveHeight 给 height 时自动纵向居中；宽度自适应（文本长就占满）
//   · 循环滚动：文本右缘滚出左缘后从右侧重新进场（无停顿）
//   · 持续**直到人为点击**：不自动停；父级点一下 → setPaused(true) 停住（或收起岛）
//   · 速度可调（默认 100px/s）；空文本直接不画
//   · 浅/深色由调用方 setLight(bool) 决定（跟随岛的主题）
//
// 接入（等 island.cpp 工作区解禁）：
//   IslandOverlay 展开态绘制处 new MarqueeText(this) 或把本组件并入 island.cpp 的
//   paintEvent 区域。本文件保持独立，不动 island.cpp。

#include <QWidget>
#include <QString>
#include <QTimer>
#include <QElapsedTimer>
#include <QColor>

class MarqueeText : public QWidget
{
    Q_OBJECT
public:
    explicit MarqueeText(QWidget *parent = nullptr);

    void setText(const QString &text);
    QString text() const { return m_text; }

    // 滚动开关：pause=true 停止（人为点击后）；false 继续滚
    void setPaused(bool on);
    bool paused() const { return m_paused; }

    void setLight(bool light);          // true=浅色
    void setSpeedPxPerSec(int px) { m_speed = qMax(10, px); }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void tick();                        // 每帧推进 x

    QString m_text;
    bool m_paused = false;
    bool m_light = false;
    int m_speed = 100;                  // px/s
    int m_x = 0;                        // 文本左缘 x
    int m_textW = 0;
    QTimer m_frame;                     // 60fps tick
    QElapsedTimer m_clock;
};