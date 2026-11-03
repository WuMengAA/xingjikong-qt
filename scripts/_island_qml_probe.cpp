// 批次 3 验收探针（一次性，验完即删）：只链 island.cpp + island_bridge.cpp + svgiconprovider.cpp，
// 拍「灵动岛改 QML 后」的真实渲染。
//
// 为什么另写一个而不是复用 scripts/_island_probe.cpp：
//   · 那个走 PrintWindow 抓窗口，抓的是**窗口 DC** —— QQuickWidget 的内容走独立渲染
//     （OpenGL/软件后端自己的 framebuffer），PrintWindow 拿到的是一片空白，
//     会误判成"QML 没渲染"。必须用 QQuickWidget::grabFramebuffer()。
//   · 那个写死 Qt 6.8.1 路径，本工程已切 6.12.0。
//
// ⚠️ 刻意不链 main.cpp：那才是被控端本体，按"本机不开刀"不能起生产进程。
//
// 参数：
//   --idle          空闲半胶囊
//   --collapsed     收起态（默认）
//   --expand        展开态
//   --actions       带动作按钮（走 showWithActions）
//   --light         浅色
//   --out <路径>    截图输出（默认 D:/Stelarith/_probe/shots/island-agent.png）
//   --spectrum      §6.3 频谱态（2026-10-13）
//   --media         P3 岛底「正在播放」收起播放条（2026-10-14）
//   --media --expand 组件1：展开互动区 + 弹幕输入行（2026-10-15）
//   --search [--phase N]  组件2：点歌搜索页（隐含展开；N=0 空态/1 骨架/
//                         2 结果[默认]/3 无结果/4 超额禁用）（2026-10-15）
//   --submit [--sphase N] 组件3：投稿页（隐含展开；N=0 空编辑/1 有内容/
//                         2 超限/3 提交中/4 成功/5 失败）（2026-10-15）
//   --likefly            组件4：点赞后飘心中途态（隐含 media + expand +
//                         likeOn=true；注入 y=-14/opacity=0.55/visible=true
//                         供截图——600ms 动画的时序由 QML 配置静态可查，
//                         探针只证中途视觉与归位隐藏）（2026-10-15）
//   --click <objectName>  组件3：2500ms 时向目标 MouseArea 派发合成鼠标
//                         press/release（绝对坐标沿父链累加计算）
//   --warm               组件5：预热 30s 发送冷却（先走真实发送链路发一条
//                         祝福 ⇒ 投稿钮禁用 + 计数行倒计时；配 --submit
//                         演示冷却态，点击被禁用的投稿钮应被守卫吞掉、
//                         岛保持展开不误收起）（2026-10-15）
//   --draft <文字>       组件5：投稿页草稿覆写（默认 14 字演示句）；配
//                         --click abtn_submit 演示敏感词拦截（结果码 1）
//                         等真实拒绝路径（2026-10-15）
#include "island.h"
#include "island_layout.h"          // 2026-11-03：期/倒面启用开关（setEnabledById）
#include "island_panel_source.h"      // 2026-11-03：期/倒面取数源
#include "danmaku.h"           // 组件5：弹幕层单例诊断（peek 不建窗口）
// 2026-10-27（G4/G5/G6）：面板三面 —— 探针走**真实数据域**（PanelDomain::build），
// 而不是在 QML 里塞假 JSON：课表数据/倒计时边界/周次都来自 C++，塞假数据等于没测。
#include "panel_domain.h"
#include "notify/provider_registry.h"   // 2026-11-01：--providers 验注册表
#include "schedule_clock.h"
#include "automation_engine.h"   // 2026-10-27 Phase 3：探针注入真引擎验「自动化」面（面 6）

#include <QJsonDocument>
#include <QJsonObject>
#include <QApplication>
#include <QDateTime>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QPixmap>
#include <QQuickWidget>
#include <QQuickItem>
#include <QQmlExpression>   // 2026-10-27 Phase 3：直接求值 faceModel.length（面签 OCR 不可靠）
#include <QQmlContext>
#include <QFontDatabase>
#include <QTextStream>
#include <QMouseEvent>
#include <QTimer>
#include <QThread>   // --hold：让第一条通知真正停留 N 秒（取证"ts=出现时刻"）
#include <QJsonDocument>   // 组件6：--report 把 CloudReporter 收到的 payload 序列化留证
#include <QJsonObject>
#include <cmath>   // §6.3 频谱探针：std::sin
#include <cstdio>  // vhclear 模式：fprintf 交割
#include <functional>  // P3 布局诊断：全子树几何转储

// ── #Win7 排障（2026-10-25）：vhclear 模式专用 ─────────────────────────────
// 复现并验收「VoiceHub 模块销毁 → setQmlObjectProperty 清空 voicehubObj」路径。
// 现场（Win7 rc.4 退出时）连报三处 TypeError：IslandRoot.qml:275/228/190——
// Cannot read property 'active'/'nextTitle' of undefined。根因：清空写的是无效
// QVariant()，QML 的 var 属性读成 **undefined** 而非 null，`vhRef !== null`
// 按严格比较放行 undefined ⇒ 守卫穿透、绑定重算读属性即抛。
// （第 4 处 vhOn:182 因收起态 _expanded=false 短路不抛，故现场恰为 3 条。）
// 本模式：注入假模块 → 删除 → 统计 TypeError（修复前应=3，修复后应=0），
// 以退出码交割：0=修好，1=仍有 TypeError。
static int g_typeErrors = 0;
static void vhclearMsgHandler(QtMsgType, const QMessageLogContext &, const QString &msg)
{
    if (msg.contains(QLatin1String("TypeError"))) ++g_typeErrors;
}

// 从岛的子控件里找 QQuickWidget（island.h 没有公开 getter，这里按类型找）
static QQuickWidget *findQuick(QWidget *w)
{
    if (auto *q = qobject_cast<QQuickWidget *>(w)) return q;
    for (QObject *c : w->children())
        if (auto *cw = qobject_cast<QWidget *>(c))
            if (auto *q = findQuick(cw)) return q;
    return nullptr;
}

// 组件6（2026-10-15）：按 objectName 在 **Item 树**（childItems）里深度优先找。
// 为什么不用 QObject::findChild：Repeater 的 delegate 物件由 QQmlDelegateModel
// 持有，其 QObject parent 不在 rootObject 的 QObject 链上 ⇒ findChild 找不到
// （实测 abtn_songrow found=0 而 searchResults.count=3、objdiag 只列出 13 个
//  顶层具名对象）。而 delegate item 的 **parentItem** 是 Repeater 的 parentItem，
// 在 Item 树里 ⇒ 用 childItems() 递归即可命中。
static QQuickItem *findItemByName(QQuickItem *root, const QString &name)
{
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    const QList<QQuickItem *> kids = root->childItems();
    for (QQuickItem *c : kids)
        if (QQuickItem *hit = findItemByName(c, name)) return hit;
    return nullptr;
}

