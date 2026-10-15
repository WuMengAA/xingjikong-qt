#include "danmaku.h"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QScreen>
#include <QStyleHints>

// ── 祝福弹幕规则常量（2026-10-15 组件5 · 设计文档 §三 表第 5 行）──────
constexpr int kBlessingMaxChars      = 30;              // 单条 30 字截断（通知是 80）
constexpr int kBlessingSpeed         = 100;             // px/s（通知 120，祝福慢一档）
constexpr qint64 kBlessingCooldownMs = 30 * 1000;       // 30s 本机发送冷却

// ── DanmakuQueue（纯逻辑，可单测）────────────────────────────────

DanmakuQueue::DanmakuQueue(QObject* parent) : QObject(parent) {}

int DanmakuQueue::push(const QString& text)
{
    const int idx = int(m_items.size());
    m_items.enqueue(text);
    if (idx == 0) emit itemReady(text);   // 队首立即准备展示
    return idx;
}

QString DanmakuQueue::next()
{
    if (m_items.isEmpty()) return QString();
    const QString t = m_items.dequeue();
    if (!m_items.isEmpty()) emit itemReady(m_items.head());
    return t;
}

// ── DanmakuWidget（无边框置顶滚动窗）────────────────────────────

// 单例存储（2026-10-15 组件5）：两个访问器共享同一份指针。
// 文件作用域静态、不导出符号 —— 替代 main.cpp 的 g_danmaku 全局（那里曾有
// 三处引用：声明 / kind=danmaku 懒建 / push；现在入口收口到本模块）。
static DanmakuWidget *g_danmakuInstance = nullptr;

DanmakuWidget *DanmakuWidget::danmakuWidget()
{
    if (!g_danmakuInstance) g_danmakuInstance = new DanmakuWidget();
    return g_danmakuInstance;
}

DanmakuWidget *DanmakuWidget::danmakuWidgetPeek()
{
    return g_danmakuInstance;   // 只读不建：没弹幕过就还是 nullptr
}

DanmakuWidget::DanmakuWidget(QWidget* parent) : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    // 弹幕不挡点击：鼠标穿透（教室机大屏，了下课内容还要能点）
    setAttribute(Qt::WA_TransparentForMouseEvents);

    m_frame.setInterval(16);
    connect(&m_frame, &QTimer::timeout, this, &DanmakuWidget::advance);
    // 冷却 tick：1Hz，只在冷却期内跑（sendBlessingLocal 启动、归零自停）
    m_cooldownTick.setInterval(1000);
    connect(&m_cooldownTick, &QTimer::timeout, this, &DanmakuWidget::onCooldownTick);

    hide();
}

void DanmakuWidget::push(const QString& text, const QColor& fg, int speedPxPerSec)
{
    const QString t = text.length() > 80 ? text.left(80) + QStringLiteral("…") : text;
    DanmakuItem item;
    item.text = t;
    item.fg = fg;                                        // 无效 = paintEvent 兜底白
    item.speed = speedPxPerSec > 0 ? speedPxPerSec : 120;
    m_queue.enqueue(item);
    if (!m_active) startNext();
}

// ── 祝福弹幕（2026-10-15 组件5 · 学生不可信源）────────────────────

// 敏感词占位实现：
//   · 词表「广告 / 外挂 / 代刷」是**本机占位**，先挡明显垃圾信息；
//   · 真词表随组件6 走云端下发（可热更新、按班级策略），届时本函数改为查云端缓存；
//   · 大小写不敏感；命中即拦（返回码 1）但**不告诉用户具体哪个词**（防试探）。
bool DanmakuWidget::isSensitive(const QString& text)
{
    static const char *kWords[] = {"广告", "外挂", "代刷"};
    for (const char *w : kWords) {
        if (text.contains(QString::fromUtf8(w), Qt::CaseInsensitive)) return true;
    }
    return false;
}

