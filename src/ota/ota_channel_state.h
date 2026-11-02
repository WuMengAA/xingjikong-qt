#pragma once
#include <QObject>
#include <QString>
#include <QJsonObject>

// 渠道自动晋升 · 端侧展示（2026-11-02）
//
// 用户点单语义（原文要点）：「构建不变、版本号不变，**只是渠道状态改变**」；
//   构建发到 alpha（灰度）→ 5 小时后自动出现在 beta（测试）→ 发布 24 小时后自动出现在 stable（正式）；
//   端侧可以选择「保持此渠道」持续跟进该渠道。
//
// 为什么单独一个模块而不是塞进 ota_update.cpp：
//   ① 这条链的**唯一职责**是把云端算好的晋升时间表翻译成一句人话 —— 与"要不要升级"的
//      版本比较逻辑完全无关；混进 applyUpdateInfo 会让那个函数同时管两件事（判定 + 展示）。
//   ② main.cpp 有行数台账（只降不升）、agent_panel.cpp 已在白名单里超线（加一行即门禁 error），
//      所以这里的取数入口必须是**零调用点**的自足模块：agent_ui.h 的 Q_PROPERTY 直接 inline 调它。
//
// 数据来源：`GET /api/public/ota` 回包的 `latest.channel / latest.publishedAt / latest.promotion`
//   （云端 ota.js latestFor 附带，见 Stelarith-cloud/src/ota-promote.js）。
//   promotion = { version, publishedAt, ageMs, inAlpha, inBeta, inStable, betaAt, stableAt }。
//   旧云端（不带这三个字段）时本模块全部函数返回空串 —— 界面不显示该行，不报错、不猜。

/** 记下最近一次 OTA 回包里的渠道三件套（在 ota_download 解析回包处调用一次）。
 *  传整个 `latest` 对象即可；缺字段视为"旧云端"，清空快照。 */
void otaChannelRemember(const QJsonObject &latest);

/** 本机当前查询渠道归一后的档位名：alpha / beta / stable（未知一律 stable）。
 *  云端与端侧共用同一套归一规则（alpha|gray→alpha、beta|rc→beta、其余→stable）。 */
QString otaChannelTierName(const QString &raw);

/** 一句人话，如「灰度 alpha · 已发布 42 分钟 · 约 4 小时后进测试 beta · 约 23 小时后进正式」。
 *  没有渠道信息（旧云端 / 还没查到）时返回空串。 */
QString otaPromotionText();

/** 这个构建此刻在本机渠道是否已可见（true = 能装；false = 还要等晋升）。
 *  无 snapshot 时按"可见"处理（不阻断既有升级路径）。 */
bool otaPromotionVisibleNow();

/** 给健康快照/事件流水复用的原始对象（不含时间换算，便于云端核对）。 */
QJsonObject otaPromotionSnapshot();

/** QML 侧唯一入口（2026-11-02）。
 *
 * 为什么不挂在 AgentUi 上：`src/ui/agent_panel.cpp`（645 行）在超线白名单里，
 * 三段式门禁对"白名单内还变胖"直接判 error —— 加一行就红。而 `src/agent_ui.h`
 * 同理（658 行）。故本对象按 MicSelector 的既有范式（`src/noise/mic_selector.cpp:101`）
 * 用 `qmlRegisterSingletonInstance` 在静态初始化期注册：**主代理文件零改动**，
 * QML 里 `import Stelarith.Ota` 即可绑定。
 *
 * 职责边界：只暴露"渠道进度"这一件事（一句人话 + 档位 + 此刻是否可见）。
 * 版本比较 / 是否要升级仍归 ota_update.cpp，两边不重叠。
 */
class OtaChannelState : public QObject
{
    Q_OBJECT
    /** 「灰度 alpha · 已发布 42 分钟 · 约 4.7 小时后进测试 beta」；无信息时为空串
     *  （QML 侧据此隐藏整行，而不是显示一个"--"吓唬现场）。 */
    Q_PROPERTY(QString promotionText READ promotionText NOTIFY changed)
    /** 归一档位名：alpha / beta / stable（展示用；判定权在云端）。 */
    Q_PROPERTY(QString tier READ tier NOTIFY changed)
    /** 本机渠道此刻是否已能看到这个构建（false = 正在等晋升）。 */
    Q_PROPERTY(bool visibleNow READ visibleNow NOTIFY changed)
public:
    static OtaChannelState *instance();

    QString promotionText() const { return otaPromotionText(); }
    QString tier() const { return otaChannelTierName(otaPromotionSnapshot()
                                  .value(QStringLiteral("channel")).toString()); }
    bool visibleNow() const { return otaPromotionVisibleNow(); }

    /** otaChannelRemember 写入后调一次（只有真正变了才 emit，避免每秒刷绑定）。 */
    void refresh();

    /** 模块级注册（幂等）。静态初始化期自动调用一次，见 .cpp 末尾说明。 */
    static void registerQmlType();

signals:
    void changed();
};
