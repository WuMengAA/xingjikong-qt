#pragma once
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include "class_device_model.h"
#include "class_event_model.h"

// 班级域服务（#460 · 2026-10-23 · viewerbackend P1 拆分第一刀：
// 「按业务拆 DeviceService/ClassService/FileService」台账里的 ClassService）
//
// 云端协议（2026-10-23 #458 class-events.js + 协议规范 v1）：
//   出：subscribe_class {code} / unsubscribe_class {code}
//   进：class-subscribed {ok,classCode,name,devices,onlineCount,events,at}
//       class-unsubscribed {ok,classCode}
//       class.event / class.member.online / class.member.offline
//       {classCode,id,at,kind,uid,data}
//
// 设计约束：
//   · 本类**不碰 WS**：出海走信号 sendRequested（backend 连到它的 sendEnvelope），
//     入站由 backend 的 onClassDomainMessage 调过来 —— 零循环依赖，探针可离线
//     单独链它跑全场景（这是能被测试的前提）。
//   · 班级归属云端已判（canAccess + knownDevices 表），本类不再校验权限，
//     也不信任载荷里的任何班级字段：classCode 只从 class-subscribed 回包里取。
//   · 断线保留数据（避免重连闪烁），置 subscribed=false；重新鉴权后若曾订过班
//     （m_lastCode）自动重订（onAuthed）—— 老师不该因为一次网络抖动就丢视图。
class ClassService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool subscribed READ isSubscribed NOTIFY subscribedChanged)
    Q_PROPERTY(QString classCode READ classCode NOTIFY classInfoChanged)
    Q_PROPERTY(QString className READ className NOTIFY classInfoChanged)
    Q_PROPERTY(int onlineCount READ onlineCount NOTIFY classInfoChanged)
    Q_PROPERTY(int deviceCount READ deviceCount NOTIFY classInfoChanged)
    Q_PROPERTY(ClassDeviceModel *deviceModel READ deviceModel CONSTANT)
    Q_PROPERTY(ClassEventModel *eventModel READ eventModel CONSTANT)
    /** 最近一条班级域错误的人话（订阅被拒等；空 = 无）。UI 收到非空弹一次。 */
    Q_PROPERTY(QString lastError READ lastError NOTIFY classError)

public:
    explicit ClassService(QObject *parent = nullptr);

    bool isSubscribed() const { return m_subscribed; }
    QString classCode() const { return m_code; }
    QString className() const { return m_name; }
    int onlineCount() const;
    int deviceCount() const { return m_devModel->count(); }
    ClassDeviceModel *deviceModel() const { return m_devModel; }
    ClassEventModel *eventModel() const { return m_evModel; }
    QString lastError() const { return m_lastError; }

    /** 缩略图（provider 用；无则空图）。 */
    QImage thumbnail(const QString &uid) const;
    int thumbRev(const QString &uid) const;

    // ── 出站动作（QML 调） ──
    Q_INVOKABLE void subscribeClass(const QString &code);
    Q_INVOKABLE void unsubscribeClass();

    // ── 入站（backend 的 onClassDomainMessage 调；参数是云端 payload 原样） ──
    void onClassSubscribed(const QJsonObject &pay);
    void onClassUnsubscribed(const QJsonObject &pay);
    void onClassEvent(const QJsonObject &pay);
    void onClassMemberOnline(const QJsonObject &pay);
    void onClassMemberOffline(const QJsonObject &pay);

    // ── 生命周期（backend 调） ──
    void onAuthed();                 ///< 重新鉴权通过：曾订过班就自动重订
    void onDisconnected();           ///< 断线：置未订但保留数据与 m_lastCode
    /** 统一错误通道旁路：pending 窗口内的 error 视为订阅失败（不静默）。 */
    void onError(const QJsonObject &pay);

signals:
    /** 出海请求（backend → sendEnvelope）。 */
    void sendRequested(const QString &type, const QJsonObject &payload);
    void subscribedChanged();
    /** 班级信息（code/name/在线数/设备数）变化。 */
    void classInfoChanged();
    /** 班级域错误（订阅被拒等）。 */
    void classError(const QString &message);
    /** 订阅失败（云端拒 / 超窗口错误）；reason 是人话。 */
    void subscribeFailed(const QString &reason);
    /** 某台设备缩略图更新（QML 拼 source 的 rev 依据）。 */
    void thumbnailChanged(const QString &uid);

private:
    void applySnapshot(const QJsonObject &pay);
    void applyThumbnail(const QString &uid, const QJsonObject &data);
    void emitClassInfo();
    void setError(const QString &msg);

    ClassDeviceModel *m_devModel = nullptr;
    ClassEventModel *m_evModel = nullptr;
    QString m_code;              ///< 已订阅的班（空 = 没订）
    QString m_name;              ///< 班名（快照给；回落 = code）
    bool m_subscribed = false;
    QString m_pendingCode;       ///< 已发订阅请求、还没回执的班
    QString m_lastCode;          ///< 最近成功订过的班（断线重订用）
    QString m_lastError;
    QHash<QString, QImage> m_thumbs;    ///< uid → 最新缩略图（provider 现取）
    QHash<QString, int> m_thumbRev;     ///< uid → 缩略图修订号（QML source 防缓存）
};
