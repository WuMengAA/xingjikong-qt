#pragma once
// 弹幕组件（2026-10-08 · 通知体系升级 §1.2 / §6.1 独立模块）
//
// 独立 QObject + 一个无边框置顶窗口，不依赖 main.cpp 的任何全局：
//   · 字幕式横向滚动，出现在屏幕上部（设计与顶部灵动岛胶囊区分：弹幕层更低，
//     不与胶囊打架）
//   · 单条滚动展示 + 队列：同时最多一条在滚，其余排队（教室机不追求弹幕墙）
//   · 默认鼠标穿透（WA_TransparentForMouseEvents），不挡课件
//   · 内容 ≥80 字右侧省略（输入侧截断；滚动时保持整条）
//   · 播完即消失；滚动期间再来一条 → 进队列排队
//
// 接入（main.cpp）：connect 到弹幕和别的信号即可，本组件不碰通知状态机。

#include <QObject>
#include <QWidget>
#include <QTimer>
#include <QString>
#include <QQueue>
#include <QElapsedTimer>

class QPainter;

class DanmakuWidget : public QWidget {
    Q_OBJECT
public:
    explicit DanmakuWidget(QWidget* parent = nullptr);

    // 入队一条弹幕；正在滚动的先滚完，新的排队
    void push(const QString& text, const QColor& fg = QColor(), int speedPxPerSec = 120);

    // 立即清空（队列 + 正在滚的），窗口隐藏
    void clearAll();

    // 队列里还有几条（诊断/测试可读）
    int pendingCount() const { return int(m_queue.size()); }
    bool isScrolling() const { return m_active; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void startNext();
    void advance();          // 每帧推进 x
    void finishCurrent();    // 滚完，隐藏并取下一条

    QQueue<QString> m_queue;
    QString m_cur;
    QTimer m_frame;          // 滚动 tick（默认 16ms ≈ 60fps）
    QElapsedTimer m_clock;
    int m_x = 0;             // 当前文本右缘 x（从右向左）
    int m_textW = 0;
    bool m_active = false;
    QColor m_fg;
    int m_speed = 120;       // px/s
};

// 逻辑壳：与 UI 分离的队列管理（可单测，不依赖窗口）。
class DanmakuQueue : public QObject {
    Q_OBJECT
public:
    explicit DanmakuQueue(QObject* parent = nullptr);

    // 入队；返回这条在队里的序号（0 = 立即展示）
    int push(const QString& text);
    // 展示完一条（由 DanmakuWidget 回调或测试调用）
    QString next();

    int pending() const { return int(m_items.size()); }
    void clear() { m_items.clear(); }

signals:
    void itemReady(const QString& text);   // 轮到某条时发出

private:
    QQueue<QString> m_items;
};