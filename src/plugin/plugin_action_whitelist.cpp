#include "plugin_action_whitelist.h"
#include "../action/execute_action.h"
#include "../core/ExecOut.h"

#include <QDebug>

namespace PluginActionWhitelist {

const QStringList &allowed()
{
    // 顺序有意义：错误提示按此顺序列出，排在前面的是"最该先试"的。
    static const QStringList kList = {
        QStringLiteral("ping"),
        QStringLiteral("time_status"),
        QStringLiteral("process_list"),
        QStringLiteral("log_tail"),
    };
    return kList;
}

bool isAllowed(const QString &action) { return allowed().contains(action.trimmed()); }

PluginResult run(const QString &action, const QJsonObject &params)
{
    PluginResult out;
    const QString a = action.trimmed();
    if (!isAllowed(a)) {
        out.result = QStringLiteral("denied");
        out.error = QStringLiteral("指令 %1 不在插件可用白名单内（可用：%2）")
                        .arg(a, allowed().join(QStringLiteral(", ")));
        // 打日志而不是静默：越权尝试是**安全信号**，出问题时这条是唯一的线索。
        qWarning("[plugin] 拒绝越权指令 action=%s", qPrintable(a));
        return out;
    }
    const ExecOut r = executeAction(a, params);
    out.result = r.result;
    out.error = r.error;
    out.data = r.data;
    return out;
}

} // namespace PluginActionWhitelist
