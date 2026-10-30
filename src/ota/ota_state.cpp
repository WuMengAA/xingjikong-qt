// OTA 状态机 + 健康快照上报（M3，2026-10-20）。
// 2026-10-30 main 拆分 #4a：从 main.cpp（状态函数原 2189-2287、otaReportHealth 原 3688-3749）
// 迁出，逻辑/注释逐字保留。无人值守自更新执行段仍留 main（#4b）。
//
// ⚠️ 全部落在**数据目录**，不落在安装目录：NSIS 装完会换安装目录里的文件，
//    放这儿的备份和标记才不会被一起换掉（2026-10-06 亲测这个坑：放安装目录=没有备份）。

#include "ota_state.h"

#include "../cloud_proto.h"          // makeEnvelope（sendEvent 间接用，保持头一致）
#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QDebug>

// ── 复用 main 的文件作用域符号（提升外部链接，同 g_uid / agentEnvPath 模式）──
extern QString g_latestVer;
extern QString g_channel;
extern const char *kAppVersion;
extern void sendEvent(const QString &level, const QString &msg, const QJsonObject &extra);

QString otaBaseDir()
{
    static QString cached;
    if (cached.isEmpty())
        cached = qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong/ota");
    return cached;
}
static QString otaStatePath()   { return otaBaseDir() + QStringLiteral("/ota-state.json"); }
QString otaOkPath()      { return otaBaseDir() + QStringLiteral("/ota-last-ok.txt"); }
QString otaPrevPath()    { return otaBaseDir() + QStringLiteral("/prev/stelarith-agent-qt.exe"); }
QString otaPendingPath() { return otaBaseDir() + QStringLiteral("/ota-pending.json"); }
QString otaPausePath()   { return otaBaseDir() + QStringLiteral("/ota-pause"); }

static void otaSaveState(const QJsonObject &s)
{
    QDir().mkpath(otaBaseDir());
    QFile f(otaStatePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("[agent-qt] ⚠ 写不下 ota-state.json（%s）—— 重试退避会退化成每次都装", qPrintable(otaStatePath()));
        return;
    }
    f.write(QJsonDocument(s).toJson(QJsonDocument::Indented));
    f.flush();
}

QJsonObject otaLoadState()
{
    QJsonObject s;
    QFile f(otaStatePath());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument d = QJsonDocument::fromJson(f.readAll());
        if (d.isObject()) s = d.object();
    }
    return s;
}

/** 下一次尝试前要等多久：失败越多等越久（3 分 → 15 分 → 1 时 → 4 时 → 12 时封顶）。 */
static qint64 otaRetryDelayMs(int failCount)
{
    static const qint64 table[] = {
        3  * 60 * 1000LL,   // 第 1 次失败：网络抖一下，三分钟后再说
        15 * 60 * 1000LL,   // 第 2 次：短时间重来也没用，等一刻钟
        1  * 60 * 60 * 1000LL,
        4  * 60 * 60 * 1000LL,
        12 * 60 * 60 * 1000LL   // 封顶：一整天最多闹这么几次
    };
    if (failCount <= 0) return 0;
    return table[failCount - 1 > 4 ? 4 : failCount - 1];
}

/** 记一次失败：把"下次最早几点能试"一起算好写进状态文件。 */
void otaNoteFailure(const QString &why)
{
    QJsonObject st = otaLoadState();
    const int n = st.value(QStringLiteral("failCount")).toInt(0) + 1;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    st.insert(QStringLiteral("failCount"), n);
    st.insert(QStringLiteral("lastResult"), QStringLiteral("failed"));
    st.insert(QStringLiteral("lastError"), why);
    st.insert(QStringLiteral("lastVer"), g_latestVer);
    st.insert(QStringLiteral("lastAt"), (double)now);
    st.insert(QStringLiteral("nextAt"), (double)(now + otaRetryDelayMs(n)));
    otaSaveState(st);
    qWarning("[agent-qt] ⏳ 自动升级失败（连续 %d 次：%s）→ %lld 分钟后再试",
             n, qPrintable(why), otaRetryDelayMs(n) / 60000LL);
    // M3（2026-10-20）：失败是 OTA 最该被云端看见的状态 —— 退避 ladder 每升一级
    // 快照就变一次（failCount/nextAt），变化才发，不会刷屏。
    otaReportHealth(QStringLiteral("failure"));
}

/** 记一次成功：清空失败计数（下次有网就立刻再试，不用再等退避）。 */
void otaNoteSuccess()
{
    QJsonObject st = otaLoadState();
    st.insert(QStringLiteral("failCount"), 0);
    st.insert(QStringLiteral("lastResult"), QStringLiteral("ok"));
    st.insert(QStringLiteral("lastVer"), QString::fromLatin1(kAppVersion));
    st.insert(QStringLiteral("lastAt"), (double)QDateTime::currentMSecsSinceEpoch());
    st.insert(QStringLiteral("nextAt"), (double)0);
    // ⚠️ 2026-10-18：成功必须把上一次的错误**清掉**。
    //   只写 lastResult=ok 而留着旧 lastError ⇒ 面板/排障时永远看到一条早已不成立的
    //   「备份当前 exe 失败」，把"其实没事"误判成"OTA 坏了"（本轮实测踩到）。
    st.remove(QStringLiteral("lastError"));
    otaSaveState(st);
    // M3（2026-10-20）：升级确认成功（新版本注册上了云端）也上报一次 ——
    // failCount 归零 + lastError 清空，快照与"失败中"是两个人，运营侧能看到闭环。
    otaReportHealth(QStringLiteral("success"));
}