// 组件3（2026-10-15）：合成鼠标点击 —— 向 QQuickWidget viewport 派发
// press/release。目标绝对坐标沿 parentItem 链累加 x/y（锚定位生效后即
// 当前位置）；scene 与 widget 坐标 1:1（framebuffer 尺寸 = widget 尺寸，
// 2026-10-15 实证）。目标 MouseArea 挂 objectName ⇒ findChild 直达。
static bool clickItem(QQuickWidget *q, QQuickItem *target)
{
    if (!q || !target) return false;
    QPointF base(0, 0);
    for (const QQuickItem *p = target; p; p = p->parentItem())
        base += QPointF(p->x(), p->y());
    const QPoint pt(qRound(base.x() + target->width() / 2.0),
                    qRound(base.y() + target->height() / 2.0));
    QMouseEvent press(QEvent::MouseButtonPress, pt, target->mapToGlobal(QPointF()),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, pt, target->mapToGlobal(QPointF()),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    // ⚠️ 事件发给 QQuickWidget 本身（它没有 viewport() —— 那是
    //    QAbstractScrollArea 的接口；QQuickWidget 自行把 widget 鼠标事件
    //    转译派发到内部 QQuickWindow）。
    qApp->sendEvent(q, &press);
    qApp->sendEvent(q, &release);
    return true;
}

// 组件3（2026-10-15）：捕获段（原 2500ms lambda 后半）—— dump 根对象属性 +
// 全子树几何 + grabFramebuffer 截图。与「注入+点击」分离，以便点击后按需
// 再等 400ms 提交 Timer 再捕获。
static void captureAndSave(IslandOverlay *isl, QQuickWidget *q, const QString &outPath,
                           const QString &extraLog)
{
    QFile logf(QStringLiteral("D:/Stelarith/_probe/shots/_probe_result.txt"));
    logf.open(QIODevice::Append | QIODevice::Text);
    QTextStream out(&logf);
    out << extraLog;

    out << "=== probe ===\n";
    // ── 字体诊断 ────────────────────────────────────────────
    // 离屏平台下中文常渲染成 tofu（方框）。要分清「代码问题」还是「环境缺字」：
    // 这里列出 Qt 实际看得见的字族数量与前几个名字。
    // 若数量为 0 或全是西文 ⇒ 环境问题（离屏 + 无 fontconfig/字体目录），不是本工程的锅。
    {
        const QStringList fams = QFontDatabase::families();
        out << "fonts: count=" << fams.size();
        const int show = qMin(8, fams.size());
        for (int i = 0; i < show; ++i) out << " | " << fams.at(i);
        out << "\n";
        QFontDatabase fdb;
        for (const QString &want : { QStringLiteral("Microsoft YaHei"),
                                     QStringLiteral("SimHei"),
                                     QStringLiteral("SimSun") }) {
            out << "  fontcheck " << want << " => "
                << (fdb.hasFamily(want) ? "HAVE" : "MISSING") << "\n";
        }
    }
    out << "island visible=" << (isl->isVisible() ? 1 : 0)
        << " geom=" << isl->geometry().width() << "x" << isl->geometry().height()
        << " idle=" << (isl->isIdle() ? 1 : 0)
        << " expanded=" << (isl->isExpanded() ? 1 : 0)
        << " interactive=" << (isl->interactive() ? 1 : 0) << "\n";
    out << "quickwidget=" << (q ? "found" : "NULL") << "\n";
    if (!q) { out.flush(); return; }
    out << "quick status=" << int(q->status())
        << " errors=" << q->errors().size()
        << " qgeom=" << q->geometry().width() << "x" << q->geometry().height() << "\n";
    for (const QQmlError &e : q->errors())
        out << "  QMLERR " << e.toString() << "\n";

    if (QQuickItem *ri = q->rootObject()) {
        out << "rootObj class=" << ri->metaObject()->className() << "\n";
        auto dumpProp = [&](const char *n) {
            QVariant v = ri->property(n);
            out << "  root." << n << " = " << (v.isValid() ? v.toString() : QStringLiteral("<invalid>"))
                << "\n";
        };
        dumpProp("_title");
        dumpProp("_desc");
        dumpProp("_icon");
        dumpProp("_idle");
        dumpProp("_expanded");
        dumpProp("_light");
        dumpProp("_interactive");
        dumpProp("_severity");
        dumpProp("_formKind");         // #330：形态串 capsule|centered|fullscreen|banner|mini
        dumpProp("_compactForm");      // #330：紧凑外框（Banner/Mini）判据
        dumpProp("_miniOrb");          // #330：Mini 正圆档（无描述）判据
        dumpProp("_morphOn");          // 2026-10-27：6 形态层是否接管收起态
        dumpProp("_spectrumActive");   // §6.3：频谱态（2026-10-13）
        dumpProp("_mediaPresenting");  // 组件1：媒体占据岛面标志（2026-10-15）
        dumpProp("mediaIsContent");    // 组件1：presenting && 标题==媒体标题
        dumpProp("_mediaPhase");       // 组件7：播放四态渲染相位（2026-10-15）
        dumpProp("_mediaNoProgress");  // 组件7：播完/禁播（无进度条两态）
        dumpProp("likeCount");         // 组件1：点赞计数（探针演示值）
        dumpProp("searchOn");          // 组件2：搜索页打开（2026-10-15）
        dumpProp("searchPhase");       // 组件2：五态 0/1/2/3/4
        dumpProp("searchQuery");       // 组件2：搜索关键词
        dumpProp("face");              // #330：多界面 —— 选中的面号
        dumpProp("mediaFaceOn");       // #330：媒体面可用性
        dumpProp("histFaceOn");        // #330：历史面可用性
        dumpProp("faceModel");         // #330：面清单
        dumpProp("faceNow");           // #330：有效面（失效自动回 0）
        // 2026-10-27 Phase 3：验证「自」面（面 6）入模 —— 面签是老师唯一的到达路径，
        // 面能渲染但面签不在 model 里 ⇒ 讲台上够不着（swipe 翻不到、也没有可点的签）。
        dumpProp("automationFaceOn");
        dumpProp("automationObj");
        // 面签是 11px 单字，截图 OCR 不可靠（自/记 易混）⇒ 直接问 QML 面清单长度。
        // 期望 6：概(0) 史(2) 态(3) 笔(4) 设(5) 自(6)（媒面按需，本轮无媒体）。
        {
            QQmlExpression expr(qmlContext(ri), ri, QStringLiteral("faceModel.length"));
            QVariant v = expr.evaluate();
            out << "  root.faceModel.length = " << v.toString() << "\n";
        }
        dumpProp("facePanelOn");       // #330：面面板是否可见
        dumpProp("facesOn");           // #330：切换器是否可见（≥2 面才开）
        dumpProp("searchQuotaUsed");   // 组件2：已点歌数
        dumpProp("searchQuotaFull");   // 组件2：配额满判据
        dumpProp("submitOn");          // 组件3：投稿页打开（2026-10-15）
        dumpProp("submitPhase");       // 组件3：四态 0 编辑/1 提交中/2 成功/3 失败
        dumpProp("submitFailCode");    // 组件5：态3 失败原因码（1 敏感词/2 冷却/3 无效）
        dumpProp("_blessingCooldownSec"); // 组件5：30s 冷却倒计时（桥驱动，1Hz）
        dumpProp("submitText");        // 组件3：投稿文字
        dumpProp("submitLen");         // 组件3：字数（CJK 一字一计）
        dumpProp("submitEmpty");       // 组件3：空判据（投稿钮禁用源）
        dumpProp("submitOver");        // 组件3：超 50 字判据（投稿钮禁用源）
        dumpProp("submitPageOn");      // 组件3：页面总可见性
        dumpProp("submitH");           // 组件3：页面高度预算
        dumpProp("interactH");         // 组件3：互动区让位验证（开页应 0）
        dumpProp("danmakuH");          // 组件3：弹幕行让位验证（开页应 0）
        dumpProp("likeOn");            // 组件4：快速反馈一次制标志（2026-10-15）
        dumpProp("likePop");           // 组件4：计数回弹缩放（1.0 静止/动画中）
        dumpProp("implicitWidth");
        dumpProp("implicitHeight");
        dumpProp("width");
        dumpProp("height");
        // states / actions 是 list，toString() 给不出内容，单独数个数
        QVariant st = ri->property("_states");
        QVariant ac = ri->property("_actions");
        out << "  root._states.count = " << st.toList().size() << "\n";
        out << "  root._actions.count = " << ac.toList().size() << "\n";
        QVariant sb = ri->property("_spectrumBands");
        out << "  root._spectrumBands.count = " << sb.toList().size() << "\n";
        QVariant sr = ri->property("searchResults");
        out << "  root.searchResults.count = " << sr.toList().size() << "\n";
        // 组件4：飘心 Item（findChild 按 objectName；动画中 y/opacity 有值）
        if (QQuickItem *fly = ri->findChild<QQuickItem *>(QStringLiteral("likeFly"))) {
            out << "  likeFly.visible = " << (fly->isVisible() ? 1 : 0)
                << " y=" << fly->y() << " opacity=" << fly->opacity() << "\n";
        } else {
            out << "  likeFly = <not found>\n";
        }
        // 组件5：投稿钮禁用态（--warm 冷却中应为 0）—— 传导置灰的客观证据
        if (QQuickItem *bs = ri->findChild<QQuickItem *>(QStringLiteral("abtn_submit"))) {
            out << "  abtn_submit.enabled = " << (bs->property("enabled").toBool() ? 1 : 0) << "\n";
        } else {
            out << "  abtn_submit = <not found>\n";
        }
        // 组件6（2026-10-15）诊断：Repeater delegate 内的 objectName 检索。
        // 实测 --click abtn_songrow 落空（found=0）而 searchResults.count=3 ⇒
        // delegate 物件虽已按 model 创建，却不在 QQuickItem::findChild 的
        // QObject 父子链上（Repeater 的 delegate 由 QQmlDelegateModel 持有）。
        // 这里把 QObject 层「具名对象有多少 / 目标在不在」摊开，不靠猜。
        {
            int named = 0, hit = 0;
            QStringList names;
            const auto objs = ri->findChildren<QObject *>(QString());
            for (QObject *o : objs) {
                if (o->objectName().isEmpty()) continue;
                named++;
                if (names.size() < 40) names << o->objectName();
                if (o->objectName() == QStringLiteral("abtn_songrow")) hit++;
            }
            out << "  -- objdiag: named=" << named
                << " abtn_songrow_hits=" << hit << "\n";
            out << "  -- objdiag names: " << names.join(QStringLiteral(",")) << "\n";
        }
        // 组件5：弹幕层单例诊断（peek 不建窗口；null = 本会话还没碰过弹幕层）。
        // last 非空 = 真有祝福被接受入队（真发送的客观证据，与 QML 侧成功态互证）。
        if (DanmakuWidget *dw = DanmakuWidget::danmakuWidgetPeek()) {
            out << "  danmaku: pending=" << dw->pendingCount()
                << " scrolling=" << (dw->isScrolling() ? 1 : 0)
                << " cooldown=" << dw->blessingCooldownRemaining()
                << " last=\"" << dw->lastBlessing() << "\"\n";
        } else {
            out << "  danmaku: <not created>\n";
        }
    }

    // ── P3 布局诊断（2026-10-14）：全子树几何转储 ──────────────────
    // mediaRow 锚 specRow.bottom 但两者不同父级（root vs expandedCol），
    // 截图里媒体栏跑到了顶部。这里把整棵 Item 树的 x/y/w/h 打出来定位真因。
    if (QQuickItem *ri = q->rootObject()) {
        out << "=== tree ===\n";
        std::function<void(QQuickItem *, int)> walk = [&](QQuickItem *it, int depth) {
            QVariant t = it->property("text");
            const QString ts = t.isValid() ? t.toString().left(14) : QString();
            out << QStringLiteral("  %1%2 y=%3 h=%4 w=%5 vis=%6%7\n")
                   .arg(QString(depth * 2, QLatin1Char(' ')))
                   .arg(QLatin1String(it->metaObject()->className()))
                   .arg(it->y()).arg(it->height()).arg(it->width())
                   .arg(it->isVisible() ? 1 : 0)
                   .arg(ts.isEmpty() ? QString() : QStringLiteral(" text=\"") + ts + QStringLiteral("\""));
            for (QObject *c : it->children())
                if (auto *ci = qobject_cast<QQuickItem *>(c)) walk(ci, depth + 1);
        };
        walk(ri, 0);
        out << "=== end tree ===\n";
    }

    // ⚠️ 关键：QML 内容必须用 grabFramebuffer()，普通 grab() 拿到空白。
    //    但若 QML 没加载（status != Ready），island.cpp 会**退回手绘** —— 手绘画在
    //    QWidget 上，grabFramebuffer 抓不到，得改抓 widget 本身。这条分支就是用来
    //    验证"打包漏了 qml/ 时不会白屏/崩，而是退化成老的手绘灵动岛"。
    if (q->status() != QQuickWidget::Ready) {
        out << "FALLBACK: QML 未加载 -> 抓 widget 手绘\n";
        QImage wimg = isl->grab().toImage();
        out << "widgetgrab=" << wimg.width() << "x" << wimg.height()
            << " null=" << (wimg.isNull() ? 1 : 0) << "\n";
        if (!wimg.isNull()) {
            QDir().mkpath(QFileInfo(outPath).absolutePath());
            const bool ok = wimg.save(outPath);
            out << "saved=" << (ok ? 1 : 0) << " path=" << outPath << "\n";
        }
        out.flush();
        return;
    }

    QImage img = q->grabFramebuffer();
    out << "framebuffer=" << img.width() << "x" << img.height()
        << " null=" << (img.isNull() ? 1 : 0) << "\n";
    // 组件5 诊断（2026-10-15）：grab 之后再读一次冷却值。若与上面 dump 的
    // 不同 ⇒ QML 绑定重求值挂在帧渲染上（dump 早于帧 ⇒ 读到旧值），生产
    // 语义不受影响（文本变化自身触发重绘，自激成帧）；若仍相同 ⇒ 1Hz tick
    // 真的没把值送到 QML，要回头查 m_cooldownTick。
    if (QQuickItem *ri2 = q->rootObject()) {
        QVariant v2 = ri2->property("_blessingCooldownSec");
        out << "  root._blessingCooldownSec(post-grab) = " << v2.toString() << "\n";
    }
    if (!img.isNull()) {
        QDir().mkpath(QFileInfo(outPath).absolutePath());
        const bool ok = img.save(outPath);
        out << "saved=" << (ok ? 1 : 0) << " path=" << outPath << "\n";
    }
    out.flush();
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // ── 文字方框（tofu）根因实验 ────────────────────────────────────────────
    // 离屏平台下 QFontDatabase::families() 返回 0（系统字体一个都枚举不到），导致中文
    // 全变方框。这里显式注册一个系统中文字体，验证"tofu 是离屏环境限制、非 QML 代码问题"。
    // 注册成功 ⇒ families()>0 ⇒ QML 默认字体能拿到中文 ⇒ tofu 消失。
    int fid = QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
    QStringList ffams = (fid >= 0) ? QFontDatabase::applicationFontFamilies(fid) : QStringList();
    if (!ffams.isEmpty())
        app.setFont(QFont(ffams.first()));

    // ⚠️ 进程一进来就先落一行"我起来了"：探针若在构造岛时崩掉（比如 QML 引擎初始化
    //    失败），没有这行就只能看到"exe 秒退、什么都没写"，无从判断死在哪一步。
    {
        QDir().mkpath(QStringLiteral("D:/Stelarith/_probe/shots"));
        QFile boot(QStringLiteral("D:/Stelarith/_probe/shots/_probe_result.txt"));
        if (boot.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream(&boot) << "boot pid=" << qApp->applicationPid()
                               << " dir=" << QCoreApplication::applicationDirPath()
                               << " fontReg=" << fid
                               << " families=" << QFontDatabase::families().size()
                               << " fam0=" << (ffams.isEmpty() ? QStringLiteral("(none)") : ffams.first())
                               << "\n";
        }
        // 组件5 诊断（2026-10-15）：冷却广播逐条追踪文件，启动时截断
        QFile ctr(QStringLiteral("D:/Stelarith/_probe/shots/_cooldown_trace.txt"));
        if (ctr.open(QIODevice::WriteOnly | QIODevice::Text)) { /* 截断 */ }
        // 组件6（2026-10-15）：云端上报追踪文件（--report 注入的 reporter 逐条落盘），启动时截断
        QFile rtr(QStringLiteral("D:/Stelarith/_probe/shots/_report_trace.txt"));
        if (rtr.open(QIODevice::WriteOnly | QIODevice::Text)) { /* 截断 */ }
    }

    bool idle = false, expand = false, actions = false, light = false, spectrum = false, media = false;
    bool search = false;   // 组件2（2026-10-15）：点歌搜索页（隐含 media + expand）
    int searchPhase = 2;   // 组件2：搜索页五态 0 空态/1 骨架/2 结果/3 无结果/4 超额禁用
    // 组件3（2026-10-15）：投稿页（隐含 media + expand）
    bool submit = false;
    int sphase = 0;        // 投稿态 0 空编辑/1 有内容/2 超限/3 提交中/4 成功/5 失败
    QString clickName;     // 合成点击目标 objectName（danmakuPillMa/danmakuSendMa/abtn_*）
    QString classArg;      // 2026-10-31：--class 注入班级名（验证菜单栏中间显示）
    QString setPropStr;    // 2026-10-31：--set name=value 通用属性注入（苹果式极简验证用）
    // 组件5（2026-10-15）：--warm 预热 30s 冷却（投稿钮禁用+倒计时演示）；
    // --draft <文字> 覆写投稿页草稿（敏感词/真实结果码路径演示）。
    bool warm = false;
    QString draftOverride;
    // 组件4（2026-10-15）：点赞飘心中途态（隐含 media + expand + likeOn=true）
    bool likefly = false;
    // 组件6（2026-10-15）：注入打印式 CloudReporter —— 验证「QML 点击 → 桥 →
    // 岛 → reporter」整条上报链真的被调到（生产里 reporter 由 main.cpp 用 g_ws
    // 注入；探针不连 ws，用落盘替身把「调到了几次、什么 payload」留成证据）。
    bool report = false;
    // 组件7（2026-10-15）：播放四态 —— 直接指定相位，隐含 --media。
    //   starting / playing / ending / ended / blocked（空 = 走 --media 默认 playing）。
    QString mediaPhaseArg;
    // #330（2026-10-17）：多界面的面号 0=概览 1=媒体 2=历史 3=状态。
    // 面只在展开态存在 ⇒ 隐含 --expand；1 隐含 --media（媒体面要有媒体）；
    // 2 会自动铺 3 条历史（见下方 show 段）。
    int faceArg = 0;
    // #330（2026-10-17）形态多样化：直接走**真实 C++ 入口**进 Banner / Mini。
    // ⚠️ 不用 setProperty 注入：几何是 C++ geometryForForm 算出来的，只有
    //    走 showIsland(..., Form::X) 才验证得到"窗口几何 + QML 版式"整条链。
    //    取值：banner（长文案通栏）/ mini（有描述 方牌）/ mini-nodesc（无描述 正圆）。
    QString formArg;
    // 2026-10-27（F 收尾第 2 项）：渠道标签 —— 复刻上课提醒真链路（setChannelForNext）。
    QString channelArg;
    // 2026-10-27（G4/G5/G6）：面板三面 —— face 名 + 额外 JSON 参数 + 课表档案路径。
    //   --panel date|timetable|countdown   开面板（真实 PanelDomain::build）
    //   --panel-json '{"to":"12:30"}'      追加参数（countdown 用）
    //   --profile <path>                   课表档案（timetable / 默认 countdown 用）
    QString panelArg;
    QString panelJsonArg;
    QString profileArg;
    // 2026-10-27（灵动岛 6 形态骨架）：--morph <mode> 直接走 C++ 演示入口
    //   progress | countdown | alert | marquee | audio | dock | notice | cycle | auto
    // 强制形态层接管收起态胶囊，给 6 形态各自的渲染抓一张图（验证 QML 守 §10.9 纯净渲染）。
    QString morphModeArg;
    // 2026-10-17：--hold <sec> 让通知**多停留**若干秒再截图/退出。
    // 用途：分辨历史 ts 取的是"通知出现时刻"还是"归档时刻"——
    //   停留 N 秒后归档，若 ts 仍是出现时刻，则 ts 比分文件写入时刻早约 N 秒。
    int holdSec = 0;
    int autocloseMs = 0;   // 2026-11-01：验「带按钮 + 显式时长 → 到点自动收起」（U盘 15s 修复）
    int endAtMsArg = 0;    // 2026-11-01：验 EndTime（绝对结束时刻）覆盖 durationMs
    bool touchMode = false;       // 2026-11-02：验「有触摸就等到无触摸才收起」
    bool providersMode = false;   // 2026-11-01：验通知提供方注册表（纯逻辑，不开窗）
    bool queueMode = false;       // 2026-11-01：验排队与仲裁（低优先不顶掉 + 补播）
    bool vhclear = false;   // #Win7 排障：vh 模块销毁清空路径验收（见文件头注释）
    bool automation = false; // 2026-10-27 Phase 3：注入真 AutomationEngine 验「自」面
    QString outPath = QStringLiteral("D:/Stelarith/_probe/shots/island-agent.png");
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--idle"))      idle = true;
        if (a == QLatin1String("--expand"))    expand = true;
        if (a == QLatin1String("--actions"))   actions = true;
        if (a == QLatin1String("--light"))     light = true;
        if (a == QLatin1String("--spectrum"))  spectrum = true;   // §6.3 频谱态（2026-10-13）
        if (a == QLatin1String("--media"))     media = true;      // P3 岛底「正在播放」（2026-10-14）
        if (a == QLatin1String("--search"))    search = true;     // 组件2：点歌搜索页（2026-10-15）
        if (a == QLatin1String("--phase") && i + 1 < argc)
            searchPhase = QString::fromLocal8Bit(argv[++i]).toInt();
        if (a == QLatin1String("--submit"))    submit = true;     // 组件3：投稿页（2026-10-15）
        if (a == QLatin1String("--sphase") && i + 1 < argc)
            sphase = QString::fromLocal8Bit(argv[++i]).toInt();
        if (a == QLatin1String("--click") && i + 1 < argc)
            clickName = QString::fromLocal8Bit(argv[++i]);
        // 2026-10-31 · 苹果式极简：通用属性注入（--set name=value），供探针直接
        //   设 historyOpen/moreMenuOn 等布尔，验证「⋯弹层 → 历史面板」链路。
        if (a == QLatin1String("--set") && i + 1 < argc)
            setPropStr = QString::fromLocal8Bit(argv[++i]);
        if (a == QLatin1String("--class") && i + 1 < argc)   // 2026-10-31：班级名（菜单栏）
            classArg = QString::fromLocal8Bit(argv[++i]);
        if (a == QLatin1String("--likefly"))   likefly = true;    // 组件4：飘心中途态
        if (a == QLatin1String("--warm"))      warm = true;       // 组件5：预热 30s 冷却
        if (a == QLatin1String("--media-phase") && i + 1 < argc) {  // 组件7：播放四态
            mediaPhaseArg = QString::fromLocal8Bit(argv[++i]);
            media = true;                                          // 隐含媒体态
        }
        if (a == QLatin1String("--draft") && i + 1 < argc)
            draftOverride = QString::fromLocal8Bit(argv[++i]);    // 组件5：草稿覆写
        if (a == QLatin1String("--report"))    report = true;     // 组件6：上报链路留证
        if (a == QLatin1String("--face") && i + 1 < argc) {   // #330 多界面
            faceArg = QString::fromLocal8Bit(argv[++i]).toInt();
            expand = true;
            if (faceArg == 1) media = true;                     // 媒体面要有媒体
        }
        // #330 形态多样化 + 2026-10-17 补 centered/fullscreen（大卡片两档此前无 QML 版式）
        if (a == QLatin1String("--form") && i + 1 < argc)
            formArg = QString::fromLocal8Bit(argv[++i]);
        if (a == QLatin1String("--channel") && i + 1 < argc)
            channelArg = QString::fromLocal8Bit(argv[++i]);
        if (a == QLatin1String("--panel") && i + 1 < argc)
            panelArg = QString::fromLocal8Bit(argv[++i]);
        if (a == QLatin1String("--panel-json") && i + 1 < argc)
            panelJsonArg = QString::fromLocal8Bit(argv[++i]);
        if (a == QLatin1String("--profile") && i + 1 < argc)
            profileArg = QString::fromLocal8Bit(argv[++i]);
        if (a == QLatin1String("--out") && i + 1 < argc)
            outPath = QString::fromLocal8Bit(argv[++i]);
        if (a == QLatin1String("--hold") && i + 1 < argc)
            holdSec = QString::fromLocal8Bit(argv[++i]).toInt();
        if (a == QLatin1String("--autoclose") && i + 1 < argc)
            autocloseMs = QString::fromLocal8Bit(argv[++i]).toInt();
        if (a == QLatin1String("--touch")) touchMode = true;
        if (a == QLatin1String("--providers")) providersMode = true;
        if (a == QLatin1String("--queue")) queueMode = true;
        if (a == QLatin1String("--endat") && i + 1 < argc)
            endAtMsArg = QString::fromLocal8Bit(argv[++i]).toInt();   // 从现在起多少毫秒后结束
        if (a == QLatin1String("--vhclear"))  vhclear = true;   // #Win7 排障
        if (a == QLatin1String("--automation")) automation = true; // 2026-10-27 Phase 3：注入真引擎验「自」面
        if (a == QLatin1String("--morph") && i + 1 < argc)
            morphModeArg = QString::fromLocal8Bit(argv[++i]);   // 6 形态演示入口
    }

    qInstallMessageHandler(vhclearMsgHandler);   // 必须在岛（QML）创建前装

    IslandOverlay *isl = IslandOverlay::instance();
    // 2026-11-03：期/倒面取数源。生产链路由 island.cpp:306 注入；探针复用同一
    // IslandOverlay，QML（compDate/compCountdown）用 typeof panelSource !== "undefined"
    // 守卫 —— 不补注时守卫成立、info 恒空 map，采样必是空皮。
    if (QQuickWidget *qw0 = findQuick(isl))
        qw0->rootContext()->setContextProperty(QStringLiteral("panelSource"),
                                               islandPanelSourceInstance());
    if (!classArg.isEmpty()) isl->setClassName(classArg);   // 2026-10-31：注入班级名供菜单栏验证
    isl->setIdleEnabled(true);
    if (light) isl->setLightMode(true);

    if (automation) {
        // 2026-10-27 Phase 3：注入真 AutomationEngine，验「自」面（面 6）加载 + 真数据
        // 链路（ruleSummaries / ruleCount）。生产里这段在 main.cpp，探针独立复现。
        auto *engine = new AutomationEngine(isl);
        isl->setQmlObjectProperty(QStringLiteral("automationObj"), engine);
        // 预置一条规则，让面有内容可渲染（验证 summaries 非空路径）。
        QJsonObject rule;
        rule.insert(QStringLiteral("id"), QStringLiteral("probe-bell"));
        rule.insert(QStringLiteral("name"), QStringLiteral("探针·每日上课铃"));
        rule.insert(QStringLiteral("enabled"), true);
        QJsonObject trig; trig.insert(QStringLiteral("type"), QStringLiteral("time"));
        trig.insert(QStringLiteral("time"), QStringLiteral("08:00"));
        rule.insert(QStringLiteral("trigger"), trig);
        QJsonObject cond; cond.insert(QStringLiteral("days"), QJsonArray{1,2,3,4,5});
        rule.insert(QStringLiteral("condition"), cond);
        QJsonObject act; act.insert(QStringLiteral("kind"), QStringLiteral("notify"));
        act.insert(QStringLiteral("title"), QStringLiteral("上课了"));
        act.insert(QStringLiteral("content"), QStringLiteral("请安静，准备上课"));
        rule.insert(QStringLiteral("action"), act);
        engine->upsertFromRemote(rule);   // 走与管理端下发同一入口（幂等）
        QTimer::singleShot(300, [engine] {
            fprintf(stderr, "AUTOMATION-ruleCount=%d\n", engine->ruleCount());
            fflush(stderr);
        });
    }

    if (vhclear) {
        // #Win7 排障（2026-10-25）：岛已建（QML 已同步加载）⇒ 300ms 后注入假
        // VoiceHub 模块 → 300ms 后删除（destroyed → 清属性 lambda 同步触发）→
        // 400ms 后统计 TypeError 并以退出码交割（0=修好，1=仍有 TypeError）。
        QTimer::singleShot(300, [isl] {
            auto *dummy = new QObject(isl);   // 无需 active 属性：活对象读 .active 得 undefined 不抛
            isl->setQmlObjectProperty(QStringLiteral("voicehubObj"), dummy);
            QTimer::singleShot(300, [dummy] {
                delete dummy;
                QTimer::singleShot(400, [] {
                    fprintf(stderr, "VHCLEAR-TYPEERRORS=%d\n", g_typeErrors);
                    fflush(stderr);
                    QCoreApplication::exit(g_typeErrors == 0 ? 0 : 1);
                });
            });
        });
        return app.exec();
    }

    if (queueMode) {
        // 2026-11-01（第 3 步 · 排队与仲裁）：验「占屏时低优先不硬顶 + 高优先结束后补播」。
        //   A = 管理端(priorityIndex 0) 2000ms；400ms 后 B = 插件(index 6) 600ms。
        //   三条断言：
        //     t=800ms  标题仍是 A  ⇒ 低优先**没顶掉**当前内容（改动前这里会变成 B）
        //     t=1300ms 标题是 B    ⇒ A 收掉后 B **补播**了（证明它是排队、不是被丢弃）
        //     t=2800ms 已空闲      ⇒ B 按**自己的 600ms** 收（不是被当成 sticky 永久挂着）
        isl->setProviderForNext(QStringLiteral("admin"));
        isl->showIsland(QStringLiteral("A·管理端通知"), QStringLiteral("高优先，先上屏"),
                        QStringLiteral("bell"), 2000, IslandOverlay::Form::Capsule, false);
        QTimer::singleShot(400, [isl]() {
            isl->setProviderForNext(QStringLiteral("plugin"));
            isl->showIsland(QStringLiteral("B·插件提示"), QStringLiteral("低优先，应排队"),
                            QStringLiteral("rocket"), 600, IslandOverlay::Form::Capsule, false);
        });
        bool *notClobbered = new bool(false);
        bool *replayed = new bool(false);
        QTimer::singleShot(800, [isl, notClobbered]() {
            *notClobbered = (isl->currentTitle() == QStringLiteral("A·管理端通知"));
            fprintf(stdout, "[queue] t=800ms title=\"%s\" %s (expect A·低优先不顶掉)\n",
                    qPrintable(isl->currentTitle()), *notClobbered ? "PASS" : "FAIL");
            fflush(stdout);
        });
        QTimer::singleShot(1000, [isl]() { isl->dismiss(); });   // 收掉 A ⇒ 触发补播
        QTimer::singleShot(1500, [isl, replayed]() {
            *replayed = (isl->currentTitle() == QStringLiteral("B·插件提示"));
            fprintf(stdout, "[queue] t=1500ms title=\"%s\" %s (expect B·补播)\n",
                    qPrintable(isl->currentTitle()), *replayed ? "PASS" : "FAIL");
            fflush(stdout);
        });
        QTimer::singleShot(3000, [isl, notClobbered, replayed]() {
            const bool idleNow = isl->isIdle();
            fprintf(stdout, "[queue] t=3000ms idle=%d %s (expect 1·按自己的时长收)\n",
                    idleNow ? 1 : 0, idleNow ? "PASS" : "FAIL");
            fflush(stdout);
            const int rc = (*notClobbered && *replayed && idleNow) ? 0 : 1;
            delete notClobbered;
            delete replayed;
            QCoreApplication::exit(rc);
        });
        return app.exec();
    }
    if (endAtMsArg > 0) {
        // 2026-11-01（顶替 ClassIsland 第 1 步）：验 **EndTime 覆盖 durationMs**。
        // 做法：durationMs 只给 1500（本该 1.5s 就收），EndTime 给 now+endAtMsArg。
        //   断言三条：
        //     · t=500ms        仍在上屏
        //     · t=2700ms       仍在上屏  ← 证明 durationMs=1500 被 EndTime 覆盖（反例保护）
        //     · t=endAt+1200   已收起    ← 证明到**绝对时刻**才收
        //   endAtMsArg 必须 > 2700 才有意义（否则第二条断言自然为假）。
        isl->setEndTimeForNext(QDateTime::currentMSecsSinceEpoch() + endAtMsArg);
        isl->showIsland(QStringLiteral("挂到这个时刻为止"),
                        QStringLiteral("durationMs=1500 应被 EndTime 覆盖"),
                        QStringLiteral("clock"), 1500, IslandOverlay::Form::Centered, false);
        bool *shownEarly = new bool(false);
        bool *shownPastDur = new bool(false);
        QTimer::singleShot(500, [isl, shownEarly]() {
            *shownEarly = !isl->isIdle();
            fprintf(stdout, "[endat] t=500ms shown=%d (expect 1)\n", *shownEarly ? 1 : 0);
            fflush(stdout);
        });
        QTimer::singleShot(2700, [isl, shownPastDur]() {
            // durationMs 只给了 1500 —— 到 2700 还挂着，说明 EndTime 真的优先
            *shownPastDur = !isl->isIdle();
            fprintf(stdout, "[endat] t=2700ms shown=%d (expect 1, durationMs=1500 被 EndTime 覆盖)\n",
                    *shownPastDur ? 1 : 0);
            fflush(stdout);
        });
        QTimer::singleShot(endAtMsArg + 1200, [isl, shownEarly, shownPastDur, endAtMsArg]() {
            const bool idleNow = isl->isIdle();
            fprintf(stdout, "[endat] t=%dms idle=%d (expect 1, 到绝对时刻才收)\n",
                    endAtMsArg + 1200, idleNow ? 1 : 0);
            fflush(stdout);
            const int rc = (*shownEarly && *shownPastDur && idleNow)
                               ? 0 : ((*shownEarly && *shownPastDur) ? 1 : 2);
            delete shownEarly;
            delete shownPastDur;
            QCoreApplication::exit(rc);
        });
        return app.exec();
    }
    if (autocloseMs > 0) {
        // 2026-11-01：验「带按钮 + 显式时长 → 到点自动收起」（U盘 15s 修复的正面断言）。
        // 走真实 overlay + showWithActions（交互态），时长给 autocloseMs：
        //   · t≈500ms 应仍可见（提示在上屏、未到点）
        //   · t=autocloseMs+1200 应收起（到点自动 dismiss，含交互态）
        isl->showWithActions(QStringLiteral("U 盘已插入"),
                             QStringLiteral("可移动磁盘 (X:)"),
                             QStringLiteral("folder"),
                             IslandOverlay::Form::Centered,
                             { QStringLiteral("打开"), QStringLiteral("忽略") },
                             [](const QString &s) {
                                 QFile f(QStringLiteral("D:/Stelarith/_probe/shots/_autoclose_hit.txt"));
                                 if (f.open(QIODevice::Append | QIODevice::Text))
                                     QTextStream(&f) << "fireAction: " << s << "\n";
                             },
                             autocloseMs);
        bool *earlyShown = new bool(false);
        QTimer::singleShot(500, [isl, earlyShown]() {
            // ⚠️ 岛默认开空闲半胶囊：到点自动收起后**窗口仍可见**（变回空闲胶囊），
            //    所以断言用 isIdle()（内容是否已关），不是 isVisible()。
            // ⚠️ 用 fprintf 而非 qInfo：本探针全局装了 vhclearMsgHandler，
            //    它**吞掉所有 Qt 消息**（只统计 TypeError），qInfo 出不来。
            *earlyShown = !isl->isIdle();
            fprintf(stdout, "[autoclose] t=500ms shown=%d (expect 1, 提示在上屏)\n",
                    *earlyShown ? 1 : 0);
            fflush(stdout);
        });
        QTimer::singleShot(autocloseMs + 1200, [isl, earlyShown, autocloseMs]() {
            const bool idleNow = isl->isIdle();
            fprintf(stdout, "[autoclose] t=%dms idle=%d (expect 1, 已自动收起)\n",
                    autocloseMs + 1200, idleNow ? 1 : 0);
            fflush(stdout);
            const int rc = (*earlyShown && idleNow) ? 0 : ((*earlyShown) ? 1 : 2);
            delete earlyShown;
            QCoreApplication::exit(rc);
        });
        return app.exec();
    }
    if (touchMode) {
        // 2026-11-02（用户：「灵动岛自动收起时机不对，有触摸就等到无触摸才收起」）。
        // 三条断言，缺一条都测不到用户说的那个毛病：
        //   ① t=400ms 按下 → 到点（1500ms）时**必须仍在上屏**（这是"有触摸就不收"）；
        //   ② 按住期间再等 2s 也还在（不是"延后一次"而是"一直延到松手"）；
        //   ③ 松手后 5s（kTouchIdleMs）过了才收（松手是重新起表，不是把剩余量续上）。
        // ⚠️ 断言用 isIdle() 不是 isVisible()：岛到点收起后窗口仍在（变回空闲胶囊）。
        const int touchDurMs = 1500;
        isl->showWithActions(QStringLiteral("触摸验"),
                             QStringLiteral("按住期间不该收"),
                             QStringLiteral("folder"),
                             IslandOverlay::Form::Centered,
                             { QStringLiteral("知道了") },
                             nullptr, touchDurMs);
        bool *heldOk = new bool(false);
        bool *heldLongOk = new bool(false);
        QTimer::singleShot(400, [isl]() {
            isl->setTouching(true);          // 模拟按下
            fprintf(stdout, "[touch] t=400ms 按下\n");
            fflush(stdout);
        });
        QTimer::singleShot(touchDurMs + 300, [isl, heldOk]() {
            // 原时长已过：按住中 ⇒ 必须仍在上屏
            *heldOk = !isl->isIdle();
            fprintf(stdout, "[touch] t=%d 按住中仍在上屏=%d (expect 1)\n",
                    touchDurMs + 300, *heldOk ? 1 : 0);
            fflush(stdout);
        });
        QTimer::singleShot(touchDurMs + 2300, [isl, heldLongOk]() {
            *heldLongOk = !isl->isIdle();
            fprintf(stdout, "[touch] t=%d 按住 +2s 仍在上屏=%d (expect 1)\n",
                    touchDurMs + 2300, *heldLongOk ? 1 : 0);
            fflush(stdout);
            isl->setTouching(false);         // 松手 → 重新起表（5s）
            fprintf(stdout, "[touch] 松手，重新起表 5000ms\n");
            fflush(stdout);
        });
        QTimer::singleShot(touchDurMs + 2300 + 5600, [isl, heldOk, heldLongOk]() {
            const bool gone = isl->isIdle();
            fprintf(stdout, "[touch] 松手 +5.6s 已收起=%d (expect 1)\n", gone ? 1 : 0);
            fflush(stdout);
            const int rc = (*heldOk && *heldLongOk && gone) ? 0 : 1;
            delete heldOk;
            delete heldLongOk;
            QCoreApplication::exit(rc);
        });
        return app.exec();
    }
    if (providersMode) {
        // 2026-11-01（第 2 步）：通知提供方注册表 —— 纯逻辑断言，不建窗口、不看 QML。
        // 跑完 **resetToDefaults()**：探针不许把状态留在机器上。
        using notify::NotifyProviders;
        // ⚠️ 探针必须给 QSettings 一个**落点**：生产在 main.cpp 设 org=Stelarith /
        //    app="Stelarith Agent"；探针两者都没设 ⇒ 默认构造的 QSettings 无路径，
        //    写入立刻丢失（表现为"写了读不回"的假失败，2026-11-01 实测踩过）。
        //    这里用**专属 app 名**，能真落盘但绝不碰生产键。
        QCoreApplication::setOrganizationName(QStringLiteral("Stelarith"));
        QCoreApplication::setApplicationName(QStringLiteral("NotifyProvidersProbe"));
        // ⚠️ 受限会话/沙箱下**注册表写不进去**（实测 QSettings::status() == AccessError，
        //    裸写一个键都回不来）。这里把默认后端切到 ini + 临时目录：
        //    验的仍是**同一套读写逻辑**，只是换个可写的落点 —— 生产照旧走注册表。
        const QString probeIniDir =
            QDir(QDir::tempPath()).filePath(QStringLiteral("stelarith_notify_provider_probe"));
        QDir().mkpath(probeIniDir);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, probeIniDir);
        NotifyProviders &R = NotifyProviders::instance();
        R.resetToDefaults();
        int fail = 0;
        auto chk = [&fail](const char *name, bool ok) {
            fprintf(stdout, "[providers] %-44s %s\n", name, ok ? "PASS" : "FAIL");
            if (!ok) ++fail;
            fflush(stdout);
        };
        const QStringList def = R.order();
        chk("默认序 = 管理端>课程>对讲>文件>U盘>自动化>插件>考试",
            def == QStringList{ QStringLiteral("admin"), QStringLiteral("class"),
                                QStringLiteral("voice"), QStringLiteral("file"),
                                QStringLiteral("usb"), QStringLiteral("automation"),
                                QStringLiteral("plugin"), QStringLiteral("exam") });
        chk("settingsModel 8 行", R.settingsModel().size() == 8);
        chk("默认 allow(class)=true", R.allow(QStringLiteral("class")));
        chk("未知名放行（provider 与 channel 两层）",
            R.allow(QStringLiteral("nope-not-exist")) &&
            R.allow(QStringLiteral("nope-not-exist"), QStringLiteral("any")));
        R.setEnabled(QStringLiteral("plugin"), false);
        {
            // 诊断（2026-11-01）：先裸写一个键作对照 —— 分清"注册表写不进去（环境）"
            // 与"我的代码没走到（逻辑）"。
            QSettings raw;
            raw.setValue(QStringLiteral("probe/raw_roundtrip"), 7);
            raw.sync();
            fprintf(stdout, "[providers] dbg raw_write_status=%d raw_readback=%s\n",
                    int(raw.status()), qPrintable(raw.value(QStringLiteral("probe/raw_roundtrip"),
                                                            QStringLiteral("<unset>")).toString()));
            QSettings probeSettings;
            fprintf(stdout, "[providers] dbg file=%s status=%d readback=%s\n",
                    qPrintable(probeSettings.fileName()), int(probeSettings.status()),
                    qPrintable(probeSettings.value(QStringLiteral("notify/providers/plugin/enabled"),
                                                   QStringLiteral("<unset>")).toString()));
            fflush(stdout);
        }
        chk("关 plugin ⇒ allow(plugin)=false", !R.allow(QStringLiteral("plugin")));
        chk("关 plugin 不影响 admin", R.allow(QStringLiteral("admin")));
        R.setChannelEnabled(QStringLiteral("class"), QStringLiteral("class.ended"), false);
        chk("关渠道 ⇒ class.ended 被拦",
            !R.allow(QStringLiteral("class"), QStringLiteral("class.ended")));
        chk("同提供方其它渠道仍放行",
            R.allow(QStringLiteral("class"), QStringLiteral("class.started")));
        chk("渠道名写错不静默丢弃",
            R.allow(QStringLiteral("class"), QStringLiteral("class.nosuch")));
        R.moveUp(QStringLiteral("plugin"));
        chk("上移生效：plugin 抢到 automation 之前",
            R.priorityIndex(QStringLiteral("plugin")) < R.priorityIndex(QStringLiteral("automation")));
        const QVariantList model = R.settingsModel();
        const QVariantMap firstRow = model.first().toMap();
        const QVariantMap lastRow = model.last().toMap();
        chk("首行 priority=1 且 canUp=false",
            firstRow.value(QStringLiteral("priority")).toInt() == 1 &&
            !firstRow.value(QStringLiteral("canUp")).toBool());
        chk("末行 canDown=false", !lastRow.value(QStringLiteral("canDown")).toBool());
        R.resetToDefaults();
        chk("重置后 order 回默认", R.order() == def);
        chk("重置后 plugin 恢复启用", R.allow(QStringLiteral("plugin")));
        fprintf(stdout, "[providers] 汇总：%s（失败 %d）\n", fail == 0 ? "PASS" : "FAIL", fail);
        fflush(stdout);
        return fail == 0 ? 0 : 1;
    }
    if (idle) {
        isl->ensureIdle();
    } else if (!morphModeArg.isEmpty()) {
        // 2026-10-27（灵动岛 6 形态骨架）：强制形态层接管收起态 —— 先给一条占位通知
        // 让岛以「可见收起态」上屏（morph 层 visible 与 idle 无关，但岛本身得先可见），
        // 再 setMorphDemo 强制模式（m_morphForced=true，recomputeMorph 让路，QML 读 bridge.morphMode）。
        isl->showIsland(QStringLiteral("灵动岛形态演示"),
                        QStringLiteral("下方为形态渲染层"),
                        QStringLiteral("sparkles"), 0);
        isl->setMorphDemo(morphModeArg);
        if (expand) isl->setExpanded(true);   // 进度/跑马灯等形态在展开态有附加控件
    } else if (!formArg.isEmpty()) {
        // #330 三档场景（2026-10-17）
        //   banner      长文案通栏：验"单行 elide 不折行、高度不涨"
        //   mini        有描述 ⇒ 112 方牌（radius 28）
        //   mini-nodesc 无描述 ⇒ 96 正圆（C++ 的 m_desc.isEmpty() 与 QML 的
        //               root._miniOrb 必须同口径，否则"窗口圆、底色方"一眼假）
        IslandOverlay::Form f = IslandOverlay::Form::Mini;
        if (formArg == QLatin1String("banner"))          f = IslandOverlay::Form::Banner;
        else if (formArg == QLatin1String("centered"))    f = IslandOverlay::Form::Centered;
        else if (formArg == QLatin1String("fullscreen"))  f = IslandOverlay::Form::Fullscreen;

        // 2026-10-17 补：centered / fullscreen 这两档此前在 QML 里**没有版式**
        //（落进胶囊分支 ⇒ 圆角/排版全错），本轮补了 IslandCardFace /
        // IslandFullscreenFace，探针要能把它们单独拉起来看。
        if (formArg == QLatin1String("centered")) {
            isl->showIsland(QStringLiteral("课件已下发"),
                            QStringLiteral("数学·第三章 二次函数.pdf · 3.2 MB"),
                            QStringLiteral("download"), 0, f);
            isl->setSeverity(QStringLiteral("inform"));
        } else if (formArg == QLatin1String("fullscreen")) {
            isl->showIsland(QStringLiteral("考试即将开始"),
                            QStringLiteral("请把与考试无关的物品放到讲台前，桌面只留笔和草稿纸"),
                            QStringLiteral("bell"), 0, f);
            isl->setSeverity(QStringLiteral("urgent"));
        } else if (formArg == QLatin1String("banner")) {
            isl->showIsland(
                QStringLiteral("全校广播：今天下午 3 点开始消防演练，请各班按指定路线撤离到操场集合，班主任带队"),
                QStringLiteral("学生处 · 全程保持安静，到操场后按班级列队"),
                QStringLiteral("speaker"), 0, f);
        } else if (formArg == QLatin1String("mini-nodesc")) {
            isl->showIsland(QStringLiteral("在播"), QString(),
                            QStringLiteral("music"), 0, f);
        } else {
            isl->showIsland(QStringLiteral("正在接收 课件.pdf"),
                            QStringLiteral("42%"), QStringLiteral("download"), 0, f);
        }
        // 2026-10-17 取证：--hold N 在这里**先停 N 秒**再弹第二条 —— 生产里最常见的
        // "带按钮的通知顶掉普通通知"。若历史 ts 取的是归档时刻，它会等于第二条弹出的
        // 时刻；取的是出现时刻，则比归档早约 N 秒。这一停就是为了把两者拉开。
        if (actions && holdSec > 0) QThread::sleep(static_cast<unsigned long>(holdSec));

        // 守卫回归（--form banner --actions）：紧凑形态放不下按钮 ⇒
        // showWithActions 必须**降级 Centered**（断言看 dump 的 _formKind）。
        if (actions) {
            isl->showWithActions(QStringLiteral("确认收到"),
                                 QStringLiteral("紧凑形态放不下按钮，应降级为居中卡片"),
                                 QStringLiteral("bell"), f,
                                 { QStringLiteral("知道了") }, nullptr, 0);
        }
    } else if (actions) {
        isl->showWithActions(QStringLiteral("考试模式已开始"),
                             QStringLiteral("本次考试 45 分钟，请勿切换窗口"),
                             QStringLiteral("ban"),
                             IslandOverlay::Form::Capsule,
                             { QStringLiteral("知道了"), QStringLiteral("需要帮助") },
                             [](const QString &s) {
                                 QFile f(QStringLiteral("D:/Stelarith/_probe/shots/_action_hit.txt"));
                                 if (f.open(QIODevice::Append | QIODevice::Text)) {
                                     QTextStream(&f) << "fireAction: " << s << "\n";
                                 }
                             },
                             0);
        if (expand) isl->setExpanded(true);
    } else if (spectrum) {
        // §6.3 语音频谱（2026-10-13）：对讲中岛展开 + 展开态画 16 条。
        // 造一段 16kHz/16bit/20ms 的合成 PCM（多频叠加，让各带强弱不同），
        // 用 40ms 节拍**反复喂** —— 只喂一次的话，岛心跳（500ms）会把条按"静音"衰减归零。
        isl->showIsland(QStringLiteral("老师正在讲话"),
                        QStringLiteral("老师正在通过语音对讲广播"),
                        QStringLiteral("mic"), 0);
        isl->setExpanded(true);
        auto *pcm = new QByteArray();
        {
            const int n = 320;   // 16kHz × 20ms
            pcm->resize(n * 2);
            auto *s = reinterpret_cast<qint16 *>(pcm->data());
            for (int i = 0; i < n; ++i) {
                double v = 0.0;
                for (int b = 0; b < 10; ++b)
                    v += (0.35 / (b + 1)) * std::sin(2.0 * 3.14159265358979
                                                     * (i * (b + 1) * 2.0 / double(n)));
                s[i] = qint16(qBound(-32767.0, v * 32767.0, 32767.0));
            }
        }
        auto *tick = new QTimer(&app);
        tick->setInterval(40);
        QObject::connect(tick, &QTimer::timeout, isl, [isl, pcm]() { isl->feedSpectrum(*pcm); });
        tick->start();
        isl->feedSpectrum(*pcm);
    } else if (mediaPhaseArg == QLatin1String("blocked")) {
        // 组件7（2026-10-15）：禁播 —— 走真链路（生产里 media_session_start 被
        // 播放时段拦住时，main.cpp 就是这么调的）。它自成一态：没有 setMediaSession。
        isl->mediaBlocked(QStringLiteral("当前时段不开放播放"));
        if (expand) isl->setExpanded(true);
    } else if (media || search || submit || likefly) {
        // 组件1（2026-10-15）：媒体播放呈现上岛 —— setMediaSession 内部以收起态
        // 胶囊上屏（与 media_session_start 成功分支同一条路，不再需要预置一条通知）。
        // --expand 才展开看互动区/弹幕行；advanceMediaElapsed 补非零已播
        // （ffplay 无回调，生产里由 1s 计时器推进）。
        // 组件2 --search：隐含展开（搜索页只在展开态可见），页面状态由下面
        // rootObject 上 setProperty 注入（与 likeCount 同范式）。
        // 组件3 --submit：同款隐含展开（投稿页同理只在展开态互动区在时可见）。
        isl->setMediaSession(QStringLiteral("晴天 - 周杰伦"), 269000,
                             QStringLiteral("稻香"),
                             QStringLiteral("D:/Stelarith/Stelarith-xingjikong-qt/Stelarith-control-qt/resources/icons/music.svg"));
        // 组件7：相位按 --media-phase 走真 C++ 链路（不用假数据）。
        // 总长 269000（4:29）⇒ ending 阈值 = max(15s, 10%) = 26.9s，
        // 已播 250000（剩 19s）正好落在 ending；ended 走 mediaFinished()。
        isl->advanceMediaElapsed(mediaPhaseArg == QLatin1String("ending") ? 250000 : 83000);
        if (mediaPhaseArg == QLatin1String("ended")) isl->mediaFinished();
        if (expand || search || submit || likefly) isl->setExpanded(true);
    } else if (!panelArg.isEmpty()) {
        // 2026-10-27（G4/G5/G6）：面板三面 —— 走**真实数据域**（PanelDomain::build），
        // 连课表源也照生产那条路建（同 ScheduleClock、同档案路径口径）。
        // 输出 PANEL-DATA 供断言：课表几节、倒计时目标、周次都在里面。
        const QString prof = profileArg.isEmpty()
            ? QStringLiteral("%1/ClassIsland/data/Profiles/Default.json")
                  .arg(qEnvironmentVariable("LOCALAPPDATA"))
            : profileArg;
        auto *clk = new ScheduleClock(prof, &app);
        clk->start();                 // tick() 同步建"今日课表"，面板随即能取到
        PanelDomain::init(clk);
        const QJsonObject params = QJsonDocument::fromJson(panelJsonArg.toUtf8()).object();
        QString perr;
        QJsonObject pdata;
        const bool pok = PanelDomain::show(panelArg, params, 0, &perr, &pdata);
        fprintf(stderr, "PANEL face=%s ok=%d err=%s\nPANEL-DATA=%s\n",
                qPrintable(panelArg), int(pok), qPrintable(perr),
                QJsonDocument(pdata).toJson(QJsonDocument::Compact).constData());
        fflush(stderr);
    } else if (!channelArg.isEmpty()) {
        // 渠道标签：与 main.cpp 的上课提醒同一条路（一次性 hint → 胶囊通知）
        isl->setChannelForNext(channelArg);
        isl->showIsland(QStringLiteral("上课啦"), QStringLiteral("数学 · 08:00"),
                        QStringLiteral("bell"), 30000);
        if (expand) isl->setExpanded(true);
    } else {
        isl->showIsland(QStringLiteral("老师正在讲话"),
                        QStringLiteral("请保持安静，认真听讲，有问题举手"),
                        QStringLiteral("bell"), 30000);
        if (expand) isl->setExpanded(true);
    }

    // #330：面 2（历史）要有历史条目 —— 连发 3 条（每条顶掉上一条 ⇒ 归档进历史），
    // 最后回到主通知，确保 _history 里有 3 条可渲染。
    if (faceArg == 2) {
        for (int i = 1; i <= 3; ++i)
            isl->showIsland(QStringLiteral("历史消息 %1").arg(i),
                            QStringLiteral("多界面探针 · 第 %1 条").arg(i),
                            QStringLiteral("bell"), 1200);
        isl->showIsland(QStringLiteral("老师正在讲话"),
                        QStringLiteral("请保持安静，认真听讲，有问题举手"),
                        QStringLiteral("bell"), 30000);
        isl->setExpanded(true);
    }

    // 组件5：--warm 预热 30s 冷却 —— 走真实发送链路（IslandOverlay::
    // sendBlessing → DanmakuWidget 单例），冷却计时从此刻起算。配 --submit
    // 演示「投稿钮禁用 + 计数行倒计时」；此时点投稿钮应被 hitDisabledControl
    // 守卫吞掉（岛保持展开，不误收起）。
    if (warm) isl->sendBlessing(QStringLiteral("探针预热：祝大家上课开心"));

    // 组件6（2026-10-15）：--report 注入落盘式 reporter。生产里这一步由 main.cpp
    // 用 g_ws 的 lambda 完成（island 只认 CloudReporter，不认识 ws）。探针没有 ws，
    // 用「追加 _report_trace.txt」的替身：能证明 QML 点击 → doSubmit/fireLike →
    // 桥 Q_INVOKABLE → 岛 reportToCloud → reporter 的**整条端侧链路**接通，
    // 缺的只剩 main.cpp 里那一段 socket 发送（那段的形状已由云端联调回归覆盖）。
    if (report) {
        isl->setCloudReporter([](const QString &type, const QJsonObject &payload) {
            QFile f(QStringLiteral("D:/Stelarith/_probe/shots/_report_trace.txt"));
            if (f.open(QIODevice::Append | QIODevice::Text))
                QTextStream(&f) << "t=" << QDateTime::currentMSecsSinceEpoch()
                                << " type=" << type
                                << " payload=" << QString::fromUtf8(
                                       QJsonDocument(payload).toJson(QJsonDocument::Compact))
                                << "\n";
        });
    }

    // #330：多界面的面号注入**必须早于采样**且留出一拍 —— setProperty 之后
    // QML 绑定是**异步重算**的，同帧读 faceNow/facesOn 会拿到未就绪值
    //（2026-10-17 实测：同帧 dump 出 <invalid>，提前 500ms 注入后稳定）。
    if (faceArg > 0) {
        // 2026-11-03：期(8)/倒(9) 默认 enabled=false（见 island_layout builtinRows），
        // 不先启用就在 faceList() 里不存在，setProperty("face") 切不过去 —— 采样必空。
        islandLayoutInstance()->setEnabledById(faceArg, true);
        {   // 诊断：模型里到底有没有这面（有才谈得上切）
            const QVariantList fl = islandLayoutInstance()->faceList();
            QStringList ids;
            for (const QVariant &e : fl) ids << QString::number(e.toMap().value("id").toInt());
            fprintf(stderr, "PROBE: faceList ids=[%s] want=%d\n",
                    ids.join(QLatin1Char(',')).toLocal8Bit().constData(), faceArg);
        }
        QTimer::singleShot(2000, [isl, faceArg]() {
            if (QQuickWidget *q2 = findQuick(isl)) {
                if (QQuickItem *ri2 = q2->rootObject()) {
                    ri2->setProperty("face", faceArg);
                    // 诊断 2：faceModel 是否感知 setEnabledById —— 再触发一次 layoutChanged
                    //（model=[0,7,4,5] 缺 8 ⇒ faceModel 的 readonly var 绑定没重算）。
                    if (IslandLayoutModel *lm = islandLayoutInstance())
                        lm->touch();
                    // 诊断：切完后 QML 侧的真实状态（faceNow=fall back 后、页点序）
                    QTimer::singleShot(300, [ri2, faceArg]() {
                        const QVariant f = ri2->property("face");
                        const QVariant fn = ri2->property("faceNow");
                        const QVariant fm = ri2->property("faceModel");
                        QStringList seq;
                        const QVariantList l = fm.toList();
                        for (const QVariant &e : l)
                            seq << QString::number(e.toMap().value("id").toInt());
                        fprintf(stderr, "PROBE: face=%s faceNow=%s model=[%s] (want %d)\n",
                                f.toString().toLocal8Bit().constData(),
                                fn.toString().toLocal8Bit().constData(),
                                seq.join(QLatin1Char(',')).toLocal8Bit().constData(),
                                faceArg);
                    });
                }
            }
        });
    }

    // 采样：等 QML 起来 + 几何动画走完（380ms）再抓
    // 组件3：本段只做「注入 + 合成点击」；dump/tree/截图提为 captureAndSave
    // （点击后可能还要等 400ms 提交 Timer 落 2/3 态 ⇒ 两段式 singleShot）。
    QTimer::singleShot(2500 + holdSec * 1000, [isl, outPath, search, searchPhase, submit, sphase, clickName, warm, draftOverride, likefly, mediaPhaseArg, faceArg, setPropStr]() {
        QQuickWidget *q = findQuick(isl);
        if (!q) { qApp->quit(); return; }

        // ── 直接问 QML 根对象：桥到底送进去没有 ────────────────────
        // ⚠️ 这一步是为了把"桥没数据"和"QML 没读"两类故障分开。
        //    2026-10-11 踩到过：QML errors=0、几何也对，但内容是空的，
        //    不加这段就只能靠猜。
        if (QQuickItem *ri = q->rootObject()) {
            // 组件1 演示值：点赞计数（探针专用；生产由组件4 接桥后改绑定）
            ri->setProperty("likeCount", 23);
            // 组件7（2026-10-15）：播放四态渲染相位覆盖 —— C++ 已按真链路设了相位，
            // 但 starting 会在 2.5s 后自转 playing、ended 会在 2.5s 后收起，而探针
            // 恰在 2500ms 截图，正踩边界 ⇒ 用 demoMediaPhase 把渲染相位钉死
            // （只影响渲染，不影响 C++ 真状态），四态截图才确定、可复现。
            if (!mediaPhaseArg.isEmpty())
                ri->setProperty("demoMediaPhase", mediaPhaseArg);
            // 组件2 演示值：点歌搜索页五态（探针专用；生产由组件6 接桥后改绑定）
            if (search) {
                // 各态配一个可信关键词：结果态「周」（占位目录 3 首周杰伦）、
                // 无结果态用生造词；空态留空。
                const QString q = (searchPhase == 1 || searchPhase == 2 || searchPhase == 4)
                                    ? QStringLiteral("周")
                                    : (searchPhase == 3 ? QStringLiteral("不存在的歌") : QString());
                ri->setProperty("searchQuery", q);
                ri->setProperty("searchPhase", searchPhase);
                if (searchPhase == 4) ri->setProperty("searchQuotaUsed", 3);   // 配额满
                ri->setProperty("searchOn", true);
                // 输入框文字同步（截图可信）：setProperty 不触发 onTextEdited，
                // 不会把上面显式设的 phase 冲掉——两条注入互不干扰。
                if (QQuickItem *in = ri->findChild<QQuickItem *>(QStringLiteral("songSearchInput")))
                    in->setProperty("text", q);
            }
            // 组件3 演示值：投稿页（探针专用；生产由组件5/6 接桥后改绑定）
            // ⚠️ --click 入口 pill（danmakuPillMa/danmakuSendMa）时**不**执行
            //    本注入块的开页：danmakuRow.visible 含 !submitOn，此处把
            //    submitOn 置 true 会把要点击的入口行藏掉（2026-10-15 实测：
            //    pillMa vis=0、点击落空）——入口场景的开页交给点击本身触发。
            const bool clickEntry = (clickName == QLatin1String("danmakuPillMa")
                                     || clickName == QLatin1String("danmakuSendMa"));
            if (submit && !clickEntry) {
                // 六态注入口径（sphase 是**演示态编号**，与 QML submitPhase
                // 状态机编号不同，必须映射——2026-10-15 冒烟踩过：直接透传
                // sphase=1 把「有内容编辑态」演成了「提交中」）：
                //   sphase 0 空编辑 ⇒ phase 0 + ""
                //   sphase 1 有内容 ⇒ phase 0 + 14 字草稿
                //   sphase 2 超限   ⇒ phase 0 + 51 字（超 1 字 ⇒ 红计数+禁用）
                //   sphase 3 提交中 ⇒ phase 1 + 草稿（不启动 Timer，停在中态）
                //   sphase 4 成功   ⇒ phase 2 + 草稿
                //   sphase 5 失败   ⇒ phase 3 + 草稿
                const QString draft14 = draftOverride.isEmpty()
                    ? QStringLiteral("老师教师节快乐，同学们加油！") : draftOverride;
                const QString draft51 = QStringLiteral("祝老师教师节快乐，身体健康，工作顺利！").repeated(3).left(51);
                int qmlPhase = 0;
                QString draft;
                switch (sphase) {
                case 1:  qmlPhase = 0; draft = draft14; break;
                case 2:  qmlPhase = 0; draft = draft51; break;
                case 3:  qmlPhase = 1; draft = draft14; break;
                case 4:  qmlPhase = 2; draft = draft14; break;
                case 5:  qmlPhase = 3; draft = draft14; break;
                default: qmlPhase = 0; draft = QString(); break;   // 0 空编辑
                }
                ri->setProperty("submitOn", true);
                ri->setProperty("submitPhase", qmlPhase);
                // ⚠️ 设 submitText 而非 submitInput.text：QML 里
                //    text: root.submitText 是绑定，直接 set text 会破坏绑定
                //    （计数/禁用态不再跟 submitText 联动）。
                if (!draft.isEmpty()) ri->setProperty("submitText", draft);
            }
            // 组件4 演示值：点赞飘心中途态（探针专用；生产由桥下发后改绑定）。
            // 不驱动完整 600ms 动画时序（截图时刻不可控）——直接把飘心 Item
            // 设到动画中途视觉（y=-14 半程 / opacity=0.55 半透明 / visible），
            // 截图证「飘心中」长相；动画结束自动隐藏由 QML onFinished 保证，
            // 点击轮（--click abtn_like）等 1400ms 后 dump 证 visible 归 0。
            if (likefly) {
                ri->setProperty("likeOn", true);
                ri->setProperty("likeCount", 24);   // 23 演示值 + 1
                if (QQuickItem *fly = ri->findChild<QQuickItem *>(QStringLiteral("likeFly"))) {
                    fly->setVisible(true);
                    fly->setProperty("y", -14);
                    fly->setProperty("opacity", 0.55);
                }
            }
            // ── 组件3：合成点击 ─────────────────────────────────────
            // 入口 pill（danmakuPillMa/danmakuSendMa）：先喂一句草稿到弹幕
            // 输入行，验证「开页携带文字」语义（onClicked 里携入 submitText）。
            // 投稿页页内按钮（abtn_*，组件3）：先注入**目标所在态**——按钮只在
            // 对应 phase 的态 Item 里可见（visible: submitPhase === N），态不对
            // ⇒ 目标不可见 ⇒ 点击穿透到根 gestureArea 把岛点成收起态
            // （2026-10-15 实测踩坑：prePhase 曾误用演示态编号 4/5，
            //   而 QML submitPhase 只认 0 编辑/1 提交中/2 成功/3 失败；
            //   2026-10-15 二踩：abtn_like[组件4] 被本块误伤——注入 submitOn=true
            //   藏掉 interactRow ⇒ 好听钮不可见 ⇒ 点击落空点穿 gestureArea）。
            //   abtn_submit/abtn_backEdit 在编辑态(0)；abtn_done/abtn_rewrite
            //   在成功态(2)；abtn_retry/abtn_backFail 在失败态(3)。
            // ⚠️ 本块**只认投稿页六钮**；其它 objectName（如组件4 的 abtn_like）
            //    无需前置态——默认媒体展开态下互动区可见、按钮 enabled。
            if (!clickName.isEmpty()) {
                if (clickEntry) {
                    if (QQuickItem *in = ri->findChild<QQuickItem *>(QStringLiteral("danmakuInput")))
                        in->setProperty("text", QStringLiteral("老师教师节快乐，同学们加油！"));
                } else if (clickName == QLatin1String("abtn_submit")
                           || clickName == QLatin1String("abtn_backEdit")
                           || clickName == QLatin1String("abtn_done")
                           || clickName == QLatin1String("abtn_rewrite")
                           || clickName == QLatin1String("abtn_retry")
                           || clickName == QLatin1String("abtn_backFail")) {
                    int qmlPhase = 0;
                    if (clickName == QLatin1String("abtn_done") || clickName == QLatin1String("abtn_rewrite"))
                        qmlPhase = 2;   // 成功态
                    else if (clickName == QLatin1String("abtn_retry") || clickName == QLatin1String("abtn_backFail"))
                        qmlPhase = 3;   // 失败态
                    // abtn_submit / abtn_backEdit ⇒ 编辑态(0)
                    ri->setProperty("submitOn", true);
                    ri->setProperty("submitPhase", qmlPhase);
                    // 组件5：--draft 覆写（敏感词等真实结果码演示走这里）
                    const QString cdraft = draftOverride.isEmpty()
                        ? QStringLiteral("老师教师节快乐，同学们加油！") : draftOverride;
                    ri->setProperty("submitText", cdraft);
                }
            }
            // 2026-10-31 · 苹果式极简：--set name=value（只设布尔真值，验证历史面板链路）
            if (!setPropStr.isEmpty()) {
                const int eq = setPropStr.indexOf(QLatin1Char('='));
                if (eq > 0) {
                    const QString pn = setPropStr.left(eq);
                    const QString pv = setPropStr.mid(eq + 1);
                    const bool bv = (pv == QLatin1String("1") || pv == QLatin1String("true"));
                    ri->setProperty(pn.toUtf8().constData(), bv);
                }
            }
        }

        // ⚠️ 注入与点击之间必须让布局 polish 一帧（+100ms 再点）：setProperty
        //    是同步的，但锚链重算/visible 传导要等事件循环。同步块内立即点击
        //    拿到的是未重算坐标 ⇒ 落空，甚至被根 gestureArea（anchors.fill 全岛、
        //    onClicked 切 expanded）吞掉把岛点成收起态（2026-10-15 实测）。
        QTimer::singleShot(100, [isl, q, outPath, clickName, setPropStr]() {
            // 组件6：先 Item 树找（覆盖 Repeater delegate），找不到再退回 QObject 树
            //（顶层声明控件的常规路径，保持既有用例零回归）。
            QQuickItem *target = clickName.isEmpty() ? nullptr
                : (q->rootObject() ? findItemByName(q->rootObject(), clickName) : nullptr);
            if (!target && !clickName.isEmpty() && q->rootObject())
                target = q->rootObject()->findChild<QQuickItem *>(clickName);
            const bool clicked = clickItem(q, target);
            // 组件5 诊断（2026-10-15）：点击后接上冷却广播追踪（widget 已在
            // 点击中创建）——b1 复现「QML 停 30 / widget 29」时定位 tick 发没发。
            if (DanmakuWidget *dw = DanmakuWidget::danmakuWidgetPeek()) {
                QObject::connect(dw, &DanmakuWidget::blessingCooldownChanged, [](int rem) {
                    QFile f(QStringLiteral("D:/Stelarith/_probe/shots/_cooldown_trace.txt"));
                    if (f.open(QIODevice::Append | QIODevice::Text))
                        QTextStream(&f) << "t=" << QDateTime::currentMSecsSinceEpoch()
                                        << " emit rem=" << rem << "\n";
                });
            }
            QString extra = clickName.isEmpty() ? QString()
                : QStringLiteral("click: target=") + clickName
                  + QStringLiteral(" found=") + (target ? QStringLiteral("1") : QStringLiteral("0"))
                  + QStringLiteral(" sent=") + (clicked ? QStringLiteral("1") : QStringLiteral("0"))
                  + QStringLiteral("\n");
            // ── 点击诊断（2026-10-15）：合成事件若没触发 onClicked，先验证坐标
            //    链路——target 绝对坐标（父链累加）+ 自身可见性 + window 状态。
            if (target) {
                QPointF base(0, 0);
                for (const QQuickItem *p = target; p; p = p->parentItem())
                    base += QPointF(p->x(), p->y());
                const QPointF c = base + QPointF(target->width() / 2.0, target->height() / 2.0);
                extra += QStringLiteral("click diag: base=(") + QString::number(base.x())
                       + QStringLiteral(",") + QString::number(base.y())
                       + QStringLiteral(") size=") + QString::number(target->width())
                       + QStringLiteral("x") + QString::number(target->height())
                       + QStringLiteral(" center=(") + QString::number(c.x())
                       + QStringLiteral(",") + QString::number(c.y())
                       + QStringLiteral(") vis=") + (target->isVisible() ? QStringLiteral("1") : QStringLiteral("0"))
                       + QStringLiteral(" win=") + (q->quickWindow() ? QStringLiteral("ok") : QStringLiteral("null"))
                       + QStringLiteral("\n");
                // childAt 逐层下钻：看该坐标处实际命中链（childAt 按子项顺序，
                // 不按 z，仅作参考——但足以暴露"目标根本没排在点击路径上"）
                {
                    QQuickItem *cur = q->rootObject();
                    QPointF local = c;
                    QString path;
                    for (int guard = 0; guard < 20 && cur; ++guard) {
                        path += QStringLiteral(" > ") + QLatin1String(cur->metaObject()->className());
                        QQuickItem *nx = cur->childAt(local.x(), local.y());
                        if (!nx || nx == cur) break;
                        local = QPointF(local.x() - nx->x(), local.y() - nx->y());
                        cur = nx;
                    }
                    extra += QStringLiteral("click hitpath:") + path + QStringLiteral("\n");
                }
            }

            // 点击「投稿/重试」会启动 400ms 提交 Timer（2500+100+400=3000ms 才落
            // 2/3 态）⇒ 再等 1400ms 才捕获；「好听」会启动 600ms 飘心动画
            // （2500+100+600=3200ms 才落回隐藏）⇒ 同样等 1400ms 证归位；
            // 其余点击/静态注入立即捕获。
            // ⚠️ 2026-10-15 组件5：abtn_submit 走真实发送 ⇒ 另起 30s 冷却 tick
            //    （1Hz）。合成点击的 QML 回调经 QQuickWidget 异步投递，实际执行
            //    比点击晚 ~400ms ⇒ 等 2600ms（而非 1400ms）给 tick 留出触发
            //    窗口，避免 capture 与首个 tick 赛跑（b1 曾因此读到旧值 30）。
            const bool waitsTimer = (clickName == QLatin1String("abtn_submit")
                                     || clickName == QLatin1String("abtn_retry")
                                     || clickName == QLatin1String("abtn_like"));
            if (waitsTimer && clicked) {
                QTimer::singleShot(2600, [isl, q, outPath, extra]() {
                    captureAndSave(isl, q, outPath, extra);
                    qApp->quit();
                });
            } else {
                captureAndSave(isl, q, outPath, extra);
                // 2026-11-03：quit 前先打一行 —— 0xC0000005 若发生在 quit 之后
                //（QQuickWidget/岛销毁序问题），这行能把它和"采样阶段崩"分开。
                fprintf(stderr, "PROBE: before quit\n");
                qApp->quit();
            }
        });
    });

    return app.exec();
}
