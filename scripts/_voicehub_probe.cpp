// 星集控 · 被控端运行时探针：VoiceHub 联动模块（_voicehub_probe.cpp，随仓库提交）
// ───────────────────────────────────────────────────────────────────────────
// 验什么：VoiceHubModule 从「HTTP/SSE 客户端」到「岛展示属性 / 动作回执」的整条链——
//   · SSE 主通道：建连快照（connection_established + broadcast_state null）、
//     broadcast_state 推广播、停播（null）、注释行忽略、断线重连（指数退避）；
//   · 轮询兜底：SSE 404（服务端禁匿名）时经 GET /api/music/broadcast 拿到状态；
//   · 本地 tick：进度按锚点+墙上钟外推（不每秒发请求）；
//   · 过期自愈：isPlaying 且超过 staleSec 没有新快照 ⇒ 当停播；
//   · 开播提醒：autoNotify 且非考试模式 ⇒ islandShow 恰好一次；
//   · 动作：voicehub_broadcast 快照 / song_request 等三个控制桩的回执随开关变化 /
//     停用后「未启用」回执；
//   · 配置：baseUrl/pollSec/staleSec/autoNotify/controlEnabled 落模块私有设置。
//
// 为什么不起真 VoiceHub：生产服务（voicehub.245959623.xyz）当前无广播且**不能
// 往生产写状态**；本地 voicehub-fork 只剩旧版参考（没有 broadcast 端点）。
// ⇒ 用 scripts/_voicehub_fake_server.mjs（Node 零依赖，复刻被测契约那一面）
// 起在 127.0.0.1，探针把模块 baseUrl 指过去。控制面 POST /__ctl、统计 GET /__ctl/stats。
// ⚠️ 不链 main.cpp（被控端本体，按「本机不开刀」规矩不能起生产进程）；
//    只链 modulehost.cpp + voicehubmodule.cpp。QSettings 用探针专属 org/app，不碰生产域。
//
// 退出码（项目既有约定）：10=全过 20=有断言失败 30=兜底超时/环境不成立。
// 判定不看 stdout——写 _voicehub_probe_report.txt（cwd 即报告目录）。

#include "modulehost.h"
#include "voicehubmodule.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

