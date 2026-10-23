#include "class_service.h"
#include "../viewerbackend.h"   // logf 声明（全局日志，定义在 main.cpp；探针自备桩）

#include <QDateTime>
#include <QJsonDocument>

ClassService::ClassService(QObject *parent)
    : QObject(parent)
    , m_devModel(new ClassDeviceModel(this))
    , m_evModel(new ClassEventModel(this))
{
}

int ClassService::onlineCount() const
{
    // 以模型为准（快照装配 + member 事件维护），不另记一份数 —— 两份必漂。
    return m_devModel->onlineCount();
}

QImage ClassService::thumbnail(const QString &uid) const
{
    return m_thumbs.value(uid);
}

int ClassService::thumbRev(const QString &uid) const
{
    return m_thumbRev.value(uid, 0);
}

void ClassService::subscribeClass(const QString &code)
{
    const QString c = code.trimmed();
    if (c.isEmpty()) {
        setError(QStringLiteral("班级码不能为空"));
        return;
    }
    // 幂等：已订同班、或同一班的订阅请求还在飞（pending），都不重发 ——
    // 连点两下同一个班不该发出两条 subscribe_class（云端虽幂等，但会回两份
    // 快照、模型重置两轮，Timeline 白闪一下）。
    // ⚠️ 两个状态都要查：已订 A 班时去订 B 班，m_pendingCode=B 而 m_code=A，
    //    只看一个都会漏（探针 ⑬ 实证过这个漏）。
    if ((m_subscribed && m_code == c) || (!m_pendingCode.isEmpty() && m_pendingCode == c))
        return;
    m_pendingCode = c;
    QJsonObject pay;
    pay.insert(QStringLiteral("code"), c);
    emit sendRequested(QStringLiteral("subscribe_class"), pay);
    logf("[class] 订阅班级 %s", c.toUtf8().constData());
}

void ClassService::unsubscribeClass()
{
    if (m_code.isEmpty() && m_pendingCode.isEmpty()) return;   // 没订过：不发
    const QString c = m_code.isEmpty() ? m_pendingCode : m_code;
    QJsonObject pay;
    pay.insert(QStringLiteral("code"), c);
    emit sendRequested(QStringLiteral("unsubscribe_class"), pay);
    // 本地立即置未订（不等回执）：用户意图就是"不看这个班了"。
    // m_lastCode 一并清掉 —— 明确退订的班，重连后不自动回来。
    m_subscribed = false;
    m_pendingCode.clear();
    m_lastCode.clear();
    m_code.clear();
    m_name.clear();
    m_devModel->resetFromArray(QJsonArray());
    m_evModel->resetFromArray(QJsonArray());
    emit subscribedChanged();
    emit classInfoChanged();
    logf("[class] 退订班级 %s", c.toUtf8().constData());
}

void ClassService::onClassSubscribed(const QJsonObject &pay)
{
    applySnapshot(pay);
    m_subscribed = true;
    m_pendingCode.clear();
    m_lastCode = m_code;
    emit subscribedChanged();
    logf("[class] ✅ 已订阅班级 %s（%s）：%d 台设备 / %d 在线",
         m_code.toUtf8().constData(), m_name.toUtf8().constData(),
         m_devModel->count(), onlineCount());
}

void ClassService::onClassUnsubscribed(const QJsonObject &pay)
{
    const QString code = pay.value(QStringLiteral("classCode")).toString();
    // 云端确认退订。本地状态在 unsubscribeClass() 里已置过，这里只对账
    // （云端回的班与本地记的不一致时以云端为准清状态）。
    if (!m_subscribed && m_code.isEmpty()) return;
    if (!code.isEmpty() && !m_code.isEmpty() && code != m_code) {
        logf("[class] WARN 退订回执班码不一致：本地 %s / 云端 %s",
             m_code.toUtf8().constData(), code.toUtf8().constData());
    }
    m_subscribed = false;
    m_code.clear();
    m_name.clear();
    m_devModel->resetFromArray(QJsonArray());
    m_evModel->resetFromArray(QJsonArray());
    emit subscribedChanged();
    emit classInfoChanged();
}

void ClassService::onClassEvent(const QJsonObject &pay)
{
    const QString kind = pay.value(QStringLiteral("kind")).toString();
    const QString uid = pay.value(QStringLiteral("uid")).toString();
    if (kind == QLatin1String("thumbnail")) {
        // 缩略图不进 Timeline（15s/台的图片流会把事件流冲垮），只进画面缓存。
        applyThumbnail(uid, pay.value(QStringLiteral("data")).toObject());
        return;
    }
    m_evModel->prepend(pay);
}

