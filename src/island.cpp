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
#include "island_bridge.h"       // P0-0：historySnapshotJson() 复用桥的 history() 组装口径
#include "svgiconprovider.h"
#include "whale_audio.h"
// 2026-10-22：系统媒体识别实例，注入 QML 给岛面/排期中心读「本机正在放什么」。
#include "smtc_monitor.h"
#include "danmaku.h"             // 2026-10-15 组件5：祝福弹幕全屏浮层

#include <QApplication>
#include <QDebug>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>      // 2026-10-17：历史记录升级用到（原先靠 island.h 间接带进来）
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QQmlContext>
#include <QQmlEngine>
#include <QPointer>        // 2026-10-19：setQmlObjectProperty 销毁时清空 QML 属性（防悬空 var）
#include <QQuickItem>      // setQmlObjectProperty：rootObject() 返回 QQuickItem*，转 QObject* 需要完整定义
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

// ── 2026-10-17 形态多样化（#330 剩余件）：Banner / Mini 两档外框的尺寸真源 ──
// ⚠️ 这组值与 QML（IslandRoot 的 shell 圆角、两个面的版式）**必须同改**——
//    与"island.cpp ↔ QML 双绘常量"同一纪律（门禁 test-island-metrics 盯着岛的几何常量）。
//    C++ 侧只负责算窗口几何（QQuickWidget 用 SizeRootObjectToView，根随视图），
//    QML 侧负责版式；两边值不一致的症状是"留白一圈"或"文字被裁"。
static const int kBannerPadX  = 24;    // 通栏左右各留 24（与 y=24 对称）
static const int kBannerMaxW  = 1280;  // 超宽屏不拉成一根线，封顶 1280
static const int kBannerH     = 72;    // 有描述
static const int kBannerHFlat = 60;    // 只有标题
static const int kMiniBox     = 112;   // 有描述：圆角方牌
static const int kMiniOrb     = 96;    // 无描述：正圆（radius = 半边）
static const int kMiniMargin  = 24;    // 距可用区（含任务栏）右下角的留白

IslandOverlay *IslandOverlay::instance()
{
    if (!g_island)
        g_island = new IslandOverlay();
    return g_island;
}

// ── 2026-10-19 远控期冻结（P0 崩溃规避，说明见 island.h）─────────────────
// 冻结时会立刻收起岛并停掉自动收起定时器：冻结期间不允许任何 QML 数据写入，
// 否则 Repeater 的整表替换照样会踩已销毁 delegate。
static bool s_islandSuppressed = false;

void IslandOverlay::setSuppressed(bool on)
{
    if (s_islandSuppressed == on) return;
    s_islandSuppressed = on;
    qInfo("[island] 抑制模式 %s（远控期防 QML Repeater 崩溃）",
          on ? "开启：岛停写，通知走托盘" : "关闭：岛恢复");
    if (on && g_island) {
        g_island->m_autoClose->stop();
        g_island->m_dismissGuard->stop();
        g_island->hide();
    }
}

bool IslandOverlay::suppressed() { return s_islandSuppressed; }

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
    // ⚠️ 2026-10-21（审查 #12）：此淡入淡出依赖 **QQuickWidget 的离屏 FBO blit 渲染
    //    路径** —— m_quick（QML 引擎）是子 QWidget，理论上子部件不继承父的
    //    QGraphicsOpacityEffect；实测生效是因为 QQuickWidget 把整帧 blit 进父窗口
    //    的合成路径上。哪天换成 QQuickView / 改渲染后端，这条会悄悄失效 ——
    //    改渲染相关代码时先回来看这里。
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

    // ── T 档：任务过期扫描（2026-10-18）───────────────────────────────────
    // 1Hz 扫全表，把到期且非 sticky 的任务标 done。
    // ⚠️ 单定时器扫表，而不是每个任务一个 QTimer —— 任务一多，定时器本身就是负担
    //    （方案 §4.2 的教训：定时器数量比任务数量更容易失控）。
    // ⚠️ 只扫表 + 同步桥，**不主动改显示**：显示切换由各自的 clear*/enterIdle 触发，
    //    避免扫描器与用户操作抢窗口（"正在传的文件被扫描器收掉"是灾难）。
    m_taskSweep = new QTimer(this);
    m_taskSweep->setInterval(1000);
    connect(m_taskSweep, &QTimer::timeout, this, [this]() { sweepExpiredTasks(); });
    m_taskSweep->start();

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
    // 2026-10-22：系统正在播放的媒体（SMTC）。
    // 与 bridge 的 media* 不是一路：bridge.media* = **我们的排期播放器**播到哪；
    // smtc.* = **这台机器上真在响什么**（学生自己开的网易云/网页视频都算）。
    // QML 用 smtc.available 决定要不要显示，别在没拿到会话管理器时显示空歌名。
    m_quick->rootContext()->setContextProperty(QStringLiteral("smtc"), SmtcMonitor::instance());
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

    // ── 2026-10-18 · 拆解顺序（这是崩溃点，别把它改回去）──────────────────────
    // 现象：进程退出时偶发 0xC0000005，故障模块 Qt6Core.dll 偏移 0x38c0 ——
    //       该地址正好是导出符号 QBindingStorage::status()（属性绑定的读取路径）。
    // 机制：m_bridge 与 m_quick 都是本窗口的子对象，QObject 析构**按添加顺序**删子对象：
    //       m_bridge 先建（先删）→ m_quick（QML 引擎）后删。引擎收尾时 QML 里还留着
    //       setContextProperty("bridge", ...) 这个**裸指针**，只要还有绑定被读一次，
    //       就落到已释放的 IslandBridge 上 → 读它的 bindingStorage → 访问冲突。
    // 修法：显式先拆引擎、再拆桥（两者都已从父对象的子对象表里摘除，base 析构不会二次删）。
    delete m_quick;
    m_quick = nullptr;
    delete m_bridge;
    m_bridge = nullptr;
}

// 空闲半胶囊的位置：比活动态更贴顶（y=10）、更小（132×44），真正"收回顶部"。
// 2026-10-21（审查 #9）：无屏回退的宽度原来是 120，与 kIdleW(132) 不一致 ——
// 离屏探针/Session0 拿到的几何和实际路径对不上号会白浪费时间，统一用 kIdleW。
QRect IslandOverlay::geometryForIdle() const
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) return QRect(0, 0, kIdleW, int(Idle));
    const QRect sg = screen->geometry();
    return QRect(sg.x() + (sg.width() - kIdleW) / 2, sg.y() + 10, kIdleW, int(Idle));
}

// 活动态的位置：宽度由内容决定（上限 60% 屏宽），顶部居中 y=24。
QRect IslandOverlay::geometryForContent(int shapeHeight) const
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) return QRect(0, 0, 200, shapeHeight);
    const QRect sg = screen->geometry();

    // ── 面板（2026-10-27 · G4/G5/G6 组件）：定宽 680 ──────────────────────
    // 与媒体同一口径：面板内容是**多行表格/大数字**，宽度由内容定会算成"标题那行"
    // 的宽度（200 出头），课表一行都排不下。
    // ⚠️ 放在媒体分支**之前**：面板与媒体互斥，面板优先（showPanel 会顶掉媒体内容）。
    if (!m_panelFace.isEmpty()) {
        const int pw = qMin(680, qMax(kMinContentW, sg.width() - 48));
        return QRect(sg.x() + (sg.width() - pw) / 2, sg.y() + 24, pw, shapeHeight);
    }

    // 2026-10-15 组件1（点歌互动）：媒体呈现时按 QML 预算宽度走 ——
    // 收起 320 / 展开 680（与 IslandRoot.implicitWidth 逐字一致）。
    // 内容定宽对短曲名只给 ~200：互动区 + 弹幕输入行在 200 宽下放不下
    // （探针实测 danmakuRow 被推到 320 高度线外，根本看不见），
    // 且 fitTitlePx 的宽度预算也按 640/320 算，窄岛下大标题会被挤成 3 行 192px。
    // ⚠️ 只对媒体呈现生效：通知类仍走内容定宽，不动既有 100+ 调用方的观感。
    if (m_mediaPresenting) {
        const int mw = m_expanded ? 680 : 320;
        return QRect(sg.x() + (sg.width() - mw) / 2, sg.y() + 24, mw, shapeHeight);
    }

    // 内容决定宽度（标题/描述最长行）
    const int maxW = qMin(sg.width() * 6 / 10, 680);
    QFontMetrics fm(font());
    const int iconW = m_icon.isEmpty() ? 0 : 28;
    const int textW0 = qMax(fm.horizontalAdvance((m_title.isEmpty() ? "" : m_title) + "  "),
                            fm.horizontalAdvance(m_desc));
    // 2026-10-27（F 收尾第 2 项）：有渠道标签时，收起态是「标签 + 6px + 描述」同排
    // （见 IslandRoot 的 csChannel）—— 宽度必须一起算进窗口，否则描述会被标签挤成
    // "数学 · 08…"，标签在而内容没了。
    // ⚠️ 这里**不能沿用上面那套 iconW+textW+48**：那是"单行标题"的估算式，右侧只算了
    //    48px；而 QML 的收起态是左内缩 48（图标 14+22+间距 12）+ 右内缩 42（鲸鱼领地），
    //    标签行要的是 48+标签+6+描述+42。2026-10-27 生产截图实测：按旧式算 209px 时
    //    描述被截成 "数学 · 0…"（差 ~24px），就是这么来的。
    // ⚠️ 度量字体按 QML 实际字号取 12px（fsNote）——QWidget 的 font() 与 QML Text 的
    //    默认字体族不同，混用会在中文上差 10%+。
    int textW = textW0;
    int needW = 0;    // 标签行的真实需求宽（0 = 无渠道，不参与）
    if (!m_channel.isEmpty()) {
        QFont cf = font();
        cf.setPixelSize(12);
        cf.setWeight(QFont::DemiBold);
        const QFontMetrics cfm(cf);
        const int chipW = cfm.horizontalAdvance(m_channel) + 14;   // 药丸左右各 7
        const int descW = cfm.horizontalAdvance(m_desc);
        needW = 48 + chipW + 6 + descW + 42;
    }
    const int w = qBound(kMinContentW, qMax(iconW + textW + 48, needW), maxW);
    return QRect(sg.x() + (sg.width() - w) / 2, sg.y() + 24, w, shapeHeight);
}

