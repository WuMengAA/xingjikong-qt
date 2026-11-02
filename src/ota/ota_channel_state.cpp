// 渠道自动晋升 · 端侧展示（2026-11-02）。理由与契约见头文件；本条只做"翻译人话"。
#include "ota_channel_state.h"

#include <QDateTime>
#include <QJsonValue>
#include <QtQml/qqml.h>

namespace {

// 最近一次回包的渠道快照。**只存 latest 三件套**，不缓存整包 ——
// 整包会把 notes/mandatory 等另一条链的数据复制一份，两边一旦不同步就是"界面说 A、升级按 B"。
QJsonObject g_snap;

const char *kPromotionKey = "promotion";

qint64 num(const QJsonObject &o, const char *k)
{
    const QJsonValue v = o.value(QLatin1String(k));
    return v.isDouble() ? static_cast<qint64>(v.toDouble()) : -1;
}

bool flag(const QJsonObject &o, const char *k)
{
    return o.value(QLatin1String(k)).toBool(false);
}

/** 毫秒 → 「42 分钟」/「3.2 小时」/「2 天」。只用于给现场看，不参与任何判定。 */
QString humanMinutes(qint64 ms)
{
    if (ms <= 0) return QStringLiteral("0 分钟");
    const qint64 min = ms / 60000;
    if (min < 60) return QStringLiteral("%1 分钟").arg(min);
    const qint64 hour = min / 60;
    if (hour < 48) {
        // 一位小数：4.7 小时比"4 小时 42 分钟"更像现场口头语，也不占地方
        const qint64 tenth = (min % 60) / 6;
        return QStringLiteral("%1.%2 小时").arg(hour).arg(tenth);
    }
    return QStringLiteral("%1 天").arg(hour / 24);
}

}   // namespace

void otaChannelRemember(const QJsonObject &latest)
{
    // 旧云端没有 promotion/channel/publishedAt ⇒ 明确清空，而不是保留上一轮的快照：
    // 保留会让界面继续显示一个"云端已经不这么算了"的时间表，比空着更误导。
    if (!latest.contains(QLatin1String(kPromotionKey))
        && !latest.contains(QLatin1String("publishedAt"))) {
        g_snap = QJsonObject();
        return;
    }
    QJsonObject snap;
    snap.insert(QStringLiteral("channel"), latest.value(QLatin1String("channel")));
    snap.insert(QStringLiteral("publishedAt"), latest.value(QLatin1String("publishedAt")));
    snap.insert(QLatin1String(kPromotionKey), latest.value(QLatin1String(kPromotionKey)));
    g_snap = snap;
    OtaChannelState::instance()->refresh();
}

QString otaChannelTierName(const QString &raw)
{
    // 与云端 ota-promote.js normalizeChannelName 同表：alpha|gray→alpha、beta|rc→beta、其余→stable。
    // ⚠️ 两份实现必须同表，任一侧改了另一侧要跟着改；本函数只用于**展示**，
    //    判定权仍在云端（端侧算错了最多显示错字，不会装错包）。
    const QString v = raw.trimmed().toLower();
    if (v == QLatin1String("alpha") || v == QLatin1String("gray")) return QStringLiteral("alpha");
    if (v == QLatin1String("beta") || v == QLatin1String("rc")) return QStringLiteral("beta");
    return QStringLiteral("stable");
}

