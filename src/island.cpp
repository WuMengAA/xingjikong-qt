// 星集控被控端 · 灵动岛实现（2026-10-07，设计文档 3.8 被控端版）
//
// 实现说明：
//   · 弹性缓动：QPropertyAnimation + OutBack（=iPhone 灵动岛的「果冻感」），
//     收起↔展开、出现↔消失都走动画，绝不用「啪一下出现」。
//   · 形状转变：Collapsed(56) ↔ Expanded(168) 两种高度，宽度随内容自适应；
//     内容重绘跟手，不「哐当」换。
//   · 交互穿透：默认 WA_TransparentForMouseEvents（纯展示，不挡下层课件）；
//     setInteractive(true) 后本岛接收点击（展开/收起）。
//   · 深/浅色：默认跟随系统 QGuiApplication::styleHints()->colorScheme()，
//     setLightMode() 可强制。
//   · 背景：与 NotifyWindow 同思路 —— WA_TranslucentBackground + 绘制
//     圆角半透明底（QSS 背景会在不透明窗口上先画一层方角底，露馅）。
//
// ── 2026-10-11 · 绘制层改 QML ────────────────────────────────────────
// 用户裁决：被控端灵动岛视觉层改 QML（系统层留 C++）。
//   承载：m_quick = QQuickWidget，铺满本窗口，加载 qml/island/IslandRoot.qml
//   桥  ：m_bridge = IslandBridge（Q_PROPERTY 暴露状态 + fireAction/requestExpand 回调）
//   paintEvent 从此**不再画任何东西**（QML 铺满后 C++ 画笔无事可做），
//   但**保留覆写**：一来留个"QML 没起来就退回手绘"的余地（见 paintEvent 里的判空），
//   二来把原来那套绘制代码留档在上面，方便比对观感是否走样。
//
// ⚠️ 逻辑（几何/动画/定时器/鼠标/生命周期）**一个字都没动**：
//    那些注释里写满踩坑记录的状态机（m_closing 淡出守卫 / kDragSlop 方向锁 /
//    explorer /select 不 ShellExecute / m_expandGuard 硬回收）是本文件的核心资产，
//    改 QML 只该换"长什么样"，不该碰"什么时候显示、点了会怎样"。

#include "island.h"
#include "island_bridge.h"
#include "svgiconprovider.h"
#include "whale_audio.h"
#include "danmaku.h"             // 2026-10-15 组件5：祝福弹幕全屏浮层

#include <QApplication>
#include <QDebug>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QDir>
#include <QFileInfo>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWidget>
#include <QScreen>
#include <QStyleHints>
#include <QSvgRenderer>
#include <QTime>          // A3：空闲态时钟（HH:mm）
#include <QUrl>
#include <QQmlError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDateTime>
#include <QtMath>          // §6.3 语音频谱：qSqrt

// 2026-10-08 lucide(morphicons 同款) SVG 图标渲染：读取 qrc 内 /icons/<name>.svg，
// 把 stroke/fill 的 currentColor 换成主题前景色后渲染成 QPixmap（深浅主题自动适配）。
static QPixmap renderSvgIcon(const QString &name, int size, const QColor &color)
{
    if (name.isEmpty()) return QPixmap();
    const QString path = QStringLiteral(":/icons/%1.svg").arg(name);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QPixmap();
    const QByteArray raw = f.readAll();
    f.close();
    QString svg = QString::fromUtf8(raw);
    const QString col = color.name(QColor::HexRgb); // #rrggbb
    svg.replace(QStringLiteral("currentColor"), col);
    QSvgRenderer r;
    if (!r.load(svg.toUtf8())) return QPixmap();
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    r.render(&p, QRect(0, 0, size, size));
    return pm;
}

static IslandOverlay *g_island = nullptr;

// 空闲半胶囊的尺寸（比活动态小一号，真的"收回顶部"）
// 4:3 比例（132×44），放得下鲸鱼 28px + 时钟
static const int kIdleW = 132;
// 活动态胶囊的最小宽度（原来写死 200；收到文件时路径很长会撑宽，短文案时兜这个下限）
static const int kMinContentW = 200;
// 触控容差（像素）：手指按下到抬起之间会有肉眼看不见的抖动，
// 小于这个位移一律算点击不算拖拽；反过来超过它就只当手势、不当点击。
static const int kDragSlop = 24;
// 展开态最长停留时间（毫秒）：到点强制收回，避免大块胶囊长期压着屏幕顶部吃点击。
// 交互态展开本来没有回收路径（m_autoClose 的守卫会吞掉），这是唯一兜底。
static const int kExpandMaxMs = 20000;
// 空闲态展开（无活跃通知，只是看一眼"暂无通知"）的自动收起时间：5s 够看一眼就收。
static const int kIdleExpandMs = 5000;

IslandOverlay *IslandOverlay::instance()
{
    if (!g_island)
        g_island = new IslandOverlay();
    return g_island;
}

IslandOverlay::IslandOverlay(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    // 半透明圆角底：和 NotifyWindow 的岛/弹窗同一套做法（全屏遮罩不走这）。
    setAttribute(Qt::WA_TranslucentBackground);
    // 纯展示默认穿透：被控端在教室大屏，灵动岛不能挡住后面的课件/白板。
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_DeleteOnClose, false);
    // 触控（2026-10-07）：教室大屏基本都是触摸屏。这里**不开 WA_AcceptTouchEvents** ——
    // 开了就得自己处理 QTouchEvent 的触点编号/接受/拒绝一条龙，而默认行为已经把触点
    // 合成为 mouse press/move/release（配 mouseMove 做手势足够），自己啃只会多做acie_ERROR多错。
    // 触控要改的是**另一件事**：手指不像鼠标那样精确，见 kTapSlop / setInteractive 里的热区。
    // 先按系统深浅色定一次基调
    const Qt::ColorScheme scheme =
        QGuiApplication::styleHints()->colorScheme();
    m_light = (scheme == Qt::ColorScheme::Light);

    // 动画器：几何（位置/大小）+ 透明度
    m_geoAnim = new QPropertyAnimation(this, "geometry", this);
    m_geoAnim->setDuration(380);
    m_geoAnim->setEasingCurve(QEasingCurve::OutBack);

    auto *eff = new QGraphicsOpacityEffect(this);
    eff->setOpacity(1.0);
    setGraphicsEffect(eff);
    m_fadeAnim = new QPropertyAnimation(eff, "opacity", this);
    m_fadeAnim->setDuration(220);
    // ⚠️ finished 只连**一次**，而且是成员函数槽（不是 lambda）：
    //    · 淡入/淡出共用这一条动画，finished 两边都会发。原先把 `hide()` 直接挂在
    //      dismiss()/enterIdle() 里 connect(lambda)，那条连接**永不解绑** ⇒ 下一次
    //      淡入跑完也会 hide()，STE_QT_ISLAND_IDLE=0 下表现为"第一条之后每条提示只闪 220ms"。
    //    · 而且 Qt::UniqueConnection **对 lambda 不生效**（运行时只打一句告警照样连），
    //      所以那两处以为的"防重复"是空的。
    //    现在改成成员槽 + m_closing 状态位：只有"这次是淡出"才 hide。
    connect(m_fadeAnim, &QPropertyAnimation::finished, this, &IslandOverlay::onFadeFinished);
    // ⚠️ 淡入/淡出必须**显式给起止值**。原来只 new 出来绑上 opacity 就完事，
    // 没 setStartValue/setEndValue ⇒ 起点终点都是"当前值"（都是 1.0）：
    //   ① 淡入淡出等于没做（一直在 1.0）；
    //   ② 更要命的是 QPropertyAnimation 值不变时 state 会一直挂在 Running，
    //      dismiss() 第一行 `if (state==Running) return` 于是**恒真** ——
    //      手动 dismiss 和 6 秒自动关闭全被它堵死，胶囊赖在屏幕上不走
    //      （实测：t=7 手动 dismiss() 后窗口仍 visible，t=7.3 还是 visible）。
    //      两个函数里都改成"先 stop() 再设起止值"就没这个问题了。

    // dismiss 兜底：淡出动画没发 finished 就强制收掉（胶囊不能赖在屏幕上不走）
    m_dismissGuard = new QTimer(this);
    m_dismissGuard->setSingleShot(true);
    connect(m_dismissGuard, &QTimer::timeout, this, [this]() {
        m_closing = false;                    // 兜底接管了，别让 finished 再 hide 一次
        if (m_idleEnabled) enterIdle(); else hide();
    });

    // 自动收起
    m_autoClose = new QTimer(this);
    m_autoClose->setSingleShot(true);
    connect(m_autoClose, &QTimer::timeout, this, [this]() {
        if (m_interactive && m_expanded) return;   // 交互态展开不自动收（见下 expandGuard 的硬超时兜底）
        dismiss();
    });

    // 展开态硬回收：交互态展开时 m_autoClose 会被守卫吞掉且不再排期，
    // 没有这条就是"点开就再也收不回"（教室大屏顶部被永久占住）。
    m_expandGuard = new QTimer(this);
    m_expandGuard->setSingleShot(true);
    connect(m_expandGuard, &QTimer::timeout, this, [this]() {
        if (m_expanded) setExpanded(false);
    });

    // A3（2026-10-11）：空闲态时钟。空闲胶囊右侧画 HH:mm，得有个心跳让分钟跳变时重绘。
    // 500ms 一跳（与 IslandClock 同口径），只在"空闲 + 可见"时 update()，其余情况直接返回，
    // 不给活动态/隐藏态添任何重绘负担。
    // A4 复用同一条心跳推进展开态的描述滚动（16px 步进 ≈ 32px/s，与 MarqueeText 默认 100px/s
    // 相比更慢更克制 —— 展开态是给人"读"的，不是跑马灯；500ms 一跳的步长按 fps 折算即可）。
    //
    // 2026-10-11 改 QML 后：这两件事 QML 自己用 Timer 做了（IslandClock / MarqueeText），
    // m_clockTick 此时只剩"给 C++ 兜底手绘用"的意义 —— **保留不动**，
    // 因为 paintEvent 里还留着旧绘制路径，删了会让兜底路走不通。
    m_clockTick = new QTimer(this);
    m_clockTick->setInterval(500);
    connect(m_clockTick, &QTimer::timeout, this, [this]() {
        if (m_idle && isVisible()) update();
        // A4：展开态且文本溢出 → 每跳左移 16px；滚出后可读区再从右侧进场（循环）
        if (m_expanded && isVisible() && m_marqueeRun && !m_desc.isEmpty()) {
            m_marqueeX -= 16;
            if (m_marqueeX + m_marqueeTextW < 0) m_marqueeX = rect().width();
            update();
        }
        // §6.3 语音频谱：对讲中若**这一跳没有新帧**（老师停顿/没声音），条高缓慢回落，
        // 别让频谱"冻"在最后一个幅度上（旧 SpectrumWidget 的 decay 定时器同口径）。
        // 有新帧时不动 —— 结果由 feedSpectrum 的 0.7/0.3 平滑负责，避免两处互相打架。
        if (m_spectrumOn && !m_spectrumBands.isEmpty() && isVisible()
            && (m_spectrumLastMs == 0
                || QDateTime::currentMSecsSinceEpoch() - m_spectrumLastMs > 400)) {
            bool changed = false;
            for (float &b : m_spectrumBands) {
                if (b > 0.02f) { b *= 0.55f; changed = true; }
                else if (b != 0.f) { b = 0.f; changed = true; }
            }
            if (changed && m_bridge) m_bridge->syncSpectrum();
        }
    });
    m_clockTick->start();

    // ── 2026-10-11 · QML 绘制层（用户裁决：视觉层改 QML）─────────────
    // 桥先建：QML 一加载就会读 bridge 的属性，不存在会报 ReferenceError。
    m_bridge = new IslandBridge(this, this);
    m_quick = new QQuickWidget(this);
    // ⚠️ 必须 TRANSPARENT：本窗口是 WA_TranslucentBackground，QQuickWidget 默认
    //    带自己的不透明底，会盖住圆角、露出方角。不清这个，胶囊看着是块方板。
    m_quick->setClearColor(Qt::transparent);
    m_quick->setAttribute(Qt::WA_TranslucentBackground);
    m_quick->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_quick->rootContext()->setContextProperty(QStringLiteral("bridge"), m_bridge);
    // 小鲸鱼音效实例（QML 侧 WhaleMascot 通过 bridgeRef.whaleClicked/whaleLongPressed 触发，
    // 不直接用 whaleAudio，但留这个 context property 以备将来 QML 直接调 playByName）。
    m_quick->rootContext()->setContextProperty(QStringLiteral("whaleAudio"), WhaleAudio::instance());
    // SVG 图标 provider（QML 侧 SvgIcon.qml 用 image://svgicon/<name>?color=...）。
    // ⚠️ 必须挂在**这个 QQuickWidget 自己的 engine** 上，不能挂全局默认 engine ——
    //    QQuickWidget 有独立 engine，挂错了图标全空白（且不报错，最难查）。
    m_quick->engine()->addImageProvider(QStringLiteral("svgicon"), new SvgIconProvider);

    // ⚠️ QML 走**磁盘加载**（不是 qrc）：改外观不用重编译，与 viewer 同一个约定。
    //    路径基于 exe 目录的 qml/island/IslandRoot.qml（build.ps1 / CMake 会拷过去）。
    //    找不到就**不致命** —— 退回手绘（paintEvent 里有判空），被控端不能因为
    //    一个外观文件缺失就起不来。
    const QString qmlRoot = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("qml/island/IslandRoot.qml"));
    if (QFileInfo::exists(qmlRoot)) {
        m_quick->setSource(QUrl::fromLocalFile(qmlRoot));
        for (const QQmlError &e : m_quick->errors())
            qWarning() << "[island] QML 报错:" << e.toString();
    } else {
        qWarning() << "[island] 找不到" << qmlRoot << "—— 退回 C++ 手绘（外观走旧路径）";
    }

    // QML 铺满窗口（几何动画改的是本窗口的 geometry，子控件跟着伸缩）
    m_quick->setGeometry(rect());

    // 初始同步一次：把构造期定下的 m_light 推给 QML
    m_bridge->sync();

    // ── 历史通知：启动时从 JSON 加载 ──
    loadHistory();

    hide();
}

