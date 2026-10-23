// 班级域探针（#460 · 2026-10-23）—— ClassService + typed Models + 缩略图 provider 离屏取证。
//
// 为什么离屏跑真逻辑而不只是编译通过：#460 的消费端（QML）在 #461，这期间
// 唯一能证明"班级域数据面正确"的就是这个探针 —— 合成快照/事件/缩略图喂进去，
// 逐条断言模型与 provider 的输出。协议字段名与云端 class-events.js 逐字对齐
// （改云端必须同批改这里，见 tests 同款的"消息定位"纪律）。
//
// 与云端回归测试的分工：tests/regression-class-events.mjs（#458）证明**云端**
// 发的消息形状正确；本探针证明 **viewer 侧消费**这些形状正确。两边字段名
// 就是契约，任一侧漂了都会有另一边挂。
//
// 用法：python scripts/_class_probe.py   （编译 + 离屏运行；退出码 0 = 全绿）

#include <QGuiApplication>
#include <QBuffer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QSize>
#include <QStringList>
#include <cstdio>

#include "class/class_service.h"
#include "class/class_thumb_provider.h"

// logf 桩：真实定义在 main.cpp（探针不链 viewerbackend，只链 src/class/）。
void logf(const char *fmt, ...) { Q_UNUSED(fmt); }

static int g_pass = 0;
static int g_fail = 0;

static void check(bool ok, const char *what)
{
    if (ok) {
        ++g_pass;
        std::printf("  [PASS] %s\n", what);
    } else {
        ++g_fail;
        std::printf("  [FAIL] %s\n", what);
    }
}

// ── 记录式收尾（替代真 WS） ──
struct Recorder {
    QList<QPair<QString, QJsonObject>> sent;   ///< sendRequested 记录
    QStringList errors;                        ///< classError 记录
    QStringList subFails;                      ///< subscribeFailed 记录
    QStringList thumbUids;                     ///< thumbnailChanged 记录
};
static Recorder g_rec;

/** 造一张真 JPEG 的 base64（探针自供帧，不依赖任何外部文件/真机抓屏）。 */
static QString makeThumbB64(int w, int h, int seed)
{
    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            img.setPixelColor(x, y, QColor((x * 7 + seed) % 256,
                                           (y * 5 + seed) % 256,
                                           (x * y + seed) % 256));
        }
    }
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    if (!img.save(&buf, "JPG", 80)) return QString();   // 插件缺失会走到这
    return QString::fromLatin1(buf.data().toBase64());
}