void ClassService::onClassMemberOnline(const QJsonObject &pay)
{
    const QString uid = pay.value(QStringLiteral("uid")).toString();
    if (uid.isEmpty()) return;
    // 设备表 upsert（未知 uid 追加：新机器上线要立刻出现在网格里，
    // 不能等下一次快照 —— 老师正盯着这个网格）。
    QJsonObject dev;
    dev.insert(QStringLiteral("uid"), uid);
    dev.insert(QStringLiteral("online"), true);
    const QString name = pay.value(QStringLiteral("name")).toString();
    if (!name.isEmpty()) dev.insert(QStringLiteral("name"), name);
    m_devModel->upsert(dev);
    m_evModel->prepend(pay);   // 云端 ring buffer 里它也是事件，Timeline 要混排
    emitClassInfo();
}

void ClassService::onClassMemberOffline(const QJsonObject &pay)
{
    const QString uid = pay.value(QStringLiteral("uid")).toString();
    if (uid.isEmpty()) return;
    // 只置位不建行：从没在快照/上线事件里出现的机器，不该因一条离线事件
    // 突然进网格（设备清单以快照为权威）。
    m_devModel->setOnline(uid, false);
    m_evModel->prepend(pay);
    emitClassInfo();
}

void ClassService::onAuthed()
{
    if (m_lastCode.isEmpty()) return;   // 没订过班：什么都不做
    if (m_subscribed) return;           // 还在订着（云端按 ws 记订阅，重连后已失效，
                                         // 但本地回执未到之前不重发，防重复订阅）
    logf("[class] 重新鉴权通过 → 自动重订班级 %s", m_lastCode.toUtf8().constData());
    subscribeClass(m_lastCode);
}

void ClassService::onDisconnected()
{
    // 保留模型数据（避免重连闪烁），只置未订。m_lastCode 留给 onAuthed 重订。
    if (m_subscribed) {
        m_subscribed = false;
        m_pendingCode.clear();
        emit subscribedChanged();
        logf("[class] 与云端断开，班级订阅标记为未订（数据保留，重连自动重订 %s）",
             m_lastCode.toUtf8().constData());
    }
}

void ClassService::onError(const QJsonObject &pay)
{
    // 统一错误通道（viewerbackend onTextMessage 的 error 分支）旁路进来。
    // 认领条件只有一个：有订阅请求在飞（pending）。**不**看 m_subscribed ——
    // 已订 A 班时去订 B 班被拒是真实场景（换班），那时 m_subscribed 仍为 true，
    // 加这道守卫会把换班失败吞掉（UI 傻等 B 班回执）。原订阅不受影响：
    // 只清 pending + 报失败，m_code/m_subscribed 原样留着。
    if (m_pendingCode.isEmpty()) return;
    const QString code = pay.value(QStringLiteral("code")).toString();
    const QString message = pay.value(QStringLiteral("message")).toString();
    const QString reason = message.isEmpty() ? code : message;
    m_pendingCode.clear();
    setError(reason);
    emit subscribeFailed(reason);
    logf("[class] FAIL 订阅班级被拒：%s", reason.toUtf8().constData());
}

void ClassService::applySnapshot(const QJsonObject &pay)
{
    m_code = pay.value(QStringLiteral("classCode")).toString();
    m_name = pay.value(QStringLiteral("name")).toString();
    if (m_name.isEmpty()) m_name = m_code;
    m_devModel->resetFromArray(pay.value(QStringLiteral("devices")).toArray());
    m_evModel->resetFromArray(pay.value(QStringLiteral("events")).toArray());
    // 快照里的设备若历史上收到过缩略图，恢复 hasThumb/rev（不清缓存）：重订阅后
    // 第一张新图到达前，QML 还能显示上一轮的最后一帧（比突然全空白好）。
    for (auto it = m_thumbRev.constBegin(); it != m_thumbRev.constEnd(); ++it) {
        m_devModel->markThumbnail(it.key(), it.value());
    }
    emitClassInfo();
}

void ClassService::applyThumbnail(const QString &uid, const QJsonObject &data)
{
    if (uid.isEmpty()) return;
    const QString b64 = data.value(QStringLiteral("b64")).toString();
    if (b64.isEmpty()) return;
    const QByteArray raw = QByteArray::fromBase64(b64.toUtf8());
    if (raw.isEmpty()) return;
    const QImage img = QImage::fromData(raw);
    if (img.isNull()) {
        logf("[class] WARN 缩略图解码失败（uid=%s，%d 字节）",
             uid.toUtf8().constData(), raw.size());
        return;
    }
    m_thumbs.insert(uid, img);
    const int rev = m_thumbRev.value(uid, 0) + 1;
    m_thumbRev.insert(uid, rev);
    m_devModel->markThumbnail(uid, rev);   // 未知 uid 返回 false：不建行，仅缓存
    emit thumbnailChanged(uid);
}

void ClassService::emitClassInfo()
{
    emit classInfoChanged();
}

void ClassService::setError(const QString &msg)
{
    m_lastError = msg;
    emit classError(msg);
}