IslandOverlay::~IslandOverlay()
{
    if (g_island == this) g_island = nullptr;
}

// 空闲半胶囊的位置：比活动态更贴顶（y=10）、更小（120×36），真正"收回顶部"。
QRect IslandOverlay::geometryForIdle() const
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) return QRect(0, 0, 120, int(Idle));
    const QRect sg = screen->geometry();
    return QRect(sg.x() + (sg.width() - kIdleW) / 2, sg.y() + 10, kIdleW, int(Idle));
}

// 活动态的位置：宽度由内容决定（上限 60% 屏宽），顶部居中 y=24。
QRect IslandOverlay::geometryForContent(int shapeHeight) const
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) return QRect(0, 0, 200, shapeHeight);
    const QRect sg = screen->geometry();

    // 2026-10-15 组件1（点歌互动）：媒体呈现时按 QML 预算宽度走 ——
    // 收起 320 / 展开 640（与 IslandRoot.implicitWidth 逐字一致）。
    // 内容定宽对短曲名只给 ~200：互动区 + 弹幕输入行在 200 宽下放不下
    // （探针实测 danmakuRow 被推到 320 高度线外，根本看不见），
    // 且 fitTitlePx 的宽度预算也按 640/320 算，窄岛下大标题会被挤成 3 行 192px。
    // ⚠️ 只对媒体呈现生效：通知类仍走内容定宽，不动既有 100+ 调用方的观感。
    if (m_mediaPresenting) {
        const int mw = m_expanded ? 640 : 320;
        return QRect(sg.x() + (sg.width() - mw) / 2, sg.y() + 24, mw, shapeHeight);
    }

    // 内容决定宽度（标题/描述最长行）
    const int maxW = qMin(sg.width() * 6 / 10, 640);
    QFontMetrics fm(font());
    const int iconW = m_icon.isEmpty() ? 0 : 28;
    const int textW = qMax(fm.horizontalAdvance((m_title.isEmpty() ? "" : m_title) + "  "),
                           fm.horizontalAdvance(m_desc));
    const int w = qBound(kMinContentW, iconW + textW + 48, maxW);
    return QRect(sg.x() + (sg.width() - w) / 2, sg.y() + 24, w, shapeHeight);
}

// 按**形态**算目标几何（#98）。过渡不用另外写：animateGeometry() 吃任意 QRect，
// 从当前几何补间到目标几何，所以"胶囊 → 居中卡片 → 全屏"之间的放大/收缩天然带动画。
QRect IslandOverlay::geometryForForm(Form f) const
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) return QRect(0, 0, 200, int(Collapsed));
    const QRect sg = screen->geometry();

    if (f == Form::Capsule)
        return geometryForContent(m_expanded ? int(Expanded) : int(Collapsed));

    if (f == Form::Centered) {
        // 屏幕正中的卡片：宽不超过屏宽 40%，高度 200（够放图标+标题+两行描述）
        const int w = qMin(sg.width() * 2 / 5, 520);
        // 有按钮行时加高，给底部按钮留位（否则描述会和按钮叠在一起）
        const int h = m_actions.isEmpty() ? 200 : 250;
        return QRect(sg.x() + (sg.width() - w) / 2,
                     sg.y() + (sg.height() - h) / 2, w, h);
    }

    // Fullscreen：铺满主屏，四周留 24px —— ClassIsland 那种"贴边但留一口气"的观感。
    // 留边而不是真全屏，是为了让它明显是"浮层"而不是"桌面换了"，学生不会误以为死机。
    const int m = 24;
    return QRect(sg.x() + m, sg.y() + m, sg.width() - 2 * m, sg.height() - 2 * m);
}

void IslandOverlay::onFadeFinished()
{
    // 淡入结束：什么都不做（这条动画是共用的，原来在这里无条件 hide() 会把新提示灭掉）。
    // 只有"这次真的是淡出"（dismiss/enterIdle 把 m_closing 置了 true）才收窗。
    if (!m_closing) return;
    m_closing = false;
    m_dismissGuard->stop();
    hide();
}

// ══════════════════════════════════════════════════════════════════
// 历史通知（2026-10-11 · 持久化）
// ══════════════════════════════════════════════════════════════════
// 通知消失（被新通知顶掉 / dismiss / enterIdle）时归档到历史。
// JSON 落盘到 LOCALAPPDATA/xingjikong/island_history.json，启动时加载。

QString IslandOverlay::historyPath() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    // QStandardPaths::AppLocalDataLocation 在 Qt 里返回的是 CompanyName/ProductName 格式，
    // 被控端没设组织名 → 返回空。兜底用已知的 LOCALAPPDATA/xingjikong。
    if (base.isEmpty())
        return QDir::homePath() + QStringLiteral("/AppData/Local/xingjikong/island_history.json");
    return base + QStringLiteral("/island_history.json");
}

void IslandOverlay::loadHistory()
{
    QFile f(historyPath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    const QJsonArray arr = doc.array();
    m_history.clear();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        HistoryItem h;
        h.title = o.value(QStringLiteral("title")).toString();
        h.desc = o.value(QStringLiteral("desc")).toString();
        h.icon = o.value(QStringLiteral("icon")).toString();
        h.ts = o.value(QStringLiteral("ts")).toVariant().toLongLong();
        h.timeStr = o.value(QStringLiteral("timeStr")).toString();
        m_history.append(h);
    }
    // 限制条数
    while (m_history.size() > kMaxHistory)
        m_history.removeFirst();
    // 2026-10-13：加载时顺手剔除超期（>7 天）条目 —— 用户「每条消息保存时间为一周，
    // 一周后清除历史」。启动即清，落盘一次（下次读到的就是干净的）。
    const int before = m_history.size();
    pruneHistory();
    if (m_history.size() != before) saveHistory();
}