bool otaPaused()
{
    return QFile::exists(otaPausePath());
}

/* ══════════════════════════════════════════════════════════════════════════
 * OTA 健康快照上报（M3，2026-10-20）
 *
 * 病灶：被控端 OTA 是"无人值守自更新"—— 失败退避、暂停刹车、回滚三件套全都只在
 *   **本机日志**里闭环。云端/管理端看不到一台机器"已经连续失败 N 次、下次几点再试、
 *   是否被管理员按下暂停"。表现：OTA 悄悄坏了对云端是静默的，只能等老师打电话。
 *   （对照：管理端批次2 刚补上"周期自动检查 + 新版本气泡"，被控端这边却没有对应回咀。）
 *
 * 补法：OTA 状态每次**变化**时经 sendEvent 上报一份健康快照。走 type:"event" 旁路
 *   （与 A6 噪音遥测同一条路）：云端 index.js:2147 收下后 pushEvent 记进事件流水，
 *   不污染按指令 id 落表的 results 表。
 *
 * ⚠️ 只在**变化**时上报（s_last 进程内比对）：
 *   · 退避 ladder 每升一级快照必然变（failCount/nextAt）⇒ 每级正好一条，不重不漏；
 *   · 周期复查 30 分钟一轮，状态不变时**一条都不发** —— 否则 events.log 会被
 *     "每分钟一条没事"的遥测淹掉（2026-10-15 逐帧事件把事件表写爆的前车之鉴）；
 *   · 进程重启后 s_last 清空 ⇒ 首轮必发一条基线（failCount=0 也算"变化"），
 *     顺便让云端看见这台机器当前的 channel 与是否带欠账上线。
 *
 * 调用点（四个）：otaNoteFailure（失败记账）/ otaNoteSuccess（升级确认成功）/
 *   maybeAutoUpdate 的暂停分支（管理员放下刹车）/ otaSelfCheck 每轮（外部变更感知 + 基线）。
 * ══════════════════════════════════════════════════════════════════════════ */
void otaReportHealth(const QString &why)
{
    const QJsonObject st = otaLoadState();
    QJsonObject snap;
    snap.insert(QStringLiteral("kind"), QStringLiteral("ota_health"));
    snap.insert(QStringLiteral("curVer"), QString::fromLatin1(kAppVersion));
    snap.insert(QStringLiteral("latestVer"), g_latestVer);
    snap.insert(QStringLiteral("failCount"), st.value(QStringLiteral("failCount")).toInt(0));
    snap.insert(QStringLiteral("lastResult"), st.value(QStringLiteral("lastResult")).toString());
    snap.insert(QStringLiteral("lastError"), st.value(QStringLiteral("lastError")).toString());
    snap.insert(QStringLiteral("lastAt"), st.value(QStringLiteral("lastAt")).toDouble());
    snap.insert(QStringLiteral("nextAt"), st.value(QStringLiteral("nextAt")).toDouble());
    snap.insert(QStringLiteral("paused"), otaPaused());
    snap.insert(QStringLiteral("channel"), g_channel);

    static QJsonObject s_last;   // 上一份上报过的快照（进程内；重启后首次必然发一条）
    // ⚠️ lastAt **不参与比对**：otaNoteSuccess 每次注册回执都会把它刷成 now ⇒ 它一进比对，
    //    机器每重连一次就上报一条"什么都没变"的 success（实测抓到的设计缺陷，2026-10-20）——
    //    "变化才上报"会被一个时间戳字段架空。它只作为载荷里的参考信息发出去。
    QJsonObject cmp = snap;
    cmp.remove(QStringLiteral("lastAt"));
    if (cmp == s_last) {
        qInfo("[agent-qt] OTA 健康快照未变化（%s），不上报", qPrintable(why));
        return;
    }
    s_last = cmp;

    // 有失败欠账或被管理员暂停 = 需要人看一眼 ⇒ warn；其余（含成功/基线）info。
    const bool trouble = snap.value(QStringLiteral("failCount")).toInt() > 0
                        || snap.value(QStringLiteral("paused")).toBool();
    sendEvent(trouble ? QStringLiteral("warn") : QStringLiteral("info"),
              QStringLiteral("ota_health"), snap);
    qInfo("[agent-qt] 📊 OTA 健康已上报（%s）：failCount=%d lastResult=%s lastError=%s paused=%s channel=%s",
          qPrintable(why),
          snap.value(QStringLiteral("failCount")).toInt(),
          qPrintable(snap.value(QStringLiteral("lastResult")).toString()),
          qPrintable(snap.value(QStringLiteral("lastError")).toString()),
          snap.value(QStringLiteral("paused")).toBool() ? "yes" : "no",
          qPrintable(snap.value(QStringLiteral("channel")).toString()));
}