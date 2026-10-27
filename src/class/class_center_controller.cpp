// 班级中心控制器实现（2026-10-28 · B0 重构移植 / B1 真数据接线）
// 设计意图与铁律见 class_center_controller.h 头部注释。
//
// 分层原则（为什么数据入口是"推"而不是"拉"）：
//   本类不持有 ViewerBackend，也不发 HTTP —— 数据由宿主 main.cpp 从 backend 的属性
//   （classes / devices / events）灌进来，出站指令走 actionOut 信号。这样：
//     ① 控制器可以脱离网络单独构造 ⇒ 探针能跑真断言（B3 验收就是这么做的）；
//     ② 数据来源换（/api/classes → 别的接口）只改宿主一行，QML 与控制器零改动；
//     ③ 不会出现"QML 里偷偷 fetch"这种越界（本类根本没有网络能力）。
#include "class_center_controller.h"

#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QVariantMap>
#include <QtMath>
#include <QDebug>

namespace {
// 与 QML 侧圆饼尺寸约定保持一致（半径 32、圆心 40,40）
constexpr qreal kDonutR  = 32.0;
constexpr qreal kDonutCx = 40.0;
constexpr qreal kDonutCy = 40.0;
// 网格：格子最小 260 宽，至少 2 列（与原型卡片观感一致）
constexpr qreal kGridMinCell = 260.0;
// 远程桌面：4:3 画面容器 + 9:16 操作面板 + 24 间距
constexpr qreal kStageGap = 24.0;
// 抽屉：原型 480 宽；窗口窄时按 42% 收，最少 320（否则面包屑会挤成两行）
constexpr qreal kDrawerW = 480.0;
constexpr qreal kDrawerMinW = 320.0;
// 紧急通知长按：3 秒，33ms 一拍
constexpr int kHoldMs = 3000;
constexpr int kHoldTickMs = 33;
// 松手回弹：蓝图定稿「进度条填充 3s linear，松手回弹 0.2s」。
// 回弹也在 C++ 里逐帧递减（QML 若用 Behavior 会把 3s 线性填充一起拖成缓动，两头不讨好）。
constexpr int kHoldRetractMs = 200;
// 异常面板最多显示几条（蓝图：右侧「最近异常」小块，超过 4 条会溢出卡片）
constexpr int kAnomalyMax = 4;
// 班级总览 15s 轮询（与 /api/devices 既有轮询节奏一致）
constexpr int kRefreshMs = 15000;

QString levelOf(const QJsonObject &e)
{
    return e.value(QStringLiteral("level")).toString();
}

QString messageOf(const QJsonObject &e)
{
    QString m = e.value(QStringLiteral("msg")).toString();
    if (m.isEmpty()) m = e.value(QStringLiteral("message")).toString();
    return m;
}
}   // namespace

ClassCenterController::ClassCenterController(QObject *parent) : QObject(parent)
{
    m_anomalyEmpty = QStringLiteral("暂无异常记录（取自云端事件流水）");
    m_classSummary = QStringLiteral("还没有班级数据（等云端返回）");
    m_liveText = QStringLiteral("还没选设备");

    // 15s 轮询：只在页面可见时跑（不可见还轮询 = 白白消耗云端与设备）
    m_refreshTimer.setInterval(kRefreshMs);
    connect(&m_refreshTimer, &QTimer::timeout, this, [this]() { emit refreshRequested(); });

    // 长按进度：C++ 计时（QML 里写 Timer/Animation 都算逻辑，铁律不允许）
    m_holdTimer.setInterval(kHoldTickMs);
    connect(&m_holdTimer, &QTimer::timeout, this, &ClassCenterController::tickHold);

    recomputeStats();
    recomputeGeometry();
    recomputeStage();
    rebuildBreadcrumb();
    rebuildThumb();
    updateLiveText();
}

// ── 视口 ────────────────────────────────────────────────────────
void ClassCenterController::setViewWidth(qreal w)
{
    if (qFuzzyCompare(m_viewW, w)) return;
    m_viewW = w;
    recomputeGeometry();
    recomputeStage();
    emit geometryChanged();
    // drawerX 同时依赖视口宽与开合态 ⇒ 视口变化也要让绑定它的 QML 重算
    emit drawerOpenChanged();
}

void ClassCenterController::setViewHeight(qreal h)
{
    if (qFuzzyCompare(m_viewH, h)) return;
    m_viewH = h;
    recomputeGeometry();
    recomputeStage();
    emit geometryChanged();
}