void IslandOverlay::saveHistory() const
{
    QJsonArray arr;
    for (const auto &h : m_history) {
        QJsonObject o;
        o.insert(QStringLiteral("title"), h.title);
        o.insert(QStringLiteral("desc"), h.desc);
        o.insert(QStringLiteral("icon"), h.icon);
        o.insert(QStringLiteral("ts"), qint64(h.ts));
        o.insert(QStringLiteral("timeStr"), h.timeStr);
        arr.append(o);
    }
    QJsonDocument doc(arr);
    QDir().mkpath(QFileInfo(historyPath()).absolutePath());
    QFile f(historyPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(doc.toJson(QJsonDocument::Compact));
        f.close();
    }
}

// 2026-10-13（用户「每条消息保存时间为一周，一周后清除历史」）：剔除超期条目。
// ⚠️ ts<=0 的条目（老数据/字段缺失）**保留** —— 宁可多留一条，也别把用户的历史误删。
// 本函数只动内存，是否落盘交给调用方（loadHistory/archiveToHistory 各自决定）。
void IslandOverlay::pruneHistory()
{
    const qint64 cutoff = QDateTime::currentSecsSinceEpoch() - kHistoryKeepSecs;
    for (int i = m_history.size() - 1; i >= 0; --i) {
        const qint64 ts = m_history.at(i).ts;
        if (ts > 0 && ts < cutoff)
            m_history.removeAt(i);
    }
}

void IslandOverlay::archiveToHistory()
{
    // 有标题才归档（空内容/空闲态不该进历史）
    if (m_title.isEmpty()) return;
    // 同标题不重复归档（5 秒内的重复跳过）
    if (!m_history.isEmpty()) {
        const auto &last = m_history.last();
        if (last.title == m_title && (QDateTime::currentSecsSinceEpoch() - last.ts) < 5)
            return;
    }

    HistoryItem h;
    h.title = m_title;
    h.desc = m_desc;
    h.icon = m_icon;
    h.ts = QDateTime::currentSecsSinceEpoch();
    h.fromAdmin = m_fromAdmin;   // 2026-10-13：管理端通知打标（历史排序置顶用）

    // 格式化时间串
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(h.ts);
    const QDateTime today = QDateTime::currentDateTime();
    if (dt.date() == today.date()) {
        h.timeStr = dt.toString(QStringLiteral("HH:mm"));
    } else if (dt.date().addDays(1) == today.date()) {
        h.timeStr = QStringLiteral("昨天 ") + dt.toString(QStringLiteral("HH:mm"));
    } else {
        h.timeStr = dt.toString(QStringLiteral("M/d HH:mm"));
    }

    m_history.append(h);
    while (m_history.size() > kMaxHistory)
        m_history.removeFirst();
    pruneHistory();   // 2026-10-13：追加后也清一次超期条目（与 loadHistory 同一口径）
    saveHistory();
}

void IslandOverlay::showIsland(const QString &title, const QString &desc,
                               const QString &icon, int durationMs, Form form,
                               bool fromAdmin)
{
    // ── 历史归档：上一条有内容的通知在被顶掉时进历史 ──
    // 判据：m_contentActive && m_title 非空（活动中的真通知被顶掉）
    // 2026-10-15 组件1：再加 !m_mediaPresenting —— 上一条是"正在播放"时不归档
    // （媒体不是通知；且 setMediaSession 里置位在 showIsland **之后**，本行读到的
    //  永远是"上一条内容"的状态：歌2顶歌1→true 不归档；媒体顶通知→false 通知照常归档；
    //  通知顶媒体→true 媒体不归档）。
    if (m_contentActive && !m_title.isEmpty() && !m_ftActive && !m_mediaPresenting)
        archiveToHistory();

    m_title = title;
    m_desc = desc;
    m_icon = icon;
    m_fromAdmin = fromAdmin;   // 2026-10-13：跟着这条内容走，归档时写进 HistoryItem
    m_durationMs = durationMs;
    m_form = form;
    // 普通 showIsland 一律无按钮：清掉上一条确认态残留的按钮/回调。
    // （带按钮的走 showWithActions —— 它在调用本函数**之后**再回填 m_actions。）
    m_actions.clear();
    m_onAction = nullptr;
    m_idle = false;
    m_hadContent = true;
    m_contentActive = true;
    m_closing = false;          // 新内容上屏：取消"淡出中"，否则淡入结束会被当成淡出去 hide
    // A4：新内容重置滚动位置 —— 否则沿用上一条的 m_marqueeX（可能是"已滚出屏外"的负值），
    //     新文案一上来就从中间/屏外开始画，看着像缺字。
    m_marqueeX = 0;
    m_marqueeRun = false;
    m_marqueeTextW = 0;
    m_dismissGuard->stop();     // ⚠️ 必须停：上一条的兜底守卫是"给上一条用的"，
                                //    不停的话它到点会把刚上屏的新提示打掉
    m_expandGuard->stop();      // 展开定时器同理（新内容回到收起态，不该继承上一条的回收时刻）
    const bool wasExpanded = m_expanded;
    m_expanded = false;         // 新内容一律回到收起态，别沿用上一条的展开高度换文案

    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) { hide(); return; }

    // 从空闲胶囊长出来（Dimensions 变了所以走动画；第一次出现才直接 setGeometry）
    const QRect target = geometryForForm(m_form);

    if (!isVisible()) {
        setGeometry(target);
        show();
        raise();
    } else {
        animateGeometry(target);
    }

    // 淡入：显式从当前不透明度（首出现是 1）补间到 1，并且先 stop 再 start。
    // ⚠️ 不能只 start()：QPropertyAnimation 会在上一次动画的终点上接着跑，
    //    而 dismiss 把它留在了 0，下次出现就永远是 0（淡入"淡"进去的是透明的）。
    if (auto *eff = qobject_cast<QGraphicsOpacityEffect *>(m_fadeAnim->targetObject()))
        m_fadeAnim->setStartValue(eff->opacity());
    m_fadeAnim->setEndValue(1.0);
    m_fadeAnim->stop();
    m_fadeAnim->setDirection(QAbstractAnimation::Forward);
    m_fadeAnim->start();

    // 自动收起。
    // ⚠️ 交互态（interactive）**一律不自动关**：这里原来写的是
    //   `durationMs>0 && !(m_interactive && m_expanded)` —— 交互态但还收着的时候
    //   倒计时照样走，6 秒后胶囊自己消失，用户压根没机会点（点了才展开，
    //   可人还没来得及伸手呢）。可点的东西不能自己跑掉，交给 setInteractive(false)
    //   或下次 showIsland 顶掉时收。
    m_autoClose->stop();
    if (durationMs > 0 && !m_interactive)
        m_autoClose->start(durationMs);

    // 新内容把上一条的展开态压平了 —— 状态变了就得发信号（见 enterIdle 里的同款说明）
    if (wasExpanded) emit expandedChanged(false);
    if (m_bridge) m_bridge->sync();
    update();

    // ── 通知上岛音效 ──
    // 2026-10-12 用户裁决：
    //   ①「通知/状态/灵动岛音效**默认关闭**」→ 总开关 whaleAudioOn() 已默认 false
    //     （QSettings settings/whaleAudioOn 可开）；
    //   ②「低优先级通知不需要也不可以添加音效」→ 只有**通知型**（bell 图标族）
    //     上岛才可能播音效；状态类（连接/文件/考试/截图等）一律不播；
    //   ③ 开时音量 = 正常通知的 25%（0.8 → 0.2）。
    // 判定：icon 以 bell / notify / alert 开头 = 通知型；其它 icon（check-circle/
    // triangle-alert/download/camera…）是状态型，静音。与 setSeverity 的 remind/
    // inform（低优先级）无关——低优先级通知本来就不该响。
    const bool isNotifyIcon = m_icon.startsWith(QLatin1String("bell"))
        || m_icon.startsWith(QLatin1String("notify"))
        || m_icon.startsWith(QLatin1String("alert"));
    if (isNotifyIcon && m_bridge && m_bridge->whaleAudioOn())
        WhaleAudio::instance()->play(WhaleAudio::Sound::Notify, 0.2);
}

// 带交互按钮的通知（确认/快捷回复）。先切交互态再走 showIsland：
// 这样 showIsland 内部算"要不要自动收"时看到的是"交互中" ⇒ 不会自己跑掉。
void IslandOverlay::showWithActions(const QString &title, const QString &desc, const QString &icon,
                                    Form form, const QStringList &actions,
                                    std::function<void(const QString &)> onAction, int durationMs,
                                    bool fromAdmin)
{
    const bool inter = !actions.isEmpty();
    if (inter) setInteractive(true);
    showIsland(title, desc, icon, inter ? 0 : durationMs, form, fromAdmin);
    m_actions = actions;
    m_onAction = onAction;
    // ⚠️ showIsland 里已经 sync 过一次，但那时 m_actions 还是空的（第 303 行才回填）。
    //    QML 侧按钮行读的是 bridge.actions ⇒ 必须在这里**再 sync 一次**，
    //    否则"确认"按钮要到下一次状态变化才出现（表现为点了没按钮）。
    if (m_bridge) m_bridge->sync();
    update();
}

// ── 文件传输进度（2026-10-11 · #198 接收进度条）──
// file_push 接收期间把进度推到岛。首次调用弹出常驻传输卡（不自动收起），
// 之后只更新进度；file_done 字节比对通过后由调用方 clearFileTransfer()。
void IslandOverlay::setFileTransfer(const QString &name, qint64 bytes, qint64 total)
{
    const bool first = !m_ftActive;
    m_ftActive = true;          // 先置位：showIsland 的归档守卫看到 true ⇒ 不归档上一条内容
    m_ftName = name;
    m_ftBytes = bytes;
    m_ftTotal = total;
    if (first) {
        // 常驻传输卡：duration 0 = 不自动收起；展开以容纳进度条。
        showIsland(QStringLiteral("正在接收 ") + name, QStringLiteral("0%"),
                   QStringLiteral("download"), 0, Form::Capsule);
        setExpanded(true);      // 展开态才放得下进度条
    }
    if (m_bridge) m_bridge->sync();
    update();
}

