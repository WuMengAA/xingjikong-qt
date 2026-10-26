#include "annotate_domain.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPainter>
#include <QQmlContext>
#include <QQuickView>
#include <QScreen>
#include <QTimer>
#include <QUrl>
#include <QtQml>

#include <windows.h>   // SetWindowLongPtr / GWL_EXSTYLE / WS_EX_* / 低层键盘钩子

namespace AnnotateDomain {

/* ══════════════════════════════════════════════════════════════
 *  置顶层级：UAC 之上不谈，任务管理器之下
 *  ── Windows 没有"设置绝对层级"的 API，实际只有三档 ──
 *    普通窗口 < 本层（WS_EX_TOPMOST） < 任务管理器 < UAC 安全桌面
 *  UAC 提示跑在独立安全桌面（Winlogon），任何 topmost 都压不过，
 *  且**不能尝试**（SetThreadDesktop / 抢安全桌面那一套是绝对红线）。
 *  所以这里只做两件事：置 topmost；若任务管理器正置顶可见，则插到它下面。
 * ══════════════════════════════════════════════════════════════ */
static BOOL CALLBACK findTaskMgrProc(HWND h, LPARAM lp)
{
    auto *out = reinterpret_cast<HWND *>(lp);
    if (!IsWindowVisible(h)) return TRUE;

    wchar_t cls[64] = {0};
    if (GetClassNameW(h, cls, 64) && wcscmp(cls, L"TaskManagerWindow") == 0) {
        *out = h;
        return FALSE;
    }
    // 兜底按标题认：任务管理器各版本/各语言的类名不完全一致
    wchar_t title[64] = {0};
    if (GetWindowTextW(h, title, 64)
        && (wcsstr(title, L"任务管理器") || wcsstr(title, L"Task Manager"))) {
        *out = h;
        return FALSE;
    }
    return TRUE;
}

static HWND findTaskManagerWindow()
{
    HWND found = nullptr;
    EnumWindows(findTaskMgrProc, reinterpret_cast<LPARAM>(&found));
    return found;
}

/* ══════════════════════════════════════════════════════════════
 *  低层键盘钩子 —— 窗口不接受焦点 ⇒ QML 收不到按键 ⇒ 逃生口放这里
 *  只吃五个组合（ESC / Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z / Ctrl+E），其余原样放行。
 *  ⚠️ 回调必须极快返回：超过 LowLevelHooksTimeout（默认 300ms）系统会静默摘钩。
 *     所以这里只做 QMetaObject 队列投递，不画、不写盘、不等待。
 * ══════════════════════════════════════════════════════════════ */
static Overlay *g_kbOwner = nullptr;

static LRESULT CALLBACK annotateKeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code != HC_ACTION || !g_kbOwner)
        return CallNextHookEx(nullptr, code, wParam, lParam);
    if (wParam != WM_KEYDOWN && wParam != WM_SYSKEYDOWN)
        return CallNextHookEx(nullptr, code, wParam, lParam);

    const auto *kb = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam);
    const bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    int action = 0;
    if (kb->vkCode == VK_ESCAPE)             action = 1;
    else if (ctrl && kb->vkCode == 'Z')      action = shift ? 3 : 2;
    else if (ctrl && kb->vkCode == 'Y')      action = 3;
    else if (ctrl && kb->vkCode == 'E')      action = 4;
    if (!action)
        return CallNextHookEx(nullptr, code, wParam, lParam);

    // 吃掉：批注层在时按 ESC 只该收批注层，不该顺手把 PPT 放映也关掉
    QMetaObject::invokeMethod(g_kbOwner, [action] {
        if (g_kbOwner) g_kbOwner->handleHotkey(action);
    }, Qt::QueuedConnection);
    return 1;
}

/* ══════════════════════════════════════════════════════════════
 *  Canvas —— QPainter 画进 QtQuick 场景图（不是 QWidget）
 * ══════════════════════════════════════════════════════════════ */

Canvas::Canvas(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setOpaquePainting(false);     // 透明底：批注模式要能看见底下的课件
    setFlag(QQuickItem::ItemHasContents, true);
}