void ClassCenterController::setPageActive(bool on)
{
    if (m_pageActive == on) return;
    m_pageActive = on;
    if (on) {
        m_refreshTimer.start();
        emit refreshRequested();     // 进页面立刻拉一次，别让老师盯着空网格等 15s
    } else {
        m_refreshTimer.stop();
    }
    emit pageActiveChanged();
}

void ClassCenterController::refresh()
{
    emit refreshRequested();
}

// ── 统计：QML 不再做除法与取整 ──────────────────────────────────
void ClassCenterController::recomputeStats()
{
    m_total = 0;
    m_online = 0;
    int classCount = 0;
    for (const QVariant &v : m_classes) {
        const QVariantMap m = v.toMap();
        m_total  += m.value(QStringLiteral("total")).toInt();
        m_online += m.value(QStringLiteral("online")).toInt();
        ++classCount;
    }
    m_percent = (m_total > 0) ? qRound(m_online * 100.0 / m_total) : 0;

    // 圆饼弧端点：pct 转弧度后取 sin/cos（原先这段在 QML 里，属违规）
    const qreal pct = (m_total > 0) ? (qreal(m_online) / qreal(m_total)) : 0.0;
    const qreal ang = pct * 2.0 * M_PI;
    m_donutEndX = kDonutCx + kDonutR * qSin(ang);
    m_donutEndY = kDonutCy - kDonutR * qCos(ang);
    m_donutLarge = pct > 0.5;

    if (classCount > 0) {
        m_classSummary = QStringLiteral("%1 个班级 · %2 台设备 · 在线 %3")
                             .arg(classCount).arg(m_total).arg(m_online);
    } else {
        m_classSummary = QStringLiteral("还没有班级数据（等云端返回）");
    }
}

void ClassCenterController::recomputeGeometry()
{
    // 左右各 32 边距（与页面 QML 的 anchors.margins 一致）
    const qreal content = qMax(0.0, m_viewW - 64.0);
    m_cols  = qMax(2, int(qFloor(content / kGridMinCell)));
    m_cellW = (m_cols > 0) ? (content / m_cols) : content;
    // 统计区：两张卡 + 24 间距 ⇒ 窄 1/3、宽 2/3
    const qreal statsW = qMax(0.0, content - 24.0);
    m_statNarrowW = statsW / 3.0;
    m_statWideW = statsW * 2.0 / 3.0;
}

// ── 几何：远程桌面 4:3 + 9:16（原先在 QML 里算，属违规）───────────
void ClassCenterController::recomputeStage()
{
    const qreal leftW  = m_stageH * (4.0 / 3.0);
    const qreal rightW = m_stageH * (9.0 / 16.0);
    const qreal need   = leftW + rightW + kStageGap;
    // 宽度不足则等比缩（保比例优先，宁可留白也不拉伸）
    m_stageScale = (need > 0) ? qMin(1.0, m_stageW / need) : 1.0;
    m_stageLeftW  = leftW  * m_stageScale;
    m_stageRightW = rightW * m_stageScale;
}

void ClassCenterController::setStageViewW(qreal w)
{
    if (qFuzzyCompare(m_stageW, w)) return;
    m_stageW = w;
    recomputeStage();
    emit geometryChanged();
}

void ClassCenterController::setStageViewH(qreal h)
{
    if (qFuzzyCompare(m_stageH, h)) return;
    m_stageH = h;
    recomputeStage();
    emit geometryChanged();
}

qreal ClassCenterController::drawerWidth() const
{
    // 原型 480；窄窗口按比例收，最少 320（否则面包屑/缩略图挤爆）
    return qMin(kDrawerW, qMax(kDrawerMinW, m_viewW * 0.42));
}

qreal ClassCenterController::drawerX() const
{
    // 关闭时整体移出右边界（x = 视口宽），打开时贴右（视口宽 - 抽屉宽）
    return m_viewW - (m_drawerOpen ? drawerWidth() : 0.0);
}

QString ClassCenterController::onlinePercentText() const
{
    return QString::number(m_percent) + QStringLiteral("%");
}

QString ClassCenterController::statSummaryText() const
{
    return QStringLiteral("总 %1 台 · 在线 %2 台").arg(m_total).arg(m_online);
}