void IslandOverlay::clearFileTransfer()
{
    if (!m_ftActive && m_ftName.isEmpty()) return;
    m_ftActive = false;
    m_ftName.clear();
    m_ftBytes = 0;
    m_ftTotal = 0;
    if (m_bridge) m_bridge->sync();
    update();
}

// ── §6.3 语音频谱（2026-10-13 · 修「频谱显示到窗口去了」）──────────────────
// 从一段 PCM 算 16 段能量（**时域分段 RMS** —— 与旧 SpectrumWidget 同口径：语音
// 看的是包络起伏，不追求精确谱；也免引 FFT 依赖）。
// ⚠️ bits/sampleRate 只用于解包；分带按"样本序"线性切，不依赖采样率。
void IslandOverlay::feedSpectrum(const QByteArray &pcm, int sampleRate, int bits)
{
    Q_UNUSED(sampleRate);
    if (pcm.isEmpty()) return;

    // 第一次来帧：置位并通知 QML 显示频谱区。
    if (!m_spectrumOn) {
        m_spectrumOn = true;
        if (m_bridge) m_bridge->syncSpectrum();
    }
    // 频谱画在**展开态**里：保证岛是展开的。
    // （已展开时 setExpanded(true) 会早退，不会打断用户；收起时自动展开，对讲期间保持可见。）
    if (isVisible() && !m_expanded) setExpanded(true);

    // 解包 → float（-1..1）
    QVector<float> frame;
    if (bits == 16 && pcm.size() >= 2) {
        const auto *s = reinterpret_cast<const qint16 *>(pcm.constData());
        const int n = int(pcm.size() / 2);
        frame.resize(n);
        for (int i = 0; i < n; ++i) frame[i] = float(s[i]) / 32768.f;
    } else if (bits == 32 && pcm.size() >= 4) {
        const auto *f = reinterpret_cast<const float *>(pcm.constData());
        const int n = int(pcm.size() / 4);
        frame.resize(n);
        for (int i = 0; i < n; ++i) frame[i] = f[i];
    } else {
        return;   // 不认识的格式：忽略（不假装有频谱）
    }
    if (frame.isEmpty()) return;

    if (m_spectrumBands.size() != kSpectrumBands)
        m_spectrumBands.fill(0.f, kSpectrumBands);

    const int n = frame.size();
    for (int b = 0; b < kSpectrumBands; ++b) {
        const int lo = (b * n) / kSpectrumBands;
        const int hi = ((b + 1) * n) / kSpectrumBands;
        double sum = 0.0;
        int c = 0;
        for (int i = lo; i < hi && i < n; ++i) {
            sum += double(frame[i]) * double(frame[i]);
            ++c;
        }
        const float e = c > 0 ? float(qSqrt(sum / double(c))) : 0.f;
        const float target = qMin(1.f, e * 6.f);   // 增益：语音幅度偏小
        m_spectrumBands[b] = 0.7f * m_spectrumBands[b] + 0.3f * target;   // 平滑，避免条形乱跳
    }
    m_spectrumLastMs = QDateTime::currentMSecsSinceEpoch();
    if (m_bridge) m_bridge->syncSpectrum();
}

void IslandOverlay::setSpectrumActive(bool on)
{
    if (m_spectrumOn == on) return;
    m_spectrumOn = on;
    if (!on) {
        m_spectrumBands.fill(0.f, kSpectrumBands);
        m_spectrumLastMs = 0;
    }
    if (m_bridge) m_bridge->syncSpectrum();
    update();
}

// ── P3 播放时段管理：岛底「正在播放」状态（2026-10-14）──────────────
// media_session_start 成功 → 由 main.cpp 调用；elapsed 用墙上钟估算（ffplay 无进度回调）。
//
// 2026-10-15 组件1（点歌互动）：播放呈现上岛 —— 媒体以**收起态胶囊**上屏
// （不开独立窗、时长 0 不自动收、交互态可点开互动）。置位顺序是核心：
//   setInteractive/showIsland 先走（归档守卫读到的还是"上一条内容"的旧标志），
//   m_mediaPresenting 最后置位 ⇒ 歌2顶歌1不归档、通知顶媒体不归档媒体、
//   媒体顶通知则通知正常进历史。
void IslandOverlay::setMediaSession(const QString &title, qint64 totalMs,
                                     const QString &nextTitle, const QString &nextCover)
{
    m_mediaOn = true;
    m_mediaTitle = title;
    m_mediaTotalMs = totalMs;
    m_mediaNextTitle = nextTitle;
    m_mediaNextCover = nextCover;
    // 图标 music.svg 在 resources/icons/ 已存在；bell/notify/alert 白名单外 ⇒ 不上岛音效。
    setInteractive(true);
    showIsland(title, QString(), QStringLiteral("music"), 0, Form::Capsule);
    m_mediaPresenting = true;   // ⚠️ 必须在 showIsland 之后（见上）
    // 组件7：广播开始 —— 2.5s 内岛显示"即将播放"，到点自动转 playing。
    m_mediaPhase = QStringLiteral("starting");
    armMediaPhaseTimer(2500);
    // 宽度按媒体预算（收起 320）重算一次：上面 showIsland 算几何时本标志还没置位
    // （归档守卫必须先读旧值），拿到的是内容定宽（短曲名 ~200，互动区放不下）。
    // 展开态不用管 —— setExpanded 发生在本函数返回之后，那时标志已置位。
    if (isVisible() && !m_expanded)
        animateGeometry(geometryForForm(m_form));
    if (m_bridge) m_bridge->sync();
    update();
}

void IslandOverlay::clearMediaSession()
{
    if (!m_mediaOn && m_mediaTitle.isEmpty()) return;
    // 2026-10-15 组件1：只有"岛面当前画的就是这条媒体"时才收回岛面。
    // 通知顶掉期间的 clearMediaSession 不碰岛 —— 那条通知有自己的生命周期
    // （自动收/确认按钮），媒体结束不该把人家吞了。
    const bool contentIsMedia = m_mediaPresenting && (m_title == m_mediaTitle);
    m_mediaOn = false;
    m_mediaTitle.clear();
    m_mediaElapsedMs = 0;
    m_mediaTotalMs = 0;
    m_mediaNextTitle.clear();
    m_mediaNextCover.clear();
    m_mediaPresenting = false;
    // 组件7：相位归零 + 停相位定时器（"播完"的 2.5s 延时也在这里被取消）。
    m_mediaPhase.clear();
    if (m_mediaPhaseTimer) m_mediaPhaseTimer->stop();
    if (contentIsMedia) {
        // "正在播放"不是通知，不进历史：先断 m_contentActive 再 dismiss，
        // 否则 enterIdle 的归档守卫会把媒体内容写进历史。
        m_contentActive = false;
        dismiss();
    }
    if (m_bridge) m_bridge->sync();
    update();
}

void IslandOverlay::advanceMediaElapsed(int ms)
{
    if (!m_mediaOn) return;
    m_mediaElapsedMs += ms;
    // ── 组件7：快结束判定 ────────────────────────────────────────
    // 相位单点判定在这里（QML 不自己算，避免两端口径漂）。阈值取
    // max(15s, 总长 10%)：长曲按 10%（4:29 → 27s 前提示），短曲保底 15s。
    // 只在 playing 时单向推进（ending 是终态，不因 elapsed 回退而退回去）。
    if (m_mediaPhase == QLatin1String("playing") && m_mediaTotalMs > 0) {
        const qint64 remain = m_mediaTotalMs - m_mediaElapsedMs;
        if (remain <= qMax<qint64>(15000, m_mediaTotalMs / 10))
            m_mediaPhase = QStringLiteral("ending");
    }
    if (m_bridge) m_bridge->sync();
    update();
}

// ── 组件7 播放四态细化（2026-10-15，用户点单）────────────────────────
// 相位定时器（单一复用）：三种需要"等一会儿再迁"的相位共用它 ——
//   starting → playing（广播开始提示 2.5s 后进常态）
//   blocked  → 清相位 + 收起（禁播提示 3.5s 后自动收）
//   ended    → clearMediaSession（播完保留歌名 2.5s 后收起）
// 到点重读 m_mediaPhase 再分派：期间若被 setMediaSession/clearMediaSession
// 抢先改了相位（歌2顶歌1、手动停），本次回调按新相位走，不会误迁。
void IslandOverlay::armMediaPhaseTimer(int ms)
{
    if (!m_mediaPhaseTimer) {
        m_mediaPhaseTimer = new QTimer(this);
        m_mediaPhaseTimer->setSingleShot(true);
        QObject::connect(m_mediaPhaseTimer, &QTimer::timeout, this, [this] {
            const QString p = m_mediaPhase;
            if (p == QLatin1String("starting")) {
                m_mediaPhase = QStringLiteral("playing");
            } else if (p == QLatin1String("blocked")) {
                m_mediaPhase.clear();
                m_mediaPresenting = false;
                m_contentActive = false;   // 禁播提示不是通知，不进历史
                dismiss();
            } else if (p == QLatin1String("ended")) {
                clearMediaSession();       // 内部已 sync + update
                return;
            } else {
                return;   // 相位已被换走（歌2顶歌1 / 手动停）⇒ 什么都不做
            }
            if (m_bridge) m_bridge->sync();
            update();
        });
    }
    m_mediaPhaseTimer->start(ms);
}

