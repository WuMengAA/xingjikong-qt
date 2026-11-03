#include "island_panel_source.h"

#include "../domain/panel_domain.h"
#include "island_countdown_target.h"
#include "island_layout.h"

#include <QJsonArray>
#include <QJsonObject>

namespace {
// 常驻倒计时面：先取管理端下发的固定目标（持久化在 QSettings），
// 没有 → 回落「课表边界」语义（buildCountdown 已支持空 params）。
// 固定目标优先级高于课表边界，符合 ClassIsland 倒计时「盯死一个时刻」的产品语义。
QJsonObject countdownParams()
{
    const QJsonObject fixed = CountdownTarget::params();
    if (!fixed.isEmpty()) return fixed;
    return QJsonObject();
}

// 剩余毫秒 → `mm:ss` / `h:mm:ss`。
// ⚠️ 不能复用 QTime::toString：超过 24h 会绕圈，而"距离运动会还有 3 天"是常态需求，
// 所以这里先按天切，剩下才是时分秒（ClassIsland 的 DaysLeft 口径一致）。
QString remainText(qint64 ms)
{
    if (ms <= 0) return QStringLiteral("00:00");
    const qint64 totalSec = ms / 1000;
    const qint64 days = totalSec / 86400;
    const qint64 h = (totalSec % 86400) / 3600;
    const qint64 m = (totalSec % 3600) / 60;
    const qint64 s = totalSec % 60;
    const auto two = [](qint64 v) {
        return QStringLiteral("%1").arg(v, 2, 10, QLatin1Char('0'));
    };
    if (days > 0) return QStringLiteral("%1天 %2:%3").arg(days).arg(h).arg(two(m));
    if (h > 0) return QStringLiteral("%1:%2:%3").arg(h).arg(two(m)).arg(two(s));
    return QStringLiteral("%1:%2").arg(two(m)).arg(two(s));
}
}   // namespace

IslandPanelSource::IslandPanelSource(QObject *parent)
    : QObject(parent)
{
}

QVariantMap IslandPanelSource::dateInfo() const
{
    // 日期面恒可用（不依赖课表）：只有「第几周」需要课表，那是 PanelDomain 内部的事。
    return PanelDomain::build(QStringLiteral("date"), QJsonObject()).data.toVariantMap();
}

QVariantMap IslandPanelSource::countdownInfo() const
{
    const PanelDomain::Result r = PanelDomain::build(QStringLiteral("countdown"), countdownParams());
    QVariantMap m = r.data.toVariantMap();
    // 失败时把原因带过去，QML 侧据此写「课表未接上」而不是画一个 00:00
    // （00:00 会被当成"正好到点"，是最坏的一种错）。
    if (!r.ok) m.insert(QStringLiteral("error"), r.error);
    return m;
}

QStringList IslandPanelSource::availableFaces() const
{
    return PanelDomain::faces();
}

QString IslandPanelSource::barLessonText() const
{
    return PanelDomain::barLessonText();
}

bool IslandPanelSource::openFace(int id)
{
    // id → PanelDomain 面名。四个「收起条上有的」面都能摊开；
    // 其余 id（概/笔/设/自/媒）不走这条路径 —— 它们要么是通知专用，
    // 要么已被用户裁决「不该在岛上，应该在设置里」。
    QString face;
    QJsonObject params;
    switch (id) {
    case 10: face = QStringLiteral("date");     break;   // 时钟槽 → 日期面（同一面里既有大钟也有日期）
    case 8:  face = QStringLiteral("date");     break;
    case 9:  face = QStringLiteral("countdown"); params = countdownParams(); break;
    case 7:  face = QStringLiteral("timetable"); break;
    default: return false;
    }

    // 常驻（durationMs=0）：看板类内容不该自己消失 —— 与 buildCountdown 的
    // 「距离下课」语义一致（那是个要一直盯着的读数）。
    QString err;
    const bool ok = PanelDomain::show(face, params, 0, &err, nullptr);
    if (!ok) qInfo("[island] 打开组件面失败 id=%d：%s", id, qPrintable(err));
    return ok;
}

