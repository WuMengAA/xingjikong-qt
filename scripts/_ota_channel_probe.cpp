// QML 侧 OTA 渠道晋升探针（2026-11-02 · 不入库）
//
// 验三件事，都是"界面真能用"而不是"能编译"：
//   ① QML 里 `import Stelarith.Ota 1.0` 能不能解析（模块没注册 ⇒ 设置页整页空白）
//   ② 云端回包的 latest.channel/publishedAt/promotion → 文案 / 档位 / 可见性 三态正确
//   ③ 旧云端（无 promotion 字段）时文案必须为空串（界面隐藏整行，而不是显示"--"）
//
// 时刻表用**云端同一组常量**：alpha 立即可见、beta 满 5h、stable 满 24h。
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>

#include <cstdio>

#include "../src/ota/ota_channel_state.h"

static int g_fail = 0;
static void chk(const char *label, bool ok, const QString &detail = QString())
{
    std::printf("%s %s%s\n", ok ? "PASS" : "FAIL", label,
                detail.isEmpty() ? "" : ("  " + detail).toUtf8().constData());
    if (!ok) ++g_fail;
}

/** 造一份与云端 ota.js latestFor 同形的回包（时间以"现在"为基准往前推 ageMs）。 */
static QJsonObject makeLatest(const QString &channel, qint64 ageMs, bool inAlpha, bool inBeta,
                              bool inStable)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString pub = QDateTime::fromMSecsSinceEpoch(now - ageMs)
                            .toUTC().toString(Qt::ISODateWithMs);
    QJsonObject promo;
    promo.insert(QStringLiteral("version"), QStringLiteral("0.6.61"));
    promo.insert(QStringLiteral("publishedAt"), pub);
    promo.insert(QStringLiteral("ageMs"), double(ageMs));
    promo.insert(QStringLiteral("inAlpha"), inAlpha);
    promo.insert(QStringLiteral("inBeta"), inBeta);
    promo.insert(QStringLiteral("inStable"), inStable);
    promo.insert(QStringLiteral("betaAt"),
                 QDateTime::fromMSecsSinceEpoch(now - ageMs + 5LL * 3600 * 1000)
                     .toUTC().toString(Qt::ISODateWithMs));
    promo.insert(QStringLiteral("stableAt"),
                 QDateTime::fromMSecsSinceEpoch(now - ageMs + 24LL * 3600 * 1000)
                     .toUTC().toString(Qt::ISODateWithMs));

    QJsonObject latest;
    latest.insert(QStringLiteral("version"), QStringLiteral("0.6.61"));
    latest.insert(QStringLiteral("channel"), channel);
    latest.insert(QStringLiteral("publishedAt"), pub);
    latest.insert(QStringLiteral("promotion"), promo);
    return latest;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ── ① QML 模块注册（模块没注册 = 设置页整页空白，是最致命的失败态）──
    QQmlEngine engine;
    QQmlComponent comp(&engine);
    comp.setData(R"QML(
import QtQml
import Stelarith.Ota 1.0

QtObject {
    property string text:    OtaChannelState.promotionText
    property string tier:    OtaChannelState.tier
    property bool   visible: OtaChannelState.visibleNow
}
)QML", QUrl(QStringLiteral("qrc:/OtaChannelProbe.qml")));
    if (comp.isError()) {
        for (const QQmlError &e : comp.errors())
            std::printf("QML ERROR: %s\n", e.toString().toUtf8().constData());
        chk("QML import Stelarith.Ota + 读三个属性", false);
        return 20;
    }
    QObject *root = comp.create();
    chk("QML import Stelarith.Ota + 读三个属性", root != nullptr);
    if (!root) return 20;

    // ── ③ 旧云端：无 promotion 字段 → 空文案（隐藏整行）──
    {
        QJsonObject legacy;
        legacy.insert(QStringLiteral("version"), QStringLiteral("0.6.61"));
        otaChannelRemember(legacy);
        chk("旧云端（无 promotion）→ 文案为空串", otaPromotionText().isEmpty(),
            QStringLiteral("实际=「%1」").arg(otaPromotionText()));
        chk("旧云端 → 不阻断升级（visibleNow 恒 true）", otaPromotionVisibleNow());
        chk("旧云端 → QML 侧同为空串",
            root->property("text").toString().isEmpty());
    }

    // ── ② 刚发 alpha（age=10 分钟）：只在 alpha 可见，文案要点出"还要等多久进 beta"──
    {
        otaChannelRemember(makeLatest(QStringLiteral("alpha"), 10LL * 60 * 1000,
                                      true, false, false));
        const QString t = otaPromotionText();
        std::printf("[文案] alpha/10min → %s\n", t.toUtf8().constData());
        chk("alpha 档位名", otaChannelTierName(QStringLiteral("alpha")) == QStringLiteral("alpha"));
        chk("alpha 立即可见", otaPromotionVisibleNow());
        chk("alpha 文案含「灰度 alpha」", t.contains(QStringLiteral("灰度 alpha")));
        chk("alpha 文案含「已发布」", t.contains(QStringLiteral("已发布")));
        chk("alpha 文案含「进测试 beta」", t.contains(QStringLiteral("进测试 beta")));
        chk("alpha 文案不含「进正式 stable」（还早）",
            !t.contains(QStringLiteral("进正式 stable")));
    }

    // ── ②2 beta 档问：同一个构建在 beta 渠道此刻**还看不到**（还没满 5h）──
    {
        otaChannelRemember(makeLatest(QStringLiteral("beta"), 4LL * 3600 * 1000,
                                      true, false, false));
        chk("beta 档：未满 5h → 不可见", !otaPromotionVisibleNow());
        chk("gray 归一为 alpha", otaChannelTierName(QStringLiteral("gray")) == QStringLiteral("alpha"));
        chk("rc 归一为 beta", otaChannelTierName(QStringLiteral("rc")) == QStringLiteral("beta"));
        chk("未知归一为 stable（不含糊落 alpha）",
            otaChannelTierName(QStringLiteral("nightly")) == QStringLiteral("stable"));
    }

    // ── ②3 满 5h：beta 可见，文案改口说"还要等进正式"──
    {
        otaChannelRemember(makeLatest(QStringLiteral("beta"), 6LL * 3600 * 1000,
                                      true, true, false));
        const QString t = otaPromotionText();
        std::printf("[文案] beta/6h → %s\n", t.toUtf8().constData());
        chk("beta 满 5h → 可见", otaPromotionVisibleNow());
        chk("beta 文案含「测试 beta」", t.contains(QStringLiteral("测试 beta")));
        chk("beta 文案含「进正式 stable」", t.contains(QStringLiteral("进正式 stable")));
    }

    // ── ②4 满 24h：stable 可见；全到了就不再念"还要等"──
    {
        otaChannelRemember(makeLatest(QStringLiteral("stable"), 26LL * 3600 * 1000,
                                      true, true, true));
        const QString t = otaPromotionText();
        std::printf("[文案] stable/26h → %s\n", t.toUtf8().constData());
        chk("stable 满 24h → 可见", otaPromotionVisibleNow());
        chk("stable 文案含「正式 stable」", t.contains(QStringLiteral("正式 stable")));
        chk("全到位后不再提「约…后进」", !t.contains(QStringLiteral("后进")));
        chk("QML 侧同步到最终文案", root->property("text").toString() == t);
        chk("QML 侧 tier=stable", root->property("tier").toString() == QStringLiteral("stable"));
    }

    // ── ③2 stable 渠道问一个刚发 alpha 15 分钟的构建：必须**不可见** ──
    //     这是本功能的核心语义：构建没变，只是还没轮到正式渠道。
    {
        otaChannelRemember(makeLatest(QStringLiteral("stable"), 15LL * 60 * 1000,
                                      true, false, false));
        chk("stable 档：刚发 15 分钟 → 不可见（等晋升）", !otaPromotionVisibleNow());
        chk("QML 侧 visible=false", root->property("visible").toBool() == false);
        chk("文案仍能显示进度（不是空串）", !otaPromotionText().isEmpty());
    }

    std::printf("\n%s\n", g_fail == 0 ? "ALL PASS" : "HAS FAILURES");
    return g_fail == 0 ? 10 : 20;
}