void IslandOverlay::mediaBlocked(const QString &reason)
{
    // 播放条家族的一态：与 setMediaSession 同款收起胶囊上屏，只是没有进度。
    // 走 m_mediaPresenting（不是 m_mediaOn）—— "岛面画的是媒体家族"，但没在播。
    // 时长参数给 0（不自动收），收回交给相位定时器（3.5s），两级收口不打架。
    m_mediaOn = false;                    // 没在播：进度条/下一首都不显示
    m_mediaTitle = reason.isEmpty() ? QStringLiteral("当前时段不开放播放") : reason;
    m_mediaElapsedMs = 0;
    m_mediaTotalMs = 0;
    m_mediaNextTitle.clear();
    m_mediaNextCover.clear();
    m_mediaPhase = QStringLiteral("blocked");
    setInteractive(false);                // 禁播没有互动可点
    showIsland(m_mediaTitle, QString(), QStringLiteral("alert"), 0, Form::Capsule);
    m_mediaPresenting = true;             // ⚠️ 必须在 showIsland 之后（同上）
    if (isVisible() && !m_expanded)
        animateGeometry(geometryForForm(m_form));
    armMediaPhaseTimer(3500);
    if (m_bridge) m_bridge->sync();
    update();
}

void IslandOverlay::mediaFinished()
{
    // ffplay 播完自行退出（-autoexit）⇒ 原来没人管，岛会一直停在"正在播放"
    // 直到老师手动停或时段截断。这里补上"播完"态：保留歌名 2.5s（让学生看到
    // 结尾），之后由相位定时器走 clearMediaSession 收起。
    if (!m_mediaOn) return;               // 已清过 / 手动停先到 ⇒ 不重复
    m_mediaOn = false;                    // 停进度（岛不再"在播"，但仍在岛上）
    m_mediaPhase = QStringLiteral("ended");
    // 保持 3s（不是 2.5s）：探针在 2500ms 抓帧，2.5s 会与清理赛跑（实测 ended
    // 截图被 clearMediaSession 抢先，岛已收起只剩收起胶囊）。3s 同时也是更舒服的
    // "播放结束"停留时长。
    armMediaPhaseTimer(3000);
    if (m_bridge) m_bridge->sync();
    update();
}

// ── 组件5 祝福弹幕（2026-10-15）────────────────────────────────
// 接线链：QML 投稿钮 → IslandBridge::sendBlessing → 本类 → DanmakuWidget。
// 弹幕是独立全屏浮层（设计文档 L32 裁决），岛只提供输入行；岛不画弹幕、
// 不持有弹幕内容，DanmakuWidget 单例自己管队列/滚动/冷却。
// 组件6 起本地入队成功后另经 CloudReporter 旁路上报云端（见 sendBlessing）。

DanmakuWidget *IslandOverlay::ensureDanmaku()
{
    if (!m_danmaku) {
        m_danmaku = DanmakuWidget::danmakuWidget();   // 单例懒建（替代 g_danmaku）
        // 冷却倒计时 1Hz → 桥全量 NOTIFY → QML 倒计时（与 media elapsed 同频率先例；
        // 只在 30s 冷却期内跑，归零自停，常态零开销）
        connect(m_danmaku, &DanmakuWidget::blessingCooldownChanged,
                this, [this](int) { if (m_bridge) m_bridge->sync(); });
    }
    return m_danmaku;
}

int IslandOverlay::sendBlessing(const QString &text)
{
    const int code = ensureDanmaku()->sendBlessingLocal(text);
    // 组件6：本地入队成功（码 0）才旁路上报云端 —— 学生不可信源走
    // 「端侧先行」：本地体验（入队滚动 + 冷却）永远是第一公民，云端是旁路。
    // 被拦（1 敏感/2 冷却/3 无效）不上报：本地都没让它过，报上去只会浪费审核队列。
    // 上报失败不重试不提示（ws 断线时 g_ws 发送自身会记日志）——课堂节奏优先。
    if (code == 0) {
        reportToCloud(QStringLiteral("blessing"),
                      QJsonObject{ { QStringLiteral("text"), text.trimmed() } });
    }
    return code;
}

int IslandOverlay::blessingCooldownSec() const
{
    // peek 不建窗口：从没发过祝福时岛也能安全读冷却（返回 0 = 无冷却）。
    // ⚠️ 这里刻意不用 ensureDanmaku()：const 方法不该有"为了读个数先造个窗口"
    //    这种副作用。
    if (DanmakuWidget *d = DanmakuWidget::danmakuWidgetPeek())
        return d->blessingCooldownRemaining();
    return 0;
}

// ── 组件6 云端上报旁路（2026-10-15）──────────────────────────────
// 三处上报（blessing / feedback / song_request）都收口到 reportToCloud：
// reporter 未注入（探针/单测）= 静默跳过；注入后由 main.cpp 的 lambda 负责
// 真正经 g_ws 发出去（island 不认识 g_ws，也不该认识——依赖方向永远是
// main → island，不是反过来）。

void IslandOverlay::setCloudReporter(CloudReporter reporter)
{
    m_cloudReporter = std::move(reporter);
}

void IslandOverlay::reportToCloud(const QString &type, const QJsonObject &payload)
{
    if (!m_cloudReporter) return;   // 未接云端：静默跳过（本地行为不受影响）
    m_cloudReporter(type, payload);
}

void IslandOverlay::reportFeedback(const QString &kind, int value)
{
    reportToCloud(QStringLiteral("feedback"),
                  QJsonObject{ { QStringLiteral("kind"), kind },
                               { QStringLiteral("value"), qMax(1, qMin(value, 100)) } });
}

void IslandOverlay::submitSongRequest(const QString &title, const QString &artist, const QString &source)
{
    reportToCloud(QStringLiteral("song_request"),
                  QJsonObject{ { QStringLiteral("title"), title.trimmed() },
                               { QStringLiteral("artist"), artist.trimmed() },
                               { QStringLiteral("source"), source } });
}

// 仅全屏形态的配色档（与旧 NotifyWindow 同白名单 + 黑底回落）
void IslandOverlay::setSeverity(const QString &severity)
{
    m_severity = severity.trimmed().toLower();
    if (m_bridge) m_bridge->sync();
    update();
}

void IslandOverlay::setIdleEnabled(bool on)
{
    m_idleEnabled = on;
    if (!on && m_idle && isVisible()) {
        // 关掉空闲态时得把残留状态一起清掉：不然 m_idle 还留着 true、
        // m_interactive 还是 true（一个"隐藏但可交互"的状态，谁也说不清它是什么）。
        m_idle = false;
        m_expanded = false;
        setInteractive(false);
        hide();
    }
    if (m_bridge) m_bridge->sync();
}

void IslandOverlay::ensureIdle()
{
    if (!m_idleEnabled) return;
    // ⚠️ 判据是 isVisible() 而不是 `m_idle && isVisible()`：后者在"岛上正显示着一条
    //    真实内容（m_idle=false）"时会走进去把它顶成空闲胶囊 —— 一个叫 ensure 的入口
    //    不该顺手销毁别人的内容态。
    if (isVisible()) return;
    enterIdle();
}

void IslandOverlay::enterIdle()
{
    // ⚠️ 形态必须回到胶囊：空闲半胶囊是**顶部**那条，若沿用上一条的 Centered/Fullscreen，
    //    收回动画的目标几何会是"屏幕正中/全屏"，空闲态就变成一块挡在桌面中间的大板子。
    m_form = Form::Capsule;
    m_dismissGuard->stop();     // 先把上一条的兜底守卫停掉（原来自检点排在 early return 之后，留了窗口）
    m_expandGuard->stop();
    m_closing = false;

    if (!m_idleEnabled) {
        // 关掉空闲态就退回老行为：彻底隐藏
        m_idle = false;
        m_expanded = false;
        m_contentActive = false;
        m_autoClose->stop();
        clearOpenPath();
        setInteractive(false);
        m_closing = true;                 // 这次是淡出，finished 到了才 hide
        m_dismissGuard->start(600);       // 淡出没发 finished 也得收掉
        auto *eff = qobject_cast<QGraphicsOpacityEffect *>(m_fadeAnim->targetObject());
        if (eff) m_fadeAnim->setStartValue(eff->opacity());
        m_fadeAnim->setEndValue(0.0);
        m_fadeAnim->stop();
        m_fadeAnim->setDirection(QAbstractAnimation::Forward);
        m_fadeAnim->start();
        if (m_bridge) m_bridge->sync();
        return;
    }

    if (m_idle && isVisible()) return;

    const bool wasExpanded = m_expanded;
    // ── 历史归档：有内容的通知在收回空闲时进历史 ──
    if (m_contentActive && !m_title.isEmpty())
        archiveToHistory();
    m_idle = true;
    m_expanded = false;
    m_contentActive = false;   // 内容这就算过去了，之后展开的是"最近一条"
    m_autoClose->stop();
    // A4：收回空闲即停滚并归零
    m_marqueeX = 0;
    m_marqueeRun = false;
    clearOpenPath();
    m_actions.clear();         // 收回即清按钮，别让空闲态还留着上一条的"确认"
    m_onAction = nullptr;
    // 空闲胶囊可点：这将近 120×36 的一小块不再穿透。作为补偿它贴在最顶端（y=10）
    // 且画得比活动态更淡，尽量少占课件的可视注意力。
    // （另一条防线在 setExpanded 里：展开态有硬回收超时，不会长期压着屏幕顶部。）
    setInteractive(true);

    if (!isVisible()) {
        setGeometry(geometryForIdle());
        show();
        raise();
    } else {
        animateGeometry(geometryForIdle());
    }
    update();
    // 状态改了就要发信号：dismiss→enterIdle 是最常见的收起路径，
    // 原来它不发 expandedChanged(false)，对外就出现"isExpanded() 已是 false、
    // 最后一次信号还是 true"的自相矛盾。
    if (wasExpanded) emit expandedChanged(false);
    if (m_bridge) m_bridge->sync();
}

