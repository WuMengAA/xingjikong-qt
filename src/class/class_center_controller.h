// 班级中心控制器（2026-10-28 · B0 重构移植 / B1 真数据接线）
// ⚠️ 存在理由（用户 2026-10-28 00:31 点名铁律）：**QML 永远不写逻辑与通信代码，全部交给 C++**。
//    本类承载原先散在 ClassCenterPage/ClassDeviceDrawer/RemoteDesktopStage/OpHeroButton 等
//    里的全部计算：在线率与圆饼弧端点、网格列宽、远程桌面 4:3/9:16 比例与缩放、操作面板色板与
//    装饰件偏转角、抽屉开合与选中态、面包屑与异常行文案、指针归一化、长按 3 秒进度；QML 退化为纯声明式。
// ── 数据来源（2026-10-28 B1：占位数据已全部换成云端真数据）────────────────
//   setClasses ← /api/classes（含每班 devices/onlineCount）｜ setDevices ← /api/devices（与班级
//   devices 求交）｜ setEvents ← /api/events（近 50 条取 warn/error）｜ setThumb ← 缩略图修订号。
//   ⚠️ 本类**不认识 ViewerBackend**（刻意）：入站由宿主灌入、出站走 actionOut 由宿主转发 ——
//      好处是探针能单独链它跑断言，不依赖网络/音频/托盘那一整条。
//
// ── 操作名 → 指令（B1 铁律：只转发既有词表、零新攻击面）──────────────────
//   notify/lock/reboot/shutdown/list_schedules/process_list/media_list 逐条核对过被控端
//   executeAction 分支与 kActionNames。「监控」不造假动作：回放能力在管理端（MonitorPage），
//   故走 navigateRequested("monitor") —— 跳转不下发指令。
#pragma once

#include <QObject>
#include <QVariantList>
#include <QString>
#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QTimer>
#include <QElapsedTimer>

class ClassCenterController : public QObject
{
    Q_OBJECT
    static constexpr qreal kStatCardH = 128.0;   // 统计卡高：QML 两张卡共用，避免 128 写两遍

    // ── 视口尺寸（QML 写入，驱动一切依赖尺寸的计算）─────────────────
    Q_PROPERTY(qreal viewWidth  READ viewWidth  WRITE setViewWidth  NOTIFY geometryChanged)
    Q_PROPERTY(qreal viewHeight READ viewHeight WRITE setViewHeight NOTIFY geometryChanged)
    /** 页面是否可见：可见才开 15s 轮询（不可见还轮询 = 白烧流量）。 */
    Q_PROPERTY(bool  pageActive READ pageActive WRITE setPageActive NOTIFY pageActiveChanged)

    // ── 数据（全部来自云端真数据）──────────────────────────────────
    Q_PROPERTY(QVariantList classes   READ classes   NOTIFY dataChanged)
    Q_PROPERTY(QVariantList anomalies READ anomalies NOTIFY dataChanged)
    Q_PROPERTY(QVariantList devices   READ devices   NOTIFY selectionChanged)
    Q_PROPERTY(bool hasClasses   READ hasClasses   NOTIFY dataChanged)
    Q_PROPERTY(bool hasAnomalies READ hasAnomalies NOTIFY dataChanged)
    Q_PROPERTY(bool hasDevices   READ hasDevices   NOTIFY selectionChanged)
    /** 一行概况文本（班级数/设备数/在线数），QML 不做字符串拼接。 */
    Q_PROPERTY(QString classSummaryText READ classSummaryText NOTIFY dataChanged)
    /** 异常面板为空时的说明（空态也要有人话，不能一片空白）。 */
    Q_PROPERTY(QString anomalyEmptyText READ anomalyEmptyText NOTIFY dataChanged)
    /** 班级改绑选项（抽屉「班级编辑」）：[{code,name,variant}] —— 当前班 variant="filled"。 */
    Q_PROPERTY(QVariantList classOptions READ classOptions NOTIFY selectionChanged)