// ── 数据入口：/api/classes ──────────────────────────────────────
// 云端字段（src/registry.js listClasses）：code / name / devices[] / onlineCount / myRole。
// devices 里是 uid 字符串；设备名与在线态从 /api/devices 补齐（见 setDevices）。
void ClassCenterController::setClasses(const QJsonArray &classes)
{
    m_rawClasses = classes;
    m_uidClass.clear();

    m_classes.clear();
    for (const QJsonValue &v : classes) {
        const QJsonObject o = v.toObject();
        const QString code = o.value(QStringLiteral("code")).toString();
        if (code.isEmpty()) continue;          // 没码的班级点不了、也下发不了，直接跳过
        const QString name = o.value(QStringLiteral("name")).toString(code);
        const QJsonArray devs = o.value(QStringLiteral("devices")).toArray();
        const int total = devs.size();
        const int online = o.value(QStringLiteral("onlineCount")).toInt();

        for (const QJsonValue &dv : devs) {
            const QString uid = dv.toString();
            if (!uid.isEmpty()) m_uidClass.insert(uid, name);
        }

        // dot 颜色与文案由 C++ 定好：QML 侧不做 online>0 判断（否则又是三元）
        QVariantMap m;
        m.insert(QStringLiteral("code"), code);
        m.insert(QStringLiteral("name"), name);
        m.insert(QStringLiteral("total"), total);
        m.insert(QStringLiteral("online"), online);
        m.insert(QStringLiteral("dot"), online > 0
                     ? QStringLiteral("#22C55E") : QStringLiteral("#BDBDBD"));
        m.insert(QStringLiteral("dotOpacity"), online > 0 ? 1.0 : 0.6);
        m.insert(QStringLiteral("summaryText"),
                 QStringLiteral("设备 %1 · 在线 %2").arg(total).arg(online));
        m_classes.append(m);
    }

    // 选中的班被删了（或首次进入）⇒ 复位，别让抽屉停在一个不存在的班
    if (!m_selCode.isEmpty()) {
        bool stillThere = false;
        for (const QVariant &v : m_classes) {
            if (v.toMap().value(QStringLiteral("code")).toString() == m_selCode) {
                stillThere = true;
                break;
            }
        }
        if (!stillThere) {
            m_selCode.clear();
            m_selName.clear();
            m_targetUid.clear();
            m_targetName.clear();
        }
    }

    recomputeStats();
    rebuildDevices();
    rebuildClassOptions();
    rebuildBreadcrumb();
    rebuildAnomalies();
    emit dataChanged();
    emit selectionChanged();

    // 模块二：老师身份且云端只回了一个班 ⇒ 直接进这个班（"沉浸式本班面板"）
    maybeAutoEnterMyClass();
}

// ── 模块二（2026-10-28 蓝图）：老师/班主任的"独立主页"────────────────
// 蓝图原话：「不再从多班级列表中新建或跳转，直接沉浸式展示本班面板，
//           省去老师的筛选成本。」
//
// 数据侧不需要新端点：云端 /api/classes 带登录票据时本来就只回"自己的班"
// （viewerbackend 的 classesFiltered 注释记着这件事，2026-10-25 已生效）。
// 所以这里只做一件事：**老师身份 + 恰好一个班 + 用户还没选过 ⇒ 自动打开该班抽屉**。
void ClassCenterController::setTeacherScope(bool on)
{
    if (m_teacherScope == on) return;
    m_teacherScope = on;
    maybeAutoEnterMyClass();
}

void ClassCenterController::maybeAutoEnterMyClass()
{
    if (!m_teacherScope) return;
    if (m_autoEnteredClass) return;          // 只自动一次，之后尊重用户的选择
    if (m_classes.size() != 1) return;       // 多班 = 管理员视图，不擅自替人挑班

    const QString code = m_classes.first().toMap()
                             .value(QStringLiteral("code")).toString();
    if (code.isEmpty()) return;
    m_autoEnteredClass = true;
    openClass(code);
}

// ── 班级改绑选项（抽屉「班级编辑」）──────────────────────────────
void ClassCenterController::rebuildClassOptions()
{
    m_classOptions.clear();
    for (const QVariant &v : m_classes) {
        const QVariantMap c = v.toMap();
        const QString code = c.value(QStringLiteral("code")).toString();
        QVariantMap m;
        m.insert(QStringLiteral("code"), code);
        m.insert(QStringLiteral("name"), c.value(QStringLiteral("name")));
        // 选中态样式由 C++ 定（QML 侧不写 `code === sel ? "filled" : "tonal"`）
        m.insert(QStringLiteral("variant"), code == m_selCode
                     ? QStringLiteral("filled") : QStringLiteral("tonal"));
        m_classOptions.append(m);
    }
    emit selectionChanged();
}