// 按**形态**算目标几何（#98）。过渡不用另外写：animateGeometry() 吃任意 QRect，
// 从当前几何补间到目标几何，所以"胶囊 → 居中卡片 → 全屏"之间的放大/收缩天然带动画。
QRect IslandOverlay::geometryForForm(Form f) const
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) return QRect(0, 0, 200, int(Collapsed));
    // 2026-10-17：本函数**不再用整屏 geometry** —— 五档全部按 availableGeometry 算。
    //   理由：任务栏在某一侧时，"按整屏居中"会偏心、"按整屏铺满"会压住任务栏。
    //   （Capsule 走 geometryForContent，它自己处理贴顶 24，与本函数无关。）

    if (f == Form::Capsule)
        return geometryForContent(m_expanded ? int(Expanded) : int(Collapsed));

    // ── Banner：顶部通栏（2026-10-17 · #330 形态多样化）──────────────
    // 用 **availableGeometry** 而不是 geometry：通栏贴顶，任务栏在顶部时会压住它。
    // 宽度 = 可用宽 − 2*24（上限 1280），高度按有没有描述分两档 —— 单行布局，
    // 长文案 elide 而不是折行（折行就退化成"更高的胶囊"，失去横幅的意义）。
    if (f == Form::Banner) {
        const QRect ag = screen->availableGeometry();
        const int w = qMin(ag.width() - 2 * kBannerPadX, kBannerMaxW);
        const int h = m_desc.isEmpty() ? kBannerHFlat : kBannerH;
        return QRect(ag.x() + (ag.width() - w) / 2, ag.y() + 24, w, h);
    }

    // ── Mini：右下角角标（2026-10-17 · #330 形态多样化）──────────────
    // 同样吃 availableGeometry（任务栏在底部时必须浮在它上面，不能压进任务栏里）。
    // 有描述 → 112 方牌；无描述 → 96 正圆（纯图标状态收成正圆更像"角标"）。
    if (f == Form::Mini) {
        const QRect ag = screen->availableGeometry();
        const int s = m_desc.isEmpty() ? kMiniOrb : kMiniBox;
        return QRect(ag.right() - kMiniMargin - s + 1,
                     ag.bottom() - kMiniMargin - s + 1, s, s);
    }

    if (f == Form::Centered) {
        // 屏幕正中的卡片：宽不超过屏宽 40%，高度 200（够放图标+标题+两行描述）
        // 2026-10-17：改用 **availableGeometry** 居中 —— 任务栏在左/上时，
        // 按 geometry 算出来的"正中"会偏向任务栏那一侧（本机任务栏在左 48px，
        // 实测卡片中心比视觉中心偏左 24px，肉眼能看出"没摆正"）。
        const QRect ag = screen->availableGeometry();
        const int w = qMin(ag.width() * 2 / 5, 520);
        // 有按钮行时加高，给底部按钮留位（否则描述会和按钮叠在一起）
        const int h = m_actions.isEmpty() ? 200 : 250;
        return QRect(ag.x() + (ag.width() - w) / 2,
                     ag.y() + (ag.height() - h) / 2, w, h);
    }

    // Fullscreen：铺满主屏，四周留 24px —— ClassIsland 那种"贴边但留一口气"的观感。
    // 留边而不是真全屏，是为了让它明显是"浮层"而不是"桌面换了"，学生不会误以为死机。
    // 2026-10-17：同样改用 availableGeometry。原来按 geometry 算 ⇒ 在任务栏那侧
    // 会**压住任务栏**（本机实测 1659x912@(24,24)，左侧 24px 盖在任务栏上），
    // 老师想点任务栏得先关掉通知 —— "留一口气"就白留了。
    const QRect agf = screen->availableGeometry();
    const int m = 24;
    return QRect(agf.x() + m, agf.y() + m, agf.width() - 2 * m, agf.height() - 2 * m);
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