QVariantList IslandPanelSource::barSlots() const
{    // ── 哪些面配得上"收起条上的常驻单元" ────────────────────────────
    //  用户 m16706：「有些功能不应该出现在岛上，而应该在设置里面」。
    //  所以这里**不是**把 faceList 全搬过来，而是白名单四个：
    //    10 钟 —— 时钟（ClassIsland 里是独立组件）
    //     8 期 —— 日期 + 星期 + 第几教学周
    //     9 倒 —— 倒计时（固定目标优先，否则盯课表边界）
    //     7 今 —— 课表简称横条（今天全部课程）
    //  明确**不上条**的：0 概（通知专用，通知走自动展开）、4 笔（批注工具，
    //  展开才用）、5 设（音量/黑名单，已在设置页有完整入口）、6 自（规则编辑）、
    //  1 媒（媒体已在展开面的媒体行完整呈现，横条上再挤一张专辑封面只会把课表挤掉）。
    static const QList<int> kBarFaces = {10, 8, 9, 7};

    // 顺序听用户的（islandLayout 的顺序就是拖动排序的结果）；启用态也听它。
    // ⚠️ islandLayout 可能未注入（管理端 InteractDialog 复用 IslandRoot 时没有模型）
    //   —— 那时回落内置序，不能让整条收起条空掉。
    IslandLayoutModel *layout = islandLayoutInstance();
    QVariantList faces;
    if (layout) {
        for (const QVariant &v : layout->faceList()) {
            const QVariantMap m = v.toMap();
            if (kBarFaces.contains(m.value(QStringLiteral("id")).toInt()))
                faces.append(m);
        }
    }
    if (faces.isEmpty()) {
        for (int id : kBarFaces) {
            QVariantMap m;
            m.insert(QStringLiteral("id"), id);
            faces.append(m);
        }
    }

    // 每个面的数据只在需要时算一次（否则每次刷新都要跑三遍 build*）
    QVariantMap dateCache;
    QVariantMap countCache;
    QVariantMap ttCache;
    bool dateLoaded = false, countLoaded = false, ttLoaded = false;

    QVariantList out;
    for (const QVariant &v : faces) {
        const QVariantMap fm = v.toMap();
        const int id = fm.value(QStringLiteral("id")).toInt();
        QVariantMap slot;
        // 2026-11-04（用户「大小调节也有问题」）：设置页的 −/+ 缩放（80~140%）
        // 原来只作用于**展开面**（IslandFaceLoader.scalePercent），收起条上的组件槽
        // 完全不受影响 —— 同一个"缩放"两处语义不一致，用户调了没反应。
        // 这里把每面的 scale 一并下发，QML 侧按它缩字号与药丸高度。
        const int sc = fm.value(QStringLiteral("scale")).toInt();
        if (sc > 0) slot.insert(QStringLiteral("scale"), sc);
        slot.insert(QStringLiteral("id"), id);

        if (id == 10) {
            // 时钟：**不在这里算时间** —— 时间要每秒走，C++ 每秒 emit 一条
            // 变化信号比 QML 自己 Date.now() 贵得多（且必然与系统钟分叉的争论）。
            // 这里只给"这一格是时钟"，QML 用 IslandClock 自己滴答。
            slot.insert(QStringLiteral("icon"), QStringLiteral("clock"));
            slot.insert(QStringLiteral("text"), QString());
            slot.insert(QStringLiteral("isClock"), true);
            out.append(slot);
            continue;
        }

        if (id == 8) {
            if (!dateLoaded) { dateCache = dateInfo(); dateLoaded = true; }
            const QString week = dateCache.value(QStringLiteral("week")).toString();
            const QString weekday = dateCache.value(QStringLiteral("weekday")).toString();
            const QString dt = dateCache.value(QStringLiteral("date")).toString();
            slot.insert(QStringLiteral("icon"), QStringLiteral("calendar"));
            // 收起条只有 38px 高、单行 —— 「星期三 2026年11月4日」这种全称会把课表槽挤出去。
            // 按用户「非必要不要彰显你的废话」压成「周三 11/4」，周次留在 sub：
            // 日期组件的价值是"今天是几号、周几"，年份和"20xx年"在收起条上纯属噪音
            // （本机 ClassIsland 主界面同样是「周三 11/04」短格式）。
            QString wd = weekday;
            if (wd.startsWith(QStringLiteral("星期"))) wd = QStringLiteral("周") + wd.mid(2);
            const QDate today = QDate::currentDate();
            slot.insert(QStringLiteral("text"),
                        QStringLiteral("%1 %2/%3").arg(wd).arg(today.month()).arg(today.day()));
            slot.insert(QStringLiteral("sub"), week);
            out.append(slot);
            continue;
        }

        if (id == 9) {
            if (!countLoaded) { countCache = countdownInfo(); countLoaded = true; }
            if (countCache.contains(QStringLiteral("error"))) continue;  // 无目标/未配置 → 不占位
            const qint64 targetMs = qint64(countCache.value(QStringLiteral("targetMs")).toDouble());
            const qint64 nowMs = qint64(countCache.value(QStringLiteral("nowMs")).toDouble());
            if (targetMs <= 0) continue;
            slot.insert(QStringLiteral("icon"), QStringLiteral("hourglass"));
            slot.insert(QStringLiteral("text"),
                        countCache.value(QStringLiteral("label")).toString());
            slot.insert(QStringLiteral("sub"), remainText(targetMs - nowMs));
            slot.insert(QStringLiteral("accent"), targetMs - nowMs <= 60000);
            out.append(slot);
            continue;
        }

        if (id == 7) {
            if (!ttLoaded) {
                ttCache = PanelDomain::build(QStringLiteral("timetable"), QJsonObject()).data.toVariantMap();
                ttLoaded = true;
            }
            const QVariantList lessons = ttCache.value(QStringLiteral("lessons")).toList();
            if (lessons.isEmpty()) continue;        // 今天没课就不占条（ClassIsland 同口径）
            // 课表槽的 text 用「简称连排」而不是一句话：用户 m14970 裁决
            // 「课表组件的显示为横向排列的每科简称」，与本机 ClassIsland 的 `早化数物英...` 一致。
            QString strip;
            for (const QVariant &lv : lessons) {
                const QString ab = lv.toMap().value(QStringLiteral("subjectAbbr")).toString();
                strip += ab.isEmpty() ? QStringLiteral("?") : ab;
            }
            slot.insert(QStringLiteral("text"), strip);
            slot.insert(QStringLiteral("wide"), true);
            // 「正在上的那一节」用强调色：ClassIsland 的 ShowCurrentLessonOnlyOnClass 同义。
            const int nowIndex = ttCache.value(QStringLiteral("nowIndex")).toInt();
            slot.insert(QStringLiteral("accent"), nowIndex >= 0);
            out.append(slot);
            continue;
        }
    }
    return out;
}

IslandPanelSource *islandPanelSourceInstance()
{
    static IslandPanelSource *s = new IslandPanelSource();
    return s;
}

// ── 长按组件槽 → 进编辑模式（2026-11-04 · m16757）────────────────────────
// 实现见头文件 openEditor() 的考据：钩子由 main.cpp 注入，探针不注入即静默 false。
namespace {
std::function<void(int)> &editorOpener()
{
    static std::function<void(int)> fn;   // 空 = 没人接管（探针/管理端共用本文件时）
    return fn;
}
}   // namespace

void IslandPanelSource::setEditorOpener(std::function<void(int)> opener)
{
    editorOpener() = std::move(opener);
}

bool IslandPanelSource::openEditor(int id)
{
    if (!editorOpener()) return false;
    editorOpener()(id);
    return true;
}