    // ── 统计（C++ 算好，QML 不除不取整）────────────────────────────
    Q_PROPERTY(int totalDevices   READ totalDevices   NOTIFY dataChanged)
    Q_PROPERTY(int onlineDevices  READ onlineDevices  NOTIFY dataChanged)
    Q_PROPERTY(int onlinePercent  READ onlinePercent  NOTIFY dataChanged)   // 已取整百分数
    Q_PROPERTY(QString onlinePercentText READ onlinePercentText NOTIFY dataChanged)
    Q_PROPERTY(QString statSummaryText   READ statSummaryText   NOTIFY dataChanged)

    // 抽屉几何与遮罩：开合判断的结果值也由 C++ 给，QML 不写三元
    Q_PROPERTY(qreal drawerWidth    READ drawerWidth    NOTIFY geometryChanged)
    Q_PROPERTY(qreal drawerX        READ drawerX        NOTIFY drawerOpenChanged)
    Q_PROPERTY(qreal overlayOpacity READ overlayOpacity NOTIFY drawerOpenChanged)
    // 视图态：直接给可见性布尔，QML 不写 "grid"==="remote" 这类比较
    Q_PROPERTY(bool remoteVisible READ remoteVisible NOTIFY stageChanged)

    // ── 圆饼弧（PathArc 端点由 C++ 算；QML 不再出现 sin/cos）────────
    Q_PROPERTY(qreal donutEndX     READ donutEndX     NOTIFY dataChanged)
    Q_PROPERTY(qreal donutEndY     READ donutEndY     NOTIFY dataChanged)
    Q_PROPERTY(bool  donutLargeArc READ donutLargeArc NOTIFY dataChanged)

    // ── 班级阵列网格（列数与格宽由 C++ 算）─────────────────────────
    Q_PROPERTY(int  gridColumns   READ gridColumns   NOTIFY geometryChanged)
    Q_PROPERTY(qreal gridCellWidth READ gridCellWidth NOTIFY geometryChanged)
    // 统计卡宽度（窄 1/3、宽 2/3）：QML 里不再写 (w-24)/3 这种算式
    Q_PROPERTY(qreal statCardNarrowW READ statCardNarrowW NOTIFY geometryChanged)
    Q_PROPERTY(qreal statCardWideW   READ statCardWideW   NOTIFY geometryChanged)
    // 窄屏（手机竖屏 ≤640）：边距减半、阵列单列、统计卡堆叠 —— QML 里不写 `width<640 ?`，避免漂移。
    Q_PROPERTY(bool  narrow       READ narrow       NOTIFY geometryChanged)
    Q_PROPERTY(qreal pageMargin   READ pageMargin   NOTIFY geometryChanged)
    Q_PROPERTY(int   statsColumns READ statsColumns NOTIFY geometryChanged)
    Q_PROPERTY(qreal statCardH    READ statCardH    NOTIFY geometryChanged)

    // ── 远程桌面舞台比例（4:3 容器 + 9:16 面板 + 统一缩放）──────────
    Q_PROPERTY(qreal stageViewW READ stageViewW WRITE setStageViewW NOTIFY geometryChanged)
    Q_PROPERTY(qreal stageViewH READ stageViewH WRITE setStageViewH NOTIFY geometryChanged)
    Q_PROPERTY(qreal stageLeftW  READ stageLeftW  NOTIFY geometryChanged)
    Q_PROPERTY(qreal stageRightW READ stageRightW NOTIFY geometryChanged)
    Q_PROPERTY(qreal stageScale  READ stageScale  NOTIFY geometryChanged)
    Q_PROPERTY(qreal stageFrameH READ stageFrameH NOTIFY geometryChanged)
    Q_PROPERTY(qreal stageBoxH   READ stageBoxH   NOTIFY geometryChanged)

    // ── 抽屉与视图态（开合是状态，不是 QML 里的 function）───────────
    Q_PROPERTY(bool    drawerOpen READ drawerOpen NOTIFY drawerOpenChanged)
    Q_PROPERTY(QString stage      READ stage      NOTIFY stageChanged)   // "grid" | "remote"
    Q_PROPERTY(bool    editing READ editing NOTIFY editingChanged)