int DanmakuWidget::pushBlessing(const QString& text)
{
    const QString t = text.trimmed();
    if (t.isEmpty()) return BlessingInvalid;
    if (isSensitive(t)) return BlessingSensitive;
    DanmakuItem item;
    item.text = t.length() > kBlessingMaxChars
                    ? t.left(kBlessingMaxChars) + QStringLiteral("…") : t;
    item.fg = QColor();               // 默认白（与管理端通知同底色，不分特殊色）
    item.speed = kBlessingSpeed;
    m_queue.enqueue(item);
    if (!m_active) startNext();
    return BlessingOk;
}

int DanmakuWidget::sendBlessingLocal(const QString& text)
{
    if (blessingCooldownRemaining() > 0) return BlessingCooldown;
    const int code = pushBlessing(text);
    if (code == BlessingOk) {
        m_lastBlessing = text.trimmed();
        m_lastLocalSave.start();                 // 冷却计时起点
        m_cooldownTick.start();                  // 1Hz 驱动倒计时广播
        emit blessingCooldownChanged(blessingCooldownRemaining());
    }
    return code;
}

int DanmakuWidget::blessingCooldownRemaining() const
{
    // 懒查 elapsed：tick 停了（归零自停）甚至从来没跑过，判定依然正确。
    // 向上取整：冷却刚过 0.1s 也显示"30"，倒计时读数 30→29→…→1 共 30 跳。
    if (!m_lastLocalSave.isValid()) return 0;
    const qint64 sinceMs = m_lastLocalSave.elapsed();
    if (sinceMs >= kBlessingCooldownMs) return 0;
    return int((kBlessingCooldownMs - sinceMs + 999) / 1000);
}

void DanmakuWidget::onCooldownTick()
{
    const int rem = blessingCooldownRemaining();
    emit blessingCooldownChanged(rem);
    if (rem <= 0) m_cooldownTick.stop();   // 归零自停：之后由懒查兜底
}

void DanmakuWidget::clearAll()
{
    m_queue.clear();
    m_cur = DanmakuItem{};
    m_active = false;
    m_frame.stop();
    hide();
}

void DanmakuWidget::startNext()
{
    if (m_queue.isEmpty()) { m_active = false; return; }
    m_cur = m_queue.dequeue();
    m_active = true;

    // 定位：屏幕上部三分一（不与顶部胶囊( y≈24, 高≈56 )重叠 → y 从 120 起）
    QScreen* screen = QApplication::primaryScreen();
    if (screen) {
        const QRect sg = screen->availableGeometry();
        const int h = 40;
        setGeometry(sg.x(), sg.y() + 120, sg.width(), h);
    } else {
        setGeometry(0, 120, 1024, 40);
    }

    QFont f = font();
    f.setPixelSize(22);
    setFont(f);
    const QFontMetrics fm(f);
    m_textW = fm.horizontalAdvance(m_cur.text);
    m_x = width();                       // 从右侧外进入
    m_clock.restart();

    show();
    raise();
    m_frame.start();
    update();
}

void DanmakuWidget::advance()
{
    if (!m_active) return;
    const qint64 elapsedMs = m_clock.isValid() ? qint64(m_clock.elapsed()) : 0;
    m_clock.restart();
    // 已走过的像素 = 速度 * 时间（速度逐条携带，见 DanmakuItem）
    const int dx = int(m_cur.speed * (elapsedMs / 1000.0));
    m_x -= dx;
    // 完全滚出左侧 → 本条结束
    if (m_x + m_textW < 0) { finishCurrent(); return; }
    update();
}

void DanmakuWidget::finishCurrent()
{
    m_frame.stop();
    m_cur = DanmakuItem{};
    m_active = false;
    hide();
    if (!m_queue.isEmpty()) startNext();
}

void DanmakuWidget::paintEvent(QPaintEvent*)
{
    if (m_cur.text.isEmpty()) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(m_cur.fg.isValid() ? m_cur.fg : QColor(255, 255, 255));
    p.setFont(font());
    p.drawText(m_x, 6, m_cur.text);
}
