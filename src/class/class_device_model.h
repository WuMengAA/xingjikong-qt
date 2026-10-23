#pragma once
#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

// 班级设备网格模型（#460 · 2026-10-23 · 班级工作台设备网格画面源）
//
// 为什么是 typed model 而不是 QJsonArray/QVariantList：项目 UI 铁律——QML 模型
// 一律 typed QAbstractListModel（role 名静态可查，qmllint/编译期就能发现拼错，
// 不像 QVariantMap 那样运行时才 undefined）。课表域的 SubjectModel/TimeSlotModel
// 是同一范式（src/schedule/）。
//
// 数据来源只有两路，都以云端为权威：
//   · class-subscribed 快照的 devices（整表替换）
//   · class.member.online / class.member.offline（单台 upsert，班级归属云端已判）
// 缩略图不进本模型的数据层：图由 ClassThumbProvider 按 uid 现取，本模型只带
// hasThumb / thumbRev 两个 role 供 QML 拼 "image://classthumb/<uid>?t<rev>"。
class ClassDeviceModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role {
        UidRole = Qt::UserRole + 1,
        NameRole,
        OnlineRole,
        VersionRole,
        LastSeenRole,
        HasThumbRole,
        ThumbRevRole,
    };

    explicit ClassDeviceModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    /** 快照整表替换（class-subscribed 的 devices 数组；旧→新顺序无关，内部按原样装配）。 */
    void resetFromArray(const QJsonArray &devices);
    /**
     * 单台 upsert（member.online，或设备信息补全）。未知 uid 追加到表尾，已知则更新。
     * @return 该设备所在行号；payload 缺 uid 返回 -1（不收无 uid 的行）。
     */
    int upsert(const QJsonObject &dev);
    /**
     * 置在线/离线（member.offline）。未知 uid 不建行、返回 false ——
     * 设备清单以快照为权威：从没在快照里出现过的机器，不该因为一条离线事件
     * 突然出现在网格里（那说明云端班级归属刚变，等下一次快照统一口径）。
     */
    bool setOnline(const QString &uid, bool online);
    /** 缩略图到达：刷新该行（hasThumb/thumbRev 一起变）。未知 uid 返回 false。 */
    bool markThumbnail(const QString &uid, int rev);

    int rowOf(const QString &uid) const;
    QString uidAt(int row) const;
    Q_INVOKABLE int count() const { return m_devices.size(); }
    /** 表内在几台在线（班级工作台头部「N 台在线」；与云端快照 onlineCount 对账用）。 */
    int onlineCount() const;

private:
    struct Device {
        QString uid;
        QString name;               // 空 = 云端没给名，QML 回落显示 uid
        bool online = true;         // 快照缺 online 字段时按在线算（老云端兼容）
        QString version;
        int lastSeenAgoSec = -1;    // 秒；-1 = 未知
        bool hasThumb = false;
        int thumbRev = 0;
    };
    QList<Device> m_devices;
};
