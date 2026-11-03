// 灵动岛组件布局模型 · 口径探针（2026-11-03）
//
// 直接链真 `src/island/island_layout.cpp` 跑，不经 QML —— 布局模型是纯状态机，
// 用 QML 验会把"模型对不对"和"渲染对不对"两件事混在一起，红了不知道该修哪边。
//
// 断言（每条都对应一个真实会犯的错）：
//  A 默认序与 IslandRoot.qml 回落序一致（两处漂移会让预览与真机长得不一样）
//  B moveRow 真改顺序且持久化（重开一个模型能读到新序）
//  C setEnabledById 关掉的面从 faceList 消失
//  D 全关时 faceList 仍返回「概」（否则展开区空白）
//  E scale 越界夹到 80/140（QML 的 −/+ 到头了还点得动）
//  F resetToDefault 删键而不是写默认值（写默认值会让"从没配过"和"配成默认"混成一种状态）
//  G 磁盘被写坏（非 JSON / 未知 id / 少面）时不崩、认出多少算多少、缺的补回来
//  H 显示方向 carousel|tiled（2026-11-04 · m14970 ① 用户点单「不要仅限于列表式的展示」）
//    为什么单独一组：这一项是**整岛渲染路径的开关**，切错了不是"样式不对"而是
//    "岛整块长得不一样"（平铺走 IslandTileGrid，轮播走 fpFlick）。而且它必须持久化
//    ——老师重启机器还应该是平铺，否则每次开机都得重设一次。
//    G3 老 bug 同款陷阱：这里写入的是 QSettings 字符串，写坏了必须认默认而不是空白岛。
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>

#include "island/island_layout.h"

static int g_pass = 0;
static int g_fail = 0;

static void check(bool ok, const QString &what)
{
    if (ok) { ++g_pass; fprintf(stdout, "OK   %s\n", qPrintable(what)); }
    else    { ++g_fail; fprintf(stdout, "FAIL %s\n", qPrintable(what)); }
}