    // ── 选中目标（抽屉面包屑 + 下发目标）───────────────────────────
    Q_PROPERTY(QString selectedClassCode READ selectedClassCode NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedClassName READ selectedClassName NOTIFY selectionChanged)
    /** 面包屑：学校 > (届) > 班级；层级是否出现由 C++ 判断，QML 只显示。 */
    Q_PROPERTY(QString breadcrumbText READ breadcrumbText NOTIFY selectionChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY targetChanged)
    Q_PROPERTY(QString targetUid  READ targetUid  WRITE setTargetUid  NOTIFY targetChanged)
    Q_PROPERTY(QString targetName READ targetName WRITE setTargetName NOTIFY targetChanged)
    /** 缩略图源：image://classthumb/<uid>?t<rev>；无图时为空串（QML 据此显示空态）。 */
    Q_PROPERTY(QString thumbSource READ thumbSource NOTIFY thumbChanged)
    Q_PROPERTY(bool    thumbAvailable READ thumbAvailable NOTIFY thumbChanged)

    // ── 远程画面（真实 VNC 流；未接通时为空串，界面显示说明文案）────
    Q_PROPERTY(QString liveSource READ liveSource NOTIFY liveChanged)
    Q_PROPERTY(bool    liveActive READ liveActive NOTIFY liveChanged)
    /** 画面区状态文案：未选设备 / 正在接通 / 等画面。QML 不写三层三元。 */
    Q_PROPERTY(QString liveStateText READ liveStateText NOTIFY liveChanged)

    // ── 通知草稿（QML 输入框双向绑定；C++ 透传与校验）────────────────
    Q_PROPERTY(QString notifyTitle   READ notifyTitle   WRITE setNotifyTitle   NOTIFY notifyDraftChanged)
    Q_PROPERTY(QString notifyContent READ notifyContent WRITE setNotifyContent NOTIFY notifyDraftChanged)
    /** 下发结果（给人看的文本；QML 不做字符串拼接）。 */
    Q_PROPERTY(QString lastOpText READ lastOpText NOTIFY opResultChanged)
    Q_PROPERTY(bool    lastOpOk   READ lastOpOk   NOTIFY opResultChanged)
    /** 结果条配色（成功绿 / 失败红）：结果值由 C++ 给，QML 不写三元。 */
    Q_PROPERTY(QColor  lastOpColor READ lastOpColor NOTIFY opResultChanged)

    // ── 紧急通知长按（原型：长按 3 秒，进度填充 3s linear，松手回弹）──
    Q_PROPERTY(qreal emergencyHoldProgress READ emergencyHoldProgress NOTIFY emergencyChanged)
    Q_PROPERTY(bool  emergencyArmed READ emergencyArmed NOTIFY emergencyChanged)

    // ── 操作面板色板（主题判断的结果值，QML 不再写三元）─────────────
    Q_PROPERTY(QColor notifyC0 READ notifyC0 NOTIFY paletteChanged)
    Q_PROPERTY(QColor notifyC1 READ notifyC1 NOTIFY paletteChanged)
    Q_PROPERTY(QColor notifyC2 READ notifyC2 NOTIFY paletteChanged)
    Q_PROPERTY(QColor notifyText READ notifyText NOTIFY paletteChanged)
    Q_PROPERTY(qreal  notifyFocalX READ notifyFocalX NOTIFY paletteChanged)
    Q_PROPERTY(qreal  notifyIconRot READ notifyIconRot NOTIFY paletteChanged)
    Q_PROPERTY(qreal  notifyBarRot  READ notifyBarRot  NOTIFY paletteChanged)

