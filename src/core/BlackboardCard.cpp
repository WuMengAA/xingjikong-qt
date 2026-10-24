// 星集控被控端 · 发布中心卡片 → 通知参数（实现）
// 契约与「为什么独立成件」见 BlackboardCard.h 文件头。
#include "BlackboardCard.h"

#include <QJsonObject>
#include <QString>

namespace BlackboardCard {

bool toNotifyParams(const QJsonObject &payload, QJsonObject &out, QString *why)
{
    const QString cardType = payload.value(QStringLiteral("type")).toString().trimmed();
    const QString title = payload.value(QStringLiteral("title")).toString().trimmed();
    const QString body = payload.value(QStringLiteral("body")).toString().trimmed();

    auto ignore = [why](const QString &reason) {
        if (why) *why = reason;
        return false;
    };

    // 空卡片不展示。云端 adminPublish 已 .trim() 判空，这里是端侧兜底：
    // 重放/脏数据不该在教室大屏上弹一个空白全屏窗（那比不弹更糟）。
    if (title.isEmpty() && body.isEmpty())
        return ignore(QStringLiteral("空卡片（无标题无正文）"));

    const bool fullscreen = (cardType == QStringLiteral("fullscreen-notice"))
                            || (cardType == QStringLiteral("homework"));
    const bool danmaku = (cardType == QStringLiteral("student-notice"))
                         || (cardType == QStringLiteral("class-notice"));
    // 未知类型宁可不展示，也不猜一个形态 —— 猜错就是把老师的内容投错地方。
    if (!fullscreen && !danmaku) {
        return ignore(QStringLiteral("未知卡片类型：%1")
                          .arg(cardType.isEmpty() ? QStringLiteral("（空）") : cardType));
    }

    out = QJsonObject();
    if (fullscreen) {
        // 全屏窗 title/content 分两处显示 ⇒ 正文**完整**保留（云端上限 4000 字）。
        // 老师只填正文时给一个中性抬头：notifyFromParams 的 title 是必填（空则直接报错）。
        out.insert(QStringLiteral("kind"), QStringLiteral("fullscreen"));
        out.insert(QStringLiteral("title"), title.isEmpty() ? QStringLiteral("班级通知") : title);
        out.insert(QStringLiteral("content"), body);
    } else {
        // 跑马灯是单行：notifyFromParams 拼的是「title：content」。
        // 老师只填正文时**不能**把正文同时塞进 title 和 content（会滚出两遍），
        // 所以此时 title 取正文前 40 字、content 留空（弹幕本身还有 80 字截断）。
        out.insert(QStringLiteral("kind"), QStringLiteral("danmaku"));
        out.insert(QStringLiteral("title"), title.isEmpty() ? body.left(40) : title);
        out.insert(QStringLiteral("content"), title.isEmpty() ? QString() : body);
    }
    return true;
}

} // namespace BlackboardCard
