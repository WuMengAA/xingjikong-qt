// 批注 · 墨迹渲染与页模型（2026-10-27 · H 批）
//
// 从 annotate_domain.cpp 拆出：那边涨到 655 行，撞了「实现文件 ≤ 600 行」红线
// （file-size 门禁 ③ 新增超限）。拆口选在 **Canvas / Overlay 之间**：
//   · Canvas  = 纯数据 + QPainter 渲染，不碰一丁点 Win32，可单测；
//   · Overlay = 窗口 / 置顶层级 / 键盘钩子，全是 Win32。
// 两边本来就没有共享状态，拆开后各自能单独读。
// 公开声明仍在 annotate_domain.h —— QML 注册的类型名不变，调用方零改动。
#include "annotate_domain.h"

#include <QPainter>

#include <utility>   // std::as_const（遍历页序时避免 Qt 容器 detach）

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

void Canvas::switchTo(const QString &key)
{
    // 换出：把当前页存起来。
    // ⚠️ **空 key 也要存**：它只是"还没分页"的那一页，不是"没有页"。
    //    旧写法 `if (!m_pageKey.isEmpty())` 会让老师在未分页时写的东西，
    //    在第一次按「新建页」的瞬间凭空消失（无可撤销、无从解释）。
    m_pages.insert(m_pageKey, m_strokes);
    m_pageKey = key;
    // 换入：取新页（没有就空）
    m_strokes = m_pages.value(key);
    m_undo.clear();
    m_redo.clear();
    m_cur = Stroke();
    m_drawing = false;
    update();
    emit changed();
}

void Canvas::setPageKey(const QString &key)
{
    if (m_pageKey == key) return;
    // 进过的页都记进页序（PPT 跳到第 7 页、白板新建页都走这里）
    if (!key.isEmpty() && !m_pageOrder.contains(key)) m_pageOrder.append(key);
    switchTo(key);
}

/* ── 白板多页（H2）───────────────────────────────────────────────
 * 页序号用连续数字键 "1".."N"（空 key 只是"还没分页"的状态）。
 * 为什么不插在中间：重排已有页会让老师刚写好的第 3 页变成第 4 页，
 * 现场无法解释；所以新建页一律追加在末页之后。
 */
void Canvas::addPage()
{
    if (m_pageOrder.isEmpty()) {
        // 首次分页：把当前（可能是空 key 的）这一页定为第 1 页，
        // 否则老师"写着写着想加一页"时，前一页的墨迹会当场消失。
        m_pageOrder.append(QStringLiteral("1"));
        m_pages.insert(QStringLiteral("1"), m_strokes);
        m_pageKey = QStringLiteral("1");
        emit changed();
    }
    int maxNo = 0;
    for (const QString &k : std::as_const(m_pageOrder)) maxNo = qMax(maxNo, k.toInt());
    const QString nk = QString::number(maxNo + 1);
    m_pages.insert(nk, QVector<Stroke>());
    m_pageOrder.append(nk);
    switchTo(nk);
}

void Canvas::nextPage()
{
    if (m_pageOrder.isEmpty()) { addPage(); return; }
    const int i = m_pageOrder.indexOf(m_pageKey);
    if (i < 0) { switchTo(m_pageOrder.first()); return; }   // 未分页态 → 回第 1 页
    if (i + 1 < m_pageOrder.size()) switchTo(m_pageOrder.at(i + 1));
    else addPage();                                        // 末页再往后 = 新建页
}

void Canvas::prevPage()
{
    if (m_pageOrder.isEmpty()) return;
    const int i = m_pageOrder.indexOf(m_pageKey);
    if (i < 0) { switchTo(m_pageOrder.first()); return; }
    if (i > 0) switchTo(m_pageOrder.at(i - 1));
}

int Canvas::pageIndex() const
{
    if (m_pageKey.isEmpty()) return 0;
    const int i = m_pageOrder.indexOf(m_pageKey);
    return i < 0 ? 0 : i + 1;
}

QString Canvas::statusLine() const
{
    return QStringLiteral("tool=%1 strokes=%2 page=%3 idx=%4/%5 board=%6")
        .arg(m_tool)
        .arg(m_strokes.size())
        .arg(m_pageKey.isEmpty() ? QStringLiteral("-") : m_pageKey)
        .arg(pageIndex())
        .arg(m_pageOrder.size())
        .arg(m_boardColor.isValid() ? m_boardColor.name() : QStringLiteral("transparent"));
}

} // namespace AnnotateDomain
