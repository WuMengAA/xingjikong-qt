// OTA 灰度渠道持久化（2026-10-30 从 main.cpp 整块迁出；逻辑与原地逐字一致，仅改声明位置）。
#include "channel_persistence.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>

// agentEnvPath() 定义在 main.cpp（数据目录 agent.env 路径；2026-10-30 提升为文件作用域外部链接）。
extern QString agentEnvPath();

QString channelFilePath()
{
    // 与 agent.env 同目录：配置在哪，渠道就在哪，不会出现两者分裂。
    return QFileInfo(agentEnvPath()).path() + QStringLiteral("/channel.txt");
}

QString resolvedChannel()
{
    // 解析链路：env → channel.txt → stable；每一跳都过一遍 channelCanonical()，
    // 旧值（rc/gray）在这里被就地翻译成新档（beta/alpha），落盘文件下次 persist 时改写。
    auto pick = [](const QString &raw) -> QString {
        const QString c = channelCanonical(raw);
        if (!c.isEmpty()) return c;
        // 认不出的渠道名绝不能静默当 stable 用（那会让一台手改错 channel.txt 的机器
        // 永远以为自己在正式渠道、实际拿到的是灰度包），但也不能让设备彻底查不到更新。
        // 折中：喊出来 + 回落 stable —— 现场看日志能定位，设备不至于停在旧版本。
        qWarning("[agent-qt] ⚠ 未知更新渠「%s」—— 回落到 stable（合法值：alpha/beta/stable）",
                 qPrintable(raw));
        return QStringLiteral("stable");
    };
    const QString env = qEnvironmentVariable("STE_QT_CHANNEL").trimmed();
    if (!env.isEmpty()) return pick(env);
    QFile f(channelFilePath());
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString v = QString::fromUtf8(f.readAll()).trimmed();
        f.close();
        if (!v.isEmpty()) return pick(v);
    }
    return QStringLiteral("stable");
}

// 渠道名归一（2026-11-02 随渠道自动晋升一并改口径）。
// 旧三档 stable/rc/gray 换成新三档 stable/beta/alpha —— 但不许让**已落盘旧值的机器**
// 变成"查不到清单 = 永远无更新"。所以这里做**读时迁移**：把旧名字翻成新名字，
// 盘上那份等下次 persistChannel 时自然被改写。
// 与云端 ota-promote.js normalizeChannelName 同表；不同的只有一点：这里未知值返回空串
// （表示"不认识"，由调用方决定回弹而不是静默当 stable —— 用户手改 channel.txt 写错时
// 应该看得见，而不是被悄悄当成正式渠道）。
QString channelCanonical(const QString &raw)
{
    const QString v = raw.trimmed().toLower();
    if (v == QLatin1String("alpha") || v == QLatin1String("gray")) return QStringLiteral("alpha");
    if (v == QLatin1String("beta")  || v == QLatin1String("rc"))   return QStringLiteral("beta");
    if (v == QLatin1String("stable") || v.isEmpty())               return QStringLiteral("stable");
    return QString();
}

// 把当前生效渠道写回 channel.txt（幂等：内容相同则不动盘）
void persistChannel(const QString &ch)
{
    const QString p = channelFilePath();
    QFile f(p);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString cur = QString::fromUtf8(f.readAll()).trimmed();
        f.close();
        if (cur == ch) return;
    }
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        f.write(ch.toUtf8());
        f.write("\n");
        f.close();
        return;
    }
    // 写不出必须喊出来：渠道没持久化 = 重启/升级后悄悄回落 stable，
    // 现场表现和"灰度机自己退出灰度"一模一样，不写日志事后根本查不到（fail-silent 红线）。
    qWarning("[agent-qt] ⚠ channel.txt 写不出（%s：%s）—— 渠道不会持久化，重启/升级后可能回落 stable",
             qPrintable(p), qPrintable(f.errorString()));
}