QPainterPath Canvas::pathOf(const Stroke &s) const
{
    QPainterPath path;
    if (s.pts.isEmpty()) return path;
    path.moveTo(s.pts.first());
    if (s.pts.size() == 1) {
        // 单点 = 一个圆点（Qt 的 drawPath 对单点路径画不出东西）
        path.lineTo(s.pts.first() + QPointF(0.01, 0.01));
        return path;
    }
    // 二次贝塞尔平滑：取相邻点中点作控制点，比折线顺眼很多（ICC CE 的 auto_fix 同理）
    for (int i = 1; i < s.pts.size() - 1; ++i) {
        const QPointF mid = (s.pts.at(i) + s.pts.at(i + 1)) / 2.0;
        path.quadTo(s.pts.at(i), mid);
    }
    path.lineTo(s.pts.last());
    return path;
}

void Canvas::paint(QPainter *p)
{
    p->setRenderHint(QPainter::Antialiasing, true);

    // 白板背景（批注模式不填，透出底下的课件）
    if (m_boardColor.isValid())
        p->fillRect(QRectF(0, 0, width(), height()), m_boardColor);

    auto drawOne = [&](const Stroke &s) {
        if (s.pts.isEmpty() || s.eraser) return;   // 橡皮轨迹不落墨
        p->save();
        QPen pen(s.color);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        if (s.highlighter) {
            // 荧光笔：加宽 + 半透明（不用 Multiply 合成，避免在深色课件上发黑）
            QColor c = s.color;
            c.setAlpha(80);
            pen.setColor(c);
            pen.setWidthF(s.width * 4.0);
        } else {
            pen.setWidthF(s.width);
        }
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        p->drawPath(pathOf(s));
        p->restore();
    };

    for (const Stroke &s : m_strokes) drawOne(s);
    if (m_drawing) drawOne(m_cur);   // 正在画的一笔实时可见
}

void Canvas::beginStroke(qreal x, qreal y)
{
    pushHistory();
    m_cur = Stroke();
    m_cur.color = m_color;
    m_cur.width = m_width;
    m_cur.highlighter = (m_tool == QLatin1String("highlighter"));
    m_cur.eraser = (m_tool == QLatin1String("eraser"));
    m_cur.pts.append(QPointF(x, y));
    m_drawing = true;
    update();
}

void Canvas::addPoint(qreal x, qreal y)
{
    if (!m_drawing) return;
    const QPointF pt(x, y);
    // 抽稀：与上一点距离 < 1.2px 就不记（高速书写时点数暴涨会让 paint 变慢）
    if (!m_cur.pts.isEmpty()) {
        const QPointF d = pt - m_cur.pts.last();
        if (d.x() * d.x() + d.y() * d.y() < 1.44) return;
    }
    m_cur.pts.append(pt);
    update();
}

/* 点到线段的最短距离平方（橡皮命中判定用）。
 * 为什么不是"点到采样点"：快速划过时相邻采样点可能隔 20~30px，
 * 只比点会让橡皮从两笔之间"漏擦"，现场表现是"擦了没反应"（本轮探针实测到了）。 */
static qreal distSqToSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const qreal len2 = ab.x() * ab.x() + ab.y() * ab.y();
    qreal t = 0.0;
    if (len2 > 0.0) {
        const QPointF ap = p - a;
        t = (ap.x() * ab.x() + ap.y() * ab.y()) / len2;
        t = qBound<qreal>(0.0, t, 1.0);
    }
    const QPointF d = p - (a + ab * t);
    return d.x() * d.x() + d.y() * d.y();
}

void Canvas::endStroke()
{
    if (!m_drawing) return;
    m_drawing = false;

    if (m_cur.eraser) {
        // 整笔擦除：删掉与橡皮轨迹相交的笔画（ICC CE 有"多种橡皮模式"，整笔擦先做，
        // 细粒度像素擦除要引入离屏掩膜，留给 H3）
        const qreal r = qMax<qreal>(m_cur.width * 2.5, 12.0);
        const qreal r2 = r * r;
        const QVector<QPointF> &ep = m_cur.pts;
        for (int i = m_strokes.size() - 1; i >= 0; --i) {
            bool hit = false;
            for (const QPointF &sp : m_strokes.at(i).pts) {
                for (int k = 0; k + 1 < ep.size(); ++k) {
                    if (distSqToSegment(sp, ep.at(k), ep.at(k + 1)) <= r2) { hit = true; break; }
                }
                // 单点橡皮（轻点一下）没有线段可算，退回点距
                if (!hit && ep.size() == 1) {
                    const QPointF d = sp - ep.first();
                    hit = (d.x() * d.x() + d.y() * d.y() <= r2);
                }
                if (hit) break;
            }
            if (hit) m_strokes.removeAt(i);
        }
    } else if (!m_cur.pts.isEmpty()) {
        m_strokes.append(m_cur);
    }

    m_cur = Stroke();
    update();
    emit changed();
}

