// 星集控被控端 · 灵动岛桥（批注域）
// 2026-11-02 从 island_bridge.cpp 按职能切出（600 行红线拆分，搬家不改逻辑）。
// ⚠️ 岛上的「笔」是本机直调批注域，不走云端：老师点下去必须立刻有反应，
//    绕云端（WS→云→WS）最快几百毫秒且断网就废。云端 annotate_* 留给管理端，
//    两条路走同一个域，状态只有一份。
#include "island_bridge.h"
#include "island.h"
#include "../annotate/annotate_domain.h"

bool IslandBridge::annotateActive() const
{
    return AnnotateDomain::Overlay::instance()->isActive();
}
QString IslandBridge::annotateTool() const
{
    auto *c = AnnotateDomain::Overlay::instance()->canvas();
    return c ? c->tool() : QStringLiteral("pen");
}
QString IslandBridge::annotatePage() const
{
    auto *c = AnnotateDomain::Overlay::instance()->canvas();
    if (!c) return QStringLiteral("1/1");
    const int idx = c->pageIndex() > 0 ? c->pageIndex() : 1;
    const int cnt = c->pageCount() > 0 ? c->pageCount() : 1;
    return QStringLiteral("%1/%2").arg(idx).arg(cnt);
}
QString IslandBridge::annotateHint() const
{
    return m_annotHint;
}
void IslandBridge::annotateAction(const QString &cmd)
{
    auto *ov = AnnotateDomain::Overlay::instance();
    const QString c = cmd.trimmed();

    if (c == QLatin1String("toggle")) {
        if (ov->isActive()) {
            ov->stop();
            m_annotHint = QStringLiteral("已结束批注");
        } else {
            // 岛必须留在批注层之上：否则一开始批注就把操作面盖住了，
            // 老师只能靠 ESC 才能出来。
            if (m_host) ov->setWindowAboveMe(reinterpret_cast<void *>(m_host->winId()));
            QString err;
            if (!ov->start(&err)) {
                m_annotHint = err.isEmpty() ? QStringLiteral("批注层启动失败") : err;
                sync();
                return;
            }
            m_annotHint = QStringLiteral("正在批注 · ESC 结束");
        }
        sync();
        return;
    }

    AnnotateDomain::Canvas *cv = ov->canvas();
    if (!cv) {
        m_annotHint = QStringLiteral("先点「开始批注」");
        sync();
        return;
    }
    if (c == QLatin1String("pen") || c == QLatin1String("highlighter")
        || c == QLatin1String("eraser")) {
        cv->setTool(c);
    } else if (c == QLatin1String("undo")) {
        cv->undo();
    } else if (c == QLatin1String("redo")) {
        cv->redo();
    } else if (c == QLatin1String("clear")) {
        cv->clearAll();
    } else if (c == QLatin1String("next")) {
        cv->nextPage();
    } else if (c == QLatin1String("prev")) {
        cv->prevPage();
    } else if (c.startsWith(QLatin1String("color:"))) {
        cv->setColor(c.mid(6));
    } else if (c.startsWith(QLatin1String("width:"))) {
        cv->setWidth(c.mid(6).toDouble());
    } else if (c.startsWith(QLatin1String("board:"))) {
        ov->setBoard(c.mid(6));
    } else {
        m_annotHint = QStringLiteral("不认识的批注动作：") + c;
    }
    sync();
}