// ── 数据入口：/api/devices ──────────────────────────────────────
void ClassCenterController::setDevices(const QJsonArray &devices)
{
    m_rawDevices = devices;
    m_uidDeviceName.clear();
    for (const QJsonValue &v : devices) {
        const QJsonObject o = v.toObject();
        const QString uid = o.value(QStringLiteral("uid")).toString();
        if (uid.isEmpty()) continue;
        const QString n = o.value(QStringLiteral("name")).toString();
        if (!n.isEmpty()) m_uidDeviceName.insert(uid, n);
    }
    rebuildDevices();
    rebuildAnomalies();
    // 当前选中设备的名字可能刚补全
    if (!m_targetUid.isEmpty()) {
        const QString n = m_uidDeviceName.value(m_targetUid, m_targetUid);
        if (n != m_targetName) {
            m_targetName = n;
            emit targetChanged();
        }
    }
}

// ── 数据入口：/api/events（最近 50 条 → 取 warn/error 当异常）──────
void ClassCenterController::setEvents(const QJsonArray &events)
{
    m_rawEvents = events;
    rebuildAnomalies();
}

void ClassCenterController::rebuildAnomalies()
{
    m_anomalies.clear();
    if (m_rawEvents.isEmpty()) {
        m_anomalyEmpty = QStringLiteral("暂无异常记录（取自云端事件流水）");
        emit dataChanged();
        return;
    }

    // 事件按推送顺序（旧→新）追加；异常面板要"最近在上" ⇒ 倒着取。
    for (int i = m_rawEvents.size() - 1; i >= 0 && m_anomalies.size() < kAnomalyMax; --i) {
        const QJsonObject e = m_rawEvents.at(i).toObject();
        const QString lv = levelOf(e);
        if (lv != QStringLiteral("warn") && lv != QStringLiteral("warning")
            && lv != QStringLiteral("error")) {
            continue;
        }
        const QString uid = e.value(QStringLiteral("uid")).toString();
        QString cls = m_uidClass.value(uid);
        if (cls.isEmpty()) cls = uid.isEmpty() ? QStringLiteral("云端") : uid;

        QString msg = messageOf(e).trimmed();
        if (msg.isEmpty()) msg = QStringLiteral("（事件无描述）");
        if (msg.size() > 42) msg = msg.left(41) + QStringLiteral("…");

        // 时间：云端给 ISO8601 UTC ⇒ 转本地 HH:mm（QML 不碰日期解析）
        QString hhmm = QStringLiteral("—");
        const QDateTime dt = QDateTime::fromString(e.value(QStringLiteral("at")).toString(),
                                                  Qt::ISODate);
        if (dt.isValid()) hhmm = dt.toLocalTime().toString(QStringLiteral("HH:mm"));

        QVariantMap m;
        m.insert(QStringLiteral("time"), hhmm);
        m.insert(QStringLiteral("cls"), cls);
        m.insert(QStringLiteral("text"), msg);
        // 整行文本也在 C++ 拼好：QML 里不做字符串拼接（含分隔符口径统一）
        m.insert(QStringLiteral("line"),
                 QStringLiteral("%1 · %2 · %3").arg(hhmm, cls, msg));
        m_anomalies.append(m);
    }

    if (m_anomalies.isEmpty()) {
        // 有事件但都是 info：说清"没有异常"，比空白强（老师会以为面板坏了）
        m_anomalyEmpty = QStringLiteral("最近 %1 条事件里没有异常").arg(m_rawEvents.size());
    }
    emit dataChanged();
}