void Canvas::pushHistory()
{
    m_undo.append(m_strokes);
    if (m_undo.size() > 50) m_undo.removeFirst();   // 上限 50 步（快照式，太占内存就裁）
    m_redo.clear();
}

void Canvas::undo()
{
    if (m_undo.isEmpty()) return;
    m_redo.append(m_strokes);
    m_strokes = m_undo.takeLast();
    update();
    emit changed();
}

void Canvas::redo()
{
    if (m_redo.isEmpty()) return;
    m_undo.append(m_strokes);
    m_strokes = m_redo.takeLast();
    update();
    emit changed();
}

void Canvas::clearAll()
{
    if (m_strokes.isEmpty() && m_cur.pts.isEmpty()) return;
    pushHistory();
    m_strokes.clear();
    m_cur = Stroke();
    m_drawing = false;
    update();
    emit changed();
}

void Canvas::setTool(const QString &tool)
{
    if (tool != QLatin1String("pen") && tool != QLatin1String("highlighter")
        && tool != QLatin1String("eraser")) {
        qWarning("[agent-qt] 批注工具只支持 pen/highlighter/eraser，收到 %s", qPrintable(tool));
        return;
    }
    if (m_tool == tool) return;
    m_tool = tool;
    emit changed();
}

void Canvas::setColor(const QString &hex)
{
    const QColor c(hex);
    if (!c.isValid()) {
        qWarning("[agent-qt] 批注颜色非法：%s", qPrintable(hex));
        return;
    }
    m_color = c;
    emit changed();
}

void Canvas::setWidth(qreal w)
{
    m_width = qBound<qreal>(1.0, w, 40.0);
    emit changed();
}

void Canvas::setBoardColor(const QColor &c)
{
    m_boardColor = c;
    update();
}

void Canvas::setPageKey(const QString &key)
{
    if (m_pageKey == key) return;
    // 换出：把当前页存起来
    if (!m_pageKey.isEmpty()) m_pages.insert(m_pageKey, m_strokes);
    m_pageKey = key;
    // 换入：取新页（没有就空）
    m_strokes = key.isEmpty() ? QVector<Stroke>() : m_pages.value(key);
    m_undo.clear();
    m_redo.clear();
    m_cur = Stroke();
    m_drawing = false;
    update();
    emit changed();
}

QString Canvas::statusLine() const
{
    return QStringLiteral("tool=%1 strokes=%2 page=%3 board=%4")
        .arg(m_tool)
        .arg(m_strokes.size())
        .arg(m_pageKey.isEmpty() ? QStringLiteral("-") : m_pageKey)
        .arg(m_boardColor.isValid() ? m_boardColor.name() : QStringLiteral("transparent"));
}

/* ══════════════════════════════════════════════════════════════
 *  Overlay —— QQuickView 窗口（QWindow，不是 QWidget）
 * ══════════════════════════════════════════════════════════════ */

Overlay *Overlay::instance()
{
    static Overlay *s = new Overlay();
    return s;
}

void registerQmlTypes()
{
    static bool done = false;
    if (done) return;
    done = true;
    qmlRegisterType<Canvas>("Stelarith.Annotate", 1, 0, "AnnotateCanvas");
}

