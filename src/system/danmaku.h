#pragma once
// 弹幕组件（2026-10-08 · 通知体系升级 §1.2 / §6.1 独立模块）
//
// 独立 QObject + 一个无边框置顶窗口，不依赖 main.cpp 的任何全局：
//   · 字幕式横向滚动，出现在屏幕上部（设计与顶部灵动岛胶囊区分：弹幕层更低，
//     不与胶囊打架）
//   · 单条滚动展示 + 队列：同时最多一条在滚，其余排队（教室机不追求弹幕墙）
//   · 默认鼠标穿透（WA_TransparentForMouseEvents），不挡课件
//   · 播完即消失；滚动期间再来一条 → 进队列排队
//
// 接入（main.cpp）：connect 到弹幕和别的信号即可，本组件不碰通知状态机。
//
// ── 2026-10-15 组件5（祝福弹幕）：双来源同窗不同则 ───────────────────
// 弹幕层现在有两个入口，**规则不同所以入口不同**（不做成一个 push 加 flag，
// 防止调用方把"学生不可信源"传进管理端可信路径）：
//   · push()            管理端通知弹幕（可信源）：80 字截断 / 120px/s / 颜色可指定 /
//                       无不过滤无冷却，内容原样上屏（老师发的，拦它干嘛）。
//   · sendBlessingLocal() 学生祝福弹幕（不可信源）：30 字截断 / 100px/s / 敏感词过滤 /
//                       30s 本机发送冷却（端侧先行，云端投稿审核端点留 TODO 组件6）。
// 两者的颜色/速度**逐条携带**（DanmakuItem）：旧实现把 fg/speed 存成"最后一条 push
// 的全局值"，双来源下会互污（祝福 100 会把排队中等候的通知改成 100）。

#include <QObject>
#include <QWidget>
#include <QTimer>
#include <QString>
#include <QQueue>
#include <QElapsedTimer>

class QPainter;

// 一条弹幕的完整自包含数据（2026-10-15 组件5）：颜色/速度逐条携带，见上。
struct DanmakuItem {
    QString text;
    QColor  fg;           // 无效 = 白（paintEvent 兜底）
    int     speed = 120;  // px/s
};

class DanmakuWidget : public QWidget {
    Q_OBJECT
public:
    explicit DanmakuWidget(QWidget* parent = nullptr);

    // ── 单例（2026-10-15 组件5 · 替代 main.cpp 的 g_danmaku 全局指针）────
    // 被控端全局唯一弹幕层：danmakuWidget() 首次调用懒建（无 parent、不 delete，
    // 进程级生命周期，与既有 g_audioPlayer 等全局同口径）；
    // danmakuWidgetPeek() 只读不建 —— 诊断/探针/岛读冷却时不该为了问一句话造窗口。
    static DanmakuWidget *danmakuWidget();
    static DanmakuWidget *danmakuWidgetPeek();

    // 入队一条弹幕（管理端可信源，§1.2 原路径）；正在滚的先滚完，新的排队。
    // 内容 ≥80 字右侧省略；颜色/速度逐条携带（见 DanmakuItem）。
    void push(const QString& text, const QColor& fg = QColor(), int speedPxPerSec = 120);

    // ── 祝福弹幕（2026-10-15 组件5 · 学生不可信源）──────────────────
    // sendBlessingLocal：完整链路（冷却闸门 → 内容校验 → 入队 + 开冷却）。
    // 返回结果码（BlessingResult）：QML/岛按码落成功/失败态文案。
    enum BlessingResult {
        BlessingOk        = 0,  // 已入队
        BlessingSensitive = 1,  // 敏感词被拦
        BlessingCooldown  = 2,  // 冷却中（30s）
        BlessingInvalid   = 3,  // 内容无效（空/纯空白）
    };
    int sendBlessingLocal(const QString& text);
    // 冷却剩余秒数（0 = 可发）。懒查 elapsed：没有 tick 在跑也正确（见 .cpp）。
    int blessingCooldownRemaining() const;
    // 最近一条被接受的祝福原文（诊断/探针用；空 = 本会话还没发过）
    QString lastBlessing() const { return m_lastBlessing; }

    // 立即清空（队列 + 正在滚的），窗口隐藏
    void clearAll();

    // 队列里还有几条（诊断/测试可读）
    int pendingCount() const { return int(m_queue.size()); }
    bool isScrolling() const { return m_active; }

protected:
    void paintEvent(QPaintEvent*) override;

signals:
    // 冷却倒计时变化（1Hz，m_cooldownTick 驱动；IslandOverlay → bridge → QML 倒计时）。
    // 归零时也会发一次 0，随后 tick 自停。
    void blessingCooldownChanged(int remainingSec);

private:
    // 过滤 + 截断 + 入队（**不含冷却**；冷却闸门在 sendBlessingLocal）。
    // 返回 0/1/3（2 只由 sendBlessingLocal 产生）。云端回显路径（组件6）也走它。
    int pushBlessing(const QString& text);
    // 敏感词占位词表（组件6 换云端下发，见 .cpp 注释）
    static bool isSensitive(const QString& text);

    void startNext();
    void advance();          // 每帧推进 x
    void finishCurrent();    // 滚完，隐藏并取下一条
    void onCooldownTick();   // 1Hz：广播剩余秒数，归零自停

    QQueue<DanmakuItem> m_queue;
    DanmakuItem m_cur;
    QTimer m_frame;          // 滚动 tick（默认 16ms ≈ 60fps）
    QElapsedTimer m_clock;
    int m_x = 0;             // 当前文本右缘 x（从右向左）
    int m_textW = 0;
    bool m_active = false;

    // ── 祝福冷却（2026-10-15 组件5）────────────────────────────────
    QElapsedTimer m_lastLocalSave;   // 上次本机祝福被接受的时刻（懒查 elapsed 闸门）
    QString m_lastBlessing;          // 最近一条被接受的祝福原文
    QTimer m_cooldownTick;           // 1Hz，驱动 blessingCooldownChanged（归零自停）
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
