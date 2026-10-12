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
#include "island.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QPixmap>
#include <QQuickWidget>
#include <QQuickItem>
#include <QFontDatabase>
#include <QTextStream>
#include <QTimer>
#include <cmath>   // §6.3 频谱探针：std::sin

// 从岛的子控件里找 QQuickWidget（island.h 没有公开 getter，这里按类型找）
static QQuickWidget *findQuick(QWidget *w)
{
    if (auto *q = qobject_cast<QQuickWidget *>(w)) return q;
    for (QObject *c : w->children())
        if (auto *cw = qobject_cast<QWidget *>(c))
            if (auto *q = findQuick(cw)) return q;
    return nullptr;
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
    }

    bool idle = false, expand = false, actions = false, light = false, spectrum = false;
    QString outPath = QStringLiteral("D:/Stelarith/_probe/shots/island-agent.png");
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--idle"))      idle = true;
        if (a == QLatin1String("--expand"))    expand = true;
        if (a == QLatin1String("--actions"))   actions = true;
        if (a == QLatin1String("--light"))     light = true;
        if (a == QLatin1String("--spectrum"))  spectrum = true;   // §6.3 频谱态（2026-10-13）
        if (a == QLatin1String("--out") && i + 1 < argc)
            outPath = QString::fromLocal8Bit(argv[++i]);
    }

    IslandOverlay *isl = IslandOverlay::instance();
    isl->setIdleEnabled(true);
    if (light) isl->setLightMode(true);

    if (idle) {
        isl->ensureIdle();
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
    } else {
        isl->showIsland(QStringLiteral("老师正在讲话"),
                        QStringLiteral("请保持安静，认真听讲，有问题举手"),
                        QStringLiteral("bell"), 30000);
        if (expand) isl->setExpanded(true);
    }

    // 采样：等 QML 起来 + 几何动画走完（380ms）再抓
    QTimer::singleShot(2500, [isl, &outPath]() {
        QFile logf(QStringLiteral("D:/Stelarith/_probe/shots/_probe_result.txt"));
        logf.open(QIODevice::Append | QIODevice::Text);
        QTextStream out(&logf);

        QQuickWidget *q = findQuick(isl);
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
        if (!q) { out.flush(); qApp->quit(); return; }
        out << "quick status=" << int(q->status())
            << " errors=" << q->errors().size()
            << " qgeom=" << q->geometry().width() << "x" << q->geometry().height() << "\n";
        for (const QQmlError &e : q->errors())
            out << "  QMLERR " << e.toString() << "\n";

        // ── 直接问 QML 根对象：桥到底送进去没有 ────────────────────
        // ⚠️ 这一步是为了把"桥没数据"和"QML 没读"两类故障分开。
        //    2026-10-11 踩到过：QML errors=0、几何也对，但内容是空的，
        //    不加这段就只能靠猜。
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
            dumpProp("_spectrumActive");   // §6.3：频谱态（2026-10-13）
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
            qApp->quit();
            return;
        }

        QImage img = q->grabFramebuffer();
        out << "framebuffer=" << img.width() << "x" << img.height()
            << " null=" << (img.isNull() ? 1 : 0) << "\n";
        if (!img.isNull()) {
            QDir().mkpath(QFileInfo(outPath).absolutePath());
            const bool ok = img.save(outPath);
            out << "saved=" << (ok ? 1 : 0) << " path=" << outPath << "\n";
        }
        out.flush();
        qApp->quit();
    });

    return app.exec();
}
