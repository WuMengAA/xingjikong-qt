#pragma once
// 星集控被控端 · Local API 鉴权（2026-10-31 · 本地 127.0.0.1:8080 网关）
//
// 职责（纯鉴权，不做 HTTP）：
//   · token 签发/加载：首次生成 32 字节随机 token（64 hex），落盘 <configdir>/localapi.token
//   · token 校验：常量时间比较，防时序侧信道
//   · 限流：每客户端 IP 固定窗口令牌桶（默认 120 req / 60s）
//   · 失败审计：鉴权失败 / 限流触发 → 追加 <configdir>/localapi-audit.log
//
// 与云端 per-device 口径一致（云 src/perms.js canAccess）：
//   本地 API 服务的是**本机设备**，身份 = 本机 classcode/devrole（classcode.txt / devrole.txt，
//   与 main.cpp 读同一真源，见 localapi_server.cpp）；token 证明调用方获准访问本设备，
//   设备身份不靠请求体自报（对应云端口径「schoolId 只能从票来，绝不接受客户端参数」）。
//
// 边界（写死，不靠配置）：本类不监听任何端口；端口绑定在 LocalApiServer（仅 127.0.0.1）。
// 本类与 QtNetwork 无依赖（只 QtCore），可单测。

#include <QString>
#include <QHash>

class LocalApiAuth
{
public:
    // configDir = %LOCALAPPDATA%\xingjikong\（与 main.cpp 数据目录同根）
    explicit LocalApiAuth(const QString &configDir);
    ~LocalApiAuth() = default;

    // 加载或生成 token（幂等）。失败（无法落盘且无旧值）返回空串 ——
    // 服务照常起，但一切受保护端点恒拒（fail-closed，绝不"无 token 放行"）。
    QString loadOrCreateToken();

    // 常量时间校验：provided == 已签发 token。
    bool validate(const QString &provided) const;

    // 限流：true=放行，false=超限（已记审计）。
    bool allow(const QString &clientIp);

    // 写一条失败审计（鉴权失败 / 限流 / 方法不允许 等），UTF-8 追加。
    void audit(const QString &clientIp, const QString &methodPath, const QString &reason);

    QString token() const { return m_token; }
    QString auditPath() const { return m_auditPath; }

private:
    QString m_configDir;
    QString m_token;
    QString m_tokenPath;
    QString m_auditPath;
    // 每客户端 IP 固定窗口限流（回环场景单 IP，但仍按 IP 记，口径通用）
    struct Window { qint64 startMs = 0; int count = 0; };
    QHash<QString, Window> m_windows;
};