QString otaPromotionText()
{
    const QJsonObject p = g_snap.value(QLatin1String(kPromotionKey)).toObject();
    if (p.isEmpty()) return QString();

    const QString tier = otaChannelTierName(g_snap.value(QStringLiteral("channel")).toString());
    const QString tierLabel = tier == QLatin1String("alpha") ? QStringLiteral("灰度 alpha")
                            : tier == QLatin1String("beta")  ? QStringLiteral("测试 beta")
                                                            : QStringLiteral("正式 stable");
    const qint64 age = num(p, "ageMs");
    QString out = tierLabel;
    if (age >= 0) out += QStringLiteral(" · 已发布 %1").arg(humanMinutes(age));

    // 只提示"还没到"的下一档：都到了就不必再念一遍（现场看"已是最新"即可）。
    if (!flag(p, "inBeta") && age >= 0) {
        const qint64 betaAt = QDateTime::fromString(
            p.value(QLatin1String("betaAt")).toString(), Qt::ISODateWithMs).toMSecsSinceEpoch();
        if (betaAt > 0) out += QStringLiteral(" · 约 %1 后进测试 beta")
                                    .arg(humanMinutes(betaAt - QDateTime::currentMSecsSinceEpoch()));
    } else if (!flag(p, "inStable") && age >= 0) {
        const qint64 stableAt = QDateTime::fromString(
            p.value(QLatin1String("stableAt")).toString(), Qt::ISODateWithMs).toMSecsSinceEpoch();
        if (stableAt > 0) out += QStringLiteral(" · 约 %1 后进正式 stable")
                                      .arg(humanMinutes(stableAt - QDateTime::currentMSecsSinceEpoch()));
    }
    return out;
}

bool otaPromotionVisibleNow()
{
    const QJsonObject p = g_snap.value(QLatin1String(kPromotionKey)).toObject();
    if (p.isEmpty()) return true;      // 旧云端 / 未查到：不阻断既有升级链路
    const QString tier = otaChannelTierName(g_snap.value(QStringLiteral("channel")).toString());
    if (tier == QLatin1String("alpha")) return flag(p, "inAlpha");
    if (tier == QLatin1String("beta"))  return flag(p, "inBeta");
    return flag(p, "inStable");
}

QJsonObject otaPromotionSnapshot()
{
    return g_snap;
}

OtaChannelState *OtaChannelState::instance()
{
    // 静态初始化早于 main ⇒ 任何 QML 引擎都拿得到（与 MicSelector 同一范式）。
    // 不用 Q_GLOBAL_STATIC：这里的生命周期到进程结束即可，且需要非 const 的 emit。
    static OtaChannelState s_inst;
    return &s_inst;
}

void OtaChannelState::refresh()
{
    // 渠道进度是"订阅式"数据：没变就不 emit，否则 QML 绑定每秒重算一次（时间在走，
    // 但文本精度只到分钟级，重算纯属浪费）。比对用文本+档位+可见性三者，
    // 保证"只剩 3 分钟进 beta"这种文本变化能刷出来。
    static QString s_text, s_tier;
    static bool s_visible = true;
    const QString t = promotionText();
    const QString k = tier();
    const bool v = visibleNow();
    if (t == s_text && k == s_tier && v == s_visible) return;
    s_text = t;
    s_tier = k;
    s_visible = v;
    emit changed();
}

void OtaChannelState::registerQmlType()
{
    // 幂等：静态初始化 + 装配期各调一次，重复注册视为已注册（照抄 MicSelector 范式）。
    static bool done = false;
    if (done) return;
    done = true;
    qmlRegisterSingletonInstance("Stelarith.Ota", 1, 0, "OtaChannelState", OtaChannelState::instance());
}

// 为什么放在静态初始化而不是 main() 装配段（与 src/noise/mic_selector.cpp:100 同因）：
//   QML 里 `import Stelarith.Ota 1.0` 在模块未注册时是**硬错误**（整页空白，且不报业务错）；
//   而静态初始化早于 main ⇒ 探针/任何自建引擎都拿得到。qmlRegisterSingletonInstance 只写
//   Qt 的局部静态类型表，不依赖 QCoreApplication 已创建。
namespace {
struct QmlTypeAutoReg {
    QmlTypeAutoReg() { OtaChannelState::registerQmlType(); }
};
[[maybe_unused]] const QmlTypeAutoReg g_qmlTypeAutoReg;
}   // namespace
