#include "timesync_domain.h"

#include <QCoreApplication>
#include <QFile>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUdpSocket>
#include <QDebug>

namespace TimesyncDomain {
namespace {

// ── 参数（改这里 = 改策略，别散落在函数体里）──────────────────────────
constexpr int    kCycleMs        = 30 * 60 * 1000;   // 30 分钟一轮（接续清单口径）
constexpr int    kFirstDelayMs   = 5000;             // 首轮延迟：等网络栈/事件循环
constexpr int    kAttemptMs      = 3000;             // 单台服务器超时
constexpr qint64 kNtpEpochSec    = 2208988800LL;     // 1900-01-01 → 1970-01-01
constexpr int    kMaxOffsetMin   = 720;              // 人工偏移上限（±12h，超过必是误操作）

// 服务器顺序：阿里云优先（国内解析稳），中国授时兜底。
// ⚠️ 都是**域名**：机房 NAT 后 IP 常变，写死 IP 会在换网段那天集体失效。
const char *kServers[] = {"ntp.aliyun.com", "cn.ntp.org.cn"};
constexpr int kServerCount = int(sizeof(kServers) / sizeof(kServers[0]));

QString  g_file;
Status   g_status;
QTimer  *g_cycle     = nullptr;   // 30 分钟周期
QUdpSocket *g_sock   = nullptr;   // SNTP 通道（复用，不每次新建）
QTimer  *g_attempt   = nullptr;   // 单次尝试超时
int      g_serverIdx = 0;
qint64   g_t1        = 0;         // 请求发出时刻（客户端，用于算 RTT 的一半）
bool     g_running   = false;     // 同步中（防重入：连点/周期撞车只跑一个）
std::function<void()> g_onChanged;

void notifyChanged()
{
    if (g_onChanged) g_onChanged();
}

// ── 持久化（只存人工偏移，理由见头文件）────────────────────────────
void saveConfig()
{
    if (g_file.isEmpty()) return;
    QJsonObject o;
    o.insert(QStringLiteral("offsetMin"), g_status.offsetMin);
    QFile f(g_file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("[timesync] 偏移持久化失败（timesync.json 打不开）—— 本次运行内有效");
        return;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void loadConfig()
{
    if (g_file.isEmpty()) return;
    QFile f(g_file);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;
    const int m = doc.object().value(QStringLiteral("offsetMin")).toInt(0);
    if (m >= -kMaxOffsetMin && m <= kMaxOffsetMin) g_status.offsetMin = m;
}

// ── 收尾：一次同步结束（成败都走这里，保证 g_running 一定复位）──────
void finish(bool ok, const QString &err)
{
    g_running = false;
    if (g_attempt) g_attempt->stop();
    if (ok) {
        g_status.ntpOk = true;
        g_status.lastSyncMs = QDateTime::currentMSecsSinceEpoch();
        g_status.error.clear();
        qInfo("[timesync] NTP 成功：%s | 系统钟差 %+lld ms | 人工偏移 %+d 分 | 总偏移 %+lld ms",
              qPrintable(g_status.server), (long long)g_status.deltaMs,
              g_status.offsetMin, (long long)skewMs());
    } else {
        // 失败 = **回落系统时间**（deltaMs 归零）。留着旧 delta 才是真坑：
        // 机器重启后网络一直不通时，旧误差会当权威用，越用越偏且日志看不出来。
        g_status.ntpOk = false;
        g_status.deltaMs = 0;
        g_status.error = err;
        qWarning("[timesync] NTP 失败（%s）→ 回落系统时间；人工偏移 %+d 分仍然生效",
                 qPrintable(err), g_status.offsetMin);
    }
    notifyChanged();
}

void tryNextServer();

// 收到回包：SNTP 报文的 transmit timestamp 在 40..47
void parseReply(const QByteArray &d)
{
    if (d.size() < 48) {
        qWarning("[timesync] %s 回包只有 %d 字节（不足 48），丢弃",
                 qPrintable(g_status.lastAttempt), int(d.size()));
        tryNextServer();
        return;
    }
    const auto *p = reinterpret_cast<const uchar *>(d.constData());
    const int stratum = p[1];
    auto be32 = [](const uchar *b) {
        return (quint32(b[0]) << 24) | (quint32(b[1]) << 16) | (quint32(b[2]) << 8) | quint32(b[3]);
    };
    // stratum 0 = Kiss-o'-Death（服务器拒绝服务）/ 16 = 未同步，都不可用
    if (stratum == 0 || stratum >= 16) {
        qWarning("[timesync] %s 返回 stratum=%d（拒绝服务/未同步），换下一台",
                 qPrintable(g_status.lastAttempt), stratum);
        tryNextServer();
        return;
    }
    const qint64 secs = qint64(be32(p + 40));
    const quint32 frac = be32(p + 44);
    if (secs < kNtpEpochSec) {   // 1970 之前 = 报文异常
        qWarning("[timesync] %s 的 timestamp 非法（secs=%lld），换下一台",
                 qPrintable(g_status.lastAttempt), (long long)secs);
        tryNextServer();
        return;
    }
    const qint64 t3 = (secs - kNtpEpochSec) * 1000
                    + qint64((quint64(frac) * 1000ULL) >> 32);   // 小数部分 → ms
    const qint64 t4 = QDateTime::currentMSecsSinceEpoch();
    // 单程延迟按 (t4 − t1)/2 估，取中值对齐：真时 ≈ t3 + 半程
    const qint64 delta = t3 - (g_t1 + t4) / 2;

    g_status.deltaMs = delta;
    g_status.server = g_status.lastAttempt;
    if (qAbs(delta) > 3600LL * 1000) {
        // 差 > 1 小时多半是"这台机器的钟真的错得离谱"（或虚拟机挂起过），
        // 照实校正但必须留一条显眼的日志，别让人以为是我们的 bug。
        qWarning("[timesync] 系统钟与 NTP 相差 %+lld 秒，已按 NTP 校正（检查主板 RTC/时区）",
                 (long long)(delta / 1000));
    }
    finish(true, QString());
}

void tryNextServer()
{
    if (!g_running) return;
    if (g_serverIdx >= kServerCount) {
        finish(false, QStringLiteral("所有 NTP 服务器无响应（UDP 123 可能被机房拦截）"));
        return;
    }
    const QString host = QString::fromLatin1(kServers[g_serverIdx]);
    g_status.lastAttempt = host;
    // 异步解析：QHostInfo::fromName 是阻塞的，机房 DNS 慢时会卡住 UI 线程（岛会顿一下）
    QHostInfo::lookupHost(host, QCoreApplication::instance(),
        [host](const QHostInfo &info) {
            if (!g_running) return;
            if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
                qWarning("[timesync] %s 解析失败（%s），换下一台",
                         qPrintable(host), qPrintable(info.errorString()));
                ++g_serverIdx;
                tryNextServer();
                return;
            }
            QHostAddress addr;
            for (const QHostAddress &a : info.addresses()) {
                if (a.protocol() == QAbstractSocket::IPv4Protocol) { addr = a; break; }
            }
            if (addr.isNull()) addr = info.addresses().first();

            // SNTP 请求：48 字节，首字节 LI=0/VN=4/Mode=3 ⇒ 0x23，其余全零
            QByteArray req(48, '\0');
            req[0] = char(0x23);
            g_t1 = QDateTime::currentMSecsSinceEpoch();
            const qint64 sent = g_sock->writeDatagram(req, addr, 123);
            if (sent != req.size()) {
                qWarning("[timesync] 向 %s 发包失败（%s），换下一台",
                         qPrintable(host), qPrintable(g_sock->errorString()));
                ++g_serverIdx;
                tryNextServer();
                return;
            }
            qInfo("[timesync] 已向 %s(%s) 发出 SNTP 请求", qPrintable(host),
                  qPrintable(addr.toString()));
            g_attempt->start(kAttemptMs);   // 超时 → 换下一台
        });
}

void beginSync()
{
    if (g_running) return;              // 防重入：周期/手动撞车只跑一个
    if (!g_sock || !g_attempt) {
        qWarning("[timesync] 未 init（socket 未建），跳过本次同步");
        return;
    }
    g_running = true;
    g_serverIdx = 0;
    tryNextServer();
}

} // namespace

/* ══════════ 公共接口 ══════════════════════════════════════════ */

void init(const QString &file)
{
    g_file = file;
    loadConfig();

    if (!g_sock) {
        g_sock = new QUdpSocket(QCoreApplication::instance());
        g_sock->bind(QHostAddress::AnyIPv4, 0);
        QObject::connect(g_sock, &QUdpSocket::readyRead, g_sock, [] {
            while (g_sock && g_sock->hasPendingDatagrams()) {
                QByteArray buf;
                buf.resize(int(g_sock->pendingDatagramSize()));
                const qint64 n = g_sock->readDatagram(buf.data(), buf.size());
                if (n < 48) continue;
                buf.resize(int(n));
                if (!g_running) continue;      // 迟到的旧回包（本次已收尾）直接丢
                parseReply(buf);
                return;                        // 只认第一份有效回包
            }
        });
    }
    if (!g_attempt) {
        g_attempt = new QTimer(QCoreApplication::instance());
        g_attempt->setSingleShot(true);
        QObject::connect(g_attempt, &QTimer::timeout, g_attempt, [] {
            if (!g_running) return;
            qWarning("[timesync] %s 超时（%d ms），换下一台",
                     qPrintable(g_status.lastAttempt), kAttemptMs);
            ++g_serverIdx;
            tryNextServer();
        });
    }
    qInfo("[timesync] 初始化完成：偏移 %+d 分（文件 %s）", g_status.offsetMin, qPrintable(file));
}

void start()
{
    if (g_cycle) return;                  // 幂等
    g_cycle = new QTimer(QCoreApplication::instance());
    g_cycle->setInterval(kCycleMs);
    QObject::connect(g_cycle, &QTimer::timeout, g_cycle, [] { beginSync(); });
    g_cycle->start();
    QTimer::singleShot(kFirstDelayMs, QCoreApplication::instance(), [] { beginSync(); });
    qInfo("[timesync] 周期同步已启动：每 %d 分钟一次，首轮 %d s 后",
          kCycleMs / 60000, kFirstDelayMs / 1000);
}

bool syncNow()
{
    if (g_running) {
        qInfo("[timesync] 已有一轮同步在跑，本次请求忽略（不排队，避免堆积）");
        return false;
    }
    beginSync();
    return true;
}

bool setOffsetMinutes(int minutes)
{
    if (minutes < -kMaxOffsetMin || minutes > kMaxOffsetMin) {
        qWarning("[timesync] 偏移 %d 分越界（允许 ±%d），拒绝", minutes, kMaxOffsetMin);
        return false;
    }
    const int old = g_status.offsetMin;
    g_status.offsetMin = minutes;
    saveConfig();
    qInfo("[timesync] 人工偏移 %+d → %+d 分（只影响课表/提醒判定，系统钟未动）",
          old, minutes);
    notifyChanged();
    return true;
}

int offsetMinutes() { return g_status.offsetMin; }

qint64 skewMs()
{
    return g_status.deltaMs + qint64(g_status.offsetMin) * 60000LL;
}

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch() + skewMs();
}

QDateTime now()
{
    return QDateTime::currentDateTime().addMSecs(skewMs());
}

Status status() { return g_status; }

void setOnChanged(std::function<void()> cb) { g_onChanged = std::move(cb); }

} // namespace TimesyncDomain
