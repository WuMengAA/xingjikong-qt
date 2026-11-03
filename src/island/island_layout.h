#pragma once
#include <QAbstractListModel>
#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QVector>

// 灵动岛 · 组件布局域（2026-11-03 · 用户点单「多一些控件还有可配置可拖动组件」）
//
// ── 它解决的是什么 ────────────────────────────────────────────────
//   改造前 `IslandRoot.qml:308-324` 的 faceModel 是**写死的六个面**：
//   概 / 今 / 媒(按需) / 笔 / 设 / 自(按需)。老师既不能关掉不看的面（比如"笔"），也不能把
//   常用的"今"挪到最前，更不能调某个面的大小。ClassIsland 那套「组件库 + 拖拽排序 +
//   组件属性」正是这条的反面。
//
// ── 为什么是 C++ 模型而不是 QML 里存数组 ─────────────────────────────
//   AGENTS.md §10.7/§10.9：QML 只画图。顺序、启用、尺寸这些是**状态**，
//   状态落 QSettings 才有跨重启一致性；写在 QML 属性里重启即丢，且门禁
//   `qml-no-network` 的后续扩展会盯 QML 里的业务计算。
//
// ── 关键设计决定（都有代价，记在这里防以后"优化"回去）────────────────
//   ① **不自造面**：本模型只能重排/启停/调参**已存在的面**。允许动态增面等于允许
//      QML 里 `Loader { source: 任意字符串 }`，那是任意代码执行面 —— 插件推岛都还有四道闸，
//      主界面不该比插件还松。
//   ② **未知 id 一律丢弃**（不是静默转成"概"）：面列表来自磁盘，磁盘可能被旧版本写过、
//      也可能被手工改坏。把陌生 id 当"概"显示，现场表现是"关掉了还冒出来一页"，
//      比直接消失更难查。丢完仍为空 ⇒ 回落内置默认序。
//   ③ **"媒"与"自"是条件面**：没有媒体在播 / 没有自动化规则时本来就不该出现。
//      它们在本模型里是 `requiresLive` 的面，`visible` 由**运行态**决定而不是由用户开关决定；
//      用户开关只决定"条件满足时要不要给它位置"。
//   ④ **顺序即数组顺序**，`order` 字段只是给 QML 显示的序号文本，不参与排序 ——
//      两套排序真源必然漂移。
//
// ── 持久化 ────────────────────────────────────────────────────────
//   QSettings 键 `island/layout`（JSON 字符串）。**键名是门禁契约**（见 AgentSettings.h
//   头注释），改它等于让所有已配置的教室机一夜之间回到默认序。
//   读失败 / 空 / 解析出错 ⇒ 用内置默认，不崩、不弹错（现场不会有人去改注册表）。

class IslandLayoutModel : public QAbstractListModel
{
    Q_OBJECT
    /** 可滑动面数量（QML 侧 `facesOn` 判 `>= 2` 用）。 */
    Q_PROPERTY(int count READ rowCount NOTIFY layoutChanged)
    /** 一句话摘要，如「6 个组件 · 已启用 5」——设置页卡片副标题。 */
    Q_PROPERTY(QString summary READ summary NOTIFY layoutChanged)
    /** 至少有一个面启用（全关掉时岛不该显示面签区）。 */
    Q_PROPERTY(bool anyEnabled READ anyEnabled NOTIFY layoutChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,   // int，与 IslandRoot.qml 既有 face id 对齐
        LabelRole,                   // QString，「概」这种单字面签
        TitleRole,                   // QString，设置页里的完整名（「概览」「今日课表」）
        NoteRole,                    // QString，一句话说明这个面能干什么
        EnabledRole,                 // bool，用户开关
        OrderRole,                   // int，1 起的显示序号（仅供显示，不参与排序）
        RequiresLiveRole,            // bool，条件面（媒体/自动化），QML 侧按运行态决定是否出现
        ScaleRole,                   // int，面内容缩放百分比 80-140，默认 100
    };
    Q_ENUM(Roles)

    explicit IslandLayoutModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    QHash<int, QByteArray> roleNames() const override;

    QString summary() const;
    bool anyEnabled() const;

    /** QML 侧可滑动面的快照（`[{id,label}]`，已按序、已过滤未启用与条件不满足的）。
     *  IslandRoot.qml 的 faceModel 改成读它。**不许在 QML 里自己过滤** ——
     *  两处过滤必然漂移出"关掉的页面还能划到"这种现场无法解释的怪事。 */
    Q_INVOKABLE QVariantList faceList() const;

    /** 拖动排序：把 from 行挪到 to 行（QML 的 ListView DelegateModel 拖动结束时调）。
     *  越界直接忽略（不留半截状态）。 */
    Q_INVOKABLE void moveRow(int from, int to);

    /** 启用/停用某一行（设置页开关）。未知 id 忽略。 */
    Q_INVOKABLE void setEnabledById(int id, bool on);
    // 2026-11-03 探针诊断用：空操作但发 layoutChanged，验证 QML 对 faceList() 的重算时机
    Q_INVOKABLE void touch();

    /** 缩放：80..140，越界夹紧。 */
    Q_INVOKABLE void setScaleById(int id, qint64 scale);

    /** 恢复内置默认序（设置页"恢复默认"）。 */
    Q_INVOKABLE void resetToDefault();

    /** 导出/导入（供诊断与探针）。 */
    QJsonArray toJson() const;
    bool fromJson(const QJsonArray &arr);

signals:
    void layoutChanged();

private:
    struct Row {
        int id = 0;
        QString label;
        QString title;
        QString note;
        bool enabled = true;
        bool requiresLive = false;
        int scale = 100;
    };

    static QVector<Row> builtinRows();
    void persist() const;
    void reload();
    int indexOfId(int id) const;

    QVector<Row> m_rows;
};

/** 全机唯一实例（岛壳与设置页是两个独立的 QQuickWidget engine，必须拿同一个对象，
 *  否则在设置页拖完顺序，岛那边还是老序 —— 而且看起来"保存成功"了）。
 *  不注册成 QML 单例模块：`IslandRoot.qml` 是**两端共用**的，管理端没有这个模块，
 *  `import` 未注册模块是硬错误（整页空白且不报业务错，2026-11-02 踩过）。
 *  改走 context property + QML 侧 `typeof` 守卫，缺了自动回落内置默认序。 */
IslandLayoutModel *islandLayoutInstance();