    Q_PROPERTY(QColor monitorC0 READ monitorC0 NOTIFY paletteChanged)
    Q_PROPERTY(QColor monitorC1 READ monitorC1 NOTIFY paletteChanged)
    Q_PROPERTY(QColor monitorC2 READ monitorC2 NOTIFY paletteChanged)
    Q_PROPERTY(QColor monitorText READ monitorText NOTIFY paletteChanged)
    Q_PROPERTY(qreal  monitorFocalX READ monitorFocalX NOTIFY paletteChanged)
    Q_PROPERTY(qreal  monitorIconRot READ monitorIconRot NOTIFY paletteChanged)
    Q_PROPERTY(qreal  monitorBarRot  READ monitorBarRot  NOTIFY paletteChanged)

    // 装饰件透明度（原型：麦克风 50%、长条框 30%）
    Q_PROPERTY(qreal decoIconOpacity READ decoIconOpacity NOTIFY paletteChanged)
    Q_PROPERTY(qreal decoBarOpacity  READ decoBarOpacity  NOTIFY paletteChanged)
    // 2:1 主按钮的径向渐变半径：随视口缩放，避免 QML 里出现 Math.max
    Q_PROPERTY(qreal heroGradRadius READ heroGradRadius NOTIFY geometryChanged)

    // 条状按钮配色（明黄 / 亮红 / 中性）
    Q_PROPERTY(QColor warnBase   READ warnBase   NOTIFY paletteChanged)
    Q_PROPERTY(QColor warnFg     READ warnFg     NOTIFY paletteChanged)
    Q_PROPERTY(QColor warnShade  READ warnShade  NOTIFY paletteChanged)
    Q_PROPERTY(QColor dangerBase READ dangerBase NOTIFY paletteChanged)
    Q_PROPERTY(QColor dangerFg   READ dangerFg   NOTIFY paletteChanged)
    Q_PROPERTY(QColor dangerShade READ dangerShade NOTIFY paletteChanged)
    Q_PROPERTY(QColor neutralBase READ neutralBase NOTIFY paletteChanged)
    Q_PROPERTY(QColor neutralFg   READ neutralFg   NOTIFY paletteChanged)
    Q_PROPERTY(QColor neutralShade READ neutralShade NOTIFY paletteChanged)

public:
    explicit ClassCenterController(QObject *parent = nullptr);

    qreal viewWidth() const { return m_viewW; }
    qreal viewHeight() const { return m_viewH; }
    void setViewWidth(qreal w);
    void setViewHeight(qreal h);
    bool pageActive() const { return m_pageActive; }
    void setPageActive(bool on);

    QVariantList classes() const { return m_classes; }
    QVariantList anomalies() const { return m_anomalies; }
    QVariantList devices() const { return m_devices; }
    bool hasClasses() const { return !m_classes.isEmpty(); }
    bool hasAnomalies() const { return !m_anomalies.isEmpty(); }
    bool hasDevices() const { return !m_devices.isEmpty(); }
    QString classSummaryText() const { return m_classSummary; }
    QString anomalyEmptyText() const { return m_anomalyEmpty; }
    QVariantList classOptions() const { return m_classOptions; }

    int totalDevices() const { return m_total; }
    int onlineDevices() const { return m_online; }
    int onlinePercent() const { return m_percent; }
    QString onlinePercentText() const;
    QString statSummaryText() const;
    qreal drawerWidth() const;
    qreal drawerX() const;
    qreal overlayOpacity() const { return m_drawerOpen ? 0.2 : 0.0; }
    bool remoteVisible() const { return m_stage == QStringLiteral("remote"); }

    qreal donutEndX() const { return m_donutEndX; }
    qreal donutEndY() const { return m_donutEndY; }
    bool  donutLargeArc() const { return m_donutLarge; }

    int   gridColumns() const { return m_cols; }
    qreal gridCellWidth() const { return m_cellW; }
    qreal statCardNarrowW() const { return m_statNarrowW; }
    qreal statCardWideW() const { return m_statWideW; }
    bool narrow() const { return m_narrow; }            qreal pageMargin() const { return m_pageMargin; }
    int  statsColumns() const { return m_statsCols; }   qreal statCardH() const { return kStatCardH; }