void IslandOverlay::setExpanded(bool on)
{
    if (m_expanded == on) return;
    m_expanded = on;

    // A4：收起（含点击停住）即把滚动位置归零 —— 需求"滚动持续直到人为点击"，
    //     点击 → 收起 → 下次再展开从头开始，不会停在半截。
    if (!on) { m_marqueeX = 0; m_marqueeRun = false; }

    // 从空闲态展开：得有东西可看。从来没显示过内容就给一句占位话，
    // 否则展开是一块空白的黑底 —— 用户会觉得"点了没反应"。
    if (on && !m_hadContent) {
        m_title = QStringLiteral("暂无进行中的通知");
        m_desc = QStringLiteral("有新的动态会在这里提示");
    }
    if (on) m_idle = false;

    if (!isVisible()) { emit expandedChanged(on); if (m_bridge) m_bridge->sync(); return; }

    // 空闲态展开再收起：直接回空闲半胶囊。
    // 中间别在"展开(168) → 标题胶囊(56) → 空闲(36)"来回三段跳 ——
    // 用户从空闲拉下来看一眼，推回去就该原样回到空闲。
    if (!on && !m_contentActive) {
        enterIdle();
        emit expandedChanged(on);
        if (m_bridge) m_bridge->sync();
        return;
    }

    // ⚠️ 展开/收起是**胶囊**那一档的概念（56↔168 高度切换）。
    //    Centered / Fullscreen 形态下点一下不该把它压回顶部胶囊 —— 那会让全屏通知
    //    被误触后瞬间缩成一条，看着像崩了。非胶囊形态：保持原几何，只记状态。
    if (m_form != Form::Capsule) {
        m_expanded = on;
        if (on) m_autoClose->start(qMax(5000, m_durationMs));
        emit expandedChanged(on);
        if (m_bridge) m_bridge->sync();
        return;
    }

    if (m_idle || on) {
        // 空闲↔展开：宽度也要跟着变（120 ↔ 内容宽），所以整块 geometry 都要给
        animateGeometry(on ? geometryForContent(int(Expanded)) : geometryForIdle());
    } else {
        animateGeometry(geometryForContent(m_expanded ? int(Expanded) : int(Collapsed)));
    }
    // 展开给足阅读时间
    if (on) m_autoClose->start(qMax(5000, m_durationMs));
    // ⚠️ 展开态必须有一条**硬回收**路径：展开就是 168px 高、最宽 640px 的大块，
    //    而且交互态（不穿透）。原来唯一的出路是"上推 ≥24px"，而 m_autoClose 的
    //    `if (m_interactive && m_expanded) return;` 又会把到点的那次直接吞掉且不再排期 ——
    //    于是空闲态/文件提示一旦被点开，就永远压在屏幕顶部吃点击。
    //    硬超时按场景分档：
    //    · 无活跃通知（从空闲拉下来看一眼"暂无通知"）→ 5s 收，用户不需要看那么久
    //    · 有活跃通知内容 → 20s 收（原来的兜底）
    if (on) m_expandGuard->start(m_contentActive ? kExpandMaxMs : kIdleExpandMs);
    else    m_expandGuard->stop();
    emit expandedChanged(on);
    if (m_bridge) m_bridge->sync();
}

void IslandOverlay::animateGeometry(const QRect &target)
{
    m_geoAnim->stop();
    m_geoAnim->setStartValue(geometry());
    m_geoAnim->setEndValue(target);
    m_geoAnim->start();
}

void IslandOverlay::dismiss()
{
    // 空闲态开启时，"收起"不是消失，而是收回成顶部那条半胶囊
    // （2026-10-07 用户要求：无任务、通知则收回顶部，显示半胶囊+横杠箭头）。
    if (m_idleEnabled) {
        if (!isVisible()) { enterIdle(); return; }
        m_dismissGuard->stop();
        m_autoClose->stop();
        enterIdle();
        return;
    }

    // 兜底：淡出动画万一没发 finished（属性/状态异常），也不能让胶囊赖在屏幕上。
    m_dismissGuard->stop();
    m_dismissGuard->start(600);

    // ⚠️ 顺序要是 stop → 设起止 → 设方向 → start 反过来：
    //    直接 start() 时 QPropertyAnimation 会从上一次终点接着跑，永远回不到 0。
    auto *eff = qobject_cast<QGraphicsOpacityEffect *>(m_fadeAnim->targetObject());
    if (eff) m_fadeAnim->setStartValue(eff->opacity());
    m_fadeAnim->setEndValue(0.0);
    m_fadeAnim->stop();
    m_fadeAnim->setDirection(QAbstractAnimation::Forward);
    // ⚠️ 2026-10-13 修：这里曾残留一条 `connect(..., [this]{ hide(); }, Qt::UniqueConnection)` ——
    //    正是本文件 132-137 行注释里记的那个坑（lambda 连接永不解绑、Qt::UniqueConnection 对
    //    lambda 无效）。当年的修复改成了"成员槽 + m_closing 状态位"，但**漏删了 dismiss() 这一处**：
    //    于是每次 dismiss() 都多挂一条 finished→hide()，非空闲路径下"第一条提示之后每条只闪 220ms"。
    //    收敛回设计意图：置 m_closing，由 onFadeFinished()（139 行那条只连一次的成员槽）统一收窗。
    m_closing = true;                 // 这次是淡出：finished 到了才 hide
    m_fadeAnim->start();
    if (m_bridge) m_bridge->sync();
}

void IslandOverlay::setLightMode(bool light)
{
    if (m_light == light) return;
    m_light = light;
    if (m_bridge) m_bridge->sync();
    update();
}

void IslandOverlay::setInteractive(bool on)
{
    if (m_interactive == on) return;
    m_interactive = on;
    setAttribute(Qt::WA_TransparentForMouseEvents, !on);
    // ⚠️⚠️ 2026-10-11 改 QML 后新增，漏了就是**事故**：
    //    QQuickWidget 是**子 QWidget**，父窗口设了 WA_TransparentForMouseEvents
    //    **不会**自动继承到它 —— 它会照常把鼠标事件吃在自己的 rect 里。
    //    结果：标称"穿透"（C++ 层属性是对的，谁查都查不出问题），实际整块岛照样
    //    挡住下面的课件/白板点击。教室里就是"老师点不中课件"。
    //    必须把同一个属性同步给 m_quick。
    // ⚠️ 还有一层：QQuickWidget 内部的 QQuickWindow 收到事件后会分给 QML 的 MouseArea。
    //    所以 QML 侧也按 interactive 决定 MouseArea 的 enabled（见 IslandRoot.qml
    //    的 gesture.enabled）—— 两边都要关，只关一边仍会"吃掉"点击。
    if (m_quick) {
        // ⚠️ 2026-10-13：删掉原来的 m_quick->setEnabled(on || !m_idle)。
        //    setEnabled(false) 会让 QQuickWidget 走"禁用态"渲染（可能变灰）并停掉部分
        //    窗口内部更新；它和 WA_TransparentForMouseEvents 是**两套独立机制**，
        //    同时用交互语义混乱，且 on||!m_idle 在"不可交互但非空闲"时判 true 含义不明。
        //    真正管穿透的是下面这条属性；精确的"哪些区域可点"由 QML 侧 MouseArea.enabled
        //    决定（见 IslandRoot.qml 的 gesture.enabled，上面注释也讲了）。只留一条。
        m_quick->setAttribute(Qt::WA_TransparentForMouseEvents, !on);
    }
    if (on) setCursor(Qt::PointingHandCursor);
    else    unsetCursor();
    // 从可点切回纯展示：这条如果还在屏上，就得给它定个消失时间（可点时不定时）。
    if (!on && isVisible() && m_durationMs > 0)
        m_autoClose->start(m_durationMs);
    if (m_bridge) m_bridge->sync();
}

void IslandOverlay::setOpenPath(const QString &path)
{
    m_openPath = path;
    if (m_bridge) m_bridge->sync();
    update();
}

void IslandOverlay::clearOpenPath()
{
    if (m_openPath.isEmpty()) return;
    m_openPath.clear();
    if (m_bridge) m_bridge->sync();
    update();
}

// 打开 m_openPath 所在的文件夹并选中该文件。
// ⚠️ 用 explorer /select 而不是 QDesktopServices::openPath(文件原路径)：后者在 Windows
// 上是 ShellExecute 打开**文件本身**（.exe 就直接跑、.pdf 拉起阅读器），被控端是学生机、
// 下发内容不是我们能担保的，等于替下发的那台设备决定运行什么。只"定位到文件"最稳。
void IslandOverlay::openInShell()
{
    if (m_openPath.isEmpty()) return;
    const QFileInfo fi(m_openPath);
    if (!fi.exists()) return;
    QStringList args;
    args << QStringLiteral("/select,") << QDir::toNativeSeparators(fi.absoluteFilePath());
    QProcess::startDetached(QStringLiteral("explorer.exe"), args);
}