// ── 沙箱验证设施（2026-10-17 P0-0）─────────────────────────────────────────
// 见 island.h 同名声明处的说明。要点：
//   · 走桥的 history()（QML 用的同一套组装：现算 timeStr/timeFull/dateGroup）——
//     自己再拼一份 JSON 必然与界面口径漂移；
//   · 只读，不触发 sync / 不写盘；
//   · 附带「岛当前这条」的 title/desc/noticeTs，便于断言"最近一条内容确实换成了新的"。
QString IslandOverlay::historySnapshotJson() const
{
    QJsonObject o;
    QJsonArray arr;
    if (m_bridge) {
        const QVariantList list = m_bridge->history();
        for (const QVariant &v : list) arr.append(QJsonObject::fromVariantMap(v.toMap()));
    }
    o.insert(QStringLiteral("history"), arr);
    o.insert(QStringLiteral("title"),    m_title);
    o.insert(QStringLiteral("desc"),     m_desc);
    o.insert(QStringLiteral("icon"),     m_icon);
    o.insert(QStringLiteral("fromAdmin"), m_fromAdmin);
    o.insert(QStringLiteral("noticeTs"), QJsonValue(double(m_shownTs)));
    o.insert(QStringLiteral("titleLen"), int(m_title.size()));
    o.insert(QStringLiteral("descLen"),  int(m_desc.size()));
    // ── T 档基线（2026-10-17）：状态串。**只读、不渲染** ——
    //    探针据此断言"旧调用行为零变化"，而不必截屏（岛窗与生产实例重叠，截图无法归因）。
    o.insert(QStringLiteral("idle"),     m_idle);
    o.insert(QStringLiteral("expanded"), m_expanded);
    o.insert(QStringLiteral("interactive"), m_interactive);
    o.insert(QStringLiteral("form"),     int(m_form));
    o.insert(QStringLiteral("contentActive"), m_contentActive);
    o.insert(QStringLiteral("ftActive"), m_ftActive);
    o.insert(QStringLiteral("mediaPresenting"), m_mediaPresenting);
    // T 档：任务表（验证"每条状态源都确实进表"的核心证据）
    QJsonArray tarr;
    for (const auto &t : m_tasks) {
        QJsonObject to;
        to.insert(QStringLiteral("id"), t.id);
        to.insert(QStringLiteral("tag"), t.tag);
        to.insert(QStringLiteral("kind"), t.kind);
        to.insert(QStringLiteral("source"), t.source);
        to.insert(QStringLiteral("title"), t.title);
        to.insert(QStringLiteral("priority"), t.priority);
        to.insert(QStringLiteral("sticky"), t.sticky);
        to.insert(QStringLiteral("progress"), t.progress);
        to.insert(QStringLiteral("state"), t.state);
        to.insert(QStringLiteral("form"), int(t.form));
        tarr.append(to);
    }
    o.insert(QStringLiteral("tasks"), tarr);
    o.insert(QStringLiteral("actions"),  QJsonArray::fromStringList(m_actions));
    if (m_bridge) o.insert(QStringLiteral("states"), QJsonArray::fromVariantList(m_bridge->states()));
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void IslandOverlay::archiveNow()
{
    archiveToHistory();
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
        h.fromAdmin = o.value(QStringLiteral("fromAdmin")).toBool(false);
        // ── 2026-10-17 新字段：老 JSON 没有这些键 → 就地推导，**不丢条** ──
        h.id = o.value(QStringLiteral("id")).toString();
        h.category = o.value(QStringLiteral("category")).toString();
        h.level = o.value(QStringLiteral("level")).toString();
        h.dateGroup = o.value(QStringLiteral("dateGroup")).toString();
        // 老数据的 read 键不存在 ⇒ 视为**已读**（历史遗留不该一升级就冒出 50 个红点，
        // 那会让用户以为刚收到一堆通知）。只有明确 false 的才是未读。
        h.read = o.contains(QStringLiteral("read"))
                     ? o.value(QStringLiteral("read")).toBool(true)
                     : true;
        // 缺字段就地补（顺序重要：category 依赖 title/desc）
        if (h.category.isEmpty()) {
            const QString c = historyCategoryOf(h.title, h.desc, h.icon, h.fromAdmin);
            h.category = c.isEmpty() ? QStringLiteral("消息") : c;   // 老条目已落盘，不强删
        }
        if (h.level.isEmpty()) h.level = historyLevelOf(h.title, h.desc);
        if (h.dateGroup.isEmpty()) h.dateGroup = historyDateGroupOf(h.ts);
        m_history.append(h);
    }
    // 老 JSON 完全没有 id ⇒ 补一遍（否则 markHistoryRead 无法定位）
    for (auto &h : m_history) {
        if (h.id.isEmpty()) h.id = nextHistoryId(h.ts);
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
        o.insert(QStringLiteral("id"), h.id);
        o.insert(QStringLiteral("title"), h.title);
        o.insert(QStringLiteral("desc"), h.desc);
        o.insert(QStringLiteral("icon"), h.icon);
        o.insert(QStringLiteral("ts"), qint64(h.ts));
        o.insert(QStringLiteral("timeStr"), h.timeStr);
        // 2026-10-17 新字段（缺任何一个，重启后分类/未读/分组就退回默认值）
        o.insert(QStringLiteral("dateGroup"), h.dateGroup);
        o.insert(QStringLiteral("category"), h.category);
        o.insert(QStringLiteral("level"), h.level);
        o.insert(QStringLiteral("read"), h.read);
        o.insert(QStringLiteral("fromAdmin"), h.fromAdmin);
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

// ── 历史记录逻辑（2026-10-17 · 用户点单「通知记录与逻辑」）──────────────
// 设计依据：docs/目标/灵动岛通用简单历史通知 §二/§十 ——
//   「历史只记有意义的事件，不是所有事件」。所以这里的第一职责是**决定要不要记**，
//   返回空串即拒绝归档（调用方 archiveToHistory 直接 return，连落盘都不做）。

QString IslandOverlay::historyCategoryOf(const QString &title, const QString &desc,
                                        const QString &icon, bool fromAdmin)
{
    // ── 1. 纯技术状态：不进历史（§十 表）──
    // 判据用「关键词 + 图标」双条件收紧，避免误杀正常文案：
    //   只有文案里出现"重连/心跳/下载/更新"并且**确实是技术动作**的才丢。
    // 说明：这些提示本来就走 showIsland 短暂展示，用户看得见，只是不该占历史。
    static const QStringList kTechWords = {
        QStringLiteral("重连"), QStringLiteral("心跳"), QStringLiteral("连接中"),
        QStringLiteral("重新连接"), QStringLiteral("下载中"), QStringLiteral("更新包"),
        QStringLiteral("正在下载"), QStringLiteral("校验中")
    };
    const QString probe = title + QLatin1Char(' ') + desc;
    for (const QString &w : kTechWords) {
        if (probe.contains(w)) return QString();   // 空 = 不进历史
    }

    // ── 2. 分类（§二 category：指令/告警/课表/消息/系统）──
    // 被控端场景没有"课表"独立源（课表走 ClassIsland 对接），故只用四类。
    const QString t = title;
    // 告警：失败/错误/异常/超时/不足 —— 这类必须留痕给老师回看
    static const QStringList kAlert = {
        QStringLiteral("失败"), QStringLiteral("错误"), QStringLiteral("异常"),
        QStringLiteral("超时"), QStringLiteral("不足"), QStringLiteral("断开"),
        QStringLiteral("离线"), QStringLiteral("警告")
    };
    for (const QString &w : kAlert)
        if (t.contains(w)) return QStringLiteral("告警");

    // 指令：管理端下发的动作回执（锁屏/关机/广播/分发/音量/进程…）
    // fromAdmin 是 2026-10-13 就有的标记（管理端 notify 指令），优先认它。
    static const QStringList kCmd = {
        QStringLiteral("锁屏"), QStringLiteral("关机"), QStringLiteral("重启"),
        QStringLiteral("广播"), QStringLiteral("分发"), QStringLiteral("推送"),
        QStringLiteral("音量"), QStringLiteral("进程"), QStringLiteral("截图"),
        QStringLiteral("文件"), QStringLiteral("接管"), QStringLiteral("考试"),
        QStringLiteral("静音"), QStringLiteral("解锁")
    };
    for (const QString &w : kCmd)
        if (t.contains(w)) return QStringLiteral("指令");
    if (fromAdmin) return QStringLiteral("指令");

    // 系统：本机自身状态（更新完成/已连接/设备接入）
    static const QStringList kSys = {
        QStringLiteral("更新"), QStringLiteral("已连接"), QStringLiteral("设备"),
        QStringLiteral("U盘"), QStringLiteral("启动"), QStringLiteral("网络")
    };
    for (const QString &w : kSys)
        if (t.contains(w)) return QStringLiteral("系统");

    // icon 兜底：bell 类（管理端通知）算消息，其余按消息处理
    Q_UNUSED(icon);
    return QStringLiteral("消息");
}

QString IslandOverlay::historyLevelOf(const QString &title, const QString &desc)
{
    const QString probe = title + QLatin1Char(' ') + desc;
    static const QStringList kErr = {
        QStringLiteral("失败"), QStringLiteral("错误"), QStringLiteral("异常"),
        QStringLiteral("超时"), QStringLiteral("拒绝"), QStringLiteral("断开")
    };
    for (const QString &w : kErr)
        if (probe.contains(w)) return QStringLiteral("error");

    static const QStringList kWarn = {
        QStringLiteral("警告"), QStringLiteral("注意"), QStringLiteral("不足"),
        QStringLiteral("提醒"), QStringLiteral("即将")
    };
    for (const QString &w : kWarn)
        if (probe.contains(w)) return QStringLiteral("warning");

    static const QStringList kOk = {
        QStringLiteral("完成"), QStringLiteral("成功"), QStringLiteral("已发送"),
        QStringLiteral("已连接"), QStringLiteral("已接收")
    };
    for (const QString &w : kOk)
        if (probe.contains(w)) return QStringLiteral("success");

    return QStringLiteral("info");
}

QString IslandOverlay::historyDateGroupOf(qint64 ts)
{
    if (ts <= 0) return QStringLiteral("更早");
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(ts);
    const QDate today = QDate::currentDate();
    if (dt.date() == today) return QStringLiteral("今天");
    if (dt.date() == today.addDays(-1)) return QStringLiteral("昨天");
    // 同年只显示月日，跨年带年份 —— 7 天保留期下基本只有同月，但跨年那两天会用到
    return dt.date().year() == today.year()
               ? dt.toString(QStringLiteral("M月d日"))
               : dt.toString(QStringLiteral("yyyy年M月d日"));
}

// ── 时间文本（2026-10-17 「通知写上时间」）──────────────────────────────
// 三个函数都**按 ts 现算**，绝不把结果持久化当显示源：
// 上一轮把 timeStr/dateGroup 固化进 JSON，隔天打开就全是错的
// （昨天存的「今天」今天还写「今天」，昨天存的无日期 "23:50" 也永远补不上日期）。
QString IslandOverlay::historyTimeOf(qint64 ts)
{
    if (ts <= 0) return QString();
    return QDateTime::fromSecsSinceEpoch(ts).toString(QStringLiteral("HH:mm"));
}

QString IslandOverlay::historyTimeFullOf(qint64 ts)
{
    if (ts <= 0) return QString();
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(ts);
    const QDate today = QDate::currentDate();
    const QString hm = dt.toString(QStringLiteral("HH:mm"));
    if (dt.date() == today)               return hm;   // 今天只说几点几分
    if (dt.date() == today.addDays(-1))   return QStringLiteral("昨天 ") + hm;
    return dt.date().year() == today.year()
               ? dt.toString(QStringLiteral("M/d ")) + hm
               : dt.toString(QStringLiteral("yyyy/M/d ")) + hm;
}

QString IslandOverlay::noticeTimeOf(qint64 ts)
{
    // 岛上那条：平时只给 HH:mm（形态最多 320px 宽，塞日期会更挤）；
    // 跨天基本不会发生（通知生命周期是秒级），但真跨了就补上日期，别显示错时间。
    if (ts <= 0) return QString();
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(ts);
    const QString hm = dt.toString(QStringLiteral("HH:mm"));
    if (dt.date() == QDate::currentDate()) return hm;
    return dt.toString(QStringLiteral("M/d ")) + hm;
}

int IslandOverlay::historyUnreadCount() const
{
    int n = 0;
    for (const auto &h : m_history)
        if (!h.read) ++n;
    return n;
}

bool IslandOverlay::markHistoryRead(const QString &id)
{
    if (id.isEmpty()) return false;
    for (auto &h : m_history) {
        if (h.id == id) {
            if (h.read) return false;
            h.read = true;
            saveHistory();
            return true;
        }
    }
    return false;
}

int IslandOverlay::markAllHistoryRead()
{
    int changed = 0;
    for (auto &h : m_history) {
        if (!h.read) { h.read = true; ++changed; }
    }
    if (changed > 0) saveHistory();
    return changed;
}

bool IslandOverlay::removeHistory(const QString &id)
{
    if (id.isEmpty()) return false;
    for (int i = 0; i < m_history.size(); ++i) {
        if (m_history.at(i).id == id) {
            m_history.removeAt(i);
            saveHistory();
            return true;
        }
    }
    return false;
}

QString IslandOverlay::nextHistoryId(qint64 ts) const
{
    // ts 打头便于人工排查；同秒撞车时追加序号（-2/-3…）
    const QString base = QString::number(ts);
    QString id = base;
    int seq = 2;
    bool clash = true;
    while (clash) {
        clash = false;
        for (const auto &h : m_history) {
            if (h.id == id) { clash = true; break; }
        }
        if (clash) id = base + QLatin1Char('-') + QString::number(seq++);
    }
    return id;
}

// ── T 档：任务登记（2026-10-17）────────────────────────────────────────────
// 本步只做两件事：同 tag 合并、上限保护。**不参与显示仲裁**（那是下一步）。
// 为什么先只登记：岛的显示路径（几何/动画/历史归档）已经很复杂，一次性换模型风险高；
// 先把数据接进来，用快照验证"每条状态源都确实进表了"，再逐步接仲裁。
void IslandOverlay::upsertTask(const Task &t)
{
    if (t.tag.isEmpty()) return;   // 无 tag 的任务无法合并，拒绝登记（防脏数据）
    // 同 tag 合并：只更新内容，**保留原 id 与 shownTs**（"同一件事"的延续）
    for (auto &e : m_tasks) {
        if (e.tag == t.tag) {
            e.title = t.title;
            e.desc = t.desc;
            e.icon = t.icon;
            e.priority = t.priority;
            e.form = t.form;
            e.severity = t.severity;
            e.sticky = t.sticky;
            e.expireAtMs = t.expireAtMs;
            e.progress = t.progress;
            e.actions = t.actions;
            e.state = t.state;
            e.fromAdmin = t.fromAdmin;
            return;
        }
    }
    // 新任务：补 id 与发生时刻
    Task n = t;
    if (n.id.isEmpty()) n.id = nextHistoryId(n.shownTs > 0 ? n.shownTs : QDateTime::currentSecsSinceEpoch());
    if (n.shownTs <= 0) n.shownTs = QDateTime::currentSecsSinceEpoch();
    if (n.kind.isEmpty()) n.kind = n.tag;
    if (n.source.isEmpty()) n.source = QStringLiteral("self");
    m_tasks.append(n);
    // 上限保护：任务表不是无限账本。溢出时丢"优先级最低且最旧"的；sticky 与高优先不丢。
    // ⚠️ 上限 8 与方案 §4.2 同值；当前每条 tag 唯一 ⇒ 实际不会触发，留作后续多状态接入的护栏。
    while (m_tasks.size() > 8) {
        int victim = -1;
        for (int i = 0; i < m_tasks.size(); ++i) {
            const auto &c = m_tasks.at(i);
            if (c.sticky || c.priority >= 9) continue;
            if (victim < 0 || c.priority < m_tasks.at(victim).priority
                || (c.priority == m_tasks.at(victim).priority && c.shownTs < m_tasks.at(victim).shownTs))
                victim = i;
        }
        if (victim < 0) break;   // 全是 sticky/高优先 ⇒ 不再强删
        m_tasks.removeAt(victim);
    }
}

// ── T 档：前台仲裁（2026-10-18）────────────────────────────────────────────
// 排序口径（与方案 §4.2 同）：priority 降序；同 priority 取最新（shownTs 大者）。
// ⚠️ 本步**只用于回落**，不作抢占 ——
//   抢占（高优先挤掉当前显示）会改变现状行为（通知顶掉对讲），回归风险大，留下一步。
//   回落只在"反正要回空闲"时发生：不回空闲，而是接手下一个未完成任务 ⇒ 纯增值、无回归面。
//   真实痛点：收文件时来一条通知 → 通知过期 → 现在直接回空闲，传输进度消失；
//            有回落则自动接回"正在接收"。
const IslandOverlay::Task *IslandOverlay::nextDisplayTask() const
{
    const Task *best = nullptr;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const auto &t : m_tasks) {
        if (t.state == 2) continue;                       // 已完成
        if (!t.sticky && t.expireAtMs > 0 && now > t.expireAtMs) continue;   // 已过期
        if (!best) { best = &t; continue; }
        if (t.priority > best->priority) { best = &t; continue; }
        if (t.priority == best->priority && t.shownTs > best->shownTs) best = &t;
    }
    return best;
}

// 过期扫描：到期且非 sticky ⇒ 标 done。由 1Hz 定时器统一扫表，
// 不为每个任务挂定时器（60 台设备 × N 任务，定时器数量比任务数更容易失控）。
void IslandOverlay::sweepExpiredTasks()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool changed = false;
    for (auto &t : m_tasks) {
        if (t.sticky) continue;
        if (t.expireAtMs <= 0) continue;
        if (now > t.expireAtMs && t.state != 2) { t.state = 2; changed = true; }
    }
    if (changed && m_bridge) m_bridge->sync();
}