    qreal stageViewW() const { return m_stageW; }
    qreal stageViewH() const { return m_stageH; }
    void setStageViewW(qreal w);
    void setStageViewH(qreal h);
    qreal stageLeftW() const { return m_stageLeftW; }
    qreal stageRightW() const { return m_stageRightW; }
    qreal stageScale() const { return m_stageScale; }
    qreal stageFrameH() const { return m_stageLeftW * (9.0 / 16.0); }
    qreal stageBoxH() const { return m_stageH * m_stageScale; }

    bool drawerOpen() const { return m_drawerOpen; }
    QString stage() const { return m_stage; }
    bool editing() const { return m_editing; }

    QString selectedClassCode() const { return m_selCode; }
    QString selectedClassName() const { return m_selName; }
    QString breadcrumbText() const { return m_breadcrumb; }
    QString deviceName() const { return m_targetName; }
    QString targetUid() const { return m_targetUid; }
    QString targetName() const { return m_targetName; }
    void setTargetUid(const QString &uid);
    void setTargetName(const QString &n);
    QString thumbSource() const { return m_thumbSource; }
    bool thumbAvailable() const { return !m_thumbSource.isEmpty(); }

    QString liveSource() const { return m_liveSource; }
    bool liveActive() const { return m_liveActive; }
    QString liveStateText() const { return m_liveText; }

    QString notifyTitle() const { return m_notifyTitle; }
    QString notifyContent() const { return m_notifyContent; }
    void setNotifyTitle(const QString &t);
    void setNotifyContent(const QString &c);

    QString lastOpText() const { return m_lastOpText; }
    bool    lastOpOk() const { return m_lastOpOk; }
    QColor  lastOpColor() const
    {
        return m_lastOpOk ? QColor(QStringLiteral("#22C55E")) : QColor(QStringLiteral("#EF4444"));
    }

    qreal emergencyHoldProgress() const { return m_holdProgress; }
    bool  emergencyArmed() const { return m_holdProgress >= 1.0; }

    // 色板（固定常量，与蓝图/原型逐条对应；改色只改这里）
    QColor notifyC0() const { return QColor(QStringLiteral("#6EE7B7")); }
    QColor notifyC1() const { return QColor(QStringLiteral("#22C55E")); }
    QColor notifyC2() const { return QColor(QStringLiteral("#16A34A")); }
    QColor notifyText() const { return QColor(QStringLiteral("#14532D")); }
    qreal  notifyFocalX() const { return 0.2; }
    qreal  notifyIconRot() const { return 45.0; }
    qreal  notifyBarRot() const { return -27.0; }

    QColor monitorC0() const { return QColor(QStringLiteral("#BAE6FD")); }
    QColor monitorC1() const { return QColor(QStringLiteral("#38BDF8")); }
    QColor monitorC2() const { return QColor(QStringLiteral("#0284C7")); }
    QColor monitorText() const { return QColor(QStringLiteral("#0C4A6E")); }
    qreal  monitorFocalX() const { return 0.8; }
    // 监控 = 镜像方向与角度（蓝图定稿）
    qreal  monitorIconRot() const { return -45.0; }
    qreal  monitorBarRot() const { return 27.0; }

    qreal decoIconOpacity() const { return 0.5; }
    qreal decoBarOpacity() const { return 0.3; }
    qreal heroGradRadius() const { return qMax(m_viewW, m_viewH) * 0.45; }

    QColor warnBase() const { return QColor(QStringLiteral("#FBBF24")); }
    QColor warnFg() const { return QColor(QStringLiteral("#FFFFFF")); }
    QColor warnShade() const { return QColor(QStringLiteral("#D97706")); }
    QColor dangerBase() const { return QColor(QStringLiteral("#EF4444")); }
    QColor dangerFg() const { return QColor(QStringLiteral("#FFFFFF")); }
    QColor dangerShade() const { return QColor(QStringLiteral("#B91C1C")); }
    QColor neutralBase() const { return QColor(QStringLiteral("#E5E7EB")); }
    QColor neutralFg() const { return QColor(QStringLiteral("#374151")); }
    QColor neutralShade() const { return QColor(QStringLiteral("#CBD5E1")); }