// ── 选中班级 → 设备清单（抽屉里的设备行 + 下发目标）──────────────
void ClassCenterController::rebuildDevices()
{
    m_devices.clear();
    if (m_selCode.isEmpty()) {
        emit selectionChanged();
        return;
    }

    // 该班的 uid 清单（云端 /api/classes 的 devices 字段是权威归属）
    QJsonArray uids;
    for (const QJsonValue &v : m_rawClasses) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("code")).toString() != m_selCode) continue;
        uids = o.value(QStringLiteral("devices")).toArray();
        m_selName = o.value(QStringLiteral("name")).toString(m_selCode);
        break;
    }

    // 设备名/在线态来自 /api/devices；不在表里的（离线很久被清理）按 uid 显示
    QMap<QString, QJsonObject> byUid;
    for (const QJsonValue &v : m_rawDevices) {
        const QJsonObject o = v.toObject();
        byUid.insert(o.value(QStringLiteral("uid")).toString(), o);
    }

    for (const QJsonValue &uv : uids) {
        const QString uid = uv.toString();
        if (uid.isEmpty()) continue;
        const QJsonObject d = byUid.value(uid);
        const QString name = m_uidDeviceName.value(uid, uid);
        const bool online = d.value(QStringLiteral("online")).toBool(false);

        QVariantMap m;
        m.insert(QStringLiteral("uid"), uid);
        m.insert(QStringLiteral("name"), name);
        m.insert(QStringLiteral("online"), online);
        m.insert(QStringLiteral("selected"), uid == m_targetUid);
        m.insert(QStringLiteral("dot"), online
                     ? QStringLiteral("#22C55E") : QStringLiteral("#BDBDBD"));
        m.insert(QStringLiteral("dotOpacity"), online ? 1.0 : 0.6);
        m_devices.append(m);
    }

    // 未选设备（或选的设备已不在这个班）⇒ 落到第一台，避免"抽屉打开但发不出指令"
    bool targetInClass = false;
    for (const QVariant &v : m_devices) {
        if (v.toMap().value(QStringLiteral("uid")).toString() == m_targetUid) {
            targetInClass = true;
            break;
        }
    }
    if (!targetInClass) {
        m_targetUid.clear();
        m_targetName.clear();
        if (!m_devices.isEmpty()) {
            const QVariantMap first = m_devices.first().toMap();
            m_targetUid = first.value(QStringLiteral("uid")).toString();
            m_targetName = first.value(QStringLiteral("name")).toString();
            // 重新标选中
            for (int i = 0; i < m_devices.size(); ++i) {
                QVariantMap m = m_devices.at(i).toMap();
                m.insert(QStringLiteral("selected"),
                         m.value(QStringLiteral("uid")).toString() == m_targetUid);
                m_devices[i] = m;
            }
        }
        emit targetChanged();
    }

    rebuildThumb();
    updateLiveText();
    emit selectionChanged();
}

void ClassCenterController::rebuildBreadcrumb()
{
    // 云端没有"学校"这个维度（registry 只有班级/设备）⇒ 校名一律显示「本校」，
    // 这不是编数据，是如实标注层级；将来接入学区/学校表只改这里。
    const QString school = QStringLiteral("本校");
    QString cohort;
    // 班名里带「2028 届」这类字样才加届这一层；没有就跳过，别显示一个「—」占位层级
    static const QRegularExpression re(QStringLiteral("(\\d{4})\\s*届"));
    const QRegularExpressionMatch mm = re.match(m_selName);
    if (mm.hasMatch()) cohort = mm.captured(1) + QStringLiteral(" 届");

    if (m_selName.isEmpty()) {
        m_breadcrumb = QStringLiteral("还没选班级");
    } else if (cohort.isEmpty()) {
        m_breadcrumb = QStringLiteral("%1 > %2").arg(school, m_selName);
    } else {
        m_breadcrumb = QStringLiteral("%1 > %2 > %3").arg(school, cohort, m_selName);
    }
    emit selectionChanged();
}

// ── 缩略图（image://classthumb/<uid>?t<rev>）────────────────────
void ClassCenterController::setThumb(const QString &uid, int rev)
{
    if (uid.isEmpty() || rev <= 0) return;
    if (m_thumbRev.value(uid) == rev) return;
    m_thumbRev.insert(uid, rev);
    if (uid == m_targetUid) rebuildThumb();
}

void ClassCenterController::rebuildThumb()
{
    QString src;
    if (!m_targetUid.isEmpty()) {
        const int rev = m_thumbRev.value(m_targetUid, 0);
        // rev=0 表示这台设备的缩略图还没到（云端只在订阅班时下发缩略图）
        if (rev > 0) {
            src = QStringLiteral("image://classthumb/%1?t%2").arg(m_targetUid).arg(rev);
        }
    }
    if (src == m_thumbSource) return;
    m_thumbSource = src;
    emit thumbChanged();
}

// ── 远程画面 ───────────────────────────────────────────────────
void ClassCenterController::setLiveActive(bool on)
{
    if (m_liveActive == on) return;
    m_liveActive = on;
    m_liveSource = on ? QStringLiteral("image://vnc/live?%1").arg(m_liveRev) : QString();
    updateLiveText();
    emit liveChanged();
}