/** class-subscribed 快照 payload（字段与云端 class-events.js classSnapshot 对齐）。 */
static QJsonObject snapPayload()
{
    QJsonObject pay;
    pay.insert(QStringLiteral("classCode"), QStringLiteral("A1B2C3"));
    pay.insert(QStringLiteral("name"), QString::fromUtf8("高一（1）班"));
    pay.insert(QStringLiteral("onlineCount"), 2);
    pay.insert(QStringLiteral("at"), QStringLiteral("2026-10-23T18:00:00.000Z"));

    QJsonArray devs;
    QJsonObject d1;
    d1.insert(QStringLiteral("uid"), QStringLiteral("dev-1"));
    d1.insert(QStringLiteral("name"), QString::fromUtf8("01号机"));
    d1.insert(QStringLiteral("online"), true);
    d1.insert(QStringLiteral("version"), QStringLiteral("0.6.46"));
    d1.insert(QStringLiteral("lastSeenAgoSec"), 3);
    QJsonObject d2;
    d2.insert(QStringLiteral("uid"), QStringLiteral("dev-2"));
    d2.insert(QStringLiteral("name"), QString::fromUtf8("02号机"));
    d2.insert(QStringLiteral("online"), true);
    QJsonObject d3;
    d3.insert(QStringLiteral("uid"), QStringLiteral("dev-3"));
    d3.insert(QStringLiteral("name"), QString::fromUtf8("03号机"));
    d3.insert(QStringLiteral("online"), false);
    devs.append(d1); devs.append(d2); devs.append(d3);
    pay.insert(QStringLiteral("devices"), devs);

    // 历史两条（云端 recentEvents：旧 → 新；Timeline 应翻成新在头）
    QJsonArray evs;
    QJsonObject e1;
    e1.insert(QStringLiteral("id"), 10);
    e1.insert(QStringLiteral("at"), QStringLiteral("2026-10-23T10:00:00.000Z"));
    e1.insert(QStringLiteral("kind"), QStringLiteral("member.online"));
    e1.insert(QStringLiteral("uid"), QStringLiteral("dev-2"));
    e1.insert(QStringLiteral("data"), QJsonObject());
    QJsonObject e2;
    e2.insert(QStringLiteral("id"), 11);
    e2.insert(QStringLiteral("at"), QStringLiteral("2026-10-23T10:05:00.000Z"));
    e2.insert(QStringLiteral("kind"), QStringLiteral("test.hello"));
    e2.insert(QStringLiteral("uid"), QStringLiteral("dev-1"));
    QJsonObject e2d;
    e2d.insert(QStringLiteral("msg"), QStringLiteral("hi"));
    e2.insert(QStringLiteral("data"), e2d);
    evs.append(e1); evs.append(e2);
    pay.insert(QStringLiteral("events"), evs);
    return pay;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);   // offscreen（python 侧设 QT_QPA_PATFORM）；JPEG 插件要 Gui

    ClassService svc;
    QObject::connect(&svc, &ClassService::sendRequested,
                     [](const QString &t, const QJsonObject &p) { g_rec.sent.append({t, p}); });
    QObject::connect(&svc, &ClassService::classError,
                     [](const QString &m) { g_rec.errors.append(m); });
    QObject::connect(&svc, &ClassService::subscribeFailed,
                     [](const QString &r) { g_rec.subFails.append(r); });
    QObject::connect(&svc, &ClassService::thumbnailChanged,
                     [](const QString &u) { g_rec.thumbUids.append(u); });

    ClassDeviceModel *dev = svc.deviceModel();
    ClassEventModel *ev = svc.eventModel();

    std::printf("==== 班级域探针（#460） ====\n");

    // ① 没订过班时，重鉴权不该发出任何订阅（防"从没订过班却自动订"）
    svc.onAuthed();
    check(g_rec.sent.isEmpty(), "未订过班：onAuthed 不发订阅");

    // ② 订阅出海 + pending 态
    svc.subscribeClass(QStringLiteral("A1B2C3"));
    check(g_rec.sent.size() == 1
              && g_rec.sent.at(0).first == QLatin1String("subscribe_class")
              && g_rec.sent.at(0).second.value(QStringLiteral("code")).toString()
                     == QLatin1String("A1B2C3"),
          "subscribe_class 出海（code 正确）");
    check(!svc.isSubscribed(), "订阅未回执前 isSubscribed=false（pending）");

    // ③ 快照装配：设备表 / 在线数 / Timeline 新在头
    svc.onClassSubscribed(snapPayload());
    check(svc.isSubscribed() && svc.classCode() == QLatin1String("A1B2C3")
              && svc.className() == QString::fromUtf8("高一（1）班"),
          "订阅回执：状态 / 班码 / 班名");
    check(svc.deviceCount() == 3 && svc.onlineCount() == 2,
          "快照：3 台设备 / 2 在线");
    check(dev->data(dev->index(0, 0), ClassDeviceModel::NameRole).toString()
              == QString::fromUtf8("01号机")
              && dev->data(dev->index(0, 0), ClassDeviceModel::VersionRole).toString()
                     == QLatin1String("0.6.46")
              && dev->data(dev->index(2, 0), ClassDeviceModel::OnlineRole).toBool() == false,
          "设备行 role：name / version / online");
    check(dev->rowOf(QStringLiteral("dev-2")) == 1 && dev->uidAt(1) == QLatin1String("dev-2"),
          "rowOf / uidAt 定位");
    check(ev->count() == 2, "快照历史：Timeline 2 条");
    check(ev->data(ev->index(0, 0), ClassEventModel::IdRole).toLongLong() == 11
              && ev->data(ev->index(1, 0), ClassEventModel::IdRole).toLongLong() == 10,
          "Timeline 新在头（快照旧→新翻转为 11,10）");
    check(ev->data(ev->index(0, 0), ClassEventModel::IsMemberRole).toBool() == false
              && ev->data(ev->index(1, 0), ClassEventModel::IsMemberRole).toBool() == true,
          "isMember：test.hello=false / member.online=true");

    // ④ 缩略图：解码 → 设备行 rev → provider 出图
    const QString b64 = makeThumbB64(64, 40, 1);
    check(!b64.isEmpty(), "探针自造 JPEG base64（save 插件可用）");
    QJsonObject td;
    td.insert(QStringLiteral("w"), 64);
    td.insert(QStringLiteral("h"), 40);
    td.insert(QStringLiteral("mime"), QStringLiteral("image/jpeg"));
    td.insert(QStringLiteral("b64"), b64);
    QJsonObject tp;
    tp.insert(QStringLiteral("classCode"), QStringLiteral("A1B2C3"));
    tp.insert(QStringLiteral("id"), 12);
    tp.insert(QStringLiteral("at"), QStringLiteral("2026-10-23T18:00:05.000Z"));
    tp.insert(QStringLiteral("kind"), QStringLiteral("thumbnail"));
    tp.insert(QStringLiteral("uid"), QStringLiteral("dev-1"));
    tp.insert(QStringLiteral("data"), td);
    svc.onClassEvent(tp);
    check(g_rec.thumbUids.contains(QStringLiteral("dev-1")), "缩略图事件 → thumbnailChanged(dev-1)");
    check(dev->data(dev->index(0, 0), ClassDeviceModel::HasThumbRole).toBool() == true
              && dev->data(dev->index(0, 0), ClassDeviceModel::ThumbRevRole).toInt() == 1,
          "设备行 hasThumb=true / thumbRev=1");
    check(ev->count() == 2, "缩略图不进 Timeline（图片流不冲事件流）");

    ClassThumbProvider prov(&svc);
    QSize gotSz;
    const QImage got = prov.requestImage(QStringLiteral("dev-1?t1"), &gotSz, QSize(160, 100));
    check(!got.isNull() && got.width() == 64 && got.height() == 40,
          "provider 返回真图（64x40，?query 剥对）");

    // 第二张 → rev=2（QML source 防缓存依据）
    QJsonObject tp2 = tp;
    tp2.insert(QStringLiteral("id"), 13);
    QJsonObject td2 = td;
    td2.insert(QStringLiteral("b64"), makeThumbB64(48, 32, 7));
    tp2.insert(QStringLiteral("data"), td2);
    svc.onClassEvent(tp2);
    check(dev->data(dev->index(0, 0), ClassDeviceModel::ThumbRevRole).toInt() == 2,
          "第二张缩略图 → thumbRev=2");
    const QImage got2 = prov.requestImage(QStringLiteral("dev-1?t2"), &gotSz, QSize());
    check(!got2.isNull() && got2.width() == 48, "provider 取到新图（48 宽）");

    // 坏 base64：不炸、rev 不变、无 signal
    QJsonObject tpBad = tp;
    tpBad.insert(QStringLiteral("id"), 14);
    QJsonObject tdB = td;
    tdB.insert(QStringLiteral("b64"), QStringLiteral("!!!not-base64!!!"));
    tpBad.insert(QStringLiteral("data"), tdB);
    const int thumbSigs = g_rec.thumbUids.size();
    svc.onClassEvent(tpBad);
    check(dev->data(dev->index(0, 0), ClassDeviceModel::ThumbRevRole).toInt() == 2
              && g_rec.thumbUids.size() == thumbSigs,
          "坏 base64：不炸、rev 不变、无 thumbnailChanged");

    // ⑤ 成员上线：未知 uid 追加 + 在线数 + Timeline
    QJsonObject mo;
    mo.insert(QStringLiteral("classCode"), QStringLiteral("A1B2C3"));
    mo.insert(QStringLiteral("id"), 15);
    mo.insert(QStringLiteral("at"), QStringLiteral("2026-10-23T18:01:00.000Z"));
    mo.insert(QStringLiteral("kind"), QStringLiteral("member.online"));
    mo.insert(QStringLiteral("uid"), QStringLiteral("dev-9"));
    mo.insert(QStringLiteral("data"), QJsonObject());
    svc.onClassMemberOnline(mo);
    check(svc.deviceCount() == 4 && dev->rowOf(QStringLiteral("dev-9")) == 3,
          "member.online：新设备追加到表尾");
    check(svc.onlineCount() == 3, "member.online：在线数 2→3");
    check(ev->count() == 3 && ev->data(ev->index(0, 0), ClassEventModel::IdRole).toLongLong() == 15,
          "member.online 进 Timeline（头）");

    // ⑥ 成员离线：置位不建行 + 原因进 detail
    QJsonObject mf;
    mf.insert(QStringLiteral("id"), 16);
    mf.insert(QStringLiteral("at"), QStringLiteral("2026-10-23T18:02:00.000Z"));
    mf.insert(QStringLiteral("kind"), QStringLiteral("member.offline"));
    mf.insert(QStringLiteral("uid"), QStringLiteral("dev-1"));
    QJsonObject md;
    md.insert(QStringLiteral("reason"), QStringLiteral("heartbeat-timeout"));
    mf.insert(QStringLiteral("data"), md);
    svc.onClassMemberOffline(mf);
    check(dev->data(dev->index(0, 0), ClassDeviceModel::OnlineRole).toBool() == false,
          "member.offline：dev-1 置离线");
    check(svc.onlineCount() == 2, "member.offline：在线数 3→2");
    check(ev->data(ev->index(0, 0), ClassEventModel::DetailRole).toString()
              == QLatin1String("heartbeat-timeout"),
          "离线原因进 Timeline detail");
    // 从没见过的 uid 离线：不建行
    QJsonObject mf2 = mf;
    mf2.insert(QStringLiteral("uid"), QStringLiteral("ghost-9"));
    mf2.insert(QStringLiteral("id"), 17);
    svc.onClassMemberOffline(mf2);
    check(svc.deviceCount() == 4, "未知 uid 离线不建设备行（清单以快照为权威）");

    // ⑦ 重复 id：水位去重
    const int evCount = ev->count();
    svc.onClassEvent(mf);   // id=16 已见过
    check(ev->count() == evCount, "重复 id 事件被水位去重");

    // ⑧ 未知 uid 缩略图：不建行但可被 provider 取到（缓存）
    QJsonObject tu = tp;
    tu.insert(QStringLiteral("id"), 18);
    tu.insert(QStringLiteral("uid"), QStringLiteral("ghost-1"));
    svc.onClassEvent(tu);
    check(svc.deviceCount() == 4, "未知 uid 缩略图不建设备行");
    const QImage ghost = prov.requestImage(QStringLiteral("ghost-1?t1"), &gotSz, QSize());
    check(!ghost.isNull() && ghost.width() == 64, "未知 uid 缩略图仍可被 provider 取到");

    // ⑨ 无 uid 缩略图：静默忽略
    QJsonObject nu = tu;
    nu.insert(QStringLiteral("uid"), QString());
    nu.insert(QStringLiteral("id"), 19);
    svc.onClassEvent(nu);
    check(svc.deviceCount() == 4, "无 uid 缩略图静默忽略");

    // ⑩ 退订：出海 + 本地即置未订 + 回执清模型
    svc.unsubscribeClass();
    check(!g_rec.sent.isEmpty()
              && g_rec.sent.last().first == QLatin1String("unsubscribe_class")
              && g_rec.sent.last().second.value(QStringLiteral("code")).toString()
                     == QLatin1String("A1B2C3"),
          "退订出海（code 正确）");
    check(!svc.isSubscribed(), "退订后本地即置未订（不等回执）");
    QJsonObject un;
    un.insert(QStringLiteral("ok"), true);
    un.insert(QStringLiteral("classCode"), QStringLiteral("A1B2C3"));
    svc.onClassUnsubscribed(un);
    check(svc.deviceCount() == 0 && ev->count() == 0, "退订回执：模型清空");

    // ⑪ 重订 → 断线保留 → 重鉴权自动重订
    svc.subscribeClass(QStringLiteral("A1B2C3"));
    svc.onClassSubscribed(snapPayload());
    check(svc.isSubscribed() && svc.deviceCount() == 3, "重订成功（快照再装配）");
    // 探针注：dev-1 的缩略图缓存在退订时未清 → 重订后 hasThumb 应恢复（防闪烁）。
    // rev 期望 2：④ 给 dev-1 发过两张（rev 1→2），坏 base64 那张不计。
    check(dev->data(dev->index(0, 0), ClassDeviceModel::HasThumbRole).toBool() == true
              && dev->data(dev->index(0, 0), ClassDeviceModel::ThumbRevRole).toInt() == 2,
          "重订后 hasThumb/rev 恢复（缓存未清，不闪白）");
    svc.onDisconnected();
    check(!svc.isSubscribed() && svc.deviceCount() == 3,
          "断线：置未订但保留数据（防重连闪烁）");
    svc.onAuthed();
    check(!g_rec.sent.isEmpty()
              && g_rec.sent.last().first == QLatin1String("subscribe_class")
              && g_rec.sent.last().second.value(QStringLiteral("code")).toString()
                     == QLatin1String("A1B2C3"),
          "重鉴权 → 自动重订原班");

    // ⑫ 订阅被拒（pending 窗口的 error）→ subscribeFailed 不静默
    svc.onClassSubscribed(snapPayload());   // 先订上，再制造"换班被拒"
    svc.subscribeClass(QStringLiteral("ZZZ999"));
    QJsonObject err;
    err.insert(QStringLiteral("code"), QStringLiteral("NO_DEVICE_PERMISSION"));
    err.insert(QStringLiteral("message"), QString::fromUtf8("无权限订阅班级 ZZZ999"));
    svc.onError(err);
    check(g_rec.subFails.size() == 1
              && g_rec.subFails.first() == QString::fromUtf8("无权限订阅班级 ZZZ999"),
          "订阅被拒 → subscribeFailed（人话原文）");
    check(svc.isSubscribed() && svc.classCode() == QLatin1String("A1B2C3"),
          "被拒后原订阅不受影响（不误清）");

    // ⑬ 幂等：已订同班不发；pending 同班不发；换班正常发
    const int sentBefore = g_rec.sent.size();
    svc.subscribeClass(QStringLiteral("A1B2C3"));      // 已订同班 → 不发
    check(g_rec.sent.size() == sentBefore, "已订同班：重复订阅不出海");
    svc.subscribeClass(QStringLiteral("ZZZ999"));      // 换班 → 发一条
    const int sentAfterPending = g_rec.sent.size();
    check(sentAfterPending == sentBefore + 1, "换班订阅正常出海");
    svc.subscribeClass(QStringLiteral("ZZZ999"));      // pending 同班 → 不发
    check(g_rec.sent.size() == sentAfterPending, "pending 同班：重复订阅不出海");
    // 清掉这个 pending（换班失败收场），别影响后续场景
    QJsonObject err2 = err;
    err2.insert(QStringLiteral("message"), QString::fromUtf8("无权限订阅班级 ZZZ999"));
    svc.onError(err2);

    // ⑭ 空班级码：不出海 + 报错（基线取跟前一条，别拿 ⑬ 的旧基线——那会儿
    // 换班订阅已经发过一条了，两基线混用就是自己绊自己）
    const int sentBefore14 = g_rec.sent.size();
    svc.subscribeClass(QStringLiteral("   "));
    check(g_rec.sent.size() == sentBefore14 && !g_rec.errors.isEmpty(),
          "空班级码：不出海且报错（不静默）");

    // ⑮ Timeline cap=200（与云端环形缓冲同容）
    for (int i = 0; i < 210; ++i) {
        QJsonObject e;
        e.insert(QStringLiteral("id"), 1000 + i);
        e.insert(QStringLiteral("kind"), QStringLiteral("test.flood"));
        e.insert(QStringLiteral("uid"), QStringLiteral("dev-1"));
        e.insert(QStringLiteral("data"), QJsonObject());
        svc.onClassEvent(e);
    }
    check(ev->count() == 200, "Timeline cap=200");
    check(ev->data(ev->index(0, 0), ClassEventModel::IdRole).toLongLong() == 1209,
          "cap 后头部是最高 id（1209）");
    // 乱序旧事件（低于水位）丢
    const int evc2 = ev->count();
    QJsonObject old = mf;
    old.insert(QStringLiteral("id"), 16);
    svc.onClassEvent(old);
    check(ev->count() == evc2, "低于水位的旧事件丢弃");

    // ⑯ provider 占位图（未知 uid / 空 id 都不崩、都返回非空）
    const QImage ph = prov.requestImage(QStringLiteral("nope?t1"), &gotSz, QSize(160, 100));
    check(!ph.isNull() && ph.width() == 160 && ph.height() == 100,
          "provider 未知 uid → 透明占位（尺寸遵循请求）");
    const QImage ph2 = prov.requestImage(QString(), &gotSz, QSize());
    check(!ph2.isNull(), "provider 空 id → 占位（不崩）");

    std::printf("==== %d/%d ====\n", g_pass, g_pass + g_fail);
    return g_fail > 0 ? 1 : 0;
}