namespace {

struct ShowRecord {
    QString title, desc, icon;
    int form = -1;
    int durationMs = -1;
};

QVector<ShowRecord> g_shows;
bool        g_examActive = false;
QHash<QString, bool> g_moduleBools;     // key = "<moduleId>/<key>"（布尔配置）
QHash<QString, QString> g_moduleStrs;   // 字符串配置（baseUrl）
QHash<QString, int> g_moduleInts;       // 整型配置（pollSec/staleSec）

int  g_fail = 0;
int  g_pass = 0;
QTextStream *g_out = nullptr;

void check(bool ok, const QString &what)
{
    if (ok) { ++g_pass; *g_out << QStringLiteral("PASS  %1\n").arg(what); }
    else    { ++g_fail; *g_out << QStringLiteral("FAIL  %1\n").arg(what); }
    g_out->flush();
}

void spin(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// ── 假服务控制面（探针专用 HTTP 客户端）────────────────────────────────────
QNetworkAccessManager *g_ctl = nullptr;
QString g_base;   // http://127.0.0.1:<port>

QJsonObject ctlPost(const QString &op, const QJsonObject &extra = QJsonObject())
{
    QJsonObject body = extra;
    body[QStringLiteral("op")] = op;
    QNetworkRequest req(QUrl(g_base + QStringLiteral("/__ctl")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *r = g_ctl->post(req, QJsonDocument(body).toJson());
    QJsonObject out;
    QEventLoop loop;
    QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(4000, &loop, &QEventLoop::quit);   // 本地服务：4s 兜底
    loop.exec();
    if (r->error() == QNetworkReply::NoError) {
        const QJsonDocument doc = QJsonDocument::fromJson(r->readAll());
        if (doc.isObject()) out = doc.object();
    }
    r->deleteLater();
    return out;
}

QJsonObject ctlStats()
{
    QNetworkReply *r = g_ctl->get(QNetworkRequest(QUrl(g_base + QStringLiteral("/__ctl/stats"))));
    QJsonObject out;
    QEventLoop loop;
    QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(4000, &loop, &QEventLoop::quit);
    loop.exec();
    if (r->error() == QNetworkReply::NoError) {
        const QJsonDocument doc = QJsonDocument::fromJson(r->readAll());
        if (doc.isObject()) out = doc.object();
    }
    r->deleteLater();
    return out;
}

ModuleAgentApi makeStubApi()
{
    ModuleAgentApi a;
    a.islandShow = [](const QString &title, const QString &desc, const QString &icon,
                      int form, int durationMs) {
        g_shows.append(ShowRecord{ title, desc, icon, form, durationMs });
    };
    a.islandAsk = [](const QString &, const QString &, const QString &, int,
                     const QStringList &, const std::function<void(const QString &)> &, int) {};
    a.notify = [](const QJsonObject &) {};
    a.settingsGet = [](const QString &) -> QJsonValue { return QJsonValue(); };
    a.settingsSet = [](const QString &, const QJsonValue &) {};
    a.moduleGet = [](const QString &moduleId, const QString &key,
                     const QJsonValue &dflt) -> QJsonValue {
        const QString k = moduleId + QLatin1Char('/') + key;
        if (g_moduleInts.contains(k))   return QJsonValue(g_moduleInts.value(k));
        if (g_moduleStrs.contains(k))   return QJsonValue(g_moduleStrs.value(k));
        if (g_moduleBools.contains(k))  return QJsonValue(g_moduleBools.value(k));
        return dflt;
    };
    a.moduleSet = [](const QString &moduleId, const QString &key, const QJsonValue &v) {
        const QString k = moduleId + QLatin1Char('/') + key;
        if (v.isBool())        g_moduleBools.insert(k, v.toBool());
        else if (v.isString()) g_moduleStrs.insert(k, v.toString());
        else if (v.isDouble()) g_moduleInts.insert(k, v.toInt());
        // 其它类型（数组/对象）本模块用不到，忽略
    };
    a.reportCloud = [](const QString &, const QJsonObject &) {};
    a.openFolder = [](const QString &) {};
    a.examActive = []() { return g_examActive; };
    return a;
}

QJsonObject makeSong(const QString &title, const QString &artist, double duration,
                     double position, bool playing)
{
    return QJsonObject{
        { QStringLiteral("songId"), 42 },
        { QStringLiteral("scheduleId"), 7 },
        { QStringLiteral("playDate"), QStringLiteral("2026-10-17") },
        { QStringLiteral("sequence"), 1 },
        { QStringLiteral("title"), title },
        { QStringLiteral("artist"), artist },
        { QStringLiteral("cover"), QStringLiteral("") },
        { QStringLiteral("musicPlatform"), QStringLiteral("netease") },
        { QStringLiteral("musicId"), QStringLiteral("999001") },
        { QStringLiteral("duration"), duration },
        { QStringLiteral("position"), position },
        { QStringLiteral("isPlaying"), playing },
        { QStringLiteral("publisherName"), QStringLiteral("播放员甲") },
    };
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    // 探针专属设置域：ModuleHost 的启停位走 QSettings，绝不能写进生产域
    QCoreApplication::setOrganizationName(QStringLiteral("stelarith-probe"));
    QCoreApplication::setApplicationName(QStringLiteral("vh-probe"));

    if (app.arguments().size() < 2) {
        fprintf(stderr, "[vh-probe] 用法：_voicehub_probe <port>\n");
        return 30;
    }
    const QString port = app.arguments().at(1);
    g_base = QStringLiteral("http://127.0.0.1:") + port;
    g_ctl = new QNetworkAccessManager(&app);

    QFile report(QStringLiteral("_voicehub_probe_report.txt"));
    if (!report.open(QIODevice::WriteOnly | QIODevice::Text)) {
        fprintf(stderr, "[vh-probe] 报告文件打不开，无法判定\n");
        return 30;
    }
    QTextStream out(&report);
    g_out = &out;
    out << QStringLiteral("== VoiceHub 联动模块运行时探针（pid=%1，base=%2）==\n")
               .arg(qApp->applicationPid()).arg(g_base);
    out.flush();

    // 探针预置配置：指向假服务 + 压缩时间参数。
    // ⚠️ 必须在 startEnabled() 之前塞 —— loadConfig() 在 start() 里读。
    // ⚠️ 两个时间参数都是 qBound 钳过的（生产下限：轮询 5s / 过期 10s）；
    //    探针用**下限值**，等待窗口必须各覆盖一个完整周期（见阶段 7/8/9 的 spin）。
    g_moduleStrs.insert(QStringLiteral("voicehub/baseUrl"), g_base);
    g_moduleInts.insert(QStringLiteral("voicehub/pollSec"), 5);
    g_moduleInts.insert(QStringLiteral("voicehub/staleSec"), 10);

    ModuleHost *mh = ModuleHost::instance();
    mh->setAgentApi(makeStubApi());
    auto *vh = new VoiceHubModule();
    mh->registerModule(vh);
    // start() 读配置前先把 pollSec/staleSec 塞进去（stub moduleGet 认这两个 int 键）。
    mh->startEnabled();
    out << QStringLiteral("INFO  模块=%1 启用=%2 运行=%3\n")
               .arg(mh->moduleIds().join(QLatin1Char(',')))
               .arg(mh->isEnabled(QStringLiteral("voicehub")))
               .arg(mh->isRunning(QStringLiteral("voicehub")));
    out.flush();
    if (!mh->isRunning(QStringLiteral("voicehub"))) {
        out << QStringLiteral("FAIL  模块未运行（start() 没被调用）\n");
        return 30;
    }

    // ── 1. 启动即建连：SSE 快照 broadcast=null ⇒ 无广播、不报错 ──
    spin(1200);   // 覆盖建连 + 首帧 + 首次轮询
    check(!vh->active(),
          QStringLiteral("1a 启动即拿到 null 快照 → 无广播（active=%1）").arg(vh->active()));
    check(vh->statusText() == QStringLiteral("无广播")
              || vh->statusText() == QStringLiteral("已连接"),
          QStringLiteral("1b 状态是「无广播/已连接」而非报错（实际 %1）").arg(vh->statusText()));

    // ── 2. SSE 推广播：六要素 + nextUp + listeners 全解析 ──
    ctlPost(QStringLiteral("state"), QJsonObject{
        { QStringLiteral("broadcast"), makeSong(QStringLiteral("广播测试曲"),
                                                QStringLiteral("验证歌手"), 200, 42, true) },
        { QStringLiteral("nextUp"), QJsonObject{
            { QStringLiteral("songId"), 7 },
            { QStringLiteral("title"), QStringLiteral("下一首·课间曲") },
            { QStringLiteral("artist"), QStringLiteral("验证歌手") },
            { QStringLiteral("duration"), 188 } } },
        { QStringLiteral("listeners"), 12 },
    });
    spin(600);
    check(vh->active(), QStringLiteral("2a SSE 推广播 → active（实际 %1）").arg(vh->active()));
    check(vh->title() == QStringLiteral("广播测试曲"),
          QStringLiteral("2b 标题解析（实际 %1）").arg(vh->title()));
    check(vh->artist() == QStringLiteral("验证歌手"),
          QStringLiteral("2c 艺术家解析（实际 %1）").arg(vh->artist()));
    check(vh->durationSec() == 200 && vh->positionSec() >= 41 && vh->positionSec() <= 43,
          QStringLiteral("2d 时长/位置解析（dur=%1 pos=%2）")
              .arg(vh->durationSec()).arg(vh->positionSec()));
    check(vh->playing() && vh->publisher() == QStringLiteral("播放员甲"),
          QStringLiteral("2e 在播/播控人解析（playing=%1 publisher=%2）")
              .arg(vh->playing()).arg(vh->publisher()));
    check(vh->nextTitle() == QStringLiteral("下一首·课间曲"),
          QStringLiteral("2f nextUp 解析（实际 %1）").arg(vh->nextTitle()));
    check(vh->listeners() == 12,
          QStringLiteral("2g listeners 解析（实际 %1）").arg(vh->listeners()));

    // ── 3. 本地 tick：进度外推约 2s（不每秒发请求 ⇒ stats.polls 不暴涨）──
    {
        const QJsonObject before = ctlStats();
        const int pollsBefore = before.value(QStringLiteral("polls")).toInt();
        const double p0 = vh->positionSec();
        spin(2200);
        const double p1 = vh->positionSec();
        const int pollsAfter = ctlStats().value(QStringLiteral("polls")).toInt();
        check(p1 >= p0 + 1.5 && p1 <= p0 + 3.0,
              QStringLiteral("3a 2.2s 内进度外推 ~2s（%1 → %2）").arg(p0).arg(p1));
        check(pollsAfter - pollsBefore <= 3,
              QStringLiteral("3b 进度不外推成每秒请求（2.2s 内 poll %1 次）")
                  .arg(pollsAfter - pollsBefore));
    }

    // ── 4. 开播提醒：恰好一次，Centered，desc 带曲名 ──
    check(g_shows.size() == 1,
          QStringLiteral("4a 新广播 → islandShow 恰好一次（实际 %1）").arg(g_shows.size()));
    if (g_shows.size() == 1) {
        const ShowRecord &s = g_shows.first();
        check(s.title == QStringLiteral("VoiceHub 正在播放"),
              QStringLiteral("4b 提醒标题（实际 %1）").arg(s.title));
        check(s.desc.contains(QStringLiteral("广播测试曲"))
                  && s.desc.contains(QStringLiteral("下一首")),
              QStringLiteral("4c 提醒 desc 含曲名+下一首（实际 %1）").arg(s.desc));
        check(s.form == 1,
              QStringLiteral("4d 形态=Centered(1)（实际 %1）").arg(s.form));
    }

    // ── 5. 停播：broadcast=null ⇒ 整块清掉（岛行会消失）──
    ctlPost(QStringLiteral("stop"));
    spin(600);
    check(!vh->active() && vh->title().isEmpty(),
          QStringLiteral("5a 停播 → active=false 且曲名清空（active=%1 title=%2）")
              .arg(vh->active()).arg(vh->title()));
    check(g_shows.size() == 1,
          QStringLiteral("5b 停播不触发新提醒（累计 %1）").arg(g_shows.size()));

    // ── 6. SSE 断线重连：服务端掐连接 ⇒ 指数退避后重连（stats 见真）──
    {
        const int before = ctlStats().value(QStringLiteral("sseConnects")).toInt();
        ctlPost(QStringLiteral("kill"));
        spin(2600);   // 首档退避 1s + 余量
        const int after = ctlStats().value(QStringLiteral("sseConnects")).toInt();
        check(after >= before + 1,
              QStringLiteral("6a SSE 断开后自动重连（连接数 %1 → %2）").arg(before).arg(after));
    }

    // ── 7. SSE 404（禁匿名）⇒ 轮询兜底照常拿到状态 ──
    ctlPost(QStringLiteral("kill"));      // 先掐现存连接（sse404 只拦新连接）
    ctlPost(QStringLiteral("sse404"));
    spin(300);
    ctlPost(QStringLiteral("state"), QJsonObject{
        { QStringLiteral("broadcast"), makeSong(QStringLiteral("兜底轮询曲"),
                                                QStringLiteral("轮唱歌手"), 120, 10, true) },
    });
    spin(6200);   // 覆盖一个完整轮询周期（pollSec 生产下限 5s）+ 余量
    check(vh->active() && vh->title() == QStringLiteral("兜底轮询曲"),
          QStringLiteral("7a SSE 404 时轮询兜底生效（active=%1 title=%2）")
              .arg(vh->active()).arg(vh->title()));
    check(vh->statusText().contains(QStringLiteral("轮询兜底")),
          QStringLiteral("7b 状态明说在兜底（实际 %1）").arg(vh->statusText()));

    // ── 8. 过期自愈：在播但 staleSec（探针用下限 10s）没有新快照 ⇒ 当停播 ──
    // 手段：SSE 已 404、轮询挂起不响应（服务端 hold）⇒ 两条通道都不来新快照。
    // 先证在播（否则 8b 会假绿——本来就没广播时"自愈成无广播"是废话）。
    check(vh->active(), QStringLiteral("8a 前置：此刻确实在播（active=%1）").arg(vh->active()));
    ctlPost(QStringLiteral("hold"));
    spin(11500);   // staleSec=10（qBound 下限）+ tick 周期 + 余量
    check(!vh->active(),
          QStringLiteral("8b 在播超期无快照 → 自愈为无广播（active=%1）").arg(vh->active()));
    ctlPost(QStringLiteral("unhold"));

    // ── 9. 动作：快照 / 控制桩回执随开关变化 / 停用回执 ──
    ctlPost(QStringLiteral("sseok"));
    ctlPost(QStringLiteral("state"), QJsonObject{
        { QStringLiteral("broadcast"), makeSong(QStringLiteral("回执验证曲"),
                                                QStringLiteral("回执歌手"), 90, 5, true) },
    });
    spin(6200);   // 轮询周期内拿到（同上 5s 下限）
    {
        ModuleResult r;
        const bool handled = mh->dispatch(QStringLiteral("voicehub_broadcast"), QJsonObject(), r);
        check(handled && r.ok && r.result == QStringLiteral("done")
                  && r.data.value(QStringLiteral("broadcast"))
                         .toObject().value(QStringLiteral("title")).toString()
                         == QStringLiteral("回执验证曲"),
              QStringLiteral("9a dispatch(voicehub_broadcast) → 快照 JSON（handled=%1 ok=%2）")
                  .arg(handled).arg(r.ok));
    }
    {
        ModuleResult r;
        const bool handled = mh->dispatch(QStringLiteral("song_request"), QJsonObject(), r);
        check(handled && !r.ok && r.result == QStringLiteral("not-open")
                  && r.error.contains(QStringLiteral("未开放")),
              QStringLiteral("9b 控制关时点歌回执「未开放」（result=%1）").arg(r.result));
        vh->setControlEnabled(true);
        ModuleResult r2;
        mh->dispatch(QStringLiteral("song_request"), QJsonObject(), r2);
        check(r2.ok == false && r2.error.contains(QStringLiteral("尚未接线")),
              QStringLiteral("9c 控制开时点歌回执「尚未接线」（error=%1）").arg(r2.error));
        ModuleResult r3;
        mh->dispatch(QStringLiteral("lyrics"), QJsonObject(), r3);
        check(!r3.ok && r3.result == QStringLiteral("not-open"),
              QStringLiteral("9d 歌词同款留桩（result=%1）").arg(r3.result));
    }
    mh->setEnabled(QStringLiteral("voicehub"), false);
    {
        ModuleResult r;
        const bool handled = mh->dispatch(QStringLiteral("voicehub_broadcast"), QJsonObject(), r);
        check(handled && !r.ok && r.error.contains(QStringLiteral("未启用")),
              QStringLiteral("9e 停用后回执「未启用」（error=%1）").arg(r.error));
        check(!mh->isRunning(QStringLiteral("voicehub")),
              QStringLiteral("9f 停用后 running=false"));
    }
    mh->setEnabled(QStringLiteral("voicehub"), true);
    check(mh->isRunning(QStringLiteral("voicehub")),
          QStringLiteral("9g 重新启用后 running=true"));

    // ── 10. 配置落模块私有设置（stub map 即 QSettings 的形状）──
    // 先改成别的地址（触发落盘 + 客户端重启），验证后改回假服务。
    vh->setBaseUrl(QStringLiteral("http://127.0.0.1:9"));
    check(g_moduleStrs.value(QStringLiteral("voicehub/baseUrl"))
              == QStringLiteral("http://127.0.0.1:9"),
          QStringLiteral("10a baseUrl 落设置（实际 %1）")
              .arg(g_moduleStrs.value(QStringLiteral("voicehub/baseUrl"))));
    check(g_moduleBools.value(QStringLiteral("voicehub/controlEnabled")) == true,
          QStringLiteral("10b controlEnabled 落设置"));
    check(g_moduleInts.value(QStringLiteral("voicehub/staleSec")) == 10,
          QStringLiteral("10c staleSec 预置生效（实际 %1）")
              .arg(g_moduleInts.value(QStringLiteral("voicehub/staleSec"))));
    vh->setBaseUrl(g_base);   // 改回（客户端重连到假服务；探针随即退出，不等它）

    // ── 汇总 ──
    out << QStringLiteral("INFO  islandShow=%1\n").arg(g_shows.size());
    out << QStringLiteral("== 汇总：PASS %1 / FAIL %2 ==\n").arg(g_pass).arg(g_fail);
    out.flush();
    return g_fail == 0 ? 10 : 20;
}