void ClassCenterController::bumpLiveFrame()
{
    ++m_liveRev;
    if (!m_liveActive) {
        // 帧到了说明 VNC 已经在跑（frameSource 切换信号可能晚一拍），以帧为准
        m_liveActive = true;
        updateLiveText();
    }
    m_liveSource = QStringLiteral("image://vnc/live?%1").arg(m_liveRev);
    emit liveChanged();
}

void ClassCenterController::updateLiveText()
{
    QString t;
    if (m_stage != QStringLiteral("remote")) {
        t.clear();
    } else if (m_targetUid.isEmpty()) {
        t = QStringLiteral("还没选设备");
    } else if (!m_liveActive) {
        t = QStringLiteral("正在接通这台设备…");
    } else {
        t = QStringLiteral("实时画面（只读；键鼠注入走设备集控页）");
    }
    if (t == m_liveText) return;
    m_liveText = t;
    emit liveChanged();
}

// ── 抽屉 / 视图态 ───────────────────────────────────────────────
void ClassCenterController::openDrawer()
{
    if (m_drawerOpen) return;
    m_drawerOpen = true;
    emit drawerOpenChanged();
}

void ClassCenterController::closeDrawer()
{
    if (!m_drawerOpen) return;
    m_drawerOpen = false;
    m_editing = false;
    emit editingChanged();
    emit drawerOpenChanged();
}

void ClassCenterController::toggleDrawer()
{
    if (m_drawerOpen) closeDrawer();
    else openDrawer();
}

void ClassCenterController::openClass(const QString &code)
{
    if (code.isEmpty() || code == m_selCode) {
        if (code.isEmpty()) return;
        openDrawer();
        return;
    }
    m_selCode = code;
    m_selName.clear();
    m_editing = false;
    emit editingChanged();
    rebuildDevices();      // 里面会把目标设备落到第一台 + 重算面包屑/缩略图
    rebuildClassOptions();
    rebuildBreadcrumb();
    openDrawer();
    emit selectionChanged();
}

// ── 抽屉「班级编辑」：把这个设备改绑到另一个班（真实云端写操作）────────
// 为什么只做「班级」这一维：云端 knownDevices 只有 classCode / role / name 三个字段，
// 没有"学校"维度，也不接受设备改名 ⇒ 只有班级改绑是真能落库的动作（POST /api/class/bind）。
// 学校/设备名在抽屉里只读展示，并在文案里说清原因 —— 不做一个点了没用的假下拉。
void ClassCenterController::rebindSelectedTo(const QString &code)
{
    if (m_targetUid.isEmpty()) {
        setOpResult(false, QStringLiteral("还没选设备，改不了归属"));
        return;
    }
    if (code.isEmpty() || code == m_selCode) return;
    QString name = code;
    for (const QVariant &v : m_classOptions) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("code")).toString() == code) {
            name = m.value(QStringLiteral("name")).toString();
            break;
        }
    }
    emit rebindRequested(m_targetUid, code);
    setOpResult(true, QStringLiteral("已请求把 %1 改绑到 %2（云端回执见运行监控）")
                          .arg(m_targetName.isEmpty() ? m_targetUid : m_targetName, name));
}

void ClassCenterController::selectDevice(const QString &uid)
{
    if (uid.isEmpty() || uid == m_targetUid) return;
    m_targetUid = uid;
    m_targetName = m_uidDeviceName.value(uid, uid);
    for (int i = 0; i < m_devices.size(); ++i) {
        QVariantMap m = m_devices.at(i).toMap();
        m.insert(QStringLiteral("selected"),
                 m.value(QStringLiteral("uid")).toString() == uid);
        m_devices[i] = m;
    }
    rebuildThumb();
    updateLiveText();
    emit selectionChanged();
    emit targetChanged();
}

void ClassCenterController::setTargetUid(const QString &uid)
{
    if (m_targetUid == uid) return;
    m_targetUid = uid;
    // 名字跟着走：不更新会留着上一台的名字（下发回执就会写成"发给 260901"而实际是另一台）
    m_targetName = uid.isEmpty() ? QString() : m_uidDeviceName.value(uid, uid);
    for (int i = 0; i < m_devices.size(); ++i) {
        QVariantMap m = m_devices.at(i).toMap();
        m.insert(QStringLiteral("selected"),
                 m.value(QStringLiteral("uid")).toString() == uid);
        m_devices[i] = m;
    }
    rebuildThumb();
    updateLiveText();
    emit selectionChanged();
    emit targetChanged();
}