// Centered / Fullscreen 的绘制（#98）。
// ⚠️ 与胶囊绘制**完全分开**：胶囊那套逻辑是按 56/168px 高、顶部居中的假设写的
//    （图标在左、标题描述横排），硬塞进全屏会把文字挤到左上角一小块。
//    这里单独写一条"居中大字"的路径，两边互不干扰。
void IslandOverlay::paintBigForm()
{
    const bool full = (m_form == Form::Fullscreen);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 底：全屏时更不透明（要压住桌面），居中卡片保持半透明（还能看见后面的课件）
    QColor bg = m_light ? QColor(250, 250, 250, full ? 248 : 235)
                        : QColor(16, 16, 18, full ? 243 : 228);
    // 全屏配色档（remind/inform/urgent 三档白名单 + 黑底回落，与旧 NotifyWindow 一致）
    // 2026-10-13 MD3 唯一模板：urgent 对齐 baseline error #B3261E(=179,38,30)，
    // 与 IslandTheme.qml severityUrgent 逐字一致（QML/C++ 双绘 parity 铁律）。
    if (full) {
        if      (m_severity == QLatin1String("urgent")) bg = QColor(179, 38, 30, 210);
        else if (m_severity == QLatin1String("inform")) bg = QColor(200, 140, 10, 205);
        else if (m_severity == QLatin1String("remind")) bg = QColor(20, 140, 60, 205);
    }
    const QColor fg = m_light ? QColor(10, 10, 10) : QColor(250, 250, 250);
    const QColor sub = m_light ? QColor(90, 90, 90) : QColor(168, 168, 178);

    QPainterPath path;
    path.addRoundedRect(rect().adjusted(1, 1, -1, -1), full ? 20 : 16, full ? 20 : 16);
    p.fillPath(path, bg);
    p.setPen(m_light ? QColor(210, 210, 210, 180) : QColor(70, 70, 70, 180));
    p.drawPath(path);

    // 图标（可选）：居中偏上，全屏时画得大
    const int iconSize = full ? 56 : 34;
    int cursorY = rect().top() + (full ? 56 : 26);
    if (!m_icon.isEmpty()) {
        const QPixmap ipm = renderSvgIcon(m_icon, iconSize, fg);
        if (!ipm.isNull()) {
            p.drawPixmap(QRect(rect().center().x() - iconSize / 2, cursorY,
                               iconSize, iconSize), ipm);
            cursorY += iconSize + (full ? 28 : 18);
        } else {
            cursorY -= (full ? 28 : 12);   // 图标画不出来就把位置还回去，别留一大块空白
        }
    }

    // 标题：全屏 42px，居中卡片 22px；过长自动换行而不是撑破（全屏时按 90% 宽折行）
    QFont tf = font();
    tf.setPixelSize(full ? 42 : 22);
    tf.setBold(true);
    p.setFont(tf);
    p.setPen(fg);
    const int textW = rect().width() - (full ? 120 : 64);
    const QRect titleRect(rect().center().x() - textW / 2, cursorY, textW, 0);
    QRect tb = p.boundingRect(titleRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_title);
    p.drawText(tb, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_title);
    cursorY += tb.height() + (full ? 26 : 16);

    // 描述：次字号，同样居中折行
    if (!m_desc.isEmpty()) {
        QFont df = font();
        df.setPixelSize(full ? 22 : 14);
        p.setFont(df);
        p.setPen(sub);
        const QRect descRect(rect().center().x() - textW / 2, cursorY, textW, 0);
        QRect db = p.boundingRect(descRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_desc);
        p.drawText(db, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_desc);
    }

    // 交互按钮行（确认/快捷回复）。第一枚按"主按钮"画（实心反白），其余描边。
    if (!m_actions.isEmpty()) {
        const QVector<QRect> rs = actionButtonRects();
        QFont bf = font();
        bf.setPixelSize(full ? 22 : 14);
        bf.setBold(true);
        p.setFont(bf);
        for (int i = 0; i < rs.size(); ++i) {
            const QRect &r = rs[i];
            const bool primary = (i == 0);
            QPainterPath bp;
            bp.addRoundedRect(r, 10, 10);
            if (primary) {
                p.fillPath(bp, m_light ? QColor(20, 20, 20) : QColor(245, 245, 245));
                p.setPen(m_light ? QColor(250, 250, 250) : QColor(10, 10, 10));
            } else {
                p.setPen(m_light ? QColor(150, 150, 150) : QColor(96, 96, 104));
                p.drawPath(bp);
                p.setPen(m_light ? QColor(30, 30, 30) : QColor(232, 232, 236));
            }
            p.drawText(r, Qt::AlignCenter, m_actions[i]);
        }
    }
}

// 窗口改尺寸：QQuickWidget 要跟着铺满。
// ⚠️ 不能只靠 setGeometry(rect()) 在构造时设一次：几何动画（m_geoAnim 改的是
//    本窗口的 "geometry" 属性）每一次补间都会触发 resizeEvent，子控件不跟就会
//    "窗口长大了、QML 还停在旧尺寸" —— 表现为展开时右边/下面缺一块。
void IslandOverlay::resizeEvent(QResizeEvent *ev)
{
    QWidget::resizeEvent(ev);
    if (m_quick) m_quick->setGeometry(rect());
}

void IslandOverlay::paintEvent(QPaintEvent *)
{
    // ── 2026-10-11 改 QML 后 ──────────────────────────────────────
    // QML 铺满整个窗口，C++ 不需要再画任何东西（画了也会被 QQuickWidget 盖住）。
    // 只在「QML 没起来」时退回下面那套手绘 —— 被控端不能因为一个外观文件
    // 缺失/编译失败就在教室大屏上白屏。
    if (m_quick && m_quick->status() == QQuickWidget::Ready) return;

    // ⚠️ 下面这套是**兜底路径**（2026-10-11 前的主路径）。
    //    保留它不是因为"可能还用得上"——是因为**必须**有个不留空白的退路：
    //    被控端是教室里跑的软件，外观组件坏掉时宁可退回旧观感，也不能一片空。
    //    改 QML 时请以 IslandRoot.qml 为准，这里只做同步（别只改一边）。

    // #98：居中卡片 / 全屏走独立绘制路径，不吃下面那套胶囊逻辑
    if (m_form != Form::Capsule) {
        paintBigForm();
        return;
    }

    // 圆角半透明底（深浅两套），沿用 NotifyWindow 思路：paintEvent 画，不走 QSS。
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // ── 空闲态：半胶囊 + 横杠箭头 ──
    if (m_idle) {
        // 画得比活动态更淡（也被理解为"半"胶囊）：退到背景里，别抢课件的注意力。
        QPainterPath ip;
        ip.addRoundedRect(rect().adjusted(1, 1, -1, -1), rect().height() / 2.0,
                          rect().height() / 2.0);
        p.fillPath(ip, m_light ? QColor(250, 250, 250, 150)
                               : QColor(20, 20, 20, 140));
        p.setPen(m_light ? QColor(200, 200, 200, 160) : QColor(58, 58, 58, 160));
        p.drawPath(ip);

        // 横杠（28×4 圆角）+ 下方一个小三角箭头：通行的"往下拉"指示，
        // 不用 emoji/字体字符（U+2304 之类在不少字体里是豆腐块，手绘最稳）。
        // ⚠️ 前景不透明度别跟着底色一起压：浅色态原来是 alpha 170 的深灰叠在 alpha 150 的
        //    白胶囊上，实测糊成一团灰、看不出是"可以拉的把手"。底色退让、符号要清。
        //
        // 2026-10-11 · A3：空闲态右侧补一个 HH:mm 时钟（教室内学生抬头就知道点没到点）。
        //   横杠+箭头从"居中"移到**左侧**，时间放右侧，两者各占一边，互不打架。
        //   字体用 13px Medium（比活动态标题略小，空闲态要退到背景里不抢戏）。
        const QColor barCol = m_light ? QColor(0x16, 0x16, 0x16, 215) : QColor(0xFA, 0xFA, 0xFA, 225);
        const QColor timeCol = m_light ? QColor(0x16, 0x16, 0x16, 200) : QColor(0xFA, 0xFA, 0xFA, 210);

        // 时间（右半区）：HH:mm —— 复用 IslandClock::currentTimeText() 的格式口径
        const QString timeText = QTime::currentTime().toString(QStringLiteral("HH:mm"));
        QFont cf = font();
        cf.setPixelSize(13);
        cf.setWeight(QFont::Medium);
        p.setFont(cf);
        p.setPen(timeCol);
        const QRect timeRect(rect().left() + rect().width() / 2 - 4, rect().top(),
                             rect().width() / 2 - 10, rect().height());
        p.drawText(timeRect, Qt::AlignVCenter | Qt::AlignRight, timeText);

        // 横杠 + 箭头（左半区居中）：缩小并左移，给时间让位
        p.setPen(Qt::NoPen);
        p.setBrush(barCol);
        const int cx = rect().left() + rect().width() / 4;
        const int cy = rect().center().y() - 2;
        p.drawRoundedRect(QRect(cx - 12, cy - 2, 24, 4), 2, 2);

        QPainterPath tri;
        tri.moveTo(cx - 5, cy + 5);
        tri.lineTo(cx + 5, cy + 5);
        tri.lineTo(cx, cy + 11);
        tri.closeSubpath();
        p.drawPath(tri);
        return;
    }

    // 圆角半透明底（深浅两套），沿用 NotifyWindow 思路：paintEvent 画，不走 QSS。
    QPainterPath path;
    path.addRoundedRect(rect().adjusted(1, 1, -1, -1), 24, 24);
    p.fillPath(path, m_light ? QColor(250, 250, 250, 235)
                            : QColor(20, 20, 20, 225));
    p.setPen(m_light ? QColor(200, 200, 200) : QColor(58, 58, 58));
    p.drawPath(path);

    const QColor fg = m_light ? QColor(0x16, 0x16, 0x16) : QColor(0xFA, 0xFA, 0xFA);
    const QColor sub = m_light ? QColor(0x6E, 0x6E, 0x6E) : QColor(0xC8, 0xC8, 0xC8);

    const bool canOpen = !m_openPath.isEmpty();
    const int hintW = canOpen ? 34 : 0;   // 右侧「▸ 打开」占的宽，正文要给它让位
    QRect body = rect().adjusted(18, 0, -(16 + hintW), 0);
    int x = body.left();
    if (!m_icon.isEmpty()) {
        // 2026-10-08：图标从 emoji 字形改为 lucide(morphicons 同款) SVG，
        // 颜色直接绑前景 fg（深主题白 / 浅主题黑），跨主题稳定不糊。
        const QPixmap ipm = renderSvgIcon(m_icon, 22, fg);
        if (!ipm.isNull()) {
            const int iy = rect().top() + (height() - 22) / 2;
            p.drawPixmap(QRect(x, iy, 22, 22), ipm);
            x += 30;
        }
    }

    if (m_expanded) {
        // 展开态：标题上移，描述换行显示
        QFont tf = font();
        tf.setPixelSize(17);
        tf.setBold(true);
        p.setFont(tf);
        p.setPen(fg);
        p.drawText(QRect(x, rect().top() + 18, body.right() - x, 24),
                   Qt::AlignVCenter | Qt::AlignLeft, m_title);
        if (!m_desc.isEmpty()) {
            QFont df = font();
            df.setPixelSize(13);
            p.setFont(df);
            p.setPen(sub);
            // A4（2026-10-11 · §6.1）：长描述**横向滚动**（需求：展开后滚动展示、持续到点击）。
            // 测量：文本宽 > 可视宽才滚；否则静态左对齐画（不用滚，别硬凑动效）。
            const int descW = body.right() - x;
            const QRect descRect(x, rect().top() + 52, descW, 70);
            m_marqueeTextW = QFontMetrics(df).horizontalAdvance(m_desc);
            if (m_marqueeTextW > descW) {
                m_marqueeRun = true;
                if (m_marqueeX == 0 && !m_idle) m_marqueeX = descW;   // 首次进入从右侧滚入
                p.save();
                p.setClipRect(descRect);   // 超出部分裁掉，别糊到胶囊外
                // 垂直居中于 70px 高区
                const int ty = descRect.top()
                               + (descRect.height() - p.fontMetrics().height()) / 2
                               + p.fontMetrics().ascent();
                p.drawText(x + m_marqueeX, ty, m_desc);
                p.restore();
            } else {
                m_marqueeRun = false;
                m_marqueeX = 0;
                p.drawText(descRect, Qt::AlignLeft | Qt::TextWordWrap, m_desc);
            }
        }
    } else {
        // 收起态：标题左、描述右
        QFont tf = font();
        tf.setPixelSize(15);
        tf.setBold(true);
        p.setFont(tf);
        p.setPen(fg);
        p.drawText(QRect(x, rect().top(), body.right() - x, height()),
                   Qt::AlignVCenter | Qt::AlignLeft, m_title);
        // ⚠️ 描述紧跟标题、按**剩余宽度**省略，且用 ElideRight：
        //    老写法把它固定塞在 x+110 起、110px 宽、右对齐，胶囊只有两三百宽时
        //    实际能显示的只剩两三个字（实测「正在自动重连…」被裁成「连...」，省成「…」）——
        //    提示看不懂，等于没提示。
        if (!m_desc.isEmpty()) {
            QFont df = font();
            df.setPixelSize(12);
            const int titleW = p.fontMetrics().horizontalAdvance(m_title);
            const int dw = body.right() - (x + titleW) - 14;
            if (dw > 48) {
                const QString shown = QFontMetrics(df).elidedText(m_desc, Qt::ElideRight, dw);
                if (!shown.isEmpty()) {
                    p.setFont(df);
                    p.setPen(sub);
                    p.drawText(QRect(x + titleW + 14, rect().top(), dw, height()),
                               Qt::AlignVCenter | Qt::AlignLeft, shown);
                }
            }
        }
    }

    // 可点提示：设了 openPath 就给个"能点"的信号（右侧一个尖角 + 稍亮的字），
    // 否则学生看见胶囊写着一串路径，根本不知道点它等于打开文件夹。
    if (canOpen) {
        // 「▸ 打开」原本用 Unicode 尖角符号，现改为 lucide chevron-right SVG + 「打开」文字，
        // 与灵动岛主图标统一走 SVG 资源，避免依赖符号字体、跨主题稳定。
        QFont hf = font();
        hf.setPixelSize(12);
        p.setFont(hf);
        p.setPen(m_interactive ? fg : sub);
        const QString label = QStringLiteral("打开");
        const int tw = p.fontMetrics().horizontalAdvance(label);
        const int chevW = 12, gap = 3;
        const int totalW = chevW + gap + tw;
        const int regionRight = body.right() + hintW;        // == rect().right()-16
        const int blockX = regionRight - totalW;              // 整块右对齐
        const int cy = rect().top() + (height() - chevW) / 2;
        const QPixmap cp = renderSvgIcon(QStringLiteral("chevron-right"), chevW,
                                         m_interactive ? fg : sub);
        if (!cp.isNull())
            p.drawPixmap(QRect(blockX, cy, chevW, chevW), cp);
        p.drawText(QRect(blockX + chevW + gap, rect().top(), tw, height()),
                   Qt::AlignVCenter | Qt::AlignLeft, label);
    }
}

