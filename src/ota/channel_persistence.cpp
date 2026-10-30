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
    const QString env = qEnvironmentVariable("STE_QT_CHANNEL").trimmed();
    if (!env.isEmpty()) return env;
    QFile f(channelFilePath());
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString v = QString::fromUtf8(f.readAll()).trimmed();
        f.close();
        if (!v.isEmpty()) return v;
    }
    return QStringLiteral("stable");
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