void ClassCenterController::setTargetName(const QString &n)
{
    if (m_targetName == n) return;
    m_targetName = n;
    emit targetChanged();
}

void ClassCenterController::enterRemote()
{
    if (m_stage == QStringLiteral("remote")) return;
    if (m_targetUid.isEmpty()) {
        setOpResult(false, QStringLiteral("还没选设备，进不了远程桌面"));
        return;
    }
    m_stage = QStringLiteral("remote");
    emit stageChanged();
    updateLiveText();
    emit remoteEntered(m_targetUid);   // 宿主：backend.setCurrentUid（触发选中即接管）
}

void ClassCenterController::backToGrid()
{
    if (m_stage == QStringLiteral("grid")) return;
    const QString uid = m_targetUid;
    m_stage = QStringLiteral("grid");
    emit stageChanged();
    updateLiveText();
    emit remoteLeft(uid);
}

void ClassCenterController::toggleEditing()
{
    m_editing = !m_editing;
    emit editingChanged();
}

void ClassCenterController::setNotifyTitle(const QString &t)
{
    if (m_notifyTitle == t) return;
    m_notifyTitle = t;
    emit notifyDraftChanged();
}

void ClassCenterController::setNotifyContent(const QString &c)
{
    if (m_notifyContent == c) return;
    m_notifyContent = c;
    emit notifyDraftChanged();
}

void ClassCenterController::setOpResult(bool ok, const QString &text)
{
    m_lastOpOk = ok;
    m_lastOpText = text;
    emit opResultChanged();
}

// ── 紧急通知长按（3 秒）────────────────────────────────────────
void ClassCenterController::beginEmergencyHold()
{
    if (m_holdTimer.isActive() && !m_holdRetracting) return;
    m_holdRetracting = false;
    m_holdProgress = 0.0;
    m_holdClock.start();          // 按真实时间算，不靠累加 tick
    m_holdTimer.start();
    emit emergencyChanged();
}

void ClassCenterController::cancelEmergencyHold()
{
    // 已经在回弹/已经归零：幂等
    if (m_holdRetracting) return;
    if (!m_holdTimer.isActive() && m_holdProgress <= 0.0) return;

    if (m_holdProgress <= 0.0) {
        m_holdTimer.stop();
        return;
    }
    // 蓝图：松手回弹 0.2s。保持计时器跑，改成递减相位（不在 QML 里做动画，
    // 否则 Behavior 会把 3s 线性填充一起变成缓动，与"3s linear"冲突）。
    m_holdRetracting = true;
    m_retractFrom = m_holdProgress;
    m_retractClock.start();
    if (!m_holdTimer.isActive()) m_holdTimer.start();
}

void ClassCenterController::tickHold()
{
    // ── 回弹相位：200ms 内把进度退回 0（同样按真实时间）──────────
    if (m_holdRetracting) {
        const qreal e = qreal(m_retractClock.elapsed()) / qreal(kHoldRetractMs);
        m_holdProgress = (e >= 1.0) ? 0.0 : m_retractFrom * (1.0 - e);
        if (e >= 1.0) {
            m_holdRetracting = false;
            m_holdTimer.stop();
        }
        emit emergencyChanged();
        return;
    }
    const qreal p = qreal(m_holdClock.elapsed()) / qreal(kHoldMs);
    if (p >= 1.0) {
        m_holdProgress = 1.0;
        emit emergencyChanged();
        m_holdTimer.stop();
        // 长按成立：已发出，进度立即复位（不再走 200ms 回弹 —— 回弹只用于"松手没按够"）
        m_holdProgress = 0.0;
        m_holdRetracting = false;
        emit emergencyChanged();
        requestOp(QStringLiteral("emergency"));
        return;
    }
    m_holdProgress = p;
    emit emergencyChanged();
}

// ── 画面指针（归一化坐标在 C++ 算，QML 只报局部坐标）────────────
void ClassCenterController::stagePointer(const QString &kind, qreal x, qreal y,
                                         const QString &button)
{
    if (m_stage != QStringLiteral("remote") || !m_liveActive) return;
    const qreal w = stageLeftW();
    const qreal h = stageFrameH();
    if (w <= 0 || h <= 0) return;
    // 夹到 [0,1]：QML 里不做 Math.min/max（铁律），边界由这里兜
    qreal nx = x / w;
    qreal ny = y / h;
    if (nx < 0) nx = 0; else if (nx > 1) nx = 1;
    if (ny < 0) ny = 0; else if (ny > 1) ny = 1;
    emit pointerOut(kind, nx, ny, button);
}