// ── VoiceHub 广播状态（2026-10-18）──────────────────────────────────────────
// 为什么在岛上用本地时钟外推：云端 30s 才给一次 position，若照搬，进度条会在
// 两次快照之间静止（看着像卡住）。记锚点 + 本地累加才走得动。
void IslandOverlay::setBroadcastTask(const BroadcastInfo &info)
{
    m_bcInfo = info;
    m_bcAnchorPos = qMax(0.0, info.position);
    m_bcAnchorAtMs = QDateTime::currentMSecsSinceEpoch();

    // 组装任务文案（岛的一行只有两栏：标题 / 描述，所以信息要压得住）
    QString title = info.artist.isEmpty()
        ? info.title
        : QStringLiteral("%1 - %2").arg(info.title, info.artist);
    if (title.isEmpty()) title = QStringLiteral("校园广播");

    QStringList bits;
    if (!info.isPlaying) bits << QStringLiteral("已暂停");
    if (info.listenerCount > 0) bits << QStringLiteral("%1 人在听").arg(info.listenerCount);
    if (info.hasNextUp && !info.nextUpTitle.isEmpty())
        bits << QStringLiteral("接下来：%1").arg(info.nextUpTitle);
    const QString desc = bits.join(QStringLiteral(" · "));

    Task t(QStringLiteral("broadcast"), QStringLiteral("broadcast"),
           QStringLiteral("admin"), title, QStringLiteral("radio"), 6, true, -1.0);
    t.desc = desc;
    t.state = info.isPlaying ? 0 : 1;      // 1 = paused（岛上有"已暂停"标）
    upsertTask(t);
    if (m_bridge) m_bridge->sync();
    update();
}

void IslandOverlay::clearBroadcastTask()
{
    bool removed = false;
    for (int i = m_tasks.size() - 1; i >= 0; --i) {
        if (m_tasks.at(i).tag == QStringLiteral("broadcast")) { m_tasks.removeAt(i); removed = true; }
    }
    m_bcInfo = BroadcastInfo();
    m_bcAnchorPos = 0;
    m_bcAnchorAtMs = 0;
    if (removed && m_bridge) m_bridge->sync();
    if (removed) update();
}

// 广播任务的实时进度（0..1；duration 未知返回 -1）。
// 桥在组装 states 时调它，避免把"外推逻辑"塞进桥里（桥只做搬运）。
double IslandOverlay::broadcastProgress() const
{
    if (!m_bcInfo.isPlaying || m_bcInfo.duration <= 0) return -1.0;
    const qint64 el = QDateTime::currentMSecsSinceEpoch() - m_bcAnchorAtMs;
    const double pos = m_bcAnchorPos + double(el) / 1000.0;
    return qBound(0.0, pos / m_bcInfo.duration, 1.0);
}

