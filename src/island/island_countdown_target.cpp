// 灵动岛「倒计时」固定目标（2026-11-03 · Task1 + Task5）
// 实现见 island_countdown_target.h 头注释。纯状态，零业务依赖。
#include "island_countdown_target.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QSettings>
#include <QTime>

namespace CountdownTarget {
namespace {
constexpr char kKey[] = "panel/countdownTarget";

bool validTo(const QString &to)
{
    if (to.isEmpty()) return false;
    if (QTime::fromString(to, QStringLiteral("HH:mm")).isValid()) return true;
    if (QDateTime::fromString(to, Qt::ISODateWithMs).isValid()) return true;
    if (QDateTime::fromString(to, Qt::ISODate).isValid()) return true;
    return false;
}
}   // namespace

QJsonObject params()
{
    QSettings s;
    const QString raw = s.value(QString::fromUtf8(kKey)).toString();
    if (raw.isEmpty()) return {};
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    if (!doc.isObject()) return {};
    const QJsonObject o = doc.object();
    // 校验：to 必须仍是合法时刻，过期/写坏 ⇒ 回落未配置（不让坏配置一直显示一个错目标）
    if (!validTo(o.value(QStringLiteral("to")).toString())) return {};
    return o;
}

QJsonObject set(const QJsonObject &in)
{
    const QString to = in.value(QStringLiteral("to")).toString().trimmed();
    const bool clear = in.value(QStringLiteral("clear")).toBool(false)
                       || to.isEmpty();
    QSettings s;
    if (clear) {
        s.remove(QString::fromUtf8(kKey));
        return QJsonObject{{QStringLiteral("ok"), true}};
    }
    if (!validTo(to)) {
        return QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"),
             QStringLiteral("to 既不是 HH:MM 也不是 ISO8601 时刻：%1").arg(to)},
        };
    }
    QJsonObject o;
    o.insert(QStringLiteral("to"), to);
    const QString label = in.value(QStringLiteral("label")).toString().trimmed();
    if (!label.isEmpty()) o.insert(QStringLiteral("label"), label);
    const QString title = in.value(QStringLiteral("title")).toString().trimmed();
    if (!title.isEmpty()) o.insert(QStringLiteral("title"), title);
    s.setValue(QString::fromUtf8(kKey),
               QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
    return QJsonObject{{QStringLiteral("ok"), true}};
}

}   // namespace CountdownTarget
