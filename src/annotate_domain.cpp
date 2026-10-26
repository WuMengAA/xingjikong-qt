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
#include <QUrl>
#include <QtQml>

#include <windows.h>   // SetWindowLongPtr / GWL_EXSTYLE / WS_EX_TOPMOST

namespace AnnotateDomain {

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

void Canvas::endStroke()
{
    if (!m_drawing) return;
    m_drawing = false;

    if (m_cur.eraser) {
        // 整笔擦除：删掉与橡皮轨迹相交的笔画（ICC CE 有"多种橡皮模式"，整笔擦先做，
        // 细粒度像素擦除要引入离屏掩膜，留给 H3）
        const qreal r = qMax<qreal>(m_cur.width * 2.5, 12.0);
        const qreal r2 = r * r;
        for (int i = m_strokes.size() - 1; i >= 0; --i) {
            bool hit = false;
            for (const QPointF &ep : m_cur.pts) {
                for (const QPointF &sp : m_strokes.at(i).pts) {
                    const QPointF d = sp - ep;
                    if (d.x() * d.x() + d.y() * d.y() <= r2) { hit = true; break; }
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
    m_view->setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);

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

    // QML 侧 ESC → 收起批注层（本地逃生口，不等管理端）
    QObject::connect(m_canvas, &Canvas::stopRequested, m_canvas, [this] { stop(); });
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
    m_view->show();
    m_view->raise();

    // ⚠️ exam_mode.cpp:197 的经验：Qt 的 WindowStaysOnTopHint 在个别环境会被降级
    //    （尤其 PPT 全屏放映 / UWP 全屏时），必须再用 WS_EX_TOPMOST 兜一次。
    //    批注层盖不住 PPT 放映 = 这个功能等于没有。
    if (const WId wid = m_view->winId()) {
        const auto hwnd = reinterpret_cast<HWND>(wid);
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
                          GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_TOPMOST);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    qInfo("[agent-qt] 批注层已显示：%dx%d %s", m_view->width(), m_view->height(),
          qPrintable(m_canvas->statusLine()));
    return true;
}

void Overlay::stop()
{
    if (!m_view) return;
    // 穿透 = hide（不切 window flags：改 flags 会重建原生窗口，闪烁且丢状态）
    m_view->hide();
    qInfo("[agent-qt] 批注层已隐藏（穿透态）");
}

bool Overlay::isActive() const
{
    return m_view && m_view->isVisible();
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
    return QStringLiteral("active=%1 board=%2 %3")
        .arg(isActive() ? 1 : 0)
        .arg(m_boardMode ? m_boardBg : QStringLiteral("transparent"))
        .arg(m_canvas->statusLine());
}

} // namespace AnnotateDomain