void IslandOverlay::archiveToHistory()
{
    // 有标题才归档（空内容/空闲态不该进历史）
    if (m_title.isEmpty()) return;
    // 2026-10-27（F）：课程提醒这类高频自动提示**不进历史** —— 一天十几次会把
    //   真正的管理端通知淹掉（历史是"回头查老师发过什么"，不是流水账）。
    if (m_skipHistory) return;
    // 同标题不重复归档（5 秒内的重复跳过）
    if (!m_history.isEmpty()) {
        const auto &last = m_history.last();
        if (last.title == m_title && (QDateTime::currentSecsSinceEpoch() - last.ts) < 5)
            return;
    }
    // 2026-10-17：逻辑判据 —— 空分类 = 纯技术状态，不进历史。
    // 放在 5 秒去重之后：技术上重复的提示也不该因为"分类为空"而绕过去重判断顺序。
    const QString category = historyCategoryOf(m_title, m_desc, m_icon, m_fromAdmin);
    if (category.isEmpty()) return;

    HistoryItem h;
    h.title = m_title;
    h.desc = m_desc;
    h.icon = m_icon;
    // ⚠️ 2026-10-17：ts 取**通知出现时刻**（m_shownTs），不是现在（归档时刻）。
    //    归档发生在"被下一条顶掉 / 收回空闲"，两者能差出整条通知的停留时间，
    //    跨零点时还会把通知分到错误的日期组。m_shownTs=0 只会出现在异常路径
    //    （比如未经 showIsland 直接设了 m_title），此时退回当前时刻兜底。
    h.ts = m_shownTs > 0 ? m_shownTs : QDateTime::currentSecsSinceEpoch();
    h.fromAdmin = m_fromAdmin;   // 2026-10-13：管理端通知打标（历史排序置顶用）
    h.id = nextHistoryId(h.ts);
    h.category = category;
    h.level = historyLevelOf(m_title, m_desc);
    // dateGroup / timeStr 仍然写一份进 JSON（便于排查与旧版兼容），但**显示不用它**：
    // bridge 组装时会按 ts 现算，否则跨天后"昨天存的今天"永远改不回来。
    h.dateGroup = historyDateGroupOf(h.ts);
    h.timeStr = historyTimeOf(h.ts);   // 行内只留 HH:mm，日期交给分组标题
    h.read = false;              // 新归档默认未读（未读点 + 未读计数）

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
    // 2026-10-19：远控期冻结（见 setSuppressed）——任何写入都可能触发 Repeater 重算，
    // 而触发的那一刻正是 delegate 处于销毁窗口，必崩。
    if (s_islandSuppressed) return;
    // ── 历史归档：上一条有内容的通知在被顶掉时进历史 ──
    // 判据：m_contentActive && m_title 非空（活动中的真通知被顶掉）
    // 2026-10-15 组件1：再加 !m_mediaPresenting —— 上一条是"正在播放"时不归档
    // （媒体不是通知；且 setMediaSession 里置位在 showIsland **之后**，本行读到的
    //  永远是"上一条内容"的状态：歌2顶歌1→true 不归档；媒体顶通知→false 通知照常归档；
    //  通知顶媒体→true 媒体不归档）。
    if (m_contentActive && !m_title.isEmpty() && !m_ftActive && !m_mediaPresenting)
        archiveToHistory();

    // 2026-10-27（G4/G5/G6）：新内容一律**顶掉面板** —— 面板是"当下要看的一块板"，
    // 来了通知就该让位（否则岛会同时是课表和通知，两套内容抢同一块展开区）。
    m_panelFace.clear();
    m_panelData = QJsonObject();

    m_title = title;
    m_desc = desc;
    m_icon = icon;
    // 2026-10-17：记录**这条内容出现的时刻**。历史归档的 ts 取它，岛本体显示的
    // 时间也取它 —— 两处必须同一个源，否则"岛上写 14:05、历史写 14:31"。
    // P0-0（同日晚）：调用方可经 setNoticeTimeForNext() 注入**发生时刻**（插件推的通知
    // 用"插件调用那一刻"而不是"agent 处理那一刻"）。一次性消费，用完即清 ——
    // 否则下一轮同样的 showIsland 会莫名继承上一次的时间。
    m_shownTs = (m_noticeHintMs > 0) ? (m_noticeHintMs / 1000)
                                     : QDateTime::currentSecsSinceEpoch();
    m_noticeHintMs = 0;
    m_fromAdmin = fromAdmin;   // 2026-10-13：跟着这条内容走，归档时写进 HistoryItem
    // 2026-10-27（F）：渠道 / 是否入历史 —— 同 m_noticeHintMs 的一次性语义，用完即清。
    m_channel = m_channelHint;
    m_channelHint.clear();
    m_skipHistory = m_skipHistoryHint;
    m_skipHistoryHint = false;
    // ── T 档：把这条内容登记成任务（本步**只记录**，不影响上面的任何显示逻辑）──
    //    tag 用 "notice"：兼容层的核心 —— 下一条通知按同 tag 合并（顶掉），
    //    行为与"原来直接顶掉"完全一致，所以 14 处调用点的外部表现不变。
        // ⚠️ C++17 不支持指派初始化器（实测 C7555），这里显式赋值。
        Task nt(QStringLiteral("notice"), QStringLiteral("notice"),
                fromAdmin ? QStringLiteral("admin") : QStringLiteral("self"),
                title, icon, 5, (durationMs <= 0), -1.0);
        nt.desc = desc;
        nt.form = form;
        nt.severity = m_severity;
        nt.expireAtMs = (durationMs > 0)
            ? QDateTime::currentMSecsSinceEpoch() + durationMs : 0;
        nt.fromAdmin = fromAdmin;
        nt.shownTs = m_shownTs;
        upsertTask(nt);
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
    // 2026-10-17（#330 形态多样化）：Banner(72px 高) / Mini(112px 见方) 放不下
    // 「确认 / 快捷回复」那一排按钮 ⇒ 有按钮就强制升到 Centered。
    // ⚠️ 这条守卫放在**调用 showIsland 之前**：showIsland 里按 form 算几何，
    //    晚改就变成"几何按角标算、内容按卡片画"的错位（按钮被裁在窗外，点不到）。
    //    与 main.cpp 里"胶囊 + 按钮 ⇒ 升 Centered"是同一口径，只是这里收口在源头。
    if (inter && (form == Form::Banner || form == Form::Mini))
        form = Form::Centered;
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
    // ── T 档：文件传输登记成任务（本步只记录）──
    //    tag="file"：与通知分开 —— 通知按 "notice" 合并，文件按 "file" 合并，
    //    两者不再互相顶掉（这正是 T 档要解决的并行问题的数据基础）。
    Task ft(QStringLiteral("file"), QStringLiteral("file"), QStringLiteral("admin"),
            QStringLiteral("正在接收 ") + name, QStringLiteral("download"), 4, true,
            (total > 0) ? double(bytes) / double(total) : -1.0);
    ft.desc = (total > 0) ? QStringLiteral("%1%").arg(int(bytes * 100 / total))
                          : QStringLiteral("0%");
    upsertTask(ft);
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
    // 2026-10-21（审查 #3）：先记"当前岛是否还显示着传输卡"。取消/失败/超时三条
    //   路径只调本函数收场，而传输卡是 duration=0（永不自收）—— 不主动 dismiss
    //   会一直挂到下一条通知。成功路径靠 file_done 的 islandShow("收到文件") 顶掉，
    //   这里只处理"岛还停在传输卡"的情况（已被别的通知顶掉时 title 不是传输卡，
    //   不误伤那条通知）。判据用卡标题前缀（setFileTransfer 内部生成，可控）。
    const bool wasShowingFileCard = m_title.startsWith(QStringLiteral("正在接收"));
    m_ftActive = false;
    m_ftName.clear();
    m_ftBytes = 0;
    m_ftTotal = 0;
    // T 档：传输结束 ⇒ 任务标记完成（本步只记录，不改变界面回收行为）
    for (auto &t : m_tasks)
        if (t.tag == QStringLiteral("file")) { t.state = 2; t.sticky = false; t.progress = 1.0; }
    if (m_bridge) m_bridge->sync();
    if (wasShowingFileCard) dismiss();   // 2026-10-21：传输卡还在岛上 ⇒ 主动收掉
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
        // T 档：对讲中登记成常驻任务（高频状态，不进历史）。
        // ⚠️ 与 setSpectrumActive(true) 同语义 —— 对讲有两条进来的路：
        //   ① 云端 audio.start → setSpectrumActive(true)
        //   ② 本地采到 PCM 帧 → feedSpectrum 首次来帧
        //   两条都要登记，否则走 ② 的路（含 dev_act spectrum 沙箱路径）任务表会漏 voice。
        upsertTask(Task(QStringLiteral("voice"), QStringLiteral("voice"),
                        QStringLiteral("admin"), QStringLiteral("对讲中"),
                        QStringLiteral("mic"), 8, true, -1.0));
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
        // T 档：对讲结束 ⇒ 语音任务标记完成（本步只记录）
        for (auto &t : m_tasks)
            if (t.tag == QStringLiteral("voice")) { t.state = 2; t.sticky = false; }
    } else {
        // T 档：对讲中登记成常驻任务（高频状态，不进历史）
        upsertTask(Task(QStringLiteral("voice"), QStringLiteral("voice"),
                        QStringLiteral("admin"), QStringLiteral("对讲中"),
                        QStringLiteral("mic"), 8, true, -1.0));
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
    // ── T 档：媒体登记成任务（本步只记录）──
    //    tag="media"：与通知/文件分开，三者不再互相顶掉。
    //    ⚠️ 媒体上岛时也走了 showIsland ⇒ 会先登记一条 tag="notice"；
    //       这里再按 "media" 登记，两条共存——这正是"并行"要的数据结构。
    upsertTask(Task(QStringLiteral("media"), QStringLiteral("media"), QStringLiteral("admin"),
                    title, QStringLiteral("music"), 3, true,
                    (totalMs > 0) ? 0.0 : -1.0));
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
    // T 档：播放结束 ⇒ 媒体任务标记完成（本步只记录）
    for (auto &t : m_tasks)
        if (t.tag == QStringLiteral("media")) { t.state = 2; t.sticky = false; }
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
    // ── T 档：把播放进度同步进任务（2026-10-18 修自带 bug）──
    // 之前只在 setMediaSession 里把 progress 设成 0，之后**再没更新过** ⇒
    // 状态列表里那条"正在播放"的进度条永远是空的（比不画还糟：看着像卡住了）。
    // ffplay 无进度回调，进度用墙上钟估算（与 m_mediaElapsedMs 同源）。
    if (m_mediaTotalMs > 0) {
        const double p = qBound(0.0, double(m_mediaElapsedMs) / double(m_mediaTotalMs), 1.0);
        for (auto &t : m_tasks)
            if (t.tag == QStringLiteral("media")) { t.progress = p; break; }
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
        m_panelFace.clear();          // 2026-10-27：面板随内容一起收
        m_panelData = QJsonObject();
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

    // ── T 档：回落（2026-10-18）────────────────────────────────────────
    // 前台要收回时，先看任务表里还有没有"未完成的常驻任务"（如正在接收/正在播放/对讲中）。
    // 有 ⇒ 把它的内容重新显示到岛面，**不回空闲** —— 否则正在进行的进度条会随通知过期一起消失。
    // 无 ⇒ 走原来的收回空闲路径，行为与以前完全一致。
    // ⚠️ 只挑**非 notice** 的任务：通知的合并语义是"同 tag 顶掉"，它不该被自己回落成自己
    //    （否则通知过期后会立刻把自己重新显示出来，变成"永远关不掉"）。
    {
        const Task *nt = nullptr;
        const Task *best = nullptr;
        for (const auto &t : m_tasks) {
            if (t.state == 2 || t.tag == QStringLiteral("notice")) continue;
            if (!t.sticky && t.expireAtMs > 0 && QDateTime::currentMSecsSinceEpoch() > t.expireAtMs) continue;
            if (!best || t.priority > best->priority
                || (t.priority == best->priority && t.shownTs > best->shownTs)) best = &t;
        }
        nt = best;
        if (nt) {
            // 用既有 showIsland 路径显示：几何/动画/淡入全都复用，不另造一套。
            // durationMs=0（sticky 不自动收）+ 保持 m_contentActive，等待它自己被 clear* 收掉。
            m_title = nt->title;
            m_desc = nt->desc;
            m_icon = nt->icon;
            m_form = nt->form;
            m_shownTs = nt->shownTs;
            m_fromAdmin = nt->fromAdmin;
            m_contentActive = true;
            m_idle = false;
            if (auto *eff = qobject_cast<QGraphicsOpacityEffect *>(m_fadeAnim->targetObject()))
                m_fadeAnim->setStartValue(eff->opacity());
            m_fadeAnim->setEndValue(1.0);
            m_fadeAnim->stop();
            m_fadeAnim->setDirection(QAbstractAnimation::Forward);
            m_fadeAnim->start();
            const QRect target = geometryForForm(m_form);
            if (!isVisible()) { setGeometry(target); show(); raise(); }
            else animateGeometry(target);
            if (m_bridge) m_bridge->sync();
            update();
            return;
        }
    }

    const bool wasExpanded = m_expanded;
    // ── 历史归档：有内容的通知在收回空闲时进历史 ──
    if (m_contentActive && !m_title.isEmpty())
        archiveToHistory();
    m_idle = true;
    m_expanded = false;
    m_contentActive = false;   // 内容这就算过去了，之后展开的是"最近一条"
    m_panelFace.clear();       // 2026-10-27：面板是内容的一部分，收回即一起收
    m_panelData = QJsonObject();
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

// ── 岛组件面板（2026-10-27 · G4/G5/G6：日期 / 课表 / 倒计时）────────────────
// 为什么复用 showIsland 而不是另写一套：几何/淡入/归档/顶掉/远控冻结全在那条链上，
// 平行实现必然漏掉其中一条（历史上"媒体"那条就漏过归档守卫）。这里只做两件事：
//   ① 把 panelFace/panelData 盖上（QML 据此切到面板面）；
//   ② 展开（看板要摊开看）。
// ⚠️ 顺序不能反：showIsland 开头会**清面板**（新内容一律顶掉面板），
//    所以必须先 showIsland 再盖标志。
void IslandOverlay::showPanel(const QString &face, const QJsonObject &data, int durationMs)
{
    if (s_islandSuppressed) return;          // 远控期冻结：与 showIsland 同口径
    if (face.isEmpty()) { closePanel(); return; }

    const QString title = data.value(QStringLiteral("title")).toString();
    const QString desc  = data.value(QStringLiteral("desc")).toString();
    const QString icon  = data.value(QStringLiteral("icon")).toString();

    // 面板不是"老师发过的通知"：不进历史（同课程提醒的 skipHistory 口径）。
    setSkipHistoryForNext(true);
    showIsland(title, desc, icon, durationMs, Form::Capsule, false);
    m_panelFace = face;
    m_panelData = data;

    setExpanded(true);   // 摊开：否则学生只看到一个比平时长一点的胶囊

    // 两条兜底定时器对面板的语义：
    //   · m_autoClose：setExpanded 里会按 m_durationMs 排期，但 durationMs<=0（常驻）
    //     时它排的是 qMax(5000,0)=5 秒 —— 常驻面板会在 5 秒后自己消失，必须停掉；
    //   · m_expandGuard：固定 20 秒硬回收（给通知用的），会把 60 秒的面板提前压回去。
    //     面板有明确时长就交给 m_autoClose；常驻则两条都不留（等 closePanel/顶掉）。
    if (durationMs <= 0) m_autoClose->stop();
    if (durationMs <= 0 || durationMs > kExpandMaxMs) m_expandGuard->stop();

    if (m_bridge) m_bridge->sync();
    update();
}

void IslandOverlay::closePanel()
{
    if (m_panelFace.isEmpty()) return;
    m_panelFace.clear();
    m_panelData = QJsonObject();
    // 先 sync 让 QML 卸掉面板面，再走既有 dismiss（收起/回空闲/归档/信号一条路）
    if (m_bridge) m_bridge->sync();
    dismiss();
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

    // ── Mini 角标：点一下"升格"成顶部胶囊并展开（2026-10-17 · #330 形态多样化）──
    // 角标只有 112px（标题 2 行 + 描述 1 行），读不全；用户点它就是想看详情。
    // 升格是**形态变化**（右下角 → 顶部居中、变宽），走 animateGeometry 自然补间。
    // ⚠️ 不升格的话这里只会改一个看不见的 m_expanded 标志 —— 表现为"点了没反应"。
    if (m_form == Form::Mini) {
        if (!on) {                    // 角标没有"收起"语义，收起方向什么都不改
            m_expanded = false;
            emit expandedChanged(false);
            if (m_bridge) m_bridge->sync();
            return;
        }
        m_form = Form::Capsule;
        m_idle = false;
        m_expanded = true;
        animateGeometry(geometryForContent(int(Expanded)));
        // 时长语义沿用 showIsland：m_durationMs<=0（常驻内容）不排自动收起，
        // 只留 expandGuard 那道硬回收（展开态压着屏幕顶部，必须有兜底）。
        m_autoClose->stop();
        if (m_durationMs > 0) m_autoClose->start(qMax(5000, m_durationMs));
        m_expandGuard->start(m_contentActive ? kExpandMaxMs : kIdleExpandMs);
        emit expandedChanged(true);
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

void IslandOverlay::setQmlObjectProperty(const QString &name, QObject *obj)
{
    // 设计说明见 island.h。根对象拿不到（QML 没加载）就静默——手绘兜底不需要它。
    if (!m_quick) return;
    QObject *root = m_quick->rootObject();
    if (!root) {
        qWarning("[island] setQmlObjectProperty(%ls) 被忽略：QML 根对象未就绪",
                 qUtf16Printable(name));
        return;
    }
    const QByteArray key = name.toUtf8();
    root->setProperty(key.constData(), QVariant::fromValue<QObject *>(obj));

    // ── 2026-10-19 远控崩溃修复（真机 dump 三方实证）────────────────────
    // 现象：远程控制开始后约 26s，agent 进程 0xC0000005，出错模块 Qt6Core.dll+0x38c0
    //       （QObject::d_func / QBindingStorage 读取，rcx=0）。栈：
    //       QObject::d_func ← QQmlDelegateModelItem::destroyObjectLater
    //       ← QQuickRepeater::setModel ← QQmlBinding 重算 ← QQmlData::destroyed
    //       ← QObject::~QObject（某个被 QML 引用的 C++ 对象正在销毁）
    // 机制：`property var` 承接 C++ QObject 时，QML 只存**裸指针**（QVariant<QObject*>），
    //       对象销毁后属性里仍是野指针。引擎在对象析构时会把依赖它的绑定全部重算一次，
    //       重算链里一旦有 Repeater.model 重赋值，Repeater 会清空 delegate —— 而此时
    //       部分 delegate 的 object 已被销毁（为 null），Qt6.12 内部对 null 调 d_func → 崩。
    //       `voicehubObj` 是**运行期可被替换/销毁**的模块对象（其他 context property
    //       都是常驻单例），正是这条链的入口。
    // 修法：对象销毁的**那一刻**把 QML 属性清成 null（QML 侧已有 vhRef null 守卫），
    //       让随后的绑定重算读到 null 而不是野指针。root 用 QPointer 兜住，
    //       避免岛/引擎先于模块销毁时 lambda 里再踩一次野指针。
    if (obj) {
        QPointer<QObject> rootGuard(root);
        connect(obj, &QObject::destroyed, root, [rootGuard, key]() {
            if (!rootGuard) return;
            // 2026-10-25（Win7 现场排障）：必须写 **null QObject\***，不能写无效
            //   QVariant()——var 语义探针实证（Qt 6.12）：无效 QVariant 在 QML 侧
            //   读成 **undefined**，`vhRef !== null` 严格比较放行 undefined，
            //   绑定重算读 .active/.nextTitle 连报 TypeError（Win7 rc.4 退出日志
            //   三连报同款），正是 10-19 远控崩溃链想防的重算入口；而
            //   QVariant::fromValue<QObject*>(nullptr) 读成 null，守卫按设计生效。
            rootGuard->setProperty(key.constData(),
                                   QVariant::fromValue<QObject *>(nullptr));
        });
    }
}

// ── 灵动岛 6 形态（2026-10-27 收口蓝图）────────────────────────────────────
// 优先级（高→低）：countdown > alert > notice > marquee > progress > audio > none
// 单条信息中枢：收起态胶囊同一时刻只展示一个形态，避免信息打架。
// ⚠️ 只认既有成员（m_panelFace/m_severity/m_title/m_desc/m_ftActive/m_spectrumOn），
//    不引入 exam/dock 等新状态位 —— 那些是后续「自动化规则引擎」批次的事，本骨架只渲染 6 形态。
void IslandOverlay::recomputeMorph()
{
    if (m_morphForced) return;            // 演示强制模式不走自动裁决
    QString mode = QStringLiteral("none");
    if (m_panelFace == QStringLiteral("countdown"))
        mode = QStringLiteral("countdown");
    else if (m_severity == QStringLiteral("alert"))
        mode = QStringLiteral("alert");
    else if (!m_title.isEmpty()) {
        // 长文案（>36 字）走跑马灯形态；短通知走静态通知形态
        const bool longText = m_desc.size() > 36;
        mode = longText ? QStringLiteral("marquee") : QStringLiteral("notice");
    }
    else if (m_ftActive)
        mode = QStringLiteral("progress");
    else if (m_spectrumOn)
        mode = QStringLiteral("audio");
    m_morphMode = mode;
    if (m_morphMode == QStringLiteral("none")) m_morphData.clear();
}

void IslandOverlay::setMorphDemo(const QString &mode)
{
    static const QStringList modes = {
        QStringLiteral("auto"), QStringLiteral("progress"), QStringLiteral("countdown"),
        QStringLiteral("alert"), QStringLiteral("marquee"),  QStringLiteral("audio"),
        QStringLiteral("dock"),  QStringLiteral("notice")
    };
    QString m = mode.trimmed();
    if (m.isEmpty() || m == QStringLiteral("cycle")) {
        const int i = modes.indexOf(m_morphForced ? m_morphMode : QStringLiteral("auto"));
        m = modes.at((i + 1) % modes.size());
    }
    if (m == QStringLiteral("auto")) {
        m_morphForced = false;
        m_morphMode = QStringLiteral("none");
        m_morphData.clear();
        recomputeMorph();
        if (m_bridge) m_bridge->sync();
        return;
    }
    m_morphForced = true;
    m_morphMode = m;
    QVariantMap d;                        // 给演示数据：让 6 形态都有可看的内容
    if (m == QStringLiteral("progress")) {
        d.insert(QStringLiteral("name"),   QStringLiteral("试卷-高三二模.pdf"));
        d.insert(QStringLiteral("bytes"),  qint64(45 * 1024 * 1024));
        d.insert(QStringLiteral("total"),  qint64(100 * 1024 * 1024));
        d.insert(QStringLiteral("speedKbps"), 3200);
    } else if (m == QStringLiteral("countdown")) {
        d.insert(QStringLiteral("remainSec"), 45 * 60);
        d.insert(QStringLiteral("label"),     QStringLiteral("距离考试结束"));
        d.insert(QStringLiteral("sub"),       QStringLiteral("本场考试为数学"));
    } else if (m == QStringLiteral("alert")) {
        d.insert(QStringLiteral("title"), QStringLiteral("已拦截 1 个应用"));
        d.insert(QStringLiteral("apps"),  QVariantList{QStringLiteral("原神"), QStringLiteral("Bilibili")});
        d.insert(QStringLiteral("reason"), QStringLiteral("考试期间禁止运行无关程序"));
    } else if (m == QStringLiteral("marquee")) {
        d.insert(QStringLiteral("text"), QStringLiteral(
            "天气转凉，请各班注意添衣保暖；本周五下午进行消防演练，请提前安排好疏散路线。"));
    } else if (m == QStringLiteral("audio")) {
        d.insert(QStringLiteral("live"), true);   // 用真频谱（m_spectrumBands）即可
    } else if (m == QStringLiteral("dock")) {
        d.insert(QStringLiteral("actions"), QVariantList{
            QVariantMap{{QStringLiteral("cmd"),  QStringLiteral("lock")},
                        {QStringLiteral("icon"), QStringLiteral("lock")}},
            QVariantMap{{QStringLiteral("cmd"),  QStringLiteral("mute")},
                        {QStringLiteral("icon"), QStringLiteral("volume-x")}},
            QVariantMap{{QStringLiteral("cmd"),  QStringLiteral("notice")},
                        {QStringLiteral("icon"), QStringLiteral("bell")}},
            QVariantMap{{QStringLiteral("cmd"),  QStringLiteral("broadcast")},
                        {QStringLiteral("icon"), QStringLiteral("radio")}}});
    } else if (m == QStringLiteral("notice")) {
        d.insert(QStringLiteral("title"), QStringLiteral("收到张老师通知"));
        d.insert(QStringLiteral("desc"),  QStringLiteral("下午第一节课调至录播教室"));
    }
    m_morphData = d;
    if (m_bridge) m_bridge->sync();
}

void IslandOverlay::morphAction(const QString &cmd)
{
    // 路由骨架：完整业务接线（锁屏/静音/广播/文件传输控制）留给后续「自动化规则引擎」批次。
    const QString c = cmd.trimmed();
    if (c == QStringLiteral("confirm") || c == QStringLiteral("close")
        || c == QStringLiteral("dismiss")) {
        dismiss();
        return;
    }
    if (c == QStringLiteral("pause") || c == QStringLiteral("resume")
        || c == QStringLiteral("cancel")) {
        qWarning("[island] morphAction(%s)：文件传输控制未接线，仅同步状态", qPrintable(c));
        if (m_bridge) m_bridge->sync();
        return;
    }
    qWarning("[island] morphAction(%s)：动作未接线（后续批次接 AutomationEngine/云端指令）",
             qPrintable(c));
    if (m_bridge) m_bridge->sync();
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
/* G2 圆角路径（曲率连续）—— 与 QML 的 `IslandSurface.qml` **同口径**。
 *
 * 为什么要有它：`QPainterPath::addRoundedRect` 画的是**圆弧**，曲率在"角 → 直边"
 * 的接缝处从 1/r 突跳到 0。大圆角/大面积上能看出一道折 —— 这正是用户说的
 * "通知样式不符合审美"（不是圆角大小，是曲率不连续）。
 *
 * 形状：中间直边 + 四角**超椭圆**（|x/a|^n + |y/b|^n = 1 的四分之一段），
 *       n=5 时观感接近 iOS squircle；n=2 退化成椭圆，n→∞ 变直角。
 *
 * ⚠️ 双绘 parity 铁律：superness（5.0）与每角段数（10）必须与 QML 那份保持一致，
 *    否则 QML 加载失败退回手绘时，同一条通知的角会突然"变形"。
 * ⚠️ 这是**兜底路径**（paintEvent 在 QML Ready 时直接 return），但它必须与新观感同步 ——
 *    教室机上 QML 坏掉时，老师看到的就是这一套。 */
static QPainterPath steG2Path(const QRectF &r, qreal radius, qreal superness = 5.0)
{
    QPainterPath path;
    const qreal rad = qBound<qreal>(0.0, radius, qMin(r.width(), r.height()) / 2.0);
    if (rad < 0.75) {                     // 太小就别绕圈，直角即可
        path.addRect(r);
        return path;
    }
    const qreal k = 2.0 / qMax<qreal>(2.0, superness);
    const qreal x0 = r.left(), y0 = r.top(), x1 = r.right(), y1 = r.bottom();
    const int N = 10;                     // 每角段数（与 QML 一致）
    const qreal hp = M_PI / 2.0;

    path.moveTo(x1 - rad, y0);
    for (int i = 1; i <= N; ++i) {        // 右上角
        const qreal t = hp * i / N;
        path.lineTo(x1 - rad + rad * std::pow(std::sin(t), k),
                    y0 + rad - rad * std::pow(std::cos(t), k));
    }
    path.lineTo(x1, y1 - rad);
    for (int i = 1; i <= N; ++i) {        // 右下角
        const qreal t = hp * i / N;
        path.lineTo(x1 - rad + rad * std::pow(std::cos(t), k),
                    y1 - rad + rad * std::pow(std::sin(t), k));
    }
    path.lineTo(x0 + rad, y1);
    for (int i = 1; i <= N; ++i) {        // 左下角（x 用 sin、y 用 cos：起点要接住底边）
        const qreal t = hp * i / N;
        path.lineTo(x0 + rad - rad * std::pow(std::sin(t), k),
                    y1 - rad + rad * std::pow(std::cos(t), k));
    }
    path.lineTo(x0, y0 + rad);
    for (int i = 1; i <= N; ++i) {        // 左上角
        const qreal t = hp * i / N;
        path.lineTo(x0 + rad - rad * std::pow(std::cos(t), k),
                    y0 + rad - rad * std::pow(std::sin(t), k));
    }
    path.closeSubpath();
    return path;
}

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

    // 2026-10-17：G2 圆角（与 QML IslandSurface 同口径），原来是 addRoundedRect 的圆弧
    const QPainterPath path = steG2Path(QRectF(rect()).adjusted(1, 1, -1, -1), full ? 20 : 16);
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

// Banner / Mini 的手绘兜底（2026-10-17 · #330 形态多样化）。
// ⚠️ 与 paintBigForm 同一性质：**只在 QML 没起来时**才走到（paintEvent 顶部判空）。
//    但不能省 —— 被控端是教室机上跑的软件，外观文件缺失时宁可退回朴素观感，
//    也不能在屏幕右下角/顶部留一块透明空气（师生会以为程序崩了）。
//    QML 侧对应 island/IslandBannerFace.qml 与 island/IslandMiniFace.qml，
//    改版式请以 QML 为准、这里同步（双绘 parity 纪律）。
void IslandOverlay::paintCompactForm()
{
    const bool banner = (m_form == Form::Banner);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 角标/横幅都是"常驻轻状态"，比居中卡片更不透明一点（字小，透太多会糊）
    const QColor bg = m_light ? QColor(250, 250, 250, 242) : QColor(20, 20, 22, 234);
    const QColor fg = m_light ? QColor(10, 10, 10) : QColor(250, 250, 250);
    const QColor sub = m_light ? QColor(90, 90, 90) : QColor(168, 168, 178);

    // 角标无描述 ⇒ 正圆（radius = 半边）；有描述 ⇒ 28 圆角方牌；横幅 ⇒ 20 通栏
    const qreal radius = banner ? 20.0 : (m_desc.isEmpty() ? rect().height() / 2.0 : 28.0);
    const QPainterPath path = steG2Path(QRectF(rect()).adjusted(1, 1, -1, -1), radius);
    p.fillPath(path, bg);
    p.setPen(m_light ? QColor(210, 210, 210, 180) : QColor(70, 70, 70, 180));
    p.drawPath(path);

    if (banner) {
        // 单行：图标 + 标题（中粗） + 描述（次色），两侧留 20
        const int padX = 20;
        const int cy = rect().center().y();
        int x = rect().left() + padX;
        if (!m_icon.isEmpty()) {
            const int is = 26;
            const QPixmap ipm = renderSvgIcon(m_icon, is, fg);
            if (!ipm.isNull()) {
                p.drawPixmap(QRect(x, cy - is / 2, is, is), ipm);
                x += is + 12;
            }
        }
        const int avail = rect().right() - padX - x;
        QFont tf = font();
        tf.setPixelSize(16);
        tf.setBold(true);
        const QFontMetrics tfm(tf);
        // 有描述时标题最多吃 45%，剩下全给描述（横幅的价值在描述那句话）
        const int titleW = m_desc.isEmpty()
                               ? avail
                               : qMin(tfm.horizontalAdvance(m_title) + 4, avail * 45 / 100);
        p.setFont(tf);
        p.setPen(fg);
        p.drawText(QRect(x, cy - 12, titleW, 24), Qt::AlignVCenter | Qt::AlignLeft,
                   tfm.elidedText(m_title, Qt::ElideRight, titleW));
        x += titleW + 12;
        if (!m_desc.isEmpty()) {
            QFont df = font();
            df.setPixelSize(13);
            const QFontMetrics dfm(df);
            const int dw = qMax(0, rect().right() - padX - x);
            p.setFont(df);
            p.setPen(sub);
            p.drawText(QRect(x, cy - 11, dw, 22), Qt::AlignVCenter | Qt::AlignLeft,
                       dfm.elidedText(m_desc, Qt::ElideRight, dw));
        }
        return;
    }

    // Mini：图标居中偏上，标题（1~2 行，居中省略），描述 1 行
    const int is = m_desc.isEmpty() ? 30 : 34;
    int y = rect().top() + (m_desc.isEmpty() ? (rect().height() - is) / 2 - 8 : 20);
    if (!m_icon.isEmpty()) {
        const QPixmap ipm = renderSvgIcon(m_icon, is, fg);
        if (!ipm.isNull()) {
            p.drawPixmap(QRect(rect().center().x() - is / 2, y, is, is), ipm);
            y += is + 8;
        }
    }
    QFont tf = font();
    tf.setPixelSize(12);
    tf.setBold(true);
    const QFontMetrics tfm(tf);
    const int tw = qMax(0, rect().width() - 16);
    p.setFont(tf);
    p.setPen(fg);
    if (m_desc.isEmpty()) {
        p.drawText(QRect(rect().left() + 8, y, tw, 18), Qt::AlignHCenter | Qt::AlignTop,
                   tfm.elidedText(m_title, Qt::ElideRight, tw));
        return;
    }
    // 两行标题：直接让 Qt 折行，再按行高裁到 2 行（超出的自然被裁掉）
    const QRect tr(rect().left() + 8, y, tw, tfm.lineSpacing() * 2);
    p.drawText(tr, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_title);
    y = tr.bottom() + 2;
    QFont df = font();
    df.setPixelSize(10);
    const QFontMetrics dfm(df);
    p.setFont(df);
    p.setPen(sub);
    p.drawText(QRect(rect().left() + 8, y, tw, 14), Qt::AlignHCenter | Qt::AlignTop,
               dfm.elidedText(m_desc, Qt::ElideRight, tw));
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

    // Banner / Mini 走各自的紧凑版式（把居中大卡片塞进 72px 高 / 112px 宽里会全溢出）
    if (m_form == Form::Banner || m_form == Form::Mini) {
        paintCompactForm();
        return;
    }
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
        const QPainterPath ip = steG2Path(QRectF(rect()).adjusted(1, 1, -1, -1),
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
    const QPainterPath path = steG2Path(QRectF(rect()).adjusted(1, 1, -1, -1), 24);
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