// 「▸ 打开」按钮的命中区：贴着右边缘那一小块，右边界 = rect().right()-16，
// 与 paintEvent 里的绘制位置严格对齐（body.right()+hintW == rect().right()-16）。
static const int kOpenBtnW = 64;
bool IslandOverlay::hitOpenButton(const QPoint &pt) const
{
    if (m_openPath.isEmpty()) return false;
    // ⚠️ 命中区就是**窗口内**贴右边缘那一条，不要想着"上下各放宽 8px"：
    //    鼠标事件的局部坐标必然落在 rect() 之内，写成 rect().top()-8 / height()+16
    //    上下那 8px 永远收不到事件（那是窗口外的坐标），所谓"放宽"是假的。
    //    真要更大的触控目标，只能把宽度本身放大（kOpenBtnW 44→64）。
    const QRect btn(rect().right() - 16 - kOpenBtnW, rect().top(), kOpenBtnW, height());
    return btn.contains(pt);
}

// 交互按钮行的几何：底部居中一排，宽度按文案自适应。绘制与命中共用这一份，绝不各算各的。
QVector<QRect> IslandOverlay::actionButtonRects() const
{
    QVector<QRect> out;
    if (m_actions.isEmpty()) return out;
    const bool full = (m_form == Form::Fullscreen);
    QFont bf = font();
    bf.setPixelSize(full ? 22 : 14);
    bf.setBold(true);
    const QFontMetrics fm(bf);
    const int gap = 12;
    const int h   = full ? 52 : 36;
    QVector<int> ws;
    int total = 0;
    for (const QString &a : m_actions) {
        const int w = qMax(full ? 160 : 104, fm.horizontalAdvance(a) + 40);
        ws.append(w);
        total += w;
    }
    total += gap * (m_actions.size() - 1);
    int x = rect().center().x() - total / 2;
    const int y = rect().bottom() - (full ? 40 : 24) - h;
    for (int i = 0; i < ws.size(); ++i) {
        out.append(QRect(x, y, ws[i], h));
        x += ws[i] + gap;
    }
    return out;
}

bool IslandOverlay::hitActionButton(const QPoint &pt, int *index) const
{
    const QVector<QRect> rs = actionButtonRects();
    for (int i = 0; i < rs.size(); ++i) {
        if (rs[i].contains(pt)) { if (index) *index = i; return true; }
    }
    return false;
}

void IslandOverlay::mousePressEvent(QMouseEvent *ev)
{
    if (!m_interactive) { ev->ignore(); return; }   // 穿透态不消费
    if (ev->button() != Qt::LeftButton) { ev->ignore(); return; }
    // ⚠️ 触控（2026-10-07）：按下**不做任何动作**，只记起点。
    //    原先把"展开/打开"写在 press 里，手指在触屏上按下就有肉眼不可见的抖动，
    //    轻轻一滑就被判定成点（表现为拉了一下就弹出资源管理器）。
    //    现在动作一律挪到 release，且中间滑过 kDragSlop 就只当拖拽、不当点击。
    // ⚠️ 起点用**全局坐标**：手势期间窗口自己会位移（空闲 y=10 → 展开 y=24），
    //    局部坐标会被窗口位移污染，同一个屏幕位置算出来的 dy 会漂。
    m_pressGlobal = ev->globalPosition();
    m_pressed = true;
    m_dragged = false;
    ev->accept();
}

void IslandOverlay::mouseMoveEvent(QMouseEvent *ev)
{
    if (!m_interactive || !m_pressed) { ev->ignore(); return; }
    // ⚠️ 方向在跨过阈值的那一刻就**锁死**，之后不再跟手重判：
    //    拖拽会触发 geometry 动画，窗口自己在变大/变宽（空闲 y=10 → 展开 y=24 还会下移），
    //    于是"下拉展开"的下一次 move 可能算出反向的 dy 又调 setExpanded(false)，
    //    表现为一拉就展开、抖一下又收回去。锁定之后下拉就是下拉。
    if (m_dragged) { ev->accept(); return; }
    const qreal dy = ev->globalPosition().y() - m_pressGlobal.y();
    if (qAbs(dy) < kDragSlop) { ev->accept(); return; }   // 还在抖动容差里，不算拖
    m_dragged = true;
    // 下拉展开、上推收起 —— 和 iPhone 灵动岛同一个方向直觉
    setExpanded(dy > 0);
    ev->accept();
}

void IslandOverlay::mouseReleaseEvent(QMouseEvent *ev)
{
    if (!m_interactive) { ev->ignore(); return; }
    if (ev->button() != Qt::LeftButton) { ev->ignore(); return; }  // 右键/中键抬起不算点击
    if (!m_pressed) { ev->ignore(); return; }                      // 没有对应的按下，不当点击
    m_pressed = false;
    if (m_dragged) {                  // 这是一次拖拽手势，收尾就完，别再当点击
        m_dragged = false;
        ev->accept();
        return;
    }

    // 交互按钮优先：「确认 / 快捷回复」点哪枚回哪句。点完即清按钮并收回（不再展开/收起）。
    int ai = -1;
    if (hitActionButton(ev->pos(), &ai)) {
        const QString reply = m_actions.value(ai);
        auto cb = m_onAction;
        m_actions.clear();
        m_onAction = nullptr;
        setInteractive(false);          // 内部会 sync
        dismiss();                      // 内部会 sync
        if (cb) cb(reply);
        ev->accept();
        return;
    }

    if (!m_openPath.isEmpty() && hitOpenButton(ev->pos())) {   // pos()=QPoint（不是 position()=QPointF）
        // ⚠️ 点击**分区**处理：岛体本身只负责展开/收起，"打开文件夹"只认右侧那个「▸ 打开」。
        //    原来一道按下就 openInShell + dismiss，而这条岛是 StaysOnTop 且开了交互（收走鼠标
        //    穿透），正好压在课件/白板上方 —— 学生本想点下面的课件、结果弹出资源管理器，
        //    教室大屏上属于事故。把破坏性动作关进一个小按钮区，误点最坏只是展开一下。
        openInShell();
        ev->accept();
        clearOpenPath();
        setInteractive(false);
        dismiss();
        return;
    }
    // 岛体：点一下展开看详情，**再点一下收回**（切换语义）。
    // ⚠️ 原来这里恒传 true：展开后再点毫无反应，而注释/归档都写的是"再点收回"，
    //    触屏用户又基本不会想到"上推"这个手势，结果点开就收不回去。
    setExpanded(!m_expanded);
    ev->accept();
}