bool Overlay::ensureView(QString *err)
{
    if (m_view) return true;

    // QML 走磁盘加载（与岛同一约定：改外观不用重编译）
    const QString qmlPath = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("qml/annotate/AnnotateRoot.qml"));
    if (!QFileInfo::exists(qmlPath)) {
        if (err) *err = QStringLiteral("批注 QML 不存在：%1（安装目录缺 qml/annotate/）").arg(qmlPath);
        return false;
    }

    registerQmlTypes();

    m_view = new QQuickView();          // QQuickView 继承 QWindow —— 不是 QWidget
    m_view->setColor(Qt::transparent);  // 透明底：批注模式要看见课件
    // WindowDoesNotAcceptFocus ⇒ Qt 建窗时直接给 WS_EX_NOACTIVATE（无焦点触控的地基）。
    // 少了它一落笔就把前台从 PPT 抢过来，翻页笔当场失效。
    m_view->setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool
                     | Qt::WindowDoesNotAcceptFocus);
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);
    m_view->setTitle(QStringLiteral("Stelarith 批注"));

    m_view->setSource(QUrl::fromLocalFile(qmlPath));
    if (m_view->status() == QQuickView::Error) {
        QStringList errs;
        for (const QQmlError &e : m_view->errors()) errs << e.toString();
        if (err) *err = QStringLiteral("批注 QML 加载失败：%1").arg(errs.join(QStringLiteral(" | ")));
        delete m_view;
        m_view = nullptr;
        return false;
    }

    m_canvas = m_view->rootObject() ? m_view->rootObject()->findChild<Canvas *>(
                                          QStringLiteral("annotateCanvas")) : nullptr;
    if (!m_canvas) {
        if (err) *err = QStringLiteral("批注 QML 里找不到 objectName=annotateCanvas 的 AnnotateCanvas");
        delete m_view;
        m_view = nullptr;
        return false;
    }

    // QML 侧 ESC → 收起批注层（键盘走钩子，见 installKeyboardHook；这条链路留给
    // 将来可能出现的"可聚焦模式"与程序内触发）
    QObject::connect(m_canvas, &Canvas::stopRequested, m_canvas, [this] { stop(); });

    // 原生事件过滤只装一次：拦 WM_MOUSEACTIVATE（见 nativeEventFilter）
    if (auto *app = QCoreApplication::instance())
        app->installNativeEventFilter(this);
    return true;
}

bool Overlay::start(QString *err)
{
    if (!ensureView(err)) return false;

    QScreen *scr = QGuiApplication::primaryScreen();
    if (!scr) { if (err) *err = QStringLiteral("没有可用的屏幕"); return false; }

    // 铺满整屏（含任务栏区域、含 PPT 全屏放映）
    m_view->setScreen(scr);
    m_view->setGeometry(scr->geometry());
    m_view->show();                     // WS_EX_NOACTIVATE 下 show 不会激活、不抢前台
    m_hwnd = reinterpret_cast<void *>(m_view->winId());
    applyWindowStyles();
    assertZOrder();

    // 层级纠偏：别的 topmost 应用（含任务管理器）随时可能插队，3s 拉回一次。
    // 只在可见期间跑，hide 后立刻停（不常年骚扰窗口管理器）。
    if (!m_zTimer) {
        m_zTimer = new QTimer(this);
        m_zTimer->setInterval(3000);
        QObject::connect(m_zTimer, &QTimer::timeout, this, [this] { assertZOrder(); });
    }
    m_zTimer->start();

    installKeyboardHook();

    qInfo("[agent-qt] 批注层已显示：%dx%d %s", m_view->width(), m_view->height(),
          qPrintable(m_canvas->statusLine()));
    return true;
}

void Overlay::stop()
{
    uninstallKeyboardHook();
    if (m_zTimer) m_zTimer->stop();
    if (!m_view) return;
    // 穿透 = hide（不切 window flags：改 flags 会重建原生窗口，闪烁且丢状态）
    m_view->hide();
    qInfo("[agent-qt] 批注层已隐藏（穿透态）");
}

bool Overlay::isActive() const
{
    return m_view && m_view->isVisible();
}

bool Overlay::createHidden(QString *err)
{
    if (!ensureView(err)) return false;
    // 建原生窗口但不显示：探针要验 WS_EX_NOACTIVATE 是否真落上（离屏平台建不出真窗口，
    // 所以这里必须用 windows 平台插件跑，但全程不 show ⇒ 桌面不受影响）
    m_hwnd = reinterpret_cast<void *>(m_view->winId());
    applyWindowStyles();
    return true;
}

/* 扩展样式：TOPMOST（盖住 PPT 全屏放映）+ NOACTIVATE（无焦点触控）+ TOOLWINDOW（不占
 * 任务栏、不进 Alt+Tab）。⚠️ exam_mode.cpp:197 的经验：Qt 的 WindowStaysOnTopHint 在个别
 * 环境会被降级（PPT 全屏放映 / UWP 全屏时），必须再用扩展样式兜一次 —— 盖不住 PPT 放映
 * 这个功能等于没有。 */
void Overlay::applyWindowStyles()
{
    if (const HWND hwnd = static_cast<HWND>(m_hwnd)) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
                          GetWindowLongPtrW(hwnd, GWL_EXSTYLE)
                              | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
    }
}