    // ── 数据入口（宿主 main.cpp 灌，QML 不调）──────────────────────
    void setClasses(const QJsonArray &classes);
    void setDevices(const QJsonArray &devices);
    void setEvents(const QJsonArray &events);
    /** 模块二（2026-10-28 蓝图）：老师/班主任身份标记。
     *  置 true 后，若云端只回了一个班（带票据时 /api/classes 本来就是"自己的班"），
     *  自动选中并打开该班抽屉 —— 这就是蓝图说的"直接沉浸式展示本班面板"。
     *  管理员保持总览阵列（多班时不擅自替人挑班）。 */
    void setTeacherScope(bool on);
    /** 缩略图修订号（ClassService::thumbnailChanged）：rev 变化 ⇒ 图源换 t 参数。 */
    void setThumb(const QString &uid, int rev);
    /** 远控画面：宿主按 backend.frameSource 与帧到达驱动。 */
    void setLiveActive(bool on);
    void bumpLiveFrame();

    // ── QML 入口（只转发意图，不携带业务判断）──────────────────────
    Q_INVOKABLE void openDrawer();
    Q_INVOKABLE void closeDrawer();
    Q_INVOKABLE void toggleDrawer();
    /** 点卡片：选中该班并展开抽屉（两件事一起做，避免 QML 里连写两句）。 */
    Q_INVOKABLE void openClass(const QString &code);
    Q_INVOKABLE void selectDevice(const QString &uid);
    Q_INVOKABLE void enterRemote();
    Q_INVOKABLE void backToGrid();
    Q_INVOKABLE void toggleEditing();
    Q_INVOKABLE void refresh();
    /** 操作面板/抽屉的统一入口：QML 只发名字，映射在 C++。 */
    Q_INVOKABLE void requestOp(const QString &name);
    /** 抽屉「班级编辑」：把这个设备改绑到 code 这个班（云端写操作，走 ClassAdmin）。 */
    Q_INVOKABLE void rebindSelectedTo(const QString &code);
    /** 紧急通知长按：按下开始计时，松手/移出取消。 */
    Q_INVOKABLE void beginEmergencyHold();
    Q_INVOKABLE void cancelEmergencyHold();
    /** 远程画面指针：x/y 是画面矩形内的局部坐标，归一化与夹取在 C++ 做。 */
    Q_INVOKABLE void stagePointer(const QString &kind, qreal x, qreal y,
                                  const QString &button = QStringLiteral("left"));

signals:
    void geometryChanged();
    void dataChanged();
    void drawerOpenChanged();
    void stageChanged();
    void paletteChanged();
    void editingChanged();
    void selectionChanged();
    void targetChanged();
    void thumbChanged();
    void notifyDraftChanged();
    void opResultChanged();
    void pageActiveChanged();
    void liveChanged();
    void emergencyChanged();
    // 操作请求上抛（QML 点击的直观名字，供探针/日志断言用）
    void opRequested(const QString &name);
    // 真正要下发的指令：**不直接持有 ViewerBackend** —— 控制器只负责"把按钮名翻成
    // 既有指令词表里的动作"，由 main.cpp 一行 connect 转发 backend->sendActionTo。
    void actionOut(const QString &uid, const QString &action, const QJsonObject &params);
    /** 需要宿主去拉数据（15s 轮询 / 手动刷新都走它）。 */
    void refreshRequested();
    /** 界面跳转（「监控」→ 运行监控页，回放入口在那页）。 */
    void navigateRequested(const QString &section);
    /** 进/出远程桌面：宿主负责 setCurrentUid（触发远控接管）与 stop。 */
    void remoteEntered(const QString &uid);
    void remoteLeft(const QString &uid);
    /** 画面指针（归一化 0..1，宿主转 backend->sendPointer）。 */
    void pointerOut(const QString &kind, qreal nx, qreal ny, const QString &button);
    /** 班级改绑（uid 改到 classCode）：宿主转 backend->classAdmin()->bindDevice。 */
    void rebindRequested(const QString &uid, const QString &classCode);

private:
    bool mapOpToAction(const QString &name, QString *action, QJsonObject *params) const;
    void recomputeGeometry();
    void recomputeStage();
    void recomputeStats();
    void rebuildDevices();
    void rebuildClassOptions();
    void rebuildAnomalies();
    void rebuildBreadcrumb();
    void rebuildThumb();
    void updateLiveText();
    void tickHold();
    void setOpResult(bool ok, const QString &text);
    /** 模块二：老师身份 + 恰好一个班 + 还没自动进过 ⇒ 打开该班（只做一次）。 */
    void maybeAutoEnterMyClass();

