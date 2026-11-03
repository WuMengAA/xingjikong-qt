// 灵动岛倒计时「固定目标」逻辑探针（2026-11-03 · Task1 + Task5）
//
// 只链真 `src/island/island_countdown_target.cpp`，不经 QML、不碰 PanelDomain。
// 模块是纯状态（QSettings 里一个 JSON），链它就能单测；同时方便反向验证。
//
// 断言（每条对应一个真实会犯的错）：
//  A 未配置时 params() 返回空对象（常驻面应回落课表边界）
//  B 配置 HH:MM 后 params() 读出 {to,label?}
//  C 配置 ISO8601 后 params() 读出
//  D 非法 to ⇒ set 返回 ok=false（不让坏配置悄悄生效）
//  E clear/to空 ⇒ 删除键，params() 回到空
//  F 写坏 QSettings 后 params() 不对着坏 JSON 崩、回落空
//  G 配置后跨「新进程读同一份 QSettings」仍有效（持久化真源）
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>

#include "island/island_countdown_target.h"

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
    QCoreApplication::setApplicationName("IslandCountdownTargetProbe");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp.path());
    QSettings().clear();

    // ── A 未配置 ──
    check(CountdownTarget::params().isEmpty(),
          QStringLiteral("A 未配置时 params() 为空（回落课表边界）"));

    // ── B 配置 HH:MM + label ──
    {
        QJsonObject in;
        in.insert(QStringLiteral("to"), QStringLiteral("12:30"));
        in.insert(QStringLiteral("label"), QStringLiteral("午休"));
        const QJsonObject res = CountdownTarget::set(in);
        check(res.value(QStringLiteral("ok")).toBool(false),
              QStringLiteral("B 配置 HH:MM 成功"));
        const QJsonObject p = CountdownTarget::params();
        check(p.value(QStringLiteral("to")).toString() == QStringLiteral("12:30"),
              QStringLiteral("B2 读出 to=12:30"));
        check(p.value(QStringLiteral("label")).toString() == QStringLiteral("午休"),
              QStringLiteral("B3 读出 label=午休"));
    }

    // ── C 配置 ISO8601 ──
    {
        QSettings().clear();
        QJsonObject in;
        in.insert(QStringLiteral("to"), QStringLiteral("2026-12-31T23:59:59"));
        in.insert(QStringLiteral("title"), QStringLiteral("跨年"));
        const QJsonObject res = CountdownTarget::set(in);
        check(res.value(QStringLiteral("ok")).toBool(false),
              QStringLiteral("C ISO8601 配置成功"));
        const QJsonObject p = CountdownTarget::params();
        check(p.value(QStringLiteral("to")).toString().startsWith(QStringLiteral("2026-12-31")),
              QStringLiteral("C2 读出 ISO8601 to"));
        check(p.value(QStringLiteral("title")).toString() == QStringLiteral("跨年"),
              QStringLiteral("C3 读出 title=跨年"));
    }

    // ── D 非法 to ──
    {
        QSettings().clear();
        QJsonObject in;
        in.insert(QStringLiteral("to"), QStringLiteral("不是时间"));
        const QJsonObject res = CountdownTarget::set(in);
        check(!res.value(QStringLiteral("ok")).toBool(false),
              QStringLiteral("D 非法 to 被拒（ok=false）"));
        check(!res.value(QStringLiteral("error")).toString().isEmpty(),
              QStringLiteral("D2 回带 error 文案"));
        check(CountdownTarget::params().isEmpty(),
              QStringLiteral("D3 非法配置不落盘（params 仍空）"));
    }

    // ── E clear / to 空 ──
    {
        QJsonObject in1;
        in1.insert(QStringLiteral("to"), QStringLiteral("08:00"));
        CountdownTarget::set(in1);
        check(!CountdownTarget::params().isEmpty(),
              QStringLiteral("E 先放一个目标"));
        QJsonObject clr;
        clr.insert(QStringLiteral("clear"), true);
        const QJsonObject r = CountdownTarget::set(clr);
        check(r.value(QStringLiteral("ok")).toBool(false),
              QStringLiteral("E2 clear 成功"));
        check(CountdownTarget::params().isEmpty(),
              QStringLiteral("E3 clear 后 params 回落空（键已删）"));

        // to 空也等同清除
        CountdownTarget::set(in1);
        const QJsonObject r2 = CountdownTarget::set(QJsonObject());
        check(r2.value(QStringLiteral("ok")).toBool(false),
              QStringLiteral("E4 空 to 也清除成功"));
        check(CountdownTarget::params().isEmpty(),
              QStringLiteral("E5 空 to 后回落空"));
    }

    // ── F 写坏 QSettings 不崩 ──
    {
        QSettings s;
        s.setValue(QStringLiteral("panel/countdownTarget"), QStringLiteral("这不是JSON{"));
        const QJsonObject p = CountdownTarget::params();
        check(p.isEmpty(), QStringLiteral("F 坏 JSON 回落空（不崩）"));
    }

    // ── G 跨进程持久化（新模型读同一份 QSettings）──
    {
        QSettings().clear();
        QJsonObject in;
        in.insert(QStringLiteral("to"), QStringLiteral("17:30"));
        CountdownTarget::set(in);
        // 模拟"重启"：重建 QSettings（同一 tmp 路径）
        QSettings s2;
        const QString raw = s2.value(QStringLiteral("panel/countdownTarget")).toString();
        const QJsonObject doc = QJsonDocument::fromJson(raw.toUtf8()).object();
        check(doc.value(QStringLiteral("to")).toString() == QStringLiteral("17:30"),
              QStringLiteral("G 重启后键仍在（跨进程有效）"));
    }

    QSettings().clear();
    fprintf(stdout, "\nRESULT: PASS %d / FAIL %d\n", g_pass, g_fail);
    return g_fail == 0 ? 10 : 20;
}