// ── 操作请求 → 既有指令词表（B1 接线）─────────────────────────────
//
// 铁律：绝不新造动作名。右侧这张表的每个动作都必须在被控端
// Stelarith-control-qt/src/main.cpp 的 executeAction 里真实存在（2026-10-28 逐个核对：
// kActionNames 与分支两处都在），否则就是"界面能点、设备无反应"的假动作。
//
// 落到 sendActionTo 而不是 sendAction：班级中心是**指定设备**下发，
// 不能用 backend 的 currentUid（那是"主画面正在看哪台"，两回事）。
bool ClassCenterController::mapOpToAction(const QString &name, QString *action, QJsonObject *params) const
{
    if (name == QStringLiteral("notify")) {
        *action = QStringLiteral("notify");
        // 被控端 notify 要求 title 必填，空标题会被拒 ⇒ 给兜底，别让人点了没反应
        params->insert(QStringLiteral("title"),
                       m_notifyTitle.isEmpty() ? QStringLiteral("通知") : m_notifyTitle);
        if (!m_notifyContent.isEmpty())
            params->insert(QStringLiteral("content"), m_notifyContent);
        return true;
    }
    if (name == QStringLiteral("emergency")) {
        *action = QStringLiteral("notify");
        params->insert(QStringLiteral("title"), QStringLiteral("紧急通知"));
        params->insert(QStringLiteral("content"),
                       m_notifyContent.isEmpty()
                           ? QStringLiteral("请立即停止当前操作，看屏幕提示")
                           : m_notifyContent);
        // 紧急口径：三档配色里的最高级（被控端按 severity 上色）
        params->insert(QStringLiteral("severity"), QStringLiteral("urgent"));
        return true;
    }
    if (name == QStringLiteral("lock"))     { *action = QStringLiteral("lock");     return true; }
    if (name == QStringLiteral("reboot"))   { *action = QStringLiteral("reboot");   return true; }
    if (name == QStringLiteral("shutdown")) { *action = QStringLiteral("shutdown"); return true; }
    if (name == QStringLiteral("schedule")) { *action = QStringLiteral("list_schedules"); return true; }
    if (name == QStringLiteral("software")) { *action = QStringLiteral("process_list");   return true; }
    // 「监控」不在这里：回放没有设备侧指令（见 requestOp 的说明）
    return false;   // 未知名字：不猜、不静默吞，交给调用方报错
}

void ClassCenterController::requestOp(const QString &name)
{
    emit opRequested(name);

    // 本地态操作（不下发设备）
    if (name == QStringLiteral("class_edit")) {
        toggleEditing();
        setOpResult(true, QStringLiteral("已展开班级信息编辑"));
        return;
    }
    if (name == QStringLiteral("device_settings")) {
        // 诚实：设备设置面板这次没接（蓝图把它排在模块一之外），点了要说话
        setOpResult(false, QStringLiteral("设备设置面板还没接入（本批只做布局与接线）"));
        return;
    }
    if (name == QStringLiteral("monitor")) {
        // 蓝图里的「监控」= 录像回放。回放能力在管理端 MonitorPage（云端 /recordings +
        // 本机回环代理），**被控端没有回放指令** ⇒ 不造假动作，切到那个页面。
        emit navigateRequested(QStringLiteral("monitor"));
        setOpResult(true, QStringLiteral("已打开运行监控（回放入口在该页）"));
        return;
    }
    if (name == QStringLiteral("refresh")) {
        refresh();
        setOpResult(true, QStringLiteral("正在刷新班级数据"));
        return;
    }

    QString action;
    QJsonObject params;
    if (!mapOpToAction(name, &action, &params)) {
        setOpResult(false, QStringLiteral("未知操作：%1（未下发）").arg(name));
        qWarning() << "[classCenter] 未知操作名" << name;
        return;
    }

    if (m_targetUid.isEmpty()) {
        // 沿用 viewerbackend 的口径：没目标就明确说没发，不报错就是骗人。
        setOpResult(false, QStringLiteral("还没选设备，指令未下发"));
        qWarning() << "[classCenter] 无目标 uid，指令" << action << "没发出去";
        return;
    }

    emit actionOut(m_targetUid, action, params);
    setOpResult(true, QStringLiteral("已把「%1」发给 %2")
                          .arg(action, m_targetName.isEmpty() ? m_targetUid : m_targetName));
}