    // 模块二状态
    bool m_teacherScope = false;
    bool m_autoEnteredClass = false;

    qreal m_viewW = 1200.0;
    qreal m_viewH = 800.0;
    qreal m_stageW = 1200.0;
    qreal m_stageH = 800.0;
    bool  m_pageActive = false;

    // 原始云端数据（保持 JSON 原样，派生值都在下面重算）
    QJsonArray m_rawClasses;
    QJsonArray m_rawDevices;
    QJsonArray m_rawEvents;
    QMap<QString, QString> m_uidClass;      ///< uid → 班级名（异常行归属用）
    QMap<QString, QString> m_uidDeviceName; ///< uid → 设备名

    QVariantList m_classes;
    QVariantList m_anomalies;
    QVariantList m_devices;
    QVariantList m_classOptions;
    QString m_classSummary;
    QString m_anomalyEmpty;

    int m_total = 0;
    int m_online = 0;
    int m_percent = 0;

    // 圆饼几何（半径 r=32、圆心 40,40 与 QML 里的尺寸约定一致）
    qreal m_donutEndX = 40.0;
    qreal m_donutEndY = 8.0;
    bool  m_donutLarge = false;

    int   m_cols = 2;
    qreal m_cellW = 260.0;
    qreal m_statNarrowW = 200.0;
    qreal m_statWideW = 400.0;
    bool m_narrow = false; qreal m_pageMargin = 32.0; int m_statsCols = 2;   // 窄屏几何（recomputeGeometry 算）

    qreal m_stageLeftW = 0.0;
    qreal m_stageRightW = 0.0;
    qreal m_stageScale = 1.0;

    bool m_drawerOpen = false;
    QString m_stage = QStringLiteral("grid");
    bool m_editing = false;

    QString m_selCode;
    QString m_selName;
    QString m_breadcrumb;
    QString m_targetUid;
    QString m_targetName;
    QString m_thumbSource;
    QMap<QString, int> m_thumbRev;

    bool m_liveActive = false;
    int  m_liveRev = 0;
    QString m_liveSource;
    QString m_liveText;

    QString m_notifyTitle;
    QString m_notifyContent;
    QString m_lastOpText;
    bool    m_lastOpOk = false;

    QTimer m_refreshTimer;      ///< 页面可见时的 15s 轮询
    QTimer m_holdTimer;         ///< 紧急通知长按进度（3s 填充 / 200ms 回弹）
    qreal  m_holdProgress = 0.0;
    bool   m_holdRetracting = false;   ///< true = 松手回弹相位（递减到 0）
    // 2026-10-28：进度按**真实流逝时间**算，不靠累加 tick。
    // 离屏探针实测：累加式在事件循环卡顿时 600ms 只走到 0.143（应 0.20）⇒ 3 秒会变成 4+ 秒。
    QElapsedTimer m_holdClock;         ///< 长按起始时刻
    QElapsedTimer m_retractClock;      ///< 回弹起始时刻
    qreal  m_retractFrom = 0.0;        ///< 回弹从哪个进度开始退
};