/* ── 无焦点触控：点我们也不许激活 ───────────────────────────────
 * 为什么还要拦 WM_MOUSEACTIVATE：WS_EX_NOACTIVATE 是"默认行为"层面的约定，
 * Qt 的 windows 插件在 MousePress 时会自己判断要不要 requestActivate；
 * 直接在消息层回 MA_NOACTIVATE 才是硬约束（窗口永远不进前台，PPT 的键盘焦点不动）。
 */
bool Overlay::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    if (!m_hwnd) return false;
    if (eventType != QByteArrayLiteral("windows_generic_MSG")
        && eventType != QByteArrayLiteral("windows_dispatcher_MSG")) {
        return false;
    }
    MSG *msg = static_cast<MSG *>(message);
    if (!msg || msg->hwnd != static_cast<HWND>(m_hwnd)) return false;

    if (msg->message == WM_MOUSEACTIVATE) {
        if (result) *result = MA_NOACTIVATE;
        return true;      // 吃掉：点击永不激活
    }
    return false;
}

/* ── 置顶层级纠偏：topmost，但排在置顶的任务管理器之下 ──────────── */
void Overlay::assertZOrder()
{
    if (!m_hwnd) return;
    const HWND hwnd = static_cast<HWND>(m_hwnd);
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)) return;

    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);

    const HWND tm = findTaskManagerWindow();
    if (tm && (GetWindowLongPtrW(tm, GWL_EXSTYLE) & WS_EX_TOPMOST)) {
        // hwndInsertAfter = tm ⇒ 我们紧跟其后 = 「仅次于任务管理器置顶」
        SetWindowPos(hwnd, tm, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
}

void Overlay::installKeyboardHook()
{
    if (m_kbHook) return;
    g_kbOwner = this;
    m_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, annotateKeyboardProc,
                                 GetModuleHandleW(nullptr), 0);
    if (!m_kbHook) {
        g_kbOwner = nullptr;
        qWarning("[agent-qt] 批注键盘钩子安装失败（err=%lu）⇒ ESC/Ctrl+Z 不可用，"
                 "只能由管理端 annotate_stop 收回", GetLastError());
    }
}

void Overlay::uninstallKeyboardHook()
{
    if (!m_kbHook) return;
    UnhookWindowsHookEx(static_cast<HHOOK>(m_kbHook));
    m_kbHook = nullptr;
    g_kbOwner = nullptr;
}

void Overlay::handleHotkey(int action)
{
    if (!isActive()) return;      // 钩子泄漏时也不该动
    switch (action) {
    case 1:
        qInfo("[agent-qt] 批注热键：ESC 收层");
        stop();
        break;
    case 2:
        if (m_canvas) m_canvas->undo();
        break;
    case 3:
        if (m_canvas) m_canvas->redo();
        break;
    case 4:
        if (m_canvas) m_canvas->clearAll();
        break;
    default:
        break;
    }
}

void Overlay::setBoard(const QString &bg)
{
    if (!m_canvas) return;
    const QString b = bg.trimmed().toLower();
    if (b.isEmpty() || b == QLatin1String("off") || b == QLatin1String("transparent")) {
        m_boardMode = false;
        m_boardBg.clear();
        m_canvas->setBoardColor(QColor());      // 无效色 = 透明
        return;
    }
    QColor c;
    if (b == QLatin1String("white")) c = QColor(0xFA, 0xFA, 0xFA);
    else if (b == QLatin1String("black")) c = QColor(0x1E, 0x1E, 0x1E);
    else if (b == QLatin1String("green")) c = QColor(0x1B, 0x5E, 0x3A);   // 传统绿板
    else c = QColor(b);                       // "#RRGGBB"
    if (!c.isValid()) {
        qWarning("[agent-qt] 白板背景非法：%s（回落 transparent）", qPrintable(bg));
        c = QColor();
    }
    m_boardMode = c.isValid();
    m_boardBg = bg.trimmed();
    m_canvas->setBoardColor(c);
}

QString Overlay::statusLine() const
{
    if (!m_canvas) return QStringLiteral("active=0 (未创建)");
    // focus=0 恒为 0（无焦点触控）；hook 是逃生口是否可用的唯一判据
    return QStringLiteral("active=%1 board=%2 hook=%3 focus=0 topmost=1 %4")
        .arg(isActive() ? 1 : 0)
        .arg(m_boardMode ? m_boardBg : QStringLiteral("transparent"))
        .arg(m_kbHook ? 1 : 0)
        .arg(m_canvas->statusLine());
}

} // namespace AnnotateDomain