int main(int argc, char **argv)
{
    QTemporaryDir tmp;
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("StelarithProbe");
    QCoreApplication::setApplicationName("IslandLayoutProbe");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp.path());
    QSettings().clear();

    // ── A 默认序 ──
    {
        IslandLayoutModel m;
        const QVariantList fl = m.faceList();
        QStringList ids;
        for (const auto &v : fl) ids << QString::number(v.toMap().value(QStringLiteral("id")).toInt());
        // 与 IslandRoot.qml 的回落序逐项一致：概 今 媒 笔 设 自
        check(ids == QStringList({"0", "7", "1", "4", "5", "6"}),
              QStringLiteral("A 默认序 = 概今媒笔设自（实际 %1）").arg(ids.join(",")));
        check(m.rowCount() == 9, QStringLiteral("A2 九行（概今钟期倒媒笔设自，实际 %1）").arg(m.rowCount()));
        check(!ids.contains("8") && !ids.contains("9"),
              QStringLiteral("A3 期/倒默认不出现（实际 %1）").arg(ids.join(",")));
    }

    // ── B moveRow + 持久化 ──
    {
        {
            IslandLayoutModel m;
            m.moveRow(0, 3);         // 概挪到第 4 位
            const QVariantList fl = m.faceList();
            const int first = fl.first().toMap().value(QStringLiteral("id")).toInt();
            check(first == 7, QStringLiteral("B 拖动后首位变「今」（实际 id=%1）").arg(first));
        }
        IslandLayoutModel m2;        // 新模型读同一份 QSettings
        const QVariantList fl = m2.faceList();
        const int first = fl.first().toMap().value(QStringLiteral("id")).toInt();
        check(first == 7, QStringLiteral("B2 顺序已持久化（新模型首位 id=%1）").arg(first));
        check(m2.rowCount() == 9, QStringLiteral("B3 持久化不丢行（实际 %1）").arg(m2.rowCount()));
    }

    // ── C 关掉一个面 ──
    {
        IslandLayoutModel m;
        m.setEnabledById(4, false);          // 关「笔」
        const QVariantList fl = m.faceList();
        bool has4 = false;
        for (const auto &v : fl) if (v.toMap().value(QStringLiteral("id")).toInt() == 4) has4 = true;
        check(!has4, QStringLiteral("C 关掉的面从 faceList 消失"));
        check(fl.size() == 5, QStringLiteral("C2 只剩 5 个面（实际 %1）").arg(fl.size()));
        check(m.anyEnabled(), QStringLiteral("C3 还有启用的面"));

        // ── D 全关 ──
        m.setEnabledById(0, false);
        m.setEnabledById(7, false);
        m.setEnabledById(1, false);
        m.setEnabledById(5, false);
        m.setEnabledById(6, false);
        const QVariantList all = m.faceList();
        check(all.size() == 1
              && all.first().toMap().value(QStringLiteral("id")).toInt() == 0,
              QStringLiteral("D 全关时仍留「概」占位（实际 %1 个）").arg(all.size()));
        check(!m.anyEnabled(), QStringLiteral("D2 anyEnabled 为假"));
    }

    // ── E 缩放夹紧 ──
    {
        IslandLayoutModel m;
        m.setScaleById(7, 999);
        int sc = 100;
        for (int r = 0; r < m.rowCount(); ++r) {
            const auto ix = m.index(r, 0);
            if (m.data(ix, IslandLayoutModel::IdRole).toInt() == 7)
                sc = m.data(ix, IslandLayoutModel::ScaleRole).toInt();
        }
        check(sc == 140, QStringLiteral("E 放大夹到 140（实际 %1）").arg(sc));
        m.setScaleById(7, 1);
        for (int r = 0; r < m.rowCount(); ++r) {
            const auto ix = m.index(r, 0);
            if (m.data(ix, IslandLayoutModel::IdRole).toInt() == 7)
                sc = m.data(ix, IslandLayoutModel::ScaleRole).toInt();
        }
        check(sc == 80, QStringLiteral("E2 缩小夹到 80（实际 %1）").arg(sc));
    }

    // ── F reset 删键 ──
    {
        IslandLayoutModel m;
        m.moveRow(2, 0);
        m.resetToDefault();
        check(!QSettings().contains(QStringLiteral("island/layout")),
              QStringLiteral("F 恢复默认后 QSettings 键已删除（不是写回默认值）"));
        const QVariantList fl = m.faceList();
        check(fl.first().toMap().value(QStringLiteral("id")).toInt() == 0,
              QStringLiteral("F2 恢复默认后回到内置序"));
    }

    // ── G 磁盘被写坏 ──
    {
        QSettings s;
        s.setValue(QStringLiteral("island/layout"), QStringLiteral("这不是JSON{"));
        IslandLayoutModel m1;
        check(m1.rowCount() == 9, QStringLiteral("G 非 JSON 时回落内置默认（%1 行）").arg(m1.rowCount()));

        s.setValue(QStringLiteral("island/layout"),
                   QStringLiteral(R"([{"id":999,"enabled":true},{"id":7,"enabled":false,"scale":120}])"));
        IslandLayoutModel m2;
        check(m2.rowCount() == 9, QStringLiteral("G2 未知 id 被丢弃、缺的面补回（%1 行）").arg(m2.rowCount()));
        const QVariantList fl = m2.faceList();
        bool has7 = false;
        for (const auto &v : fl) if (v.toMap().value(QStringLiteral("id")).toInt() == 7) has7 = false;
        check(!has7, QStringLiteral("G3 文件里 enabled=false 的「今」确实不出现"));
    }

    // ── H 显示方向 carousel|tiled（2026-11-04 · m14970 ①「不要仅限于列表式的展示」）──
    //    为什么单独一组：它是**整岛渲染路径的开关** —— 切错了不是"样式不对"，
    //    而是岛整块长得不一样（平铺走 IslandTileGrid，轮播走 fpFlick）。
    {
        IslandLayoutModel m;
        check(m.direction() == QStringLiteral("carousel"),
              QStringLiteral("H 默认轮播（实际 %1）").arg(m.direction()));

        m.setDirection(QStringLiteral("tiled"));
        check(m.direction() == QStringLiteral("tiled"),
              QStringLiteral("H2 可切到平铺（实际 %1）").arg(m.direction()));

        // 持久化：老师重启机器还应该是平铺，否则每次开机都得重设一次。
        IslandLayoutModel m2;
        check(m2.direction() == QStringLiteral("tiled"),
              QStringLiteral("H3 方向已持久化（新模型 %1）").arg(m2.direction()));

        // 非法值必须**静默忽略并保留当前值**，不能切进一个不存在的模式
        // （切坏 = 岛整块空白，比不切更糟）。
        m2.setDirection(QStringLiteral("diagonal"));
        check(m2.direction() == QStringLiteral("tiled"),
              QStringLiteral("H4 非法方向被忽略、保留原值（实际 %1）").arg(m2.direction()));

        // 磁盘里被写成不认识的值 ⇒ 读回时认默认，绝不弹错、绝不留空。
        QSettings s;
        s.setValue(QStringLiteral("island/direction"), QStringLiteral("garbage"));
        IslandLayoutModel m3;
        check(m3.direction() == QStringLiteral("carousel"),
              QStringLiteral("H5 磁盘坏值回落 carousel（实际 %1）").arg(m3.direction()));

        // resetToDefault 要把方向一起复位并删键（同 F 口径：删键而非写默认值）。
        IslandLayoutModel m4;
        m4.setDirection(QStringLiteral("tiled"));
        m4.resetToDefault();
        check(m4.direction() == QStringLiteral("carousel"),
              QStringLiteral("H6 恢复默认把方向一并复位（实际 %1）").arg(m4.direction()));
        check(!QSettings().contains(QStringLiteral("island/direction")),
              QStringLiteral("H7 恢复默认后方向键已删除"));
    }

    QSettings().clear();
    fprintf(stdout, "\nRESULT: PASS %d / FAIL %d\n", g_pass, g_fail);
    return g_fail == 0 ? 10 : 20;
